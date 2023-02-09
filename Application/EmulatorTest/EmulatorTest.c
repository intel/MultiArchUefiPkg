/** @file

    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include <Library/CpuLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include "TestProtocol.h"

#define NO_INLINE __attribute__((noinline))
#pragma GCC diagnostic ignored "-Wunused-variable"

STATIC EFI_GUID mEmuTestProtocolGuid = EMU_TEST_PROTOCOL_GUID;
STATIC UINT64 TestArray[EFI_PAGE_SIZE / sizeof (UINT64)];
STATIC EMU_TEST_DEBUG_STATE mBeginDebugState;
STATIC EMU_TEST_PROTOCOL *mTest = NULL;

STATIC VOID
LogResult (
  IN  CONST CHAR8   *String,
  IN        BOOLEAN Result
  )
{
  STATIC UINT32 Index = 0;

  DEBUG ((Result ? DEBUG_INFO : DEBUG_ERROR, "Test %03u:\t%a: %a\n",
          Index++, Result ? "PASS" : "FAIL", String));
}

UINT64
EFIAPI
TestExit (
 VOID
 )
{
  ProcessLibraryDestructorList (gImageHandle, gST);
  return gBS->Exit(gImageHandle, EFI_SUCCESS, 0, NULL);
}

STATIC
UINT64
EFIAPI
TestCbArgs (
  IN  UINT64 Arg1,
  IN  UINT64 Arg2,
  IN  UINT64 Arg3,
  IN  UINT64 Arg4,
  IN  UINT64 Arg5,
  IN  UINT64 Arg6,
  IN  UINT64 Arg7,
  IN  UINT64 Arg8,
  IN  UINT64 Arg9,
  IN  UINT64 Arg10,
  IN  UINT64 Arg11,
  IN  UINT64 Arg12,
  IN  UINT64 Arg13,
  IN  UINT64 Arg14,
  IN  UINT64 Arg15,
  IN  UINT64 Arg16
  )
{
  if (Arg1 == ARG_VAL(1) &&
      Arg2 == ARG_VAL(2) &&
      Arg3 == ARG_VAL(3) &&
      Arg4 == ARG_VAL(4) &&
      Arg5 == ARG_VAL(5) &&
      Arg6 == ARG_VAL(6) &&
      Arg7 == ARG_VAL(7) &&
      Arg8 == ARG_VAL(8) &&
      Arg9 == ARG_VAL(9) &&
      Arg10 == ARG_VAL(10) &&
      Arg11 == ARG_VAL(11) &&
      Arg12 == ARG_VAL(12) &&
      Arg13 == ARG_VAL(13) &&
      Arg14 == ARG_VAL(14) &&
      Arg15 == ARG_VAL(15) &&
      Arg16 == ARG_VAL(16)) {
    return EFI_SUCCESS;
  }

  return EFI_INVALID_PARAMETER;
}

STATIC
VOID
EFIAPI
TestCbLj (
  IN  VOID *Buffer
  )
{
  DEBUG ((DEBUG_INFO, "Now calling TestLj\n"));
  mTest->TestLj (Buffer);

  UNREACHABLE ();
}

STATIC
VOID
DoTestProtocolTests (
  IN  EMU_TEST_PROTOCOL *Test
  )
{
  UINT64 Ret;
  char *hostType = "unknown";
  char *myType = "unknown";

  switch (mBeginDebugState.HostMachineType) {
  case EFI_IMAGE_MACHINE_AARCH64:
    hostType = "AArch64";
    break;
  case EFI_IMAGE_MACHINE_RISCV64:
    hostType = "RiscV64";
    break;
  case EFI_IMAGE_MACHINE_X64:
    hostType = "X64";
    break;
  default:
    hostType = "unknown";
  }

  switch (mBeginDebugState.CallerMachineType) {
  case EFI_IMAGE_MACHINE_AARCH64:
    myType = "AArch64";
    break;
  case EFI_IMAGE_MACHINE_RISCV64:
    myType = "RiscV64";
    break;
  case EFI_IMAGE_MACHINE_X64:
    myType = "X64";
    break;
  default:
    myType = "unknown";
  }

  DEBUG ((DEBUG_INFO, "%a tester running on %a host\n", myType, hostType));

  Ret = Test->TestRet ();
  LogResult ("Value return", Ret == RET_VAL);

  Ret = Test->TestArgs (ARG_VAL(1), ARG_VAL(2), ARG_VAL(3), ARG_VAL(4),
                        ARG_VAL(5), ARG_VAL(6), ARG_VAL(7), ARG_VAL(8),
                        ARG_VAL(9), ARG_VAL(10), ARG_VAL(11), ARG_VAL(12),
                        ARG_VAL(13), ARG_VAL(14), ARG_VAL(15), ARG_VAL(16));
  LogResult ("Argument passing", Ret == EFI_SUCCESS);

  Ret = Test->TestCbArgs(TestCbArgs);
  LogResult ("Callback args passing", Ret == EFI_SUCCESS);

  Ret = Test->TestSj (TestCbLj);
  LogResult ("TestSj/TestLj passing", Ret == EFI_SUCCESS);
}

STATIC
NO_INLINE
VOID
TestNullCall (
  VOID
  )
{
  UINT64 Ret;
  UINT64 EFIAPI (*Call)(VOID) = (VOID *) 8;

  Ret = Call ();
  LogResult ("NULL(0x8) call", Ret == EFI_UNSUPPORTED);
}

STATIC
NO_INLINE
VOID
TestNullCall2 (
  VOID
  )
{
  UINT64 Ret;
  UINT64 EFIAPI (*Call)(VOID) = (VOID *) 0;

  /*
   * Different from TestNullCall to catch edge case behavior
   * for fetching instructions from 0.
   */

  Ret = Call ();
  LogResult ("NULL(0x0) call", Ret == EFI_UNSUPPORTED);
}

STATIC
NO_INLINE
VOID
TestNullDeref (
  VOID
  )
{
  volatile UINT64 *Ptr = (UINT64 *) 0x8;

  LogResult ("NULL read", *Ptr == 0xAFAFAFAFAFAFAFAFUL);

  *Ptr = (UINT64) Ptr;
  asm volatile("" : : : "memory");
  LogResult ("NULL write", TRUE);
}

STATIC
NO_INLINE
VOID
TestCpuSleep (
  VOID
  )
{
  CpuSleep ();
  LogResult ("CpuSleep", TRUE);
}

STATIC
EFIAPI
VOID
TestTimerHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  BOOLEAN *IsDone = Context;

  *IsDone = TRUE;
}

STATIC
NO_INLINE
UINT64
TestPerfMyCall1 (
  VOID
  )
{
  return 1;
}

STATIC
NO_INLINE
UINT64
TestPerfEmpty (
  IN  volatile BOOLEAN *IsDone
  )
{
  UINT64 Result;

  /*
   * Test empty loops.
   */

  Result = 0;
  while (!*IsDone) {
    Result += 1;
  }

  return Result;
}

STATIC
NO_INLINE
UINT64
TestPerfMyCall (
  IN  volatile BOOLEAN *IsDone
  )
{
  UINT64 Result;

  /*
   * Test emulated function call loops.
   */

  Result = 0;
  while (!*IsDone) {
    Result += TestPerfMyCall1 ();
  }

  return Result;
}

STATIC
NO_INLINE
UINT64
TestPerfNativeCall (
  IN  volatile BOOLEAN *IsDone
  )
{
  UINT64 Result;

  /*
   * Test emulated function call loops.
   */

  Result = 0;
  while (!*IsDone) {
    gBS->GetNextMonotonicCount (NULL);
    Result++;
  }

  return Result;
}

#define GEN_TEST_PERF_LOAD(x)                               \
  STATIC                                                    \
  NO_INLINE                                                 \
  UINT64                                                    \
  TestPerfLoad##x (                                         \
    IN  UINT64   *TestArray,                                \
    IN  UINTN    ArrayLen,                                  \
    IN  volatile BOOLEAN *IsDone                            \
    )                                                       \
  {                                                         \
    UINT64 Result;                                          \
                                                            \
    Result = 0;                                             \
    while (!*IsDone) {                                      \
      Result += *(UINT##x *) &TestArray[Result % ArrayLen]; \
    }                                                       \
                                                            \
    return Result;                                          \
  }
GEN_TEST_PERF_LOAD (64)
GEN_TEST_PERF_LOAD (32)
GEN_TEST_PERF_LOAD (16)
GEN_TEST_PERF_LOAD (8)
#undef GEN_TEST_PERF_LOAD

#define GEN_TEST_PERF_STORE(x)                        \
  STATIC                                              \
  NO_INLINE                                           \
  UINT##x                                             \
  TestPerfStore##x (                                  \
    IN  UINT64   *TestArray,                          \
    IN  UINTN    ArrayLen,                            \
    IN  volatile BOOLEAN *IsDone                      \
    )                                                 \
  {                                                   \
    UINT64 Result;                                    \
                                                      \
    Result = 0;                                       \
    while (!*IsDone) {                                \
      *(UINT##x *) &TestArray[Result % ArrayLen] = 1; \
      Result++;                                       \
    }                                                 \
                                                      \
    return Result;                                    \
  }
GEN_TEST_PERF_STORE (64)
GEN_TEST_PERF_STORE (32)
GEN_TEST_PERF_STORE (16)
GEN_TEST_PERF_STORE (8)
#undef GEN_TEST_PERF_STORE

STATIC
NO_INLINE
VOID
TestPerf (
  VOID
  )
{
  UINT64 Result;
  EFI_STATUS Status;
  EFI_EVENT OneShotTimer;
  volatile BOOLEAN IsDone;
  UINTN Index;

  DEBUG ((DEBUG_INFO, "Doing perf tests...\n"));

  Status = gBS->CreateEventEx (
    EVT_TIMER | EVT_NOTIFY_SIGNAL, // Type
    TPL_CALLBACK,                  // NotifyTpl
    TestTimerHandler,              // NotifyFunction
    (VOID *)&IsDone,               // NotifyContext
    NULL,                          // No group
    &OneShotTimer                  // Event
    );
  ASSERT_EFI_ERROR (Status);

#define PREP() do {                             \
    IsDone = 0;                                 \
    Status = gBS->SetTimer (                    \
      OneShotTimer,                             \
      TimerRelative,                            \
      EFI_TIMER_PERIOD_SECONDS (1)              \
      );                                        \
    ASSERT_EFI_ERROR (Status);                  \
  } while (0)

#define PTEST(x) do {                           \
    PREP();                                     \
    DEBUG ((DEBUG_INFO, #x " loops %u\n",       \
            TestPerf##x (&IsDone)));            \
  } while (0)

  PTEST (Empty);
  PTEST (MyCall);
  PTEST (NativeCall);

  for (Index = 0; Index < ARRAY_SIZE (TestArray); Index++) {
    TestArray[Index] = 1;
  }

#undef PTEST
#define PTEST(x) do {                                                   \
    PREP();                                                             \
    DEBUG ((DEBUG_INFO, #x " loops %u\n",                               \
            TestPerf##x (TestArray, ARRAY_SIZE (TestArray), &IsDone))); \
  } while (0)

  PTEST (Load64);
  PTEST (Load32);
  PTEST (Load16);
  PTEST (Load8);

  PTEST (Store64);
  PTEST (Store32);
  PTEST (Store16);
  PTEST (Store8);

#undef PTEST

  LogResult ("TestPerf", TRUE);
  gBS->CloseEvent (OneShotTimer);
}

STATIC
NO_INLINE
VOID
TestTimer (
  BOOLEAN WithCpuSleep
  )
{
  EFI_STATUS Status;
  EFI_EVENT OneShotTimer;
  volatile BOOLEAN IsDone = FALSE;

  Status = gBS->CreateEvent (
    EVT_TIMER | EVT_NOTIFY_SIGNAL, // Type
    TPL_CALLBACK,                  // NotifyTpl
    TestTimerHandler,              // NotifyFunction
    (VOID *)&IsDone,               // NotifyContext
    &OneShotTimer                  // Event
    );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->SetTimer (
    OneShotTimer,
    TimerRelative,
    EFI_TIMER_PERIOD_SECONDS (1)
    );
  ASSERT_EFI_ERROR (Status);

  while (!IsDone) {
    if (WithCpuSleep) {
      CpuSleep ();
    }
  }

  if (WithCpuSleep) {
    LogResult ("CpuSleep loop + timer", TRUE);
  } else {
    LogResult ("Tight loop + timer", TRUE);
  }
  gBS->CloseEvent (OneShotTimer);
}

EFI_STATUS
EFIAPI
EmulatorTestEntryPoint (
  IN  EFI_HANDLE       ImageHandle,
  IN  EFI_SYSTEM_TABLE *SystemTable
  )
{
  gBS->LocateProtocol (&mEmuTestProtocolGuid, NULL, (VOID **) &mTest);
  if (mTest == NULL) {
    DEBUG ((DEBUG_ERROR, "EMU_TEST_PROTOCOL is missing\n"));
  } else {
    mTest->TestGetDebugState (&mBeginDebugState);
    DEBUG ((DEBUG_INFO, "Initial %lu contexts\n", mBeginDebugState.ContextCount));
    DoTestProtocolTests (mTest);

    if (mBeginDebugState.HostMachineType !=
        mBeginDebugState.CallerMachineType) {
      /*
       * Only run these tests if we know we are being emulated.
       */
      TestNullCall ();
      TestNullCall2 ();
      TestNullDeref ();
    }
  }

  TestCpuSleep ();
  TestTimer (FALSE);
  TestTimer (TRUE);
  TestPerf ();
  DEBUG ((DEBUG_INFO, "Tests completed!\n"));

  if (mTest != NULL) {
    EMU_TEST_DEBUG_STATE DebugState;
    mTest->TestGetDebugState (&DebugState);

    DEBUG ((DEBUG_INFO, "Contexts total %lu = X64 %lu + AArch64 %lu\n",
            DebugState.ContextCount, DebugState.X86ContextCount,
            DebugState.AArch64ContextCount));
    ASSERT ((DebugState.ContextCount == (DebugState.X86ContextCount +
                                         DebugState.AArch64ContextCount)));
    DEBUG ((DEBUG_INFO, "Emu timeout period %lu ms\n",
            DebugState.ExitPeriodMs));
    DEBUG ((DEBUG_INFO, "X64 timeout period %lu ticks 0x%lx tbs\n",
            DebugState.X86ExitPeriodTicks, DebugState.X86ExitPeriodTbs));
    DEBUG ((DEBUG_INFO, "AArch64 timeout period %lu ticks 0x%lu tbs\n",
            DebugState.AArch64ExitPeriodTicks,
            DebugState.AArch64ExitPeriodTbs));

    mTest->TestCbArgs((VOID *) TestExit);
    return EFI_ABORTED;
  }

  return EFI_SUCCESS;
}
