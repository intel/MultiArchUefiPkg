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

#define INSN_HLT               0xf4
#define MAX_ARGS               16
#define EMU_STACK_SIZE         (1024 * 1024)
#define NATIVE_STACK_SIZE      (1024 * 1024)
#define RETURN_TO_NATIVE_MAGIC ((UINTN) &CpuReturnToNative)
#define SYSV_X64_ABI_REDZONE   128

uc_engine *gUE;
EFI_PHYSICAL_ADDRESS UnicornCodeGenBuf;
EFI_PHYSICAL_ADDRESS UnicornCodeGenBufEnd;
STATIC EFI_PHYSICAL_ADDRESS mEmuStackStart;
STATIC EFI_PHYSICAL_ADDRESS mEmuStackTop;
STATIC UINTN mInCritical;
STATIC uc_context *mOrigContext;

#ifdef ON_PRIVATE_STACK
BASE_LIBRARY_JUMP_BUFFER mOriginalStack;
STATIC EFI_PHYSICAL_ADDRESS mNativeStackStart;
STATIC EFI_PHYSICAL_ADDRESS mNativeStackTop;
#endif /* ON_PRIVATE_STACK */

typedef enum {
  CPU_REASON_INVALID,
  CPU_REASON_NONE,
  CPU_REASON_RETURN_TO_NATIVE,
#ifdef UPSTREAM_UC
  CPU_REASON_CALL_TO_NATIVE,
#endif /* UPSTREAM_UC */
  CPU_REASON_FAILED_EMU,
} CpuExitReason;

static UINT8 CpuReturnToNative[] = { INSN_HLT };

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
    CpuDump ();
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
    CpuDump ();
  }
  DEBUG_CODE_END ();
}

#ifndef UPSTREAM_UC
STATIC
BOOLEAN
CpuIsNative (
  IN  uc_engine *UE,
  IN  UINT64    Rip
  )
{
  if (Rip == RETURN_TO_NATIVE_MAGIC) {
    return FALSE;
  }

  if (IsNativeCall (Rip)) {
    return TRUE;
  }

  return FALSE;
}
#endif /* UPSTREAM_UC */

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

  mNativeStackStart = (EFI_PHYSICAL_ADDRESS)
    AllocatePages (EFI_SIZE_TO_PAGES (NATIVE_STACK_SIZE));
  if (mNativeStackStart == 0) {
    return EFI_OUT_OF_RESOURCES;
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
#ifndef UPSTREAM_UC
  size_t     UnicornCodeGenSize;
#endif /* UPSTREAM_UC */

  Status = CpuPrivateStackInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mEmuStackStart = (EFI_PHYSICAL_ADDRESS)
    AllocatePages (EFI_SIZE_TO_PAGES (EMU_STACK_SIZE));
  if (mEmuStackStart == 0) {
    return EFI_OUT_OF_RESOURCES;
  }
  mEmuStackTop = mEmuStackStart + EMU_STACK_SIZE;

  UcErr = uc_open(UC_ARCH_X86, UC_MODE_64, &gUE);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_open failed: %a\n", uc_strerror (UcErr)));
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

  UcErr = uc_mem_protect (gUE, RETURN_TO_NATIVE_MAGIC & ~(EFI_PAGE_SIZE - 1),
                          EFI_PAGE_SIZE, UC_PROT_ALL);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_mem_protect on RETURN_TO_NATIVE_MAGIC failed: %a\n",
            uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  REG_WRITE (RSP, mEmuStackTop);

#ifndef UPSTREAM_UC
  UcErr = uc_set_native_thunks (gUE, CpuIsNative, NativeThunk);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_set_native_thunks failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  UcErr = uc_get_code_gen_buf (gUE, (void **) &UnicornCodeGenBuf,
                               &UnicornCodeGenSize);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "uc_get_code_gen_buf failed: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }
  UnicornCodeGenBufEnd = UnicornCodeGenBuf + UnicornCodeGenSize;
#endif /* UPSTREAM_UC */

  UcErr = uc_context_alloc (gUE, &mOrigContext);
  if (UcErr != UC_ERR_OK) {
    DEBUG ((DEBUG_ERROR, "could not allocate orig context: %a\n", uc_strerror (UcErr)));
    return EFI_UNSUPPORTED;
  }

  UcErr = uc_context_save (gUE, mOrigContext);
  ASSERT (UcErr == UC_ERR_OK);

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
UINTN
CpuEnterCritical (
  EFI_TPL *OutTpl
  )
{
  *OutTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  return mInCritical++;
}

STATIC
UINTN
CpuLeaveCritical (
  EFI_TPL *OutTpl
  )
{
  *OutTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  return mInCritical--;
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

  REGS ();

#undef REG
#undef REGS
}

STATIC
UINT64
CpuRunFuncInternal (
  IN  EFI_VIRTUAL_ADDRESS ProgramCounter,
  IN  UINT64              *Args
  )
{
  unsigned      Index;
  uc_err        UcErr;
  CpuExitReason ExitReason;
  UINT64        Rip = ProgramCounter;

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
    UcErr = uc_emu_start (gUE, Rip, 0, 0, 0);
    Rip = REG_READ (RIP);

#ifdef UPSTREAM_UC
    if (UcErr == UC_ERR_FETCH_PROT) {
      ExitReason = CPU_REASON_CALL_TO_NATIVE;
    } else
#endif /* UPSTREAM_UC */
      if (UcErr != UC_ERR_OK) {
        ExitReason = CPU_REASON_FAILED_EMU;
      } else {
        /*
         * '0' is passed as the 'until' value to uc_emu_start,
         * but it should never be triggering due to the call
         * to uc_ctl_exits_enable.
         */
        ASSERT (Rip != 0);
        /*
         * This could be a 'hlt' as well, but not easy way
         * to detect this at the moment. Just continue.
         */
        ExitReason = CPU_REASON_NONE;
        if (Rip == (RETURN_TO_NATIVE_MAGIC + 1)) {
          ExitReason = CPU_REASON_RETURN_TO_NATIVE;
        }
      }

    ASSERT (ExitReason != CPU_REASON_INVALID);

#ifdef UPSTREAM_UC
    if (ExitReason == CPU_REASON_CALL_TO_NATIVE) {
      NativeThunk (gUE, Rip);
      Rip = CpuStackPop64 ();
    } else
#endif /* UPSTREAM_UC */
      if (ExitReason == CPU_REASON_RETURN_TO_NATIVE) {
        break;
      } else if (ExitReason == CPU_REASON_FAILED_EMU) {
        DEBUG ((DEBUG_ERROR, "Emulation failed: %a\n", uc_strerror (UcErr)));
        CpuDump ();
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
   *
   * A full flush is dangerous - we could be doing this in a native call
   * thunked from a TB (e.g. uc_set_native_thunks).
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

VOID
CpuRunFuncOnPrivateStack (
  IN  CpuRunFuncContext *Context
  )
{
  uc_err UcErr;
  gBS->RestoreTPL (Context->Tpl);

  if (Context->Nesting > 0) {
    UcErr = uc_context_alloc (gUE, &Context->PrevContext);
    if (UcErr != UC_ERR_OK) {
      DEBUG ((DEBUG_ERROR, "could not allocate context: %a\n", uc_strerror (UcErr)));
      Context->Ret = EFI_OUT_OF_RESOURCES;
      goto out;
    }

    UcErr = uc_context_save (gUE, Context->PrevContext);
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

  Context->Ret = CpuRunFuncInternal (Context->ProgramCounter, Context->Args);

  if (Context->PrevContext != NULL) {
    UcErr = uc_context_restore (gUE, Context->PrevContext);
    ASSERT (UcErr == UC_ERR_OK);
  }

out:
  /*
   * Critical section ends when code no longer modifies emulated state,
   * including registers.
   */
  CpuLeaveCritical (&Context->Tpl);

#ifdef ON_PRIVATE_STACK
  if (Context->PrevContext == NULL) {
    LongJump (&mOriginalStack, -1);
  }
#endif /* ON_PRIVATE_STACK */
}

UINT64
CpuRunFunc (
  IN  EFI_VIRTUAL_ADDRESS ProgramCounter,
  IN  UINT64              *Args
  )
{
  uc_err            UcErr;
  CpuRunFuncContext Context = { ProgramCounter, Args };

  Context.Nesting = CpuEnterCritical (&Context.Tpl);

#ifdef ON_PRIVATE_STACK
  if (Context.Nesting == 0) {
    if (SetJump(&mOriginalStack) == 0) {
      SwitchStack ((VOID *) CpuRunFuncOnPrivateStack,
                   &Context, NULL, (VOID *) mNativeStackTop);
    }
  } else
#endif /* ON_PRIVATE_STACK */
  {
    CpuRunFuncOnPrivateStack (&Context);
  }

  /*
   * Come back here from CpuRunFuncOnPrivateStack, directly
   * (when Context.PrevContext != NULL) or via LongJump (when
   * Context.PrevContext == NULL).
   */
  gBS->RestoreTPL (Context.Tpl);
  if (Context.PrevContext != NULL) {
    UcErr = uc_context_free (Context.PrevContext);
    ASSERT (UcErr == UC_ERR_OK);
  }

  return Context.Ret;
}
