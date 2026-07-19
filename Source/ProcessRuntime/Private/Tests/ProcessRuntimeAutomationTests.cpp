#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
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

#endif
