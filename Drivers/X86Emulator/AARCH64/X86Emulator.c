/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "X86Emulator.h"
#include <Library/DefaultExceptionHandlerLib.h>

extern CONST UINT64 X86EmulatorThunk[];

VOID
EFIAPI
X86InterpreterSyncExceptionCallback (
  IN     EFI_EXCEPTION_TYPE   ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  EFI_SYSTEM_CONTEXT_AARCH64  *AArch64Context;
  X86_IMAGE_RECORD            *Record;
  UINTN                       Ec;

  AArch64Context = SystemContext.SystemContextAArch64;

  /*
   * Instruction permission faults or PC misalignment faults thunk
   * us to emulated code.
   */
  Ec = AArch64Context->ESR >> 26;
  if ((Ec == 0x21 && (AArch64Context->ESR & 0x3c) == 0xc) || Ec == 0x22) {
    Record = FindImageRecord (AArch64Context->ELR);
    if (Record != NULL) {
      AArch64Context->X16 = AArch64Context->ELR;
      AArch64Context->X17 = (UINT64)Record;
      AArch64Context->ELR = (UINT64)X86EmulatorThunk;
      return;
    }
  }

  if (UnicornCodeGenBuf != UnicornCodeGenBufEnd &&
      AArch64Context->ELR >= UnicornCodeGenBuf &&
      AArch64Context->ELR < UnicornCodeGenBufEnd) {
    /*
     * It looks like we crashed in the JITed code. Check whether we are
     *
     * We can't handle this exception. Try to produce some meaningful
     * diagnostics regarding the X86 code this maps onto.
     */
    DEBUG ((DEBUG_ERROR, "Exception occurred in TBs\n"));
  }

  DumpImageRecords ();
  CpuDump ();
  DefaultExceptionHandler (ExceptionType, SystemContext);
}

EFI_STATUS
ArchInit (
  VOID
  )
{
  EFI_STATUS Status;

  Status = gCpu->RegisterInterruptHandler (gCpu,
                                           EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS,
                                           &X86InterpreterSyncExceptionCallback);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RegisterInterruptHandler failed: %r\n", Status));
  }

  return Status;
}

VOID
ArchCleanup (
  VOID
  )
{
  EFI_STATUS Status;
  Status = gCpu->RegisterInterruptHandler (gCpu,
                                           EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS,
                                           NULL);
  ASSERT (!EFI_ERROR (Status));
}
