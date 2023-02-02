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
#define MS_TO_NS(x)            (x * 1000000UL)
#define UC_EMU_EXIT_PERIOD_MS  10

uc_engine *gUE;
EFI_PHYSICAL_ADDRESS UnicornCodeGenBuf;
EFI_PHYSICAL_ADDRESS UnicornCodeGenBufEnd;
STATIC EFI_PHYSICAL_ADDRESS mEmuStackStart;
STATIC EFI_PHYSICAL_ADDRESS mEmuStackTop;
STATIC CpuRunContext *mTopContext;
STATIC uc_context *mOrigContext;
STATIC UINT64 mUcEmuExitPeriodTicks;

#ifdef ON_PRIVATE_STACK
BASE_LIBRARY_JUMP_BUFFER mOriginalStack;
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
  if (Address == RETURN_TO_NATIVE_MAGIC ||
      IsNativeCall (Address)) {
    return TRUE;
  }

  return FALSE;
}

STATIC
VOID
CpuTimeoutCb (
  IN  uc_engine *UE,
  IN  UINT64    Address,
  IN  UINT32    Size,
  IN  VOID      *UserData)
{
  if (GetPerformanceCounter () >
      mTopContext->TimeoutAbsTicks) {
    uc_emu_stop (UE);
  }
}

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
  DEBUG_CODE_BEGIN ();
  {
    X86EmulatorDump ();
  }
  DEBUG_CODE_END ();
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
  DEBUG_CODE_BEGIN ();
  {
    X86EmulatorDump ();
  }
  DEBUG_CODE_END ();
}

VOID
CpuCleanup (
  VOID
  )
{
  uc_err UcErr;
  ASSERT (gUE != NULL);

  UcErr = uc_close (gUE);
  ASSERT (UcErr == UC_ERR_OK);
}

STATIC
EFI_STATUS
CpuPrivateStackInit (
  VOID
  )
{
#ifdef ON_PRIVATE_STACK
  EFI_STATUS Status;

  Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES (NATIVE_STACK_SIZE), &mNativeStackStart);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "failed to allocate private stack: %r\n", Status));
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

EFI_STATUS
CpuInit (
  VOID
  )
{
  uc_err     UcErr;
  EFI_STATUS Status;
  uc_hook    IoReadHook, IoWriteHook;
  uc_hook    TimeoutHook;
  uc_hook    IsNativeHook;
  size_t     UnicornCodeGenSize;

  Status = CpuPrivateStackInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /*
   * Prefer to put the emulated stack below 4GiB to deal
   * with x64 code that may work incorrectly with 64-bit
   * stack values (...by manipulating ESP in 64-bit code!).
   */
  mEmuStackStart = SIZE_4GB - 1;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES (EMU_STACK_SIZE), &mEmuStackStart);
  if (EFI_ERROR (Status)) {
    Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES (EMU_STACK_SIZE), &mEmuStackStart);
  }
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "failed to allocate emulated stack: %r\n", Status));
    return Status;
  }
  mEmuStackTop = mEmuStackStart + EMU_STACK_SIZE;

  UcErr = uc_open(UC_ARCH_X86, UC_MODE_64, &gUE);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_open failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  /*
   * Use a block hook to check for timeouts. We must run UC with interrupts
   * off because it's not reentrant enough, so we need to bail out
   * periodically to allow EFI events to fire in situations where
   * emulated code can run with no traps, such a tight loop of code
   * polling a variable.
   *
   * Eventually this could be optimized within unicorn by generating
   * the minimum code to read TSC, compare and exit emu on a match.
   */
  UcErr = uc_hook_add (gUE, &TimeoutHook, UC_HOOK_BLOCK, CpuTimeoutCb,
                       NULL, 1, 0);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "Timeout hook failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  /*
   * Use a UC_HOOK_TB_FIND_FAILURE hook to detect native code execution.
   */
  UcErr = uc_hook_add (gUE, &IsNativeHook, UC_HOOK_TB_FIND_FAILURE,
                       CpuIsNativeCb, NULL, 1, 0);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "IsNative hook failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  /*
   * Port I/O hooks.
   */
  if (gCpuIo2 != NULL) {
    UcErr = uc_hook_add (gUE, &IoReadHook, UC_HOOK_INSN, CpuIoReadCb,
                         NULL, 1, 0, UC_X86_INS_IN);
    if (UcErr != UC_ERR_OK) {
      DEBUG ((DEBUG_ERROR, "PIO read hook failed: %a\n", uc_strerror (UcErr)));
      return EFI_UNSUPPORTED;
    }

    UcErr = uc_hook_add (gUE, &IoWriteHook, UC_HOOK_INSN, CpuIoWriteCb,
                         NULL, 1, 0, UC_X86_INS_OUT);
    if (UcErr != UC_ERR_OK) {
      DEBUG ((DEBUG_ERROR, "PIO write hook failed: %a\n", uc_strerror (UcErr)));
      return EFI_UNSUPPORTED;
    }
  }

  /*
   * Read/Write accesses that result from NULL pointer accesses.
   */
  UcErr = uc_mmio_map (gUE, 0, EFI_PAGE_SIZE,
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
  UcErr = uc_mem_map_ptr (gUE, EFI_PAGE_SIZE,
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
  UcErr = uc_ctl_exits_enable(gUE);
  ASSERT (UcErr == UC_ERR_OK);

  REG_WRITE (RSP, mEmuStackTop);

  UcErr = uc_get_code_gen_buf (gUE, (void **) &UnicornCodeGenBuf,
                               &UnicornCodeGenSize);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_get_code_gen_buf failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }
  UnicornCodeGenBufEnd = UnicornCodeGenBuf + UnicornCodeGenSize;

  UcErr = uc_context_alloc (gUE, &mOrigContext);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "could not allocate orig context: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  UcErr = uc_context_save (gUE, mOrigContext);
  ASSERT (UcErr == UC_ERR_OK);

  mTopContext = NULL;

  /*
   * Performing this in every CpuIsNativeCb is expensive according to bare metal
   * Arm tests, so do this once.
   */
  mUcEmuExitPeriodTicks = DivU64x32 (
    MultU64x64 (
      UC_EMU_EXIT_PERIOD_MS,
      GetPerformanceCounterProperties (NULL, NULL)
      ),
    1000u
    );

  return EFI_SUCCESS;
}

STATIC
VOID
CpuStackPushRedZone (
  VOID
  )
{
  UINT64 Rsp;

  Rsp = REG_READ (RSP);
  Rsp -= SYSV_X64_ABI_REDZONE;
  REG_WRITE (RSP, Rsp);
}

STATIC
UINT64
CpuStackPop64 (
  VOID
  )
{
  UINT64 Rsp;
  UINT64 Val;

  Rsp = REG_READ (RSP);
  Val = *(UINT64 *) Rsp;
  Rsp += 8;
  REG_WRITE (RSP, Rsp);
  return Val;
}

STATIC
VOID
CpuStackPush64 (
  IN  UINT64 Val
  )
{
  UINT64 Rsp;

  Rsp = REG_READ (RSP);
  Rsp -= 8;
  REG_WRITE (RSP, Rsp);

  *(UINT64 *) Rsp = Val;
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
}

VOID
CpuDump (
  VOID
  )
{
  UINT64 Val;
  UNUSED UINTN Printed = 0;

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
    Val = REG_READ(x);                                  \
    DEBUG ((DEBUG_ERROR, "%6a = 0x%016lx", #x, Val));   \
    if ((Printed & 1) == 0) {                           \
      DEBUG ((DEBUG_ERROR, "\n"));                      \
    } else {                                            \
      DEBUG ((DEBUG_ERROR, " "));                       \
    }                                                   \
  } while (0);

  DEBUG ((DEBUG_ERROR, "Emulated state:\n"));
  REGS ();

#undef REG
#undef REGS

  Val = REG_READ(RSP);
  if (!(Val >= mEmuStackStart && Val < mEmuStackTop)) {
    /*
     * It's not completely invalid for a binary to move it's
     * stack pointer elsewhere, but it is highly unusual and
     * worth noting. I've seen some programs that corrupt the
     * stack pointer by manipulating ESP instead of RSP
     * (which clears the high bits of RSP).
     */
    DEBUG ((DEBUG_ERROR, "RSP is outside 0x%lx-0x%lx\n",
            mEmuStackStart, mEmuStackTop));
  }
}

STATIC
UINT64
CpuRunCtxInternal (
  IN  CpuRunContext *Context
  )
{
  unsigned      Index;
  uc_err        UcErr;
  CpuExitReason ExitReason;
  UINT64        *Args = Context->Args;
  UINT64        Rip = Context->ProgramCounter;

  DEBUG ((DEBUG_INFO, "XXX x64 fn %lx(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx)\n",
          Rip, Args[0], Args[1], Args[2], Args[3], Args[4], Args[5], Args[6], Args[7], Args[8]));

  REG_WRITE (RCX, Args[0]);
  REG_WRITE (RDX, Args[1]);
  REG_WRITE (R8, Args[2]);
  REG_WRITE (R9, Args[3]);

  for (Index = 0; Index < (MAX_ARGS - 4); Index++) {
    /*
     * Push arguments on stack in reverse order.
     */
    CpuStackPush64 (Args[(MAX_ARGS - 1) - Index]);
  }

  for (Index = 0; Index < 4; Index++) {
    /*
     * Home zone for the called function.
     */
    CpuStackPush64 (0);
  }

  /*
   * Return pointer, magic value that brings us back.
   */
  CpuStackPush64 (RETURN_TO_NATIVE_MAGIC);

  for (;;) {
    ExitReason = CPU_REASON_INVALID;
    /*
     * Unfortunately UC is not reentrant enough, so we can't use uc_set_native_thunks
     * (native code could call x64 code!) and we can't take any asynchronous x64 code
     * execution (events). Disable interrupts while emulating and use a UC_BLOCK_HOOK
     * to periodically bail out to allow and allow events/timers to fire.
     *
     * Prefer masking interrupts to manipulating TPL due to overhead of the later.
     */
    DisableInterrupts ();
    Context->TimeoutAbsTicks = mUcEmuExitPeriodTicks +
      GetPerformanceCounter ();
    UcErr = uc_emu_start (gUE, Rip, 0, 0, 0);
    EnableInterrupts ();

    Rip = REG_READ (RIP);

    if (UcErr == UC_ERR_FIND_TB) {
      if (Rip == RETURN_TO_NATIVE_MAGIC) {
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
      ASSERT (Rip != 0);
      /*
       * This could be due to CpuTimeoutCb firing, or
       * this could be a 'hlt' as well, but no easy way
       * to detect this at the moment. Just continue.
       */
      ExitReason = CPU_REASON_NONE;
    }

    ASSERT (ExitReason != CPU_REASON_INVALID);

    if (ExitReason == CPU_REASON_CALL_TO_NATIVE) {
      NativeThunk (gUE, Rip);
      Rip = CpuStackPop64 ();
    } else if (ExitReason == CPU_REASON_RETURN_TO_NATIVE) {
      break;
    } else if (ExitReason == CPU_REASON_FAILED_EMU) {
      DEBUG ((DEBUG_ERROR, "Emulation failed: %a\n", uc_strerror (UcErr)));
      X86EmulatorDump ();
      break;
    }
  }

  if (ExitReason != CPU_REASON_FAILED_EMU) {
    /*
     * Pop stack passed parameters.
     */
    for (Index = 0; Index < 4; Index++) {
      /*
       * Home Zone, modifiable by function.
       */
      CpuStackPop64 ();
    }

    for (; Index < MAX_ARGS; Index++) {
      UINT64 Val = CpuStackPop64 ();
      DEBUG_CODE_BEGIN ();
      {
        if (Val != Args[Index]) {
          /*
           * The code doesn't know how many args were passed, so you can
           * have false positives due to actual Args[] values changing
           * (not the x64 stack getting corrupted) - because it's just
           * some variable on the stack, not an actual argument.
           */
          DEBUG ((DEBUG_ERROR,
                  "Possible Arg%u mismatch (got 0x%lx instead of 0x%lx)\n",
                  Index, Val, Args[Index]));
        }
      }
      DEBUG_CODE_END ();
    }
  }

  if (ExitReason == CPU_REASON_FAILED_EMU) {
    return EFI_UNSUPPORTED;
  }

  return REG_READ (RAX);
}

VOID
CpuUnregisterCodeRange (
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  )
{
  uc_err UcErr;

  UcErr = uc_mem_protect (gUE, ImageBase, ImageSize, UC_PROT_READ | UC_PROT_WRITE);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_mem_protect failed: %a\n", uc_strerror (UcErr)));
  }

  /*
   * Because images can be loaded into a previously used range,
   * stale TBs can lead to "strange" crashes.
   */
  uc_ctl_remove_cache(gUE, ImageBase, ImageBase + ImageSize);
}

VOID
CpuRegisterCodeRange (
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  )
{
  uc_err UcErr;

  UcErr = uc_mem_protect (gUE, ImageBase, ImageSize, UC_PROT_ALL);
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
  EFI_STATUS Status;
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

VOID
CpuRunCtxOnPrivateStack (
  IN  CpuRunContext *Context
  )
{
  uc_err UcErr;

  gBS->RestoreTPL (Context->Tpl);

  if (Context->PrevContext != NULL) {
    UcErr = uc_context_alloc (gUE, &Context->PrevUcContext);
    if (UcErr != UC_ERR_OK) {
      DEBUG ((DEBUG_ERROR, "could not allocate UC context: %a\n", uc_strerror (UcErr)));
      Context->Ret = EFI_OUT_OF_RESOURCES;
      goto out;
    }

    UcErr = uc_context_save (gUE, Context->PrevUcContext);
    ASSERT (UcErr == UC_ERR_OK);

    /*
     * EFIAPI (MS x64 ABI) has no concept of a red zone, however code built outside of Tiano
     * can be suspect. Better be safe than sorry!
     */
    CpuStackPushRedZone ();
  } else {
    UcErr = uc_context_restore (gUE, mOrigContext);
    ASSERT (UcErr == UC_ERR_OK);
  }

  Context->Ret = CpuRunCtxInternal (Context);

  if (Context->PrevUcContext != NULL) {
    UcErr = uc_context_restore (gUE, Context->PrevUcContext);
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
  IN  EFI_VIRTUAL_ADDRESS ProgramCounter,
  IN  UINT64              *Args
  )
{
  UINT64 Ret;
  CpuRunContext *Context;

  Context = CpuAllocContext ();
  if (Context == NULL) {
    DEBUG ((DEBUG_ERROR, "Could not allocate CpuRunContext\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Context->ProgramCounter = ProgramCounter;
  Context->Args = Args;

  Ret = CpuRunCtx (Context);

  CpuFreeContext (Context);
  return Ret;
}

STATIC
VOID
CpuCleanupContextsOnImageExit (
  IN  CpuRunContext *ImageEntryContext
  )
{
  EFI_TPL Tpl;
  CpuRunContext *Context;

  Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Context = mTopContext;
  mTopContext = ImageEntryContext->PrevContext;
  ImageEntryContext->PrevContext = NULL;
  gBS->RestoreTPL (Tpl);

  while (Context != NULL) {
    uc_err UcErr;
    CpuRunContext *ThisContext = Context;
    Context = Context->PrevContext;

    if (ThisContext->PrevUcContext != NULL) {
      UcErr = uc_context_restore (gUE, ThisContext->PrevUcContext);
      ASSERT (UcErr == UC_ERR_OK);
    }

    CpuFreeContext (ThisContext);
  }
}

EFI_STATUS
EFIAPI
CpuRunImage (
  IN  EFI_HANDLE       ImageHandle,
  IN  EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  CpuRunContext *Context;
  X86_IMAGE_RECORD *Record;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  UINT64 Args[2] = { (UINT64) ImageHandle, (UINT64) SystemTable };

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

  Record = FindImageRecordByAddress ((UINT64) LoadedImage->ImageBase);
  ASSERT (Record != NULL);
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
  CpuCleanupContextsOnImageExit (Context);
  Status = gBS->Exit (Record->ImageHandle,
                      Record->ImageExitStatus,
                      Record->ImageExitDataSize,
                      Record->ImageExitData);

  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
CpuExitImage (
  IN  UINT64 OriginalRip,
  IN  UINT64 ReturnAddress,
  IN  UINT64 *Args
  )
{
  EFI_TPL          Tpl;
  CpuRunContext    *Context;
  X86_IMAGE_RECORD *CurrentImageRecord;
  EFI_HANDLE       Handle =  (VOID *) Args[0];

  CurrentImageRecord = FindImageRecordByHandle (Handle);
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

#ifndef NDEBUG
EFI_STATUS
EFIAPI
CpuGetDebugState (
  OUT X86_EMU_TEST_DEBUG_STATE *DebugState
  )
{
  EFI_TPL Tpl;
  CpuRunContext *Context;

  ASSERT (DebugState != NULL);

  DebugState->CurrentContextCount = 0;
  Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Context = mTopContext;
  while (Context != NULL) {
    DebugState->CurrentContextCount++;
    Context = Context->PrevContext;
  }
  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}
#endif
