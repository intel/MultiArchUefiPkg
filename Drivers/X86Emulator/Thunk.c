/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include <unicorn.h>
#include "X86Emulator.h"
#include "TestProtocol.h"

typedef UINT64 (*Fn)(UINT64, UINT64, UINT64, UINT64,
                     UINT64, UINT64, UINT64, UINT64,
                     UINT64, UINT64, UINT64, UINT64,
                     UINT64, UINT64, UINT64, UINT64);

typedef RET16 (*Ret16Fn)(UINT64, UINT64, UINT64, UINT64,
                         UINT64, UINT64, UINT64, UINT64,
                         UINT64, UINT64, UINT64, UINT64,
                         UINT64, UINT64, UINT64, UINT64);

typedef RET_LARGE (*RetLargeFn)(UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64);

STATIC
EFI_STATUS
EFIAPI
ThunkUnsupported (
  VOID
  )
{
  CpuDump ();
  return EFI_UNSUPPORTED;
}

#define THUNK_RET_LARGE (1 << 0)
#define THUNK_RET_16    (1 << 1)

STATIC
UINT64
ThunkValidateSupportedCall (
  IN  UINT64 Rip,
  OUT UINT64 *Flags
  )
{
  *Flags = 0;

  /*
   * Prevent things that won't work in principle or that
   * could kill the emulator.
   *
   * TODO: catch/filter SetMemoryAttributes to ignore any
   * attempts to change attributes for the emulated image itself?
   */
  if (Rip < EFI_PAGE_SIZE) {
    DEBUG ((DEBUG_ERROR, "NULL-pointer native call to 0x%lx\n", Rip));
    return (UINT64) &ThunkUnsupported;
  } else if (Rip == (UINTN) gBS->ExitBootServices) {
    DEBUG ((DEBUG_ERROR,
            "Unsupported emulated ExitBootServices\n"));
    return (UINT64) &ThunkUnsupported;
  } else if (Rip == (UINTN) gCpu->RegisterInterruptHandler) {
    DEBUG ((DEBUG_ERROR,
            "Unsupported emulated RegisterInterruptHandler\n"));
    return (UINT64) &ThunkUnsupported;
  }

#ifndef NDEBUG
  /*
   * These are examples of how to deal with "weird" native calls,
   * e.g. when a function returns 16 bytes or larger structure.
   *
   * This is not normal to the APIs/protocols in UEFI environment.
   * This shows how you manually make X86EmulatorPkg of certain
   * such calls.
   *
   * Note it's not possible to "detect" the API signature from
   * the X64 machine code itself, although a separate database
   * could be used here to query the native method sigs (e.g.
   * debugging info, another protocol, etc).
   *
   * SIMD/FP interop or SysV ABI could be handled similarly.
   */
  if (Rip == (UINTN) TestRet16) {
    *Flags = THUNK_RET_16;
  } else if (Rip == (UINTN) TestLargeRet) {
    *Flags = THUNK_RET_LARGE;
  }
#endif /* NDEBUG */
  return Rip;
}

VOID
ThunkToNative (
  IN  uc_engine *UE,
  IN  UINT64    Rip
  )
{
  UINT64 *StackArgs;
  UINT64 Rax;
  UINT64 Rsp;
  UINT64 Rcx;
  UINT64 Rdx;
  UINT64 R8;
  UINT64 R9;
  UINT64 Flags;

  Rip = ThunkValidateSupportedCall (Rip, &Flags);

  Rsp = REG_READ (RSP);
  Rcx = REG_READ (RCX);
  Rdx = REG_READ (RDX);
  R8 = REG_READ (R8);
  R9 = REG_READ (R9);

  StackArgs = (UINT64 *) Rsp;

  /*
   * EFIAPI (MS) x86_64 Stack Layout (in UINT64's):
   *
   * ----------------
   *   ...
   *   arg9
   *   arg8
   *   arg7
   *   arg6
   *   arg5
   *   arg4
   *   home zone (reserved for called function)
   *   home zone (reserved for called function)
   *   home zone (reserved for called function)
   *   home zone (reserved for called function)
   *   return pointer
   * ----------------
   */
  DEBUG ((DEBUG_VERBOSE, "XXX native fn 0x%lx(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx) flags 0x%lx\n",
          Rip, Rcx, Rdx, R8, R9, StackArgs[5], StackArgs[6], StackArgs[7], StackArgs[8],
          StackArgs[9], Flags));

  if ((Flags & THUNK_RET_LARGE) != 0) {
    RetLargeFn Func = (VOID *) Rip;
    *(RET_LARGE *) Rcx = Func (Rdx, R8, R9, StackArgs[5], StackArgs[6], StackArgs[7],
                               StackArgs[8], StackArgs[9], StackArgs[10], StackArgs[11],
                               StackArgs[12], StackArgs[13], StackArgs[14], StackArgs[15],
                               StackArgs[16], StackArgs[17]);
    Rax = Rcx;
  } else if ((Flags & THUNK_RET_16) != 0) {
    Ret16Fn Func = (VOID *) Rip;
    *(RET16 *) Rcx = Func (Rdx, R8, R9, StackArgs[5], StackArgs[6], StackArgs[7],
                           StackArgs[8], StackArgs[9], StackArgs[10], StackArgs[11],
                           StackArgs[12], StackArgs[13], StackArgs[14], StackArgs[15],
                           StackArgs[16], StackArgs[17]);
    Rax = Rcx;
  } else {
    Fn Func = (VOID *) Rip;
    Rax = Func (Rcx, Rdx, R8, R9, StackArgs[5], StackArgs[6], StackArgs[7],
                StackArgs[8], StackArgs[9], StackArgs[10], StackArgs[11],
                StackArgs[12], StackArgs[13], StackArgs[14], StackArgs[15],
                StackArgs[16]);
  }

  REG_WRITE (RAX, Rax);
}
