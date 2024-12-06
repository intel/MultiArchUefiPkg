/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>
    Copyright (c) 2024 Loongson Technology Corporation Limited. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "Emulator.h"
#include <Library/CpuExceptionHandlerLib.h>
#include <Register/LoongArch64/Csr.h>

extern CONST UINT64  EmulatorThunk[];

VOID
EFIAPI
DefaultExceptionHandler(
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  );

VOID
EFIAPI
EmulatorSyncExceptionCallback (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  EFI_SYSTEM_CONTEXT_LOONGARCH64  *LoongArch64Context;
  ImageRecord                     *Record;

  LoongArch64Context = SystemContext.SystemContextLoongArch64;

  /*
   * Instruction permission faults or PC misalignment faults thunk
   * us to emulated code.
   */
   Record = ImageFindByAddress (LoongArch64Context->ERA);
   if (Record != NULL) {
     LoongArch64Context->R12 = LoongArch64Context->ERA;
     LoongArch64Context->R13 = (UINT64)Record;
     LoongArch64Context->ERA = (UINT64)EmulatorThunk;
     return;
   }

  /*
   * We can't handle these exception. Try to produce some meaningful
   * diagnostics regarding the emulated code this maps onto.
   */
  if (CpuAddrIsCodeGen (LoongArch64Context->ERA)) {
    /*
     * It looks like we crashed in the JITed code.
     *
     * TBD: can we lookup/decode the TB info?
     */
    DEBUG ((DEBUG_ERROR, "Exception occurred in TBs\n"));
  }

  if ((LoongArch64Context->ERA >= (UINT64)gDriverImage->ImageBase) &&
      (LoongArch64Context->ERA <= ((UINT64)gDriverImage->ImageBase +
                               gDriverImage->ImageSize - 1)))
  {
    DEBUG ((
      DEBUG_ERROR,
      "Exception occured at driver PC +0x%lx\n",
      LoongArch64Context->ERA - (UINT64)gDriverImage->ImageBase
      ));
  }

  /*
   * Emulate failed, dump the messages.
   */
  EmulatorDump ();
  DefaultExceptionHandler (ExceptionType, SystemContext);
}

EFI_STATUS
ArchInit (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = gCpu->RegisterInterruptHandler (
                   gCpu,
                   EXCEPT_LOONGARCH_PNX << CSR_ESTAT_EXC_SHIFT,
                   &EmulatorSyncExceptionCallback
                   );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RegisterInterruptHandler failed: %r\n", Status));
  }

  Status = gCpu->RegisterInterruptHandler (
                   gCpu,
                   EXCEPT_LOONGARCH_ADE << CSR_ESTAT_EXC_SHIFT,
                   &EmulatorSyncExceptionCallback
                   );
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
  EFI_STATUS  Status;

  Status = gCpu->RegisterInterruptHandler (
                   gCpu,
                   EXCEPT_LOONGARCH_PNX << CSR_ESTAT_EXC_SHIFT,
                   NULL
                   );
  ASSERT (!EFI_ERROR (Status));

  Status = gCpu->RegisterInterruptHandler (
                   gCpu,
                   EXCEPT_LOONGARCH_ADE << CSR_ESTAT_EXC_SHIFT,
                   NULL
                   );
  ASSERT (!EFI_ERROR (Status));
}
