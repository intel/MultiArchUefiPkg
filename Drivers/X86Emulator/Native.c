/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include <unicorn.h>
#include "X86Emulator.h"
#include "TestProtocol.h"

typedef union {
  UINT64 (*NativeFn)(UINT64, UINT64, UINT64, UINT64,
                     UINT64, UINT64, UINT64, UINT64,
                     UINT64, UINT64, UINT64, UINT64,
                     UINT64, UINT64, UINT64, UINT64);
  UINT64 (*WrapperFn)(UINT64 OriginalRip, UINT64 ReturnAddress,
                      UINT64 *Args);
  UINT64 Rip;
} Fn;

EFI_STATUS
EFIAPI
NativeUnsupported (
  IN  UINT64 OriginalRip,
  IN  UINT64 ReturnAddress,
  IN  UINT64 *Args
  )
{
  DEBUG ((DEBUG_ERROR, "Unsupported native call 0x%lx from x64 RIP 0x%lx\n",
          OriginalRip, ReturnAddress));
  X86EmulatorDump ();
  return EFI_UNSUPPORTED;
}

STATIC
UINT64
NativeValidateSupportedCall (
  IN  UINT64  Rip
  )
{
  /*
   * Prevent things that won't work in principle or that
   * could kill the emulator.
   */
  if (Rip < EFI_PAGE_SIZE) {
    DEBUG ((DEBUG_ERROR, "NULL-pointer native call to 0x%lx\n", Rip));
    return (UINT64) &NativeUnsupported;
  }

  return EfiWrappersOverride (Rip);
}

VOID
NativeThunkX86 (
  IN  CpuEmu *CpuEmu,
  IN  UINT64 Rip
  )
{
  UINT64 *StackArgs;
  BOOLEAN WrapperCall;
  UINT64  Rax;
  UINT64  Rsp;
  UINT64  Rcx;
  UINT64  Rdx;
  UINT64  R8;
  UINT64  R9;
  Fn      Func;
  CpuRunContext *CurrentTopContext = CpuGetTopContext ();

  Func.Rip = NativeValidateSupportedCall (Rip);
  WrapperCall = Func.Rip != Rip;

  Rsp = REG_READ (CpuEmu, UC_X86_REG_RSP);
  Rcx = REG_READ (CpuEmu, UC_X86_REG_RCX);
  Rdx = REG_READ (CpuEmu, UC_X86_REG_RDX);
  R8 = REG_READ (CpuEmu, UC_X86_REG_R8);
  R9 = REG_READ (CpuEmu, UC_X86_REG_R9);

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

  DEBUG ((DEBUG_VERBOSE, "x64 0x%lx -> %a 0x%lx(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx)\n",
          StackArgs[0], WrapperCall ? "wrapper" : "native", Func.Rip, Rcx, Rdx, R8, R9,
          StackArgs[5], StackArgs[6], StackArgs[7], StackArgs[8], StackArgs[9]));

  if (WrapperCall) {
    StackArgs[1] = Rcx;
    StackArgs[2] = Rdx;
    StackArgs[3] = R8;
    StackArgs[4] = R9;
    Rax = Func.WrapperFn (Rip, StackArgs[0], StackArgs + 1);
  } else {
    Rax = Func.NativeFn (Rcx, Rdx, R8, R9, StackArgs[5], StackArgs[6], StackArgs[7],
                         StackArgs[8], StackArgs[9], StackArgs[10], StackArgs[11],
                         StackArgs[12], StackArgs[13], StackArgs[14], StackArgs[15],
                         StackArgs[16]);
  }

  if (CpuGetTopContext () != CurrentTopContext) {
    /*
     * Consider the following sequence:
     * - x64->native call
     * - native does SetJump
     * - native->x64 call
     * - x64->native call
     * - native does LongJump
     *
     * This isn't that crazy - e.g. an x64 binary starting another
     * x64 binary, which calls gBS->Exit. While we can handle gBS->Exit
     * cleanly ourselves, let's detect code that does something similar,
     * which will result in UC engine state being out of sync with the
     * expected context state.
     */
    CpuCompressLeakedContexts (CurrentTopContext, FALSE);
  }

  REG_WRITE (CpuEmu, UC_X86_REG_RAX, Rax);
}
