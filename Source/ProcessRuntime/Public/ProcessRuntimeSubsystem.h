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
    bool GetStatus(const FManagedProcessId& ProcessId, FManagedProcessStatus& OutStatus) const;
    bool IsOwnedProcess(const FManagedProcessId& ProcessId) const;

    FOnManagedProcessStateChanged& OnProcessStateChanged() { return ProcessStateChanged; }
    FOnManagedProcessOutput& OnProcessOutput() { return ProcessOutput; }

private:
    struct FManagedProcessRecord;

    void HandleOutputOnGameThread(FName ProcessName, FString Output);
    void HandleCompletedOnGameThread(FName ProcessName, int32 ExitCode);
    void HandleCanceledOnGameThread(FName ProcessName);
    void BroadcastStatus(FName ProcessName, const FManagedProcessRecord& Record);
    void ReleaseRecordProcess(FManagedProcessRecord& Record);
    bool AssignWin64JobObject(FManagedProcessRecord& Record, FString& OutWarning);

    TMap<FName, TSharedPtr<FManagedProcessRecord>> Processes;
    FOnManagedProcessStateChanged ProcessStateChanged;
    FOnManagedProcessOutput ProcessOutput;
    bool bDeinitializing = false;
};
