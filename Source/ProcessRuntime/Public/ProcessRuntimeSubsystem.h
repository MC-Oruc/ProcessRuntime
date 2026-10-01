#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "ProcessRuntimeTypes.h"
#include "ProcessRuntimeSubsystem.generated.h"

UCLASS()
class PROCESSRUNTIME_API UProcessRuntimeSubsystem final : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    bool StartProcess(const FManagedProcessId& ProcessId, const FManagedProcessSpec& Spec, FString& OutError);
    bool StopProcess(const FManagedProcessId& ProcessId, FString& OutError);
    bool RestartProcess(const FManagedProcessId& ProcessId, const FManagedProcessSpec& Spec, FString& OutError);
    bool SetPriority(const FManagedProcessId& ProcessId, EManagedProcessPriority Priority, FString& OutError);
    bool GetStatus(const FManagedProcessId& ProcessId, FManagedProcessStatus& OutStatus) const;
    bool IsOwnedProcess(const FManagedProcessId& ProcessId) const;

    FOnManagedProcessStateChanged& OnProcessStateChanged() { return ProcessStateChanged; }
    FOnManagedProcessOutput& OnProcessOutput() { return ProcessOutput; }

private:
    struct FManagedProcessRecord;

    void HandleOutputOnGameThread(FName ProcessName, uint64 Generation, FString Output);
    void HandleCompletedOnGameThread(FName ProcessName, uint64 Generation, int32 ExitCode);
    void HandleCanceledOnGameThread(FName ProcessName, uint64 Generation);
    void BroadcastStatus(FName ProcessName, const FManagedProcessRecord& Record);
    void ReleaseRecordProcess(FManagedProcessRecord& Record);
    bool AssignWin64JobObject(FManagedProcessRecord& Record, FString& OutWarning);
    bool ApplyWin64JobPriority(FManagedProcessRecord& Record, EManagedProcessPriority Priority, FString& OutError);

    TMap<FName, TSharedPtr<FManagedProcessRecord>> Processes;
    FOnManagedProcessStateChanged ProcessStateChanged;
    FOnManagedProcessOutput ProcessOutput;
    uint64 NextGeneration = 1;
    bool bDeinitializing = false;
};
