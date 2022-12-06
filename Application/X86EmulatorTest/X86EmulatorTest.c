/** @file

    Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>

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
#include "TestProtocol.h"

#define NO_INLINE __attribute__((noinline))
#pragma GCC diagnostic ignored "-Wunused-variable"

STATIC EFI_GUID mX86EmuTestProtocolGuid = X86_EMU_TEST_PROTOCOL_GUID;
STATIC UINT64 TestArray[EFI_PAGE_SIZE / sizeof (UINT64)];

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

STATIC
UINT64
EFIAPI
TestCb (
  IN  UINT64 Arg1,
  IN  UINT64 Arg2
  )
{
  return Arg1 + Arg2;
}

STATIC
VOID
DoTestProtocolTests (
  IN  X86_EMU_TEST_PROTOCOL *Test
  )
{
  UINT64 Ret;
  RET16 Ret16;
  RET_LARGE LargeRet;
  char *hostType = "unknown";

  if (Test->HostMachineType == EFI_IMAGE_MACHINE_AARCH64) {
    hostType = "AArch64";
  } else if (Test->HostMachineType == EFI_IMAGE_MACHINE_RISCV64) {
    hostType = "RiscV64";
  }

  DEBUG ((DEBUG_INFO, "Running on %a host\n", hostType));

  Ret = Test->TestRet ();
  LogResult ("Value return", Ret == RET_VAL);

  Ret16.Field1 = 0xAA;
  Ret16.Field2 = 0xBB;
  Ret16 = Test->TestRet16 (ARG_VAL(1));
  LogResult ("16-byte value return",
             Ret16.Field1 == FIELD_VAL(1) &&
             Ret16.Field2 == FIELD_VAL(2));

  LargeRet.Field1 = 0xAA;
  LargeRet.Field2 = 0xBB;
  LargeRet.Field3 = 0xCC;
  LargeRet.Field4 = 0xDD;
  LargeRet = Test->TestLargeRet (ARG_VAL(1));
  LogResult ("Large value return",
             LargeRet.Field1 == FIELD_VAL(1) &&
             LargeRet.Field2 == FIELD_VAL(2) &&
             LargeRet.Field3 == FIELD_VAL(3) &&
             LargeRet.Field4 == FIELD_VAL(4));

  Ret = Test->TestArgs (ARG_VAL(1), ARG_VAL(2), ARG_VAL(3), ARG_VAL(4),
                        ARG_VAL(5), ARG_VAL(6), ARG_VAL(7), ARG_VAL(8),
                        ARG_VAL(9), ARG_VAL(10), ARG_VAL(11), ARG_VAL(12),
                        ARG_VAL(13), ARG_VAL(14), ARG_VAL(15), ARG_VAL(16));
  LogResult ("Argument passing", Ret == EFI_SUCCESS);

  Ret = Test->TestCb(ARG_VAL(1), ARG_VAL(2), TestCb);
  LogResult ("Native->emulated thunk", Ret == ARG_VAL(1) + ARG_VAL(2));
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
TestHlt (
  VOID
  )
{
  asm volatile("hlt");
  LogResult ("hlt", TRUE);
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
TestPerfEmuCall1 (
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
TestPerfEmuCall (
  IN  volatile BOOLEAN *IsDone
  )
{
  UINT64 Result;

  /*
   * Test emulated function call loops.
   */

  Result = 0;
  while (!*IsDone) {
    Result += TestPerfEmuCall1 ();
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

  Status = gBS->CreateEvent (
    EVT_TIMER | EVT_NOTIFY_SIGNAL, // Type
    TPL_CALLBACK,                  // NotifyTpl
    TestTimerHandler,              // NotifyFunction
    (VOID *)&IsDone,               // NotifyContext
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
  PTEST (EmuCall);
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
  VOID
  )
{
  EFI_STATUS Status;
  EFI_EVENT OneShotTimer;
  volatile BOOLEAN IsDone = FALSE;

  DEBUG ((DEBUG_INFO, "Testing timer handler %p delivery\n", TestTimerHandler));
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

  while (!IsDone) { CpuSleep(); };
  LogResult ("timer", TRUE);
  gBS->CloseEvent (OneShotTimer);
}

EFI_STATUS
EFIAPI
X86EmulatorTestEntryPoint (
  IN  EFI_HANDLE       ImageHandle,
  IN  EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  X86_EMU_TEST_PROTOCOL *Test;

  Status = gBS->LocateProtocol (&mX86EmuTestProtocolGuid, NULL, (VOID **) &Test);
  if (Status != EFI_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "X86_EMU_TEST_PROTOCOL is missing\n"));
  } else {
    DoTestProtocolTests (Test);
    /*
     * Not all of these work with the old X86Emulator implementation...
     */
    TestNullCall ();
    TestNullCall2 ();
    TestNullDeref ();
    TestHlt ();
    TestTimer ();
  }

  TestPerf ();
  DEBUG ((DEBUG_INFO, "Tests completed!\n"));

  return EFI_SUCCESS;
}
