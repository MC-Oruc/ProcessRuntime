#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "ProcessRuntimeSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProcessRuntimeIdTest,
    "ProcessRuntime.Unit.ProcessId",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProcessRuntimeIdTest::RunTest(const FString& Parameters)
{
    const FManagedProcessId Empty;
    const FManagedProcessId Valid(TEXT("Test.Process"));
    TestFalse(TEXT("Default ID is invalid"), Empty.IsValid());
    TestTrue(TEXT("Named ID is valid"), Valid.IsValid());
    TestEqual(TEXT("Named ID is stable"), Valid.Value, FName(TEXT("Test.Process")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProcessRuntimeSpecDefaultsTest,
    "ProcessRuntime.Unit.SpecDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProcessRuntimeSpecDefaultsTest::RunTest(const FString& Parameters)
{
    const FManagedProcessSpec Spec;
    TestTrue(TEXT("Processes are hidden by default"), Spec.bHidden);
    TestTrue(TEXT("Output capture is enabled by default"), Spec.bCaptureOutput);
    TestEqual(TEXT("Recent output is bounded to 200 lines"), Spec.MaxRecentOutputLines, 200);
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
#endif
    return true;
}

#endif
