#pragma once

#include "CoreMinimal.h"
#include "ProcessRuntimeTypes.generated.h"

UENUM(BlueprintType)
enum class EManagedProcessState : uint8
{
    Stopped,
    Starting,
    Running,
    Stopping,
    Exited,
    Failed
};

USTRUCT(BlueprintType)
struct PROCESSRUNTIME_API FManagedProcessId
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcessRuntime")
    FName Value;

    FManagedProcessId() = default;
    explicit FManagedProcessId(const FName InValue) : Value(InValue) {}

    bool IsValid() const { return !Value.IsNone(); }
    bool operator==(const FManagedProcessId& Other) const { return Value == Other.Value; }
    bool operator!=(const FManagedProcessId& Other) const { return !(*this == Other); }
};

FORCEINLINE uint32 GetTypeHash(const FManagedProcessId& Id)
{
    return GetTypeHash(Id.Value);
}

USTRUCT(BlueprintType)
struct PROCESSRUNTIME_API FManagedProcessSpec
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcessRuntime")
    FString ExecutablePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcessRuntime")
    FString Arguments;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcessRuntime")
    FString WorkingDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcessRuntime")
    bool bHidden = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcessRuntime")
    bool bCaptureOutput = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcessRuntime", meta = (ClampMin = "1", ClampMax = "10000"))
    int32 MaxRecentOutputLines = 200;
};

USTRUCT(BlueprintType)
struct PROCESSRUNTIME_API FManagedProcessStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ProcessRuntime")
    EManagedProcessState State = EManagedProcessState::Stopped;

    UPROPERTY(BlueprintReadOnly, Category = "ProcessRuntime")
    int32 ExitCode = 0;

    UPROPERTY(BlueprintReadOnly, Category = "ProcessRuntime")
    FString LastError;

    UPROPERTY(BlueprintReadOnly, Category = "ProcessRuntime")
    TArray<FString> RecentOutput;

    UPROPERTY(BlueprintReadOnly, Category = "ProcessRuntime")
    FDateTime StartedAtUtc;

    bool IsActive() const
    {
        return State == EManagedProcessState::Starting || State == EManagedProcessState::Running || State == EManagedProcessState::Stopping;
    }
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnManagedProcessStateChanged, const FManagedProcessId&, const FManagedProcessStatus&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnManagedProcessOutput, const FManagedProcessId&, const FString&);
