#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "ProcessRuntimeSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProcessRuntimePriorityTest,
    "ProcessRuntime.Runtime.Priority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProcessRuntimePriorityTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
    UProcessRuntimeSubsystem* Runtime = GEngine ? GEngine->GetEngineSubsystem<UProcessRuntimeSubsystem>() : nullptr;
    TestNotNull(TEXT("ProcessRuntime subsystem exists"), Runtime);
    if (!Runtime) return false;

    FManagedProcessSpec Spec;
    Spec.ExecutablePath = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
    Spec.Arguments = TEXT("/C \"ping -n 5 127.0.0.1 >nul\"");
    Spec.bCaptureOutput = false;
    Spec.Priority = EManagedProcessPriority::BelowNormal;
    const FManagedProcessId ProcessId(*FString::Printf(TEXT("Test.Priority.%llu"), FPlatformTime::Cycles64()));
    FString Error;
    if (!TestTrue(TEXT("Priority test process launches"), Runtime->StartProcess(ProcessId, Spec, Error)))
    {
        AddError(Error);
        return false;
    }

    FManagedProcessStatus Status;
    TestTrue(TEXT("Priority status is available"), Runtime->GetStatus(ProcessId, Status));
    TestEqual(TEXT("Initial priority is below normal"), Status.Priority, EManagedProcessPriority::BelowNormal);
    TestTrue(TEXT("Active process priority changes"), Runtime->SetPriority(ProcessId, EManagedProcessPriority::AboveNormal, Error));
    TestTrue(TEXT("Updated priority status is available"), Runtime->GetStatus(ProcessId, Status));
    TestEqual(TEXT("Updated priority is above normal"), Status.Priority, EManagedProcessPriority::AboveNormal);
    TestTrue(TEXT("Priority test process stops"), Runtime->StopProcess(ProcessId, Error));
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProcessRuntimeInvalidExecutableTest,
    "ProcessRuntime.Unit.InvalidExecutable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProcessRuntimeInvalidExecutableTest::RunTest(const FString& Parameters)
{
    UProcessRuntimeSubsystem* Runtime = GEngine ? GEngine->GetEngineSubsystem<UProcessRuntimeSubsystem>() : nullptr;
    TestNotNull(TEXT("ProcessRuntime subsystem exists"), Runtime);
    if (!Runtime) return false;
    FManagedProcessSpec Spec;
    Spec.ExecutablePath = TEXT("Z:/TextGen/DoesNotExist/process.exe");
    FString Error;
    TestFalse(TEXT("Missing executable is rejected"), Runtime->StartProcess(FManagedProcessId(TEXT("Test.InvalidExecutable")), Spec, Error));
    TestTrue(TEXT("Missing executable returns a useful error"), Error.Contains(TEXT("does not exist")));
    return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
    FWaitForManagedProcessOutput,
    FAutomationTestBase*, Test,
    FManagedProcessId, ProcessId,
    double, DeadlineSeconds);

bool FWaitForManagedProcessOutput::Update()
{
    UProcessRuntimeSubsystem* Runtime = GEngine ? GEngine->GetEngineSubsystem<UProcessRuntimeSubsystem>() : nullptr;
    if (!Runtime)
    {
        Test->AddError(TEXT("ProcessRuntime subsystem disappeared."));
        return true;
    }
    FManagedProcessStatus Status;
    if (!Runtime->GetStatus(ProcessId, Status)) return false;
    if (Status.State == EManagedProcessState::Exited || Status.State == EManagedProcessState::Failed)
    {
        Test->TestEqual(TEXT("Process exits normally"), Status.State, EManagedProcessState::Exited);
        const FString Output = FString::Join(Status.RecentOutput, TEXT("\n"));
        Test->TestTrue(TEXT("stdout is captured"), Output.Contains(TEXT("process-runtime-stdout")));
        Test->TestTrue(TEXT("stderr is captured"), Output.Contains(TEXT("process-runtime-stderr")));
        return true;
    }
    if (FPlatformTime::Seconds() >= DeadlineSeconds)
    {
        FString Error;
        Runtime->StopProcess(ProcessId, Error);
        Test->AddError(TEXT("Timed out waiting for managed process completion."));
        return true;
    }
    return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FVerifyIdempotentManagedProcessStop,
    FAutomationTestBase*, Test,
    FManagedProcessId, ProcessId);

bool FVerifyIdempotentManagedProcessStop::Update()
{
    UProcessRuntimeSubsystem* Runtime = GEngine ? GEngine->GetEngineSubsystem<UProcessRuntimeSubsystem>() : nullptr;
    if (!Runtime)
    {
        Test->AddError(TEXT("ProcessRuntime subsystem disappeared."));
        return true;
    }
    FString Error;
    Test->TestTrue(TEXT("Stopping an exited process succeeds"), Runtime->StopProcess(ProcessId, Error));
    Error.Reset();
    Test->TestTrue(TEXT("Repeated stop remains idempotent"), Runtime->StopProcess(ProcessId, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProcessRuntimeOutputAndCompletionTest,
    "ProcessRuntime.Runtime.OutputAndCompletion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProcessRuntimeOutputAndCompletionTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
    UProcessRuntimeSubsystem* Runtime = GEngine ? GEngine->GetEngineSubsystem<UProcessRuntimeSubsystem>() : nullptr;
    TestNotNull(TEXT("ProcessRuntime subsystem exists"), Runtime);
    if (!Runtime) return false;
    FManagedProcessSpec Spec;
    Spec.ExecutablePath = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
    Spec.Arguments = TEXT("/C \"echo process-runtime-stdout & echo process-runtime-stderr 1>&2\"");
    Spec.MaxRecentOutputLines = 10;
    const FManagedProcessId ProcessId(TEXT("Test.OutputAndCompletion"));
    FString Error;
    if (!TestTrue(TEXT("Test process launches"), Runtime->StartProcess(ProcessId, Spec, Error)))
    {
        AddError(Error);
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForManagedProcessOutput(this, ProcessId, FPlatformTime::Seconds() + 10.0));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyIdempotentManagedProcessStop(this, ProcessId));
#endif
    return true;
}

#endif
