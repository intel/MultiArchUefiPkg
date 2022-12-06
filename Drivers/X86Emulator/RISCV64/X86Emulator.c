/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "X86Emulator.h"
#include <Library/CpuExceptionHandlerLib.h>

extern CONST UINT64 X86EmulatorThunk[];

STATIC
EFI_STATUS
RecoverPcFromCall (
  IN  EFI_SYSTEM_CONTEXT_RISCV64 *RiscV64Context,
  OUT EFI_PHYSICAL_ADDRESS *Pc
  )
{
  UINT32 Insn;
  UINT64 Ra = RiscV64Context->X1;

  Insn = *(UINT16 *)(Ra - 2);
  if ((Insn & 0x3) == 2 &&         // op
      (Insn & 0xf000) == 0x9000 && // funct4
      (Insn & 0xf80) != 0 &&       // rs1
      (Insn & 0x7c) == 0) {        // rs2
    UINTN Rs = (Insn >> 7) & 0x1F;
    *Pc = (&RiscV64Context->X0)[Rs];

    if ((*Pc & RiscV64Context->SEPC) != RiscV64Context->SEPC) {
      DEBUG ((DEBUG_ERROR, "Unknown x64 RIP: PC 0x%lx but SEPC 0x%lx\n",
              *Pc,  RiscV64Context->SEPC));
      return EFI_NOT_FOUND;
    }

    return EFI_SUCCESS;
  }

  if (Ra % 4) {
    /*
     * It was definitely an (unknown) compressed instruction.
     */
    DEBUG ((DEBUG_ERROR, "Unknown x64 RIP: bad instruction 0x%x\n", Insn));
    return EFI_NOT_FOUND;
  }

  Insn = *(UINT32 *)(Ra - 4);
  DEBUG ((DEBUG_ERROR, "Unknown x64 RIP: bad instruction 0x%x\n", Insn));

  return EFI_NOT_FOUND;
}

VOID
EFIAPI
X86InterpreterSyncExceptionCallback (
  IN     EFI_EXCEPTION_TYPE   ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  EFI_SYSTEM_CONTEXT_RISCV64 *RiscV64Context;
  X86_IMAGE_RECORD           *Record;

  RiscV64Context = SystemContext.SystemContextRiscV64;

  if (ExceptionType == EXCEPT_RISCV_INST_ACCESS_PAGE_FAULT ||
      ExceptionType == EXCEPT_RISCV_ILLEGAL_INST) {
    Record = FindImageRecord (RiscV64Context->SEPC);
    if (Record != NULL) {
      EFI_STATUS Status;
      /*
       * SEPC always has bits 0 and 1 cleared as per
       * IALIGN. So it's good enough to validate the
       * PC came from the emulated image, but not good
       * enough for transfer control. We have to recover
       * the address by decoding the instruction that
       * made the call.
       */
      Status = RecoverPcFromCall (RiscV64Context,
                                  &RiscV64Context->X5);
      if (!EFI_ERROR (Status)) {
        RiscV64Context->X6 = (UINT64)Record;
        RiscV64Context->SEPC = (UINT64)X86EmulatorThunk;
        return;
      }
    }
  }

  if (UnicornCodeGenBuf != UnicornCodeGenBufEnd &&
      RiscV64Context->SEPC >= UnicornCodeGenBuf &&
      RiscV64Context->SEPC < UnicornCodeGenBufEnd) {
    /*
     * It looks like we crashed in the JITed code. Check whether we are
     *
     * We can't handle this exception. Try to produce some meaningful
     * diagnostics regarding the X86 code this maps onto.
     */
    DEBUG ((DEBUG_ERROR, "Exception occurred during emulation:\n"));
    CpuDump ();
  }

  DumpCpuContext (ExceptionType, SystemContext);
}

STATIC BOOLEAN mSapfHandler;
STATIC BOOLEAN mLapfHandler;
STATIC BOOLEAN mIapfHandler;
STATIC BOOLEAN mIllHandler;

EFI_STATUS
ArchInit (
  VOID
  )
{
  EFI_STATUS Status;

  Status = gCpu->RegisterInterruptHandler (gCpu,
                                           EXCEPT_RISCV_STORE_ACCESS_PAGE_FAULT,
                                           &X86InterpreterSyncExceptionCallback);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RegisterInterruptHandler for store page fault failed: %r\n", Status));
    goto out;
  }
  mSapfHandler = TRUE;

  Status = gCpu->RegisterInterruptHandler (gCpu,
                                           EXCEPT_RISCV_LOAD_ACCESS_PAGE_FAULT,
                                           &X86InterpreterSyncExceptionCallback);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RegisterInterruptHandler for load page fault failed: %r\n", Status));
    goto out;
  }
  mLapfHandler = TRUE;

  Status = gCpu->RegisterInterruptHandler (gCpu,
                                           EXCEPT_RISCV_INST_ACCESS_PAGE_FAULT,
                                           &X86InterpreterSyncExceptionCallback);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RegisterInterruptHandler for fetch page fault failed: %r\n", Status));
    goto out;
  }
  mIapfHandler = TRUE;

  /*
   * If CpuDxe doesn't configure MMU, then the illegal instruction
   * trap may work, but good luck!
   */
  Status = gCpu->RegisterInterruptHandler (gCpu,
                                           EXCEPT_RISCV_ILLEGAL_INST,
                                           &X86InterpreterSyncExceptionCallback);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RegisterInterruptHandler for illegal inst fault failed: %r\n", Status));
    goto out;
  }
  mIllHandler = TRUE;

out:
  if (EFI_ERROR (Status)) {
    ArchCleanup ();
  }
  return Status;
}

VOID
ArchCleanup (
  VOID
  )
{
  EFI_STATUS Status;

  if (mSapfHandler) {
    Status = gCpu->RegisterInterruptHandler (gCpu,
                                             EXCEPT_RISCV_STORE_ACCESS_PAGE_FAULT,
                                             NULL);
    ASSERT (!EFI_ERROR (Status));
  }

  if (mLapfHandler) {
    Status = gCpu->RegisterInterruptHandler (gCpu,
                                             EXCEPT_RISCV_LOAD_ACCESS_PAGE_FAULT,
                                             NULL);
    ASSERT (!EFI_ERROR (Status));
  }

  if (mIapfHandler) {
    Status = gCpu->RegisterInterruptHandler (gCpu,
                                             EXCEPT_RISCV_INST_ACCESS_PAGE_FAULT,
                                             NULL);
    ASSERT (!EFI_ERROR (Status));
  }

  if (mIllHandler) {
    Status = gCpu->RegisterInterruptHandler (gCpu,
                                             EXCEPT_RISCV_ILLEGAL_INST,
                                             NULL);
    ASSERT (!EFI_ERROR (Status));
  }
}
