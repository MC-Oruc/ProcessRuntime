#include "ProcessRuntimeSubsystem.h"

#include "Async/Async.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "ProcessRuntimeModule.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
#if PLATFORM_WINDOWS
DWORD ToWin64PriorityClass(const EManagedProcessPriority Priority)
{
    switch (Priority)
    {
    case EManagedProcessPriority::Idle: return IDLE_PRIORITY_CLASS;
    case EManagedProcessPriority::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
    case EManagedProcessPriority::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
    case EManagedProcessPriority::High: return HIGH_PRIORITY_CLASS;
    case EManagedProcessPriority::Normal:
    default: return NORMAL_PRIORITY_CLASS;
    }
}
#endif
}

struct UProcessRuntimeSubsystem::FManagedProcessRecord
{
    FManagedProcessSpec Spec;
    FManagedProcessStatus Status;
    TUniquePtr<FMonitoredProcess> Process;
    TOptional<FManagedProcessSpec> PendingRestartSpec;
    void* JobHandle = nullptr;
    uint64 Generation = 0;
    bool bExpectedStop = false;
};

void UProcessRuntimeSubsystem::Deinitialize()
{
    bDeinitializing = true;

    for (TPair<FName, TSharedPtr<FManagedProcessRecord>>& Pair : Processes)
    {
        FManagedProcessRecord& Record = *Pair.Value;
        if (Record.Process)
        {
            Record.Process->OnOutput().Unbind();
            Record.Process->OnCompleted().Unbind();
            Record.Process->OnCanceled().Unbind();
            Record.Process->Cancel(true);
        }
        ReleaseRecordProcess(Record);
    }

    Processes.Empty();
    ProcessStateChanged.Clear();
    ProcessOutput.Clear();
    Super::Deinitialize();
}

bool UProcessRuntimeSubsystem::StartProcess(const FManagedProcessId& ProcessId, const FManagedProcessSpec& Spec, FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();

    if (bDeinitializing)
    {
        OutError = TEXT("ProcessRuntime is shutting down.");
        return false;
    }
    if (!ProcessId.IsValid())
    {
        OutError = TEXT("Process ID is empty.");
        return false;
    }

    const FString ExecutablePath = FPaths::ConvertRelativePathToFull(Spec.ExecutablePath);
    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ExecutablePath))
    {
        OutError = FString::Printf(TEXT("Executable does not exist: %s"), *ExecutablePath);
        return false;
    }

    if (const TSharedPtr<FManagedProcessRecord>* Existing = Processes.Find(ProcessId.Value))
    {
        if ((*Existing)->Status.IsActive())
        {
            OutError = FString::Printf(TEXT("Owned process '%s' is already active."), *ProcessId.Value.ToString());
            return false;
        }
        ReleaseRecordProcess(**Existing);
        Processes.Remove(ProcessId.Value);
    }

    TSharedPtr<FManagedProcessRecord> Record = MakeShared<FManagedProcessRecord>();
    Record->Spec = Spec;
    Record->Spec.ExecutablePath = ExecutablePath;
    Record->Spec.WorkingDirectory = Spec.WorkingDirectory.IsEmpty()
        ? FPaths::GetPath(ExecutablePath)
        : FPaths::ConvertRelativePathToFull(Spec.WorkingDirectory);
    Record->Status.State = EManagedProcessState::Starting;
    Record->Status.StartedAtUtc = FDateTime::UtcNow();
    Record->Status.Priority = Record->Spec.Priority;
    Record->Generation = NextGeneration++;

    const FName ProcessName = ProcessId.Value;
    const uint64 Generation = Record->Generation;
    TWeakObjectPtr<UProcessRuntimeSubsystem> WeakThis(this);
    Record->Process = MakeUnique<FMonitoredProcess>(
        Record->Spec.ExecutablePath,
        Record->Spec.Arguments,
        Record->Spec.WorkingDirectory,
        Record->Spec.bHidden,
        Record->Spec.bCaptureOutput);
    Record->Process->SetSleepInterval(0.05f);
    Record->Process->OnOutput().BindLambda([WeakThis, ProcessName, Generation](FString Output)
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis, ProcessName, Generation, Output = MoveTemp(Output)]() mutable
        {
            if (WeakThis.IsValid())
            {
                WeakThis->HandleOutputOnGameThread(ProcessName, Generation, MoveTemp(Output));
            }
        });
    });
    Record->Process->OnCompleted().BindLambda([WeakThis, ProcessName, Generation](const int32 ExitCode)
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis, ProcessName, Generation, ExitCode]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->HandleCompletedOnGameThread(ProcessName, Generation, ExitCode);
            }
        });
    });
    Record->Process->OnCanceled().BindLambda([WeakThis, ProcessName, Generation]()
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis, ProcessName, Generation]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->HandleCanceledOnGameThread(ProcessName, Generation);
            }
        });
    });

    FManagedProcessRecord* RecordPtr = Record.Get();
    Processes.Add(ProcessName, MoveTemp(Record));
    BroadcastStatus(ProcessName, *RecordPtr);

    if (!RecordPtr->Process->Launch())
    {
        RecordPtr->Status.State = EManagedProcessState::Failed;
        RecordPtr->Status.LastError = FString::Printf(TEXT("Failed to launch executable: %s"), *ExecutablePath);
        OutError = RecordPtr->Status.LastError;
        ReleaseRecordProcess(*RecordPtr);
        BroadcastStatus(ProcessName, *RecordPtr);
        return false;
    }

    FString JobWarning;
    if (!AssignWin64JobObject(*RecordPtr, JobWarning) && !JobWarning.IsEmpty())
    {
        UE_LOG(LogProcessRuntime, Warning, TEXT("%s"), *JobWarning);
    }

    RecordPtr->Status.State = EManagedProcessState::Running;
    UE_LOG(LogProcessRuntime, Log, TEXT("Started owned process '%s': %s %s"), *ProcessName.ToString(), *ExecutablePath, *Spec.Arguments);
    BroadcastStatus(ProcessName, *RecordPtr);
    return true;
}

bool UProcessRuntimeSubsystem::StopProcess(const FManagedProcessId& ProcessId, FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();

    TSharedPtr<FManagedProcessRecord>* Found = Processes.Find(ProcessId.Value);
    if (!Found || !(*Found)->Process || !(*Found)->Status.IsActive())
    {
        return true;
    }

    FManagedProcessRecord& Record = **Found;
    if (Record.Status.State == EManagedProcessState::Stopping)
    {
        return true;
    }
    Record.bExpectedStop = true;
    Record.Status.State = EManagedProcessState::Stopping;
    Record.Process->Cancel(true);
    BroadcastStatus(ProcessId.Value, Record);
    return true;
}

bool UProcessRuntimeSubsystem::SetPriority(
    const FManagedProcessId& ProcessId,
    const EManagedProcessPriority Priority,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();

    TSharedPtr<FManagedProcessRecord>* Found = Processes.Find(ProcessId.Value);
    if (!Found || !(*Found)->Process || !(*Found)->Status.IsActive())
    {
        OutError = FString::Printf(TEXT("Owned process '%s' is not active."), *ProcessId.Value.ToString());
        return false;
    }

    FManagedProcessRecord& Record = **Found;
    if (Record.Status.Priority == Priority)
    {
        return true;
    }

    if (!ApplyWin64JobPriority(Record, Priority, OutError))
    {
        return false;
    }

    Record.Spec.Priority = Priority;
    Record.Status.Priority = Priority;
    UE_LOG(LogProcessRuntime, Verbose, TEXT("Changed owned process '%s' priority to %d."),
        *ProcessId.Value.ToString(), static_cast<int32>(Priority));
    BroadcastStatus(ProcessId.Value, Record);
    return true;
}

bool UProcessRuntimeSubsystem::RestartProcess(const FManagedProcessId& ProcessId, const FManagedProcessSpec& Spec, FString& OutError)
{
    check(IsInGameThread());
    if (TSharedPtr<FManagedProcessRecord>* Found = Processes.Find(ProcessId.Value))
    {
        if ((*Found)->Process && (*Found)->Status.IsActive())
        {
            (*Found)->PendingRestartSpec = Spec;
            return StopProcess(ProcessId, OutError);
        }
    }
    return StartProcess(ProcessId, Spec, OutError);
}

bool UProcessRuntimeSubsystem::GetStatus(const FManagedProcessId& ProcessId, FManagedProcessStatus& OutStatus) const
{
    if (const TSharedPtr<FManagedProcessRecord>* Found = Processes.Find(ProcessId.Value))
    {
        OutStatus = (*Found)->Status;
        return true;
    }
    OutStatus = FManagedProcessStatus();
    return false;
}

bool UProcessRuntimeSubsystem::IsOwnedProcess(const FManagedProcessId& ProcessId) const
{
    return Processes.Contains(ProcessId.Value);
}

void UProcessRuntimeSubsystem::HandleOutputOnGameThread(const FName ProcessName, const uint64 Generation, FString Output)
{
    check(IsInGameThread());
    TSharedPtr<FManagedProcessRecord>* Found = Processes.Find(ProcessName);
    if (!Found || (*Found)->Generation != Generation)
    {
        return;
    }

    FManagedProcessRecord& Record = **Found;
    if (Output.IsEmpty())
    {
        return;
    }

    Record.Status.RecentOutput.Add(Output);
    const int32 MaxLines = FMath::Max(1, Record.Spec.MaxRecentOutputLines);
    if (Record.Status.RecentOutput.Num() > MaxLines)
    {
        Record.Status.RecentOutput.RemoveAt(0, Record.Status.RecentOutput.Num() - MaxLines, EAllowShrinking::No);
    }

    UE_LOG(LogProcessRuntime, Log, TEXT("[%s] %s"), *ProcessName.ToString(), *Output);
    ProcessOutput.Broadcast(FManagedProcessId(ProcessName), Output);
}

void UProcessRuntimeSubsystem::HandleCompletedOnGameThread(const FName ProcessName, const uint64 Generation, const int32 ExitCode)
{
    check(IsInGameThread());
    TSharedPtr<FManagedProcessRecord>* Found = Processes.Find(ProcessName);
    if (!Found || (*Found)->Generation != Generation)
    {
        return;
    }

    FManagedProcessRecord& Record = **Found;
    Record.Status.ExitCode = ExitCode;
    Record.Status.State = Record.bExpectedStop ? EManagedProcessState::Stopped : (ExitCode == 0 ? EManagedProcessState::Exited : EManagedProcessState::Failed);
    if (!Record.bExpectedStop && ExitCode != 0)
    {
        Record.Status.LastError = FString::Printf(TEXT("Process exited unexpectedly with code %d."), ExitCode);
    }
    ReleaseRecordProcess(Record);
    BroadcastStatus(ProcessName, Record);

    if (Record.PendingRestartSpec.IsSet() && !bDeinitializing)
    {
        const FManagedProcessSpec RestartSpec = Record.PendingRestartSpec.GetValue();
        Record.PendingRestartSpec.Reset();
        FString RestartError;
        StartProcess(FManagedProcessId(ProcessName), RestartSpec, RestartError);
    }
}

void UProcessRuntimeSubsystem::HandleCanceledOnGameThread(const FName ProcessName, const uint64 Generation)
{
    check(IsInGameThread());
    TSharedPtr<FManagedProcessRecord>* Found = Processes.Find(ProcessName);
    if (!Found || (*Found)->Generation != Generation)
    {
        return;
    }

    FManagedProcessRecord& Record = **Found;
    Record.Status.ExitCode = -1;
    Record.Status.State = EManagedProcessState::Stopped;
    ReleaseRecordProcess(Record);
    BroadcastStatus(ProcessName, Record);

    if (Record.PendingRestartSpec.IsSet() && !bDeinitializing)
    {
        const FManagedProcessSpec RestartSpec = Record.PendingRestartSpec.GetValue();
        Record.PendingRestartSpec.Reset();
        FString RestartError;
        StartProcess(FManagedProcessId(ProcessName), RestartSpec, RestartError);
    }
}

void UProcessRuntimeSubsystem::BroadcastStatus(const FName ProcessName, const FManagedProcessRecord& Record)
{
    ProcessStateChanged.Broadcast(FManagedProcessId(ProcessName), Record.Status);
}

void UProcessRuntimeSubsystem::ReleaseRecordProcess(FManagedProcessRecord& Record)
{
    Record.Process.Reset();
#if PLATFORM_WINDOWS
    if (Record.JobHandle)
    {
        CloseHandle(static_cast<HANDLE>(Record.JobHandle));
        Record.JobHandle = nullptr;
    }
#else
    Record.JobHandle = nullptr;
#endif
}

bool UProcessRuntimeSubsystem::AssignWin64JobObject(FManagedProcessRecord& Record, FString& OutWarning)
{
    OutWarning.Reset();
#if PLATFORM_WINDOWS
    if (!Record.Process || !Record.Process->GetProcessHandle().IsValid())
    {
        OutWarning = TEXT("Cannot assign Job Object because the process handle is invalid.");
        return false;
    }

    HANDLE JobHandle = CreateJobObject(nullptr, nullptr);
    if (!JobHandle)
    {
        OutWarning = TEXT("CreateJobObject failed; normal process-tree shutdown remains active.");
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits = {};
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PRIORITY_CLASS;
    Limits.BasicLimitInformation.PriorityClass = ToWin64PriorityClass(Record.Spec.Priority);
    if (!SetInformationJobObject(JobHandle, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
    {
        CloseHandle(JobHandle);
        OutWarning = TEXT("SetInformationJobObject failed; normal process-tree shutdown remains active.");
        return false;
    }

    if (!AssignProcessToJobObject(JobHandle, Record.Process->GetProcessHandle().Get()))
    {
        const DWORD ErrorCode = GetLastError();
        CloseHandle(JobHandle);
        OutWarning = FString::Printf(TEXT("AssignProcessToJobObject failed with Win32 error %lu; normal process-tree shutdown remains active."), ErrorCode);
        return false;
    }

    Record.JobHandle = JobHandle;
#endif
    return true;
}

bool UProcessRuntimeSubsystem::ApplyWin64JobPriority(
    FManagedProcessRecord& Record,
    const EManagedProcessPriority Priority,
    FString& OutError)
{
    OutError.Reset();
#if PLATFORM_WINDOWS
    if (!Record.JobHandle)
    {
        OutError = TEXT("Cannot change process priority because the Job Object is unavailable.");
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits = {};
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PRIORITY_CLASS;
    Limits.BasicLimitInformation.PriorityClass = ToWin64PriorityClass(Priority);
    if (!SetInformationJobObject(
        static_cast<HANDLE>(Record.JobHandle),
        JobObjectExtendedLimitInformation,
        &Limits,
        sizeof(Limits)))
    {
        OutError = FString::Printf(TEXT("SetInformationJobObject failed with Win32 error %lu."), GetLastError());
        return false;
    }
    return true;
#else
    OutError = TEXT("Managed process priority changes are currently supported on Windows only.");
    return false;
#endif
}
