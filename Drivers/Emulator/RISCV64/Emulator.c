/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "Emulator.h"
#include <Library/CpuExceptionHandlerLib.h>

#define INSN_C_ADDR_MASK  (-1ULL - 1)
#define INSN_ADDR_MASK    (-1ULL - 3)

extern CONST UINT64  EmulatorThunk[];

STATIC
EFI_STATUS
RecoverPcFromCall (
  IN  ImageRecord                 *ImageRecord,
  IN  EFI_SYSTEM_CONTEXT_RISCV64  *RiscV64Context,
  OUT EFI_PHYSICAL_ADDRESS        *ProgramCounter
  )
{
  UINT32  Insn;
  UINT64  Ra = RiscV64Context->X1;

  /*
   * SEPC always clears 0 and 1 as per IALIGN. Thus SEPC is always less
   * than PC.
   *
   * To recover PC, we look at the X1 (aka RA) register. We know
   * there was a branch to the PC value, but it could have been either
   * a compressed or a regular jalr, so we look at RA - 4 and RA - 2,
   * decode the instruction and reconstruct the PC value.
   *
   * Of course, the reconstructed PC value should match SEPC.
   *
   * If SEPC is ever larger than PC, then:
   * - Must have been using illegal instruction handler (not MMU)
   * - The actual instruction at PC is unfortunately a valid RISC-V
   *   instruction and it's been executed, randomly corrupting something
   *   (but at least not crashing).
   */
  Insn = *(UINT32 *)(Ra - 4);
  if ((Insn & 0x3) == 0x3) {
    /*
     * 32-bit instruction.
     */
    if (((Insn & 0x7f) == 0x67) &&  // opcode == jalr
        ((Insn & 0xf80) == 0x80) && // rd == x1
        ((Insn & 0x3000) == 0))     // func3
    {
      struct {
        signed int    x : 12;
      } imm12;
      UINTN  Rs = (Insn >> 15) & 0x1F;

      imm12.x         = Insn >> 20;
      *ProgramCounter = (&RiscV64Context->X0)[Rs] + imm12.x;

      /*
       * If the C extension is not implemented, SEPC can be 2 bits away from PC.
       * Otherwise it is 1 bit away from PC, as 32-bit instructions can be
       * aligned at 16-bit boundary as well.
       */
      if (((*ProgramCounter & INSN_C_ADDR_MASK) != RiscV64Context->SEPC) &&
          ((*ProgramCounter & INSN_ADDR_MASK) != RiscV64Context->SEPC))
      {
        DEBUG ((
          DEBUG_ERROR,
          "%a PC 0x%lx but SEPC 0x%lx, opcode 0x%04x @ 0x%lx-4\n",
          ImageRecord->Cpu->Name,
          *ProgramCounter,
          RiscV64Context->SEPC,
          Insn,
          Ra
          ));
        return EFI_NOT_FOUND;
      }

      return EFI_SUCCESS;
    }

    DEBUG ((
      DEBUG_ERROR,
      "Unknown %a PC: bad opcode 0x%04x @ 0x%lx-4\n",
      ImageRecord->Cpu->Name,
      Insn,
      Ra - 4
      ));

    return EFI_NOT_FOUND;
  }

  Insn = *(UINT16 *)(Ra - 2);
  if ((Insn  & 0x3) != 0x3) {
    if (((Insn & 0x3) == 2) &&         // op == C2
        ((Insn & 0xf000) == 0x9000) && // funct4 == c.jalr
        ((Insn & 0xf80) != 0) &&       // rs1
        ((Insn & 0x7c) == 0))          // rs2
    {
      UINTN  Rs = (Insn >> 7) & 0x1F;
      *ProgramCounter = (&RiscV64Context->X0)[Rs];

      /*
       * SEPC can be 1 bit away from PC.
       */
      if ((*ProgramCounter & INSN_C_ADDR_MASK) != RiscV64Context->SEPC) {
        DEBUG ((
          DEBUG_ERROR,
          "Unexpected %a PC: PC 0x%lx but SEPC 0x%lx, opcode 0x%02x @ 0x%lx-2\n",
          ImageRecord->Cpu->Name,
          *ProgramCounter,
          RiscV64Context->SEPC,
          Insn,
          Ra
          ));
        return EFI_NOT_FOUND;
      }

      return EFI_SUCCESS;
    }

    DEBUG ((
      DEBUG_ERROR,
      "Unknown %a PC: bad opcode 0x%02x at 0x%lx-2\n",
      ImageRecord->Cpu->Name,
      Insn,
      Ra
      ));
    return EFI_NOT_FOUND;
  }

  DEBUG ((
    DEBUG_ERROR,
    "Unknown %a PC: could not find branch before RA 0x%lx\n",
    ImageRecord->Cpu->Name,
    Ra
    ));
  return EFI_NOT_FOUND;
}

STATIC
inline
BOOLEAN
SupportedException (
  IN     EFI_EXCEPTION_TYPE  ExceptionType
  )
{
  if (ExceptionType == EXCEPT_RISCV_INST_ACCESS_PAGE_FAULT) {
    return TRUE;
  }

 #ifdef MAU_TRY_WITHOUT_MMU
  if (ExceptionType == EXCEPT_RISCV_ILLEGAL_INST) {
    return TRUE;
  }

 #endif /* MAU_TRY_WITHOUT_MMU */

  return FALSE;
}

VOID
EFIAPI
EmulatorSyncExceptionCallback (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  EFI_SYSTEM_CONTEXT_RISCV64  *RiscV64Context;
  ImageRecord                 *Record;

  RiscV64Context = SystemContext.SystemContextRiscV64;
  Record         = ImageFindByAddress (RiscV64Context->SEPC);

  if (SupportedException (ExceptionType)) {
    if (Record != NULL) {
      EFI_STATUS  Status;

      /*
       * SEPC always has bits 0 and 1 cleared as per
       * IALIGN. So it's good enough to validate the
       * PC came from the emulated image, but not good
       * enough for transfer control. We have to recover
       * the address by decoding the instruction that
       * made the call.
       */
      Status = RecoverPcFromCall (
                 Record,
                 RiscV64Context,
                 &RiscV64Context->X5
                 );
      if (!EFI_ERROR (Status)) {
        RiscV64Context->X6   = (UINT64)Record;
        RiscV64Context->SEPC = (UINT64)EmulatorThunk;
        return;
      }

      /*
       * On errors, RecoverPcFromCall already logged.
       */
      Record = NULL;
    }
  }

  /*
   * We can't handle this exception. Try to produce some meaningful
   * diagnostics regarding the emulated code this maps onto.
   */

  if (Record != NULL) {
    /*
     * This can happen if CpuDxe doesn't configure MMU, so there is no
     * execute protection on the emulated code ranges.
     *
     * Two ways of getting here:
     *
     * 1) Invoked instruction didn't look like a valid RISC-V instruction.
     *    This is a recoverable situation, but the driver was not built
          with MAU_TRY_WITHOUT_MMU, so SupportedException returned FALSE.
     *
     * 2) If the invoked emulated code actually looked like a valid RISC-V
     *    instruction, and the instruction performed an invalid operation.
     */
    DEBUG ((
      DEBUG_ERROR,
      "Exception occured due to executing %a as RISC-V code.\n"
      "Running on a UEFI build without a required working MMU.\n",
      Record->Cpu->Name
      ));
  } else if (CpuAddrIsCodeGen (RiscV64Context->SEPC)) {
    /*
     * It looks like we crashed in the JITed code.
     *
     * TBD: can we lookup/decode the TB info?
     */
    DEBUG ((DEBUG_ERROR, "Exception occurred in TBs\n"));
  } else if ((RiscV64Context->SEPC >= (UINT64)gDriverImage->ImageBase) &&
             (RiscV64Context->SEPC <= ((UINT64)gDriverImage->ImageBase +
                                       gDriverImage->ImageSize - 1)))
  {
    DEBUG ((
      DEBUG_ERROR,
      "Exception occured at driver PC +0x%lx, RA +0x%lx\n",
      RiscV64Context->SEPC - (UINT64)gDriverImage->ImageBase,
      RiscV64Context->X1 - (UINT64)gDriverImage->ImageBase
      ));
  }

  EmulatorDump ();
  DumpCpuContext (ExceptionType, SystemContext);

  CpuBreakpoint ();
}

STATIC UINTN  mExceptions[] = {
  EXCEPT_RISCV_STORE_ACCESS_PAGE_FAULT,
  EXCEPT_RISCV_LOAD_ACCESS_PAGE_FAULT,
  EXCEPT_RISCV_INST_ACCESS_PAGE_FAULT,

  /*
   * If CpuDxe doesn't configure MMU, then the illegal instruction
   * trap may work, but good luck!
   */
  EXCEPT_RISCV_ILLEGAL_INST,
 #ifndef NDEBUG

  /*
   * These are for debugging only. Don't take these by default to
   * avoid conflicts with other client. Useful if you suspect
   * crashes in JITted code or the driver itself.
   */
  EXCEPT_RISCV_LOAD_ACCESS_FAULT
 #endif
};

EFI_STATUS
ArchInit (
  VOID
  )
{
  INTN        Index;
  EFI_STATUS  Status;
  EFI_STATUS  Status2;

  for (Index = 0; Index < ARRAY_SIZE (mExceptions); Index++) {
    Status = gCpu->RegisterInterruptHandler (
                     gCpu,
                     mExceptions[Index],
                     &EmulatorSyncExceptionCallback
                     );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "RegisterInterruptHandler 0x%x: %r\n", mExceptions[Index], Status));
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    while (--Index >= 0) {
      Status2 = gCpu->RegisterInterruptHandler (gCpu, mExceptions[Index], NULL);
      ASSERT (!EFI_ERROR (Status2));
    }
  }

  return Status;
}

VOID
ArchCleanup (
  VOID
  )
{
  INTN        Index;
  EFI_STATUS  Status;

  for (Index = 0; Index < ARRAY_SIZE (mExceptions); Index++) {
    Status = gCpu->RegisterInterruptHandler (gCpu, mExceptions[Index], NULL);
    ASSERT (!EFI_ERROR (Status));
  }
}
