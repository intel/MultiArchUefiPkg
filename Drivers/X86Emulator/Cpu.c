/** @file

    Copyright (c) 2017 Alexander Graf <agraf@suse.de>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include <unicorn.h>
#include "X86Emulator.h"

#define MAX_ARGS               16
#define EMU_STACK_SIZE         (1024 * 1024)
#define NATIVE_STACK_SIZE      (1024 * 1024)
#define RETURN_TO_NATIVE_MAGIC ((UINTN) &CpuReturnToNative)
#define SYSV_X64_ABI_REDZONE   128
#define CURRENT_FP()           ((EFI_PHYSICAL_ADDRESS) __builtin_frame_address(0))

CpuContext CpuX86;
STATIC CpuRunContext *mTopContext;

#ifndef EMU_TIMEOUT_NONE
#define UC_EMU_EXIT_PERIOD_TB_MAX     0x100000
#define UC_EMU_EXIT_PERIOD_TB_INITIAL 0x1000
#define UC_EMU_EXIT_PERIOD_TB_MIN     0x100
#define UC_EMU_EXIT_PERIOD_MS         10
#endif /* EMU_TIMEOUT_NONE */

#ifdef ON_PRIVATE_STACK
STATIC BASE_LIBRARY_JUMP_BUFFER mOriginalStack;
STATIC EFI_PHYSICAL_ADDRESS mNativeStackStart;
STATIC EFI_PHYSICAL_ADDRESS mNativeStackTop;
#endif /* ON_PRIVATE_STACK */

typedef enum {
  CPU_REASON_INVALID,
  CPU_REASON_NONE,
  CPU_REASON_RETURN_TO_NATIVE,
  CPU_REASON_CALL_TO_NATIVE,
  CPU_REASON_FAILED_EMU,
} CpuExitReason;

#define INSN_INT3 0xcc
/*
 * Never executed, only the address is used by CpuIsNativeCb/CpuRunCtxInternal
 * to detect return back to native code.
 */
static UINT8 CpuReturnToNative[] = { INSN_INT3 };

STATIC
BOOLEAN
CpuIsNativeCb (
  IN  uc_engine *UE,
  IN  UINT64    Address,
  IN  VOID      *UserData)
{
  if (Address == RETURN_TO_NATIVE_MAGIC || EmulatorIsNativeCall (Address)) {
    return TRUE;
  }

  return FALSE;
}

#ifndef EMU_TIMEOUT_NONE
STATIC
VOID
CpuTimeoutCb (
  IN  uc_engine *UE,
  IN  UINT64    Address,
  IN  UINT32    Size,
  IN  VOID      *UserData)
{
  CpuContext *Cpu = mTopContext->Cpu;
  /*
   * GetPerformanceCounter () is a system register read, and is more expensive
   * than reading a variable. Moreover, in an emulated environment,
   * GetPerformanceCounter () is extremely expensive, as it's likely a native
   * helper.
   *
   * So the UC_HOOK_BLOCK callback is going to be as simple, fast and short
   * as possible. mExitPeriodTbs is then re-calibrated once we return
   * from uc_emu_start.
   */
  if ((++(Cpu->TbCount) & (Cpu->ExitPeriodTbs - 1)) == 0) {
    mTopContext->StoppedOnTimeout = TRUE;
    uc_emu_stop (UE);
  }
}
#endif /* EMU_TIMEOUT_NONE */

STATIC
UINT32
CpuIoReadCb (
  IN  uc_engine *UE,
  IN  UINT32    Port,
  IN  UINT32    Size,
  IN  VOID      *UserData)
{
  UINT32 Result = 0;

  switch (Size) {
  case 1:
    gCpuIo2->Io.Read(gCpuIo2, EfiCpuIoWidthUint8, Port, 1, &Result);
    break;
  case 2:
    gCpuIo2->Io.Read(gCpuIo2, EfiCpuIoWidthUint16, Port, 1, &Result);
    break;
  default:
    ASSERT (Size == 4);
    gCpuIo2->Io.Read(gCpuIo2, EfiCpuIoWidthUint32, Port, 1, &Result);
    break;
  }

  DEBUG ((DEBUG_VERBOSE, "PCI I/O read%u from 0x%x = 0x%x\n", Size,
          Port, Result));
  return Result;
}

STATIC
VOID
CpuIoWriteCb (
  IN  uc_engine *UE,
  IN  UINT32    Port,
  IN  UINT32    Size,
  IN  UINT32    Value,
  IN  VOID      *UserData)
{
  DEBUG ((DEBUG_VERBOSE, "PCI I/O write%u to 0x%x = 0x%x\n", Size,
          Port, Value));

  switch (Size) {
  case 1:
    gCpuIo2->Io.Write(gCpuIo2, EfiCpuIoWidthUint8, Port, 1, &Value);
    break;
  case 2:
    gCpuIo2->Io.Write(gCpuIo2, EfiCpuIoWidthUint16, Port, 1, &Value);
    break;
  default:
    ASSERT (Size == 4);
    gCpuIo2->Io.Write(gCpuIo2, EfiCpuIoWidthUint32, Port, 1, &Value);
    break;
  }
}

STATIC
UINT64
CpuNullReadCb (
  IN  uc_engine *UE,
  IN  UINT64    Offset,
  IN  UINT32    Size,
  IN  VOID      *UserData)
{
  DEBUG ((DEBUG_ERROR, "UINT%u NULL-ptr read to 0x%lx\n",
          Size * 8, Offset));

  DEBUG_CODE_BEGIN (); {
    EmulatorDump ();
  } DEBUG_CODE_END ();

  return 0xAFAFAFAFAFAFAFAFUL;
}

STATIC
VOID
CpuNullWriteCb (
  IN  uc_engine *UE,
  IN  UINT64    Offset,
  IN  UINT32    Size,
  IN  UINT64    Value,
  IN  VOID      *UserData)
{
  DEBUG ((DEBUG_ERROR, "UINT%u NULL-ptr write to 0x%lx\n",
          Size * 8, Offset));

  DEBUG_CODE_BEGIN (); {
    EmulatorDump ();
  } DEBUG_CODE_END ();
}

STATIC
VOID
CpuCleanupEx (
  IN  CpuContext *Cpu
  )
{
  uc_err UcErr;
  ASSERT (Cpu != NULL);
  ASSERT (Cpu->UE != NULL);

  UcErr = uc_close (Cpu->UE);
  ASSERT (UcErr == UC_ERR_OK);
}

VOID
CpuCleanup (
  VOID
  )
{
  CpuCleanupEx (&CpuX86);
}

STATIC
EFI_STATUS
CpuPrivateStackInit (
  VOID
  )
{
#ifdef ON_PRIVATE_STACK
  EFI_STATUS Status;

  Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData,
                               EFI_SIZE_TO_PAGES (NATIVE_STACK_SIZE), &mNativeStackStart);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to allocate private stack: %r\n", Status));
    return Status;
  }
  mNativeStackTop = mNativeStackStart + NATIVE_STACK_SIZE - EFI_PAGE_SIZE;
  Status = gCpu->SetMemoryAttributes (gCpu, mNativeStackStart, NATIVE_STACK_SIZE,
                                      EFI_MEMORY_XP);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gCpu->SetMemoryAttributes (gCpu, mNativeStackTop,
                                      EFI_PAGE_SIZE,
                                      EFI_MEMORY_RO | EFI_MEMORY_XP);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gCpu->SetMemoryAttributes (gCpu, mNativeStackStart,
                                      EFI_PAGE_SIZE,
                                      EFI_MEMORY_RO | EFI_MEMORY_XP);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  DEBUG ((DEBUG_INFO, "Native stack is at 0x%lx-0x%lx\n", mNativeStackStart, mNativeStackTop));

#endif /* ON_PRIVATE_STACK */
  return EFI_SUCCESS;
}

STATIC
VOID
CpuX86Dump (
  IN  CpuContext *Cpu
  )
{
         UINT64 Val;
  UNUSED UINTN  Printed = 0;

#define REGS()                                  \
  REG(RIP);                                     \
  REG(RFLAGS);                                  \
  REG(RDI);                                     \
  REG(RSI);                                     \
  REG(RBP);                                     \
  REG(RSP);                                     \
  REG(RBX);                                     \
  REG(RDX);                                     \
  REG(RCX);                                     \
  REG(RAX);                                     \
  REG(R8);                                      \
  REG(R9);                                      \
  REG(R10);                                     \
  REG(R11);                                     \
  REG(R12);                                     \
  REG(R13);                                     \
  REG(R14);                                     \
  REG(R15);

#define REG(x) do {                                     \
    Printed++;                                          \
    Val = REG_READ (Cpu, UC_X86_REG_##x);               \
    DEBUG ((DEBUG_ERROR, "%6a = 0x%016lx", #x, Val));   \
    if ((Printed & 1) == 0) {                           \
      DEBUG ((DEBUG_ERROR, "\n"));                      \
    } else {                                            \
      DEBUG ((DEBUG_ERROR, " "));                       \
    }                                                   \
  } while (0);

  REGS ();

#undef REG
#undef REGS
}

STATIC
VOID
CpuStackPushRedZone (
  IN  CpuContext *Cpu
  )
{
  UINT64 Rsp;

  Rsp = REG_READ (Cpu, Cpu->StackReg);
  Rsp -= SYSV_X64_ABI_REDZONE;
  REG_WRITE (Cpu, Cpu->StackReg, Rsp);
}

UINT64
CpuStackPop64 (
  IN  CpuContext *Cpu
  )
{
  UINT64 Rsp;
  UINT64 Val;

  Rsp = REG_READ (Cpu, Cpu->StackReg);
  Val = *(UINT64 *) Rsp;
  Rsp += 8;
  REG_WRITE (Cpu, Cpu->StackReg, Rsp);
  return Val;
}

STATIC
VOID
CpuStackPush64 (
  IN  CpuContext *Cpu,
  IN  UINT64     Val
  )
{
  UINT64 Rsp;

  Rsp = REG_READ (Cpu, Cpu->StackReg);
  Rsp -= 8;
  REG_WRITE (Cpu, Cpu->StackReg, Rsp);

  *(UINT64 *) Rsp = Val;
}

STATIC
VOID
CpuX86EmuThunkPre (
  IN  struct CpuContext *Cpu,
  IN  UINT64            *Args
  )
{
  unsigned Index;

  REG_WRITE (Cpu, UC_X86_REG_RCX, Args[0]);
  REG_WRITE (Cpu, UC_X86_REG_RDX, Args[1]);
  REG_WRITE (Cpu, UC_X86_REG_R8, Args[2]);
  REG_WRITE (Cpu, UC_X86_REG_R9, Args[3]);

  for (Index = 0; Index < (MAX_ARGS - 4); Index++) {
    /*
     * Push arguments on stack in reverse order.
     */
    CpuStackPush64 (Cpu, Args[(MAX_ARGS - 1) - Index]);
  }

  for (Index = 0; Index < 4; Index++) {
    /*
     * Home zone for the called function.
     */
    CpuStackPush64 (Cpu, 0);
  }

  /*
   * Return pointer, magic value that brings us back.
   */
  CpuStackPush64 (Cpu, RETURN_TO_NATIVE_MAGIC);
}

STATIC
VOID
CpuX86EmuThunkPost (
  IN  struct CpuContext *Cpu,
  IN  UINT64            *Args
  )
{
  unsigned Index;

  /*
   * Pop stack passed parameters.
   */
  for (Index = 0; Index < 4; Index++) {
    /*
     * Home Zone, modifiable by function.
     */
    CpuStackPop64 (Cpu);
  }

  for (; Index < MAX_ARGS; Index++) {
    UINT64 Val = CpuStackPop64 (Cpu);

    DEBUG_CODE_BEGIN (); {
      if (Val != Args[Index]) {
        /*
         * The code doesn't know how many args were passed, so you can
         * have false positives due to actual Args[] values changing
         * (not the emulated stack getting corrupted) - because it's just
         * some variable on the stack, not an actual argument.
         */
        DEBUG ((DEBUG_ERROR,
                "Possible Arg%u mismatch (got 0x%lx instead of 0x%lx)\n",
                Index, Val, Args[Index]));
      }
    } DEBUG_CODE_END ();
  }
}

STATIC
EFI_STATUS
CpuInitEx (
  IN  uc_arch    Arch,
  OUT CpuContext *Cpu
  )
{
  uc_err     UcErr;
  EFI_STATUS Status;
  uc_hook    IoReadHook;
  uc_hook    IoWriteHook;
#ifndef EMU_TIMEOUT_NONE
  uc_hook    TimeoutHook;
#endif /* EMU_TIMEOUT_NONE */
  uc_hook    IsNativeHook;
  size_t     UnicornCodeGenSize;

  if (Arch == UC_ARCH_X86) {
    Cpu->Name = "x64";
    Cpu->StackReg = UC_X86_REG_RSP;
    Cpu->ProgramCounterReg = UC_X86_REG_RIP;
    Cpu->ReturnValueReg = UC_X86_REG_RAX;
    Cpu->Dump = CpuX86Dump;
    Cpu->EmuThunkPre = CpuX86EmuThunkPre;
    Cpu->EmuThunkPost = CpuX86EmuThunkPost;
    Cpu->NativeThunk = NativeThunkX86;
  } else {
    return EFI_UNSUPPORTED;
  }

  /*
   * Prefer to put the emulated stack below 4GiB to deal
   * with x64 code that may work incorrectly with 64-bit
   * stack values (...by manipulating ESP in 64-bit code!).
   */
  Cpu->EmuStackStart = SIZE_4GB - 1;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES (EMU_STACK_SIZE), &Cpu->EmuStackStart);
  if (EFI_ERROR (Status)) {
    Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES (EMU_STACK_SIZE), &Cpu->EmuStackStart);
  }
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to allocate emulated stack: %r\n", Status));
    return Status;
  }
  Cpu->EmuStackTop = Cpu->EmuStackStart + EMU_STACK_SIZE;

  UcErr = uc_open(Arch, UC_MODE_64, &Cpu->UE);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_open failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  /*
   * You get better performance, but building with EMU_TIMEOUT_NONE
   * is highly discouraged - any emulated code that does a tight
   * loop (polling on some memory updated by an event) will cause
   * a hard hang.
   */
#ifndef EMU_TIMEOUT_NONE
  /*
   * Use a block hook to check for timeouts. We must run UC with timer
   * off because it's not reentrant enough, so we need to bail out
   * periodically to allow EFI events to fire in situations where
   * emulated code can run with no traps, such a tight loop of code
   * polling a variable updated by an event.
   *
   * Eventually this could be optimized within unicorn by generating
   * the minimum code to read TSC, compare and exit emu on a match.
   */
  UcErr = uc_hook_add (Cpu->UE, &TimeoutHook, UC_HOOK_BLOCK, CpuTimeoutCb,
                       NULL, 1, 0);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "Timeout hook failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }
#endif /* EMU_TIMEOUT_NONE */

  /*
   * Use a UC_HOOK_TB_FIND_FAILURE hook to detect native code execution.
   */
  UcErr = uc_hook_add (Cpu->UE, &IsNativeHook, UC_HOOK_TB_FIND_FAILURE,
                       CpuIsNativeCb, NULL, 1, 0);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "IsNative hook failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  if (Arch == UC_ARCH_X86) {
    /*
     * Port I/O hooks.
     */
    if (gCpuIo2 != NULL) {
      UcErr = uc_hook_add (Cpu->UE, &IoReadHook, UC_HOOK_INSN, CpuIoReadCb,
                           NULL, 1, 0, UC_X86_INS_IN);
      if (UcErr != UC_ERR_OK) {
        DEBUG ((DEBUG_ERROR, "PIO read hook failed: %a\n", uc_strerror (UcErr)));
        return EFI_UNSUPPORTED;
      }

      UcErr = uc_hook_add (Cpu->UE, &IoWriteHook, UC_HOOK_INSN, CpuIoWriteCb,
                           NULL, 1, 0, UC_X86_INS_OUT);
      if (UcErr != UC_ERR_OK) {
        DEBUG ((DEBUG_ERROR, "PIO write hook failed: %a\n", uc_strerror (UcErr)));
        return EFI_UNSUPPORTED;
      }
    }
  }

  /*
   * Read/Write accesses that result from NULL pointer accesses.
   */
  UcErr = uc_mmio_map (Cpu->UE, 0, EFI_PAGE_SIZE,
                       CpuNullReadCb, NULL,
                       CpuNullWriteCb, NULL);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_mmio_map failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  /*
   * Map all memory but the zero page R/W. Some portions are made
   * executable later (e.g. via CpuRegisterCodeRange).
   */
  UcErr = uc_mem_map_ptr (Cpu->UE, EFI_PAGE_SIZE,
                          (1UL << 48) - EFI_PAGE_SIZE,
                          UC_PROT_READ | UC_PROT_WRITE,
                          (VOID *) EFI_PAGE_SIZE);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_mem_map_ptr failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  /*
   * Don't want to set an exit, to avoid the useless tb flush on every
   * uc_emu_start return. uc_ctl_exits_enable also disables the 'until'
   * parameter to uc_emu_start, which (given it's 0 value) also avoids
   * an unexpected UC_ERR_OK return on a NULL fn pointer call.
   */
  UcErr = uc_ctl_exits_enable(Cpu->UE);
  ASSERT (UcErr == UC_ERR_OK);

  REG_WRITE (Cpu, Cpu->StackReg, Cpu->EmuStackTop);

  UcErr = uc_get_code_gen_buf (Cpu->UE, (void **) &Cpu->UnicornCodeGenBuf,
                               &UnicornCodeGenSize);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_get_code_gen_buf failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }
  Cpu->UnicornCodeGenBufEnd = Cpu->UnicornCodeGenBuf + UnicornCodeGenSize;

  UcErr = uc_context_alloc (Cpu->UE, &Cpu->InitialState);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "could not allocate orig context: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  UcErr = uc_context_save (Cpu->UE, Cpu->InitialState);
  ASSERT (UcErr == UC_ERR_OK);

  mTopContext = NULL;

#ifndef EMU_TIMEOUT_NONE
  Cpu->TbCount = 0;
  Cpu->ExitPeriodTbs = UC_EMU_EXIT_PERIOD_TB_INITIAL;
  Cpu->ExitPeriodTicks = DivU64x32 (
    MultU64x64 (
      UC_EMU_EXIT_PERIOD_MS,
      GetPerformanceCounterProperties (NULL, NULL)
      ),
    1000u
    );
#endif /* EMU_TIMEOUT_NONE */

  return EFI_SUCCESS;
}

EFI_STATUS
CpuInit (
  VOID
  )
{
  EFI_STATUS Status;

  Status = CpuPrivateStackInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return CpuInitEx (UC_ARCH_X86, &CpuX86);
}


STATIC
VOID
CpuEnterCritical (
  IN  CpuRunContext *Context
  )
{
  Context->Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Context->PrevContext = mTopContext;

  mTopContext = Context;
  Context->Cpu->Contexts++;
}

STATIC
VOID
CpuLeaveCritical (
  IN  CpuRunContext *Context
  )
{
  Context->Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  ASSERT (mTopContext == Context);

  mTopContext = mTopContext->PrevContext;
  Context->Cpu->Contexts--;

  ASSERT (Context->Cpu->Contexts >= 0);
}

VOID
CpuDump (
  VOID
  )
{
  UINT64     Val;
  CpuContext *Cpu;

  if (mTopContext == NULL) {
    /*
     * Nothing to do. We're not executing anything.
     */
    return;
  }

  Cpu = mTopContext->Cpu;

  DEBUG ((DEBUG_ERROR, "Emulated state:\n"));
  Cpu->Dump (Cpu);

  Val = REG_READ (Cpu, Cpu->StackReg);
  if (!(Val >= Cpu->EmuStackStart && Val < Cpu->EmuStackTop)) {
    /*
     * It's not completely invalid for a binary to move it's
     * stack pointer elsewhere, but it is highly unusual and
     * worth noting. I've seen some programs that corrupt the
     * stack pointer by manipulating ESP instead of RSP
     * (which clears the high bits of RSP).
     */
    DEBUG ((DEBUG_ERROR, "Emulated stack is outside 0x%lx-0x%lx\n",
            Cpu->EmuStackStart, Cpu->EmuStackTop));
  }
}

STATIC
UINT64
CpuRunCtxInternal (
  IN  CpuRunContext *Context
  )
{
  uc_err        UcErr;
  CpuExitReason ExitReason;
  UINT64        *Args = Context->Args;
  UINT64        ProgramCounter = Context->ProgramCounter;
  CpuContext    *Cpu = Context->Cpu;

  DEBUG ((DEBUG_INFO, "XXX %a fn %lx(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx)\n",
          Cpu->Name, ProgramCounter, Args[0], Args[1], Args[2], Args[3], Args[4],
          Args[5], Args[6], Args[7], Args[8]));

  ASSERT (Cpu->EmuThunkPre != NULL);
  Cpu->EmuThunkPre (Cpu, Args);

  for (;;) {
    ExitReason = CPU_REASON_INVALID;
    /*
     * Unfortunately UC is not reentrant enough, so we can't use uc_set_native_thunks
     * (native code could call emulated code!) and we can't take any asynchronous emu code
     * execution (events). Disable interrupts while emulating and use a UC_BLOCK_HOOK
     * to periodically bail out to allow and allow events/timers to fire.
     *
     * Prefer masking interrupts to manipulating TPL due to overhead of the later.
     */
    DisableInterrupts (); {
#ifndef EMU_TIMEOUT_NONE
      Context->StoppedOnTimeout = FALSE;
      Context->TimeoutAbsTicks = Cpu->ExitPeriodTicks + GetPerformanceCounter ();
#endif /* EMU_TIMEOUT_NONE */

      UcErr = uc_emu_start (Cpu->UE, ProgramCounter, 0, 0, 0);

#ifndef EMU_TIMEOUT_NONE
      if (Context->StoppedOnTimeout) {
        UINT64 Ticks = GetPerformanceCounter ();

        if (Ticks > mTopContext->TimeoutAbsTicks) {
          if (Cpu->ExitPeriodTbs > UC_EMU_EXIT_PERIOD_TB_MIN) {
            Cpu->ExitPeriodTbs = Cpu->ExitPeriodTbs >> 1;
          }
        } else if (Ticks < mTopContext->TimeoutAbsTicks) {
          if (Cpu->ExitPeriodTbs < UC_EMU_EXIT_PERIOD_TB_MAX) {
            Cpu->ExitPeriodTbs = Cpu->ExitPeriodTbs << 1;
          }
        }
      }
#endif /* EMU_TIMEOUT_NONE */
    } EnableInterrupts ();

    ProgramCounter = REG_READ (Cpu, Cpu->ProgramCounterReg);

    if (UcErr == UC_ERR_FIND_TB) {
      if (ProgramCounter == RETURN_TO_NATIVE_MAGIC) {
        ExitReason = CPU_REASON_RETURN_TO_NATIVE;
      } else {
        ExitReason = CPU_REASON_CALL_TO_NATIVE;
      }
    } else if (UcErr != UC_ERR_OK) {
      ExitReason = CPU_REASON_FAILED_EMU;
    } else {
      /*
       * '0' is passed as the 'until' value to uc_emu_start,
       * but it should never be triggering due to the call
       * to uc_ctl_exits_enable.
       */
      ASSERT (ProgramCounter != 0);
      /*
       * This could be due to CpuTimeoutCb firing, or
       * this could be a 'hlt' as well, but no easy way
       * to detect this at the moment. Just continue.
       */
      ExitReason = CPU_REASON_NONE;
    }

    ASSERT (ExitReason != CPU_REASON_INVALID);

    if (ExitReason == CPU_REASON_CALL_TO_NATIVE) {
      Cpu->NativeThunk (Cpu, &ProgramCounter);
    } else if (ExitReason == CPU_REASON_RETURN_TO_NATIVE) {
      break;
    } else if (ExitReason == CPU_REASON_FAILED_EMU) {
      DEBUG ((DEBUG_ERROR, "Emulation failed: %a\n", uc_strerror (UcErr)));
      EmulatorDump ();
      break;
    }
  }

  if (ExitReason != CPU_REASON_FAILED_EMU) {
    ASSERT (Cpu->EmuThunkPost != NULL);
    Cpu->EmuThunkPost (Cpu, Args);
  } else {
    return EFI_UNSUPPORTED;
  }

  return REG_READ (Cpu, Cpu->ReturnValueReg);
}

VOID
CpuUnregisterCodeRange (
  IN  CpuContext           *Cpu,
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  )
{
  uc_err UcErr;

  UcErr = uc_mem_protect (Cpu->UE, ImageBase, ImageSize, UC_PROT_READ | UC_PROT_WRITE);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_mem_protect failed: %a\n", uc_strerror (UcErr)));
  }

  /*
   * Because images can be loaded into a previously used range,
   * stale TBs can lead to "strange" crashes.
   */
  uc_ctl_remove_cache(Cpu->UE, ImageBase, ImageBase + ImageSize);
}

VOID
CpuRegisterCodeRange (
  IN  CpuContext           *Cpu,
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  )
{
  uc_err UcErr;

  UcErr = uc_mem_protect (Cpu->UE, ImageBase, ImageSize, UC_PROT_ALL);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_mem_protect failed: %a\n", uc_strerror (UcErr)));
  }
}

STATIC
CpuRunContext *
CpuAllocContext (
  VOID
  )
{
  EFI_STATUS    Status;
  CpuRunContext *Context;

  Status = gBS->AllocatePool (EfiBootServicesData,
    sizeof (*Context), (VOID **) &Context);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  gBS->SetMem (Context, sizeof (*Context), 0);
  return Context;
}

STATIC
VOID
CpuFreeContext (
  IN  CpuRunContext *Context
  )
{
  if (Context->PrevUcContext != NULL) {
    uc_err UcErr;
    UcErr = uc_context_free (Context->PrevUcContext);
    ASSERT (UcErr == UC_ERR_OK);
  }

  gBS->FreePool (Context);
}

#ifdef CHECK_ORPHAN_CONTEXTS
/*
 * Every time the emulator executes emulated code on behalf
 * of the caller, a new context is allocated and inserted
 * into the linked list pointed to by mTopContext. This is
 * then freed. Normal control flow cannot result in contexts
 * left behind... what about SetJump/LongJump? This is where
 * things get messy. Imagine a scenario like this:
 * - native code does SetJump
 * - native code calls emulated function, passes Cb
 * - emulated function calls native Cb
 * - native Cb does LongJump.
 *
 * ...this would result in emulated function never regaining control,
 * and thus the CpuRunContext associated with its exection
 * would never be freed. Note: this is different from the situation
 * covered by NativeThunk, as the original SetJump caller was not
 * called from emulated code.
 *
 * This may seem rather contrived, but consider this is exactly
 * how EFI_BOOT_SERVICES.Exit() would behave for an emulated
 * image that was invoked from a native image, if it were not
 * wrapped via CpuExitImage!
 *
 * Anyway, this is not normal, and not expected to be done
 * by any kind of application or driver executed via this emulated
 * emulator. But CHECK_ORPHAN_CONTEXTS allows checking for such
 * behavior.
 */

STATIC
CpuRunContext *
CpuDetectOrphanContexts (
  IN  CpuRunContext *Context
  )
{
  CpuRunContext *Orphan = NULL;

  /*
   * This is different from how CpuCompressLeakedContexts is used
   * in NativeThunk. Here, we are trying to identify older contexts
   * which represent state that is never going to be returned to.
   * Such contexts are just freed.
   *
   * Context == mTopContext -> [ live context ]
   *                        -> [ context corresponding
   *                             to returned code ]
   *                        -> [ context corresponding
   *                             to returned code ]
   *
   * Let's call such contexts /orphaned/ contexts. Unlike leaked
   * contexts handled by the gBS->ExitWrapper or NativeThunk,
   * these have no useful state, they just need to be cleaned up.
   *
   * Note: this function must be called before dropping to Context->Tpl,
   * due to:
   * - LeakCookie initialization. LeakCookie cannot be initialized
   *   earlier, because the context is allocated before switching
   *   to a private stack.
   * - List modifiction.
   *
   * This does mean we can't free here, so that's done in
   * CpuFreeOrphanContexts.
   */

  Context->LeakCookie = CURRENT_FP ();
  /*
   * Stack grows downward, thus the new context leak cookie
   * should be smaller than the previous context leak cookie.
   */

  while (Context->PrevContext != NULL &&
         Context->LeakCookie >= Context->PrevContext->LeakCookie) {
    /*
     * Contexts could have different Context->Cpu, if say an Arm binary invoked
     * an x86 protocol. Important to do the following operations in the context
     * of the right CpuContext.
     */
    CpuContext *Cpu = Context->Cpu;

    if (Orphan == NULL) {
      Orphan = Context->PrevContext;
    }

    Cpu->Contexts--;
    ASSERT (Cpu->Contexts >= 0);
    Context->PrevContext = Context->PrevContext->PrevContext;
  }

  if (Orphan != NULL) {
    CpuRunContext *Iter;
    Iter = Orphan;
    /*
     * Context->PrevContext will be NULL or the first context
     * with a LeakCookie that doesn't look like a clearly leaked
     * context.
     */
    while (Iter->PrevContext != Context->PrevContext) {
      Iter = Iter->PrevContext;
    }

    Iter->PrevContext = NULL;
  }

  return Orphan;
}

STATIC
VOID
CpuFreeOrphanContexts (
  IN CpuRunContext *Context
  )
{
  while (Context != NULL) {
    CpuRunContext *ThisContext = Context;
    Context = Context->PrevContext;

    DEBUG ((DEBUG_ERROR, "\nDetected orphan context %p\n\n", ThisContext));
    CpuFreeContext (ThisContext);
  }
}
#endif /* CHECK_ORPHAN_CONTEXTS */

VOID
CpuRunCtxOnPrivateStack (
  IN  CpuRunContext *Context
  )
{
  uc_err     UcErr;
  CpuContext *Cpu = Context->Cpu;

#ifdef CHECK_ORPHAN_CONTEXTS
  CpuRunContext *OrphanContexts = CpuDetectOrphanContexts (Context);
#endif /* CHECK_ORPHAN_CONTEXTS */

  gBS->RestoreTPL (Context->Tpl);

#ifdef CHECK_ORPHAN_CONTEXTS
  CpuFreeOrphanContexts (OrphanContexts);
#endif /* CHECK_ORPHAN_CONTEXTS */

  if (Cpu->Contexts > 1) {
    UcErr = uc_context_alloc (Cpu->UE, &Context->PrevUcContext);
    if (UcErr != UC_ERR_OK) {
      DEBUG ((DEBUG_ERROR, "could not allocate UC context: %a\n", uc_strerror (UcErr)));
      Context->Ret = EFI_OUT_OF_RESOURCES;
      goto out;
    }

    UcErr = uc_context_save (Cpu->UE, Context->PrevUcContext);
    ASSERT (UcErr == UC_ERR_OK);

    /*
     * EFIAPI (MS x64 ABI) has no concept of a red zone, however code built outside of Tiano
     * can be suspect. Better be safe than sorry!
     */
    CpuStackPushRedZone (Cpu);
  } else {
    UcErr = uc_context_restore (Cpu->UE, Cpu->InitialState);
    ASSERT (UcErr == UC_ERR_OK);
  }

  Context->Ret = CpuRunCtxInternal (Context);

  if (Context->PrevUcContext != NULL) {
    UcErr = uc_context_restore (Cpu->UE, Context->PrevUcContext);
    ASSERT (UcErr == UC_ERR_OK);
  }

out:
  /*
   * Critical section ends when code no longer modifies emulated state,
   * including registers.
   */
  CpuLeaveCritical (Context);

#ifdef ON_PRIVATE_STACK
  if (Context->PrevUcContext == NULL) {
    LongJump (&mOriginalStack, -1);
  }
#endif /* ON_PRIVATE_STACK */
}

UINT64
CpuRunCtx (
  IN  CpuRunContext *Context
  )
{
  CpuEnterCritical (Context);

#ifdef ON_PRIVATE_STACK
  if (!(CURRENT_FP () >= mNativeStackStart &&
        CURRENT_FP () < mNativeStackTop)) {
    if (SetJump (&mOriginalStack) == 0) {
      SwitchStack ((VOID *) CpuRunCtxOnPrivateStack,
                   Context, NULL, (VOID *) mNativeStackTop);
    }
  } else
#endif /* ON_PRIVATE_STACK */
  {
    CpuRunCtxOnPrivateStack (Context);
  }

  /*
   * Come back here from CpuRunCtxOnPrivateStack, directly
   * (when Context.PrevUcContext != NULL) or via LongJump (when
   * Context.PrevUcContext == NULL).
   */
  gBS->RestoreTPL (Context->Tpl);
  return Context->Ret;
}

UINT64
CpuRunFunc (
  IN  CpuContext          *Cpu,
  IN  EFI_VIRTUAL_ADDRESS ProgramCounter,
  IN  UINT64              *Args
  )
{
  UINT64 Ret;
  CpuRunContext *Context;

  ASSERT (Cpu != NULL);

  Context = CpuAllocContext ();
  if (Context == NULL) {
    DEBUG ((DEBUG_ERROR, "Could not allocate CpuRunContext\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Context->Cpu = Cpu;
  Context->ProgramCounter = ProgramCounter;
  Context->Args = Args;

  Ret = CpuRunCtx (Context);

  CpuFreeContext (Context);
  return Ret;
}

CpuRunContext *
CpuGetTopContext (
  VOID
  )
{
  return mTopContext;
}

VOID
CpuCompressLeakedContexts (
  IN  CpuRunContext *CurrentContext,
  IN  BOOLEAN        OnImageExit
  )
{
  EFI_TPL       Tpl;
  CpuRunContext *Context;
  CpuRunContext *FromContext;
  CpuRunContext *ToContext;

  /*
   * CpuCompressLeakedContexts deals with situations where
   * mTopContext is not consistent with actual execution, due
   * to native code doing long jumps, thus skipping the normal
   * cleanup done in CpuRunFunc/CpuRunImage. In these situations,
   * when NativeThunk gets control back from the native call, mTopContext
   * will be newer than the NativeThunk context - mTopContext
   * needs to be adjusted, popping off the contexts in between
   * and applying their state (which represents that emulated non-volatile
   * state that would have been restored by LongJump, if it were done
   * by emulated code!).
   *
   * mTopContext ->    [ context corresponding to returned code ]
   *                   [ context corresponding to returned code ]
   * CurrentContext -> [ live context ]
   */

  Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Context = FromContext = mTopContext;
  ToContext = mTopContext = OnImageExit ? CurrentContext->PrevContext : CurrentContext;

  while (Context != ToContext) {
    uc_err UcErr;
    /*
     * Contexts could have different Context->Cpu, if say an Arm binary invoked
     * an x86 protocol. Important to do the following operations in the context
     * of the right CpuContext.
     */
    CpuContext *Cpu = Context->Cpu;

    Cpu->Contexts--;
    ASSERT (Cpu->Contexts >= 0);

    if (Context->PrevUcContext != NULL) {
      UcErr = uc_context_restore (Cpu->UE, Context->PrevUcContext);
    }

    Context = Context->PrevContext;

  }
  gBS->RestoreTPL (Tpl);

  while (FromContext != ToContext) {
    Context = FromContext;
    FromContext = Context->PrevContext;

    CpuFreeContext (Context);
  }
}

EFI_STATUS
EFIAPI
CpuRunImage (
  IN  EFI_HANDLE       ImageHandle,
  IN  EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS                Status;
  CpuRunContext             *Context;
  ImageRecord               *Record;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  UINT64                    Args[2] = { (UINT64) ImageHandle, (UINT64) SystemTable };

  Context = CpuAllocContext ();
  if (Context == NULL) {
    DEBUG ((DEBUG_ERROR, "Could not allocate CpuRunContext\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->HandleProtocol (ImageHandle,
                                &gEfiLoadedImageProtocolGuid,
                                (VOID **)&LoadedImage);
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_ERROR, "Can't get emulated image entry point: %r\n", Status));
    return Status;
  }

  Record = ImageFindByAddress ((UINT64) LoadedImage->ImageBase);
  ASSERT (Record != NULL);
  ASSERT (Record->Cpu != NULL);

  Context->Cpu = Record->Cpu;
  Record->ImageHandle = ImageHandle;

  Context->ImageRecord = Record;
  Context->ProgramCounter = Record->ImageEntry;
  Context->Args = Args;

  if (SetJump (&Record->ImageExitJumpBuffer) == 0) {
    Status = CpuRunCtx (Context);
    /*
     * Image just returned.
     */
    CpuFreeContext (Context);
    return Status;
  }

  /*
   * Image exited via gBS->Exit.
   */
  CpuCompressLeakedContexts (Context, TRUE);
  Status = gBS->Exit (Record->ImageHandle,
                      Record->ImageExitStatus,
                      Record->ImageExitDataSize,
                      Record->ImageExitData);

  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
CpuExitImage (
  IN  CpuContext *Cpu,
  IN  UINT64     OriginalProgramCounter,
  IN  UINT64     ReturnAddress,
  IN  UINT64     *Args
  )
{
  EFI_TPL       Tpl;
  CpuRunContext *Context;
  ImageRecord   *CurrentImageRecord;
  EFI_HANDLE    Handle =  (VOID *) Args[0];

  CurrentImageRecord = ImageFindByHandle (Handle);
  if (CurrentImageRecord == NULL) {
    DEBUG((DEBUG_ERROR, "CpuExitImage: bad Handle argument 0x%lx\n", Handle));
    return EFI_INVALID_PARAMETER;
  }
  ASSERT (CurrentImageRecord->ImageHandle == Handle);

  Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Context = mTopContext;
  while (Context != NULL && Context->ImageRecord == NULL) {
    Context = Context->PrevContext;
  }

  ASSERT (Context);
  if (Context->ImageRecord != CurrentImageRecord) {
    /*
     * The image with this handle is *not* the last image
     * invoked.
     */
    Context = NULL;
  }
  gBS->RestoreTPL (Tpl);

  if (Context == NULL) {
    DEBUG((DEBUG_ERROR, "Context->ImageRecord != CurrentImageRecord\n"));
    return EFI_INVALID_PARAMETER;
  }

  CurrentImageRecord->ImageExitStatus = Args[1];
  CurrentImageRecord->ImageExitDataSize = Args[2];
  CurrentImageRecord->ImageExitData = (VOID *) Args[3];
  LongJump (&CurrentImageRecord->ImageExitJumpBuffer, -1);

  UNREACHABLE ();
}

BOOLEAN
CpuAddrIsCodeGen (
  IN  EFI_PHYSICAL_ADDRESS Address
  )
{
  if (Address >= CpuX86.UnicornCodeGenBuf &&
      Address < CpuX86.UnicornCodeGenBufEnd) {
    return TRUE;
  }

  return FALSE;
}

#ifndef NDEBUG
EFI_STATUS
EFIAPI
CpuGetDebugState (
  OUT X86_EMU_TEST_DEBUG_STATE *DebugState
  )
{
  EFI_TPL       Tpl;
  CpuRunContext *Context;

  ASSERT (DebugState != NULL);

  ZeroMem (DebugState, sizeof (*DebugState));

  Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Context = mTopContext;
  while (Context != NULL) {
    DebugState->ContextCount++;
    Context = Context->PrevContext;
  }

#ifndef EMU_TIMEOUT_NONE
  DebugState->ExitPeriodMs = UC_EMU_EXIT_PERIOD_MS;
  DebugState->X86ExitPeriodTicks = CpuX86.ExitPeriodTicks;
  DebugState->X86ExitPeriodTbs = CpuX86.ExitPeriodTbs;
#endif /* EMU_TIMEOUT_NONE */
  DebugState->X86ContextCount = CpuX86.Contexts;
  gBS->RestoreTPL (Tpl);

  return EFI_SUCCESS;
}
#endif
