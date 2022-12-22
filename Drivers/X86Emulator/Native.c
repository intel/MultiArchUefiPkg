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

STATIC
EFI_STATUS
EFIAPI
NativeUnsupported (
  VOID
  )
{
  CpuDump ();
  return EFI_UNSUPPORTED;
}

STATIC
UINT64
NativeValidateSupportedCall (
  IN  UINT64 Rip
  )
{
  /*
   * Prevent things that won't work in principle or that
   * could kill the emulator.
   *
   * TODO: catch/filter SetMemoryAttributes to ignore any
   * attempts to change attributes for the emulated image itself?
   */
  if (Rip < EFI_PAGE_SIZE) {
    DEBUG ((DEBUG_ERROR, "NULL-pointer native call to 0x%lx\n", Rip));
    return (UINT64) &NativeUnsupported;
  } else if (Rip == (UINTN) gBS->ExitBootServices) {
    DEBUG ((DEBUG_ERROR,
            "Unsupported emulated ExitBootServices\n"));
    return (UINT64) &NativeUnsupported;
  } else if (Rip == (UINTN) gCpu->RegisterInterruptHandler) {
    DEBUG ((DEBUG_ERROR,
            "Unsupported emulated RegisterInterruptHandler\n"));
    return (UINT64) &NativeUnsupported;
  }

  return Rip;
}

VOID
NativeThunk (
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
  Fn Func;

  Rip = NativeValidateSupportedCall (Rip);

  Rsp = REG_READ (RSP);
  Rcx = REG_READ (RCX);
  Rdx = REG_READ (RDX);
  R8 = REG_READ (R8);
  R9 = REG_READ (R9);

  StackArgs = (UINT64 *) Rsp;
  Func = (VOID *) Rip;

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
  DEBUG ((DEBUG_VERBOSE, "XXX native fn 0x%lx(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx)\n",
          Rip, Rcx, Rdx, R8, R9, StackArgs[5], StackArgs[6],
          StackArgs[7], StackArgs[8], StackArgs[9]));

  Rax = Func (Rcx, Rdx, R8, R9, StackArgs[5], StackArgs[6], StackArgs[7],
              StackArgs[8], StackArgs[9], StackArgs[10], StackArgs[11],
              StackArgs[12], StackArgs[13], StackArgs[14], StackArgs[15],
              StackArgs[16]);

  REG_WRITE (RAX, Rax);
}
