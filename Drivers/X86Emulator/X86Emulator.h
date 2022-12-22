/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#pragma once

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PeCoffLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/Cpu.h>
#include <Protocol/CpuIo2.h>
#include <Protocol/DebugSupport.h>
#include <Protocol/PeCoffImageEmulator.h>
#include <Protocol/LoadedImage.h>

#ifdef MDE_CPU_AARCH64
#define NATIVE_INSN_ALIGNMENT 4
#elif defined (MDE_CPU_RISCV64)
/*
 * Built with compressed instructions, possibly.
 */
#define NATIVE_INSN_ALIGNMENT 2
#endif

#define UNUSED __attribute__((unused))

#define REG_READ(x) ({                                  \
      UINT64 Reg;                                       \
      UNUSED uc_err UcErr;                              \
      UcErr = uc_reg_read (gUE, UC_X86_REG_##x, &Reg);  \
      ASSERT (UcErr == UC_ERR_OK);                      \
      Reg;                                              \
    })

#define REG_WRITE(x, Val) ({                              \
      UNUSED uc_err UcErr;                                \
      UINT64 Temp = Val;                                  \
      UcErr = uc_reg_write (gUE, UC_X86_REG_##x, &Temp);  \
      ASSERT (UcErr == UC_ERR_OK);                        \
    })

typedef struct uc_struct uc_engine;
typedef struct uc_context uc_context;

extern EFI_CPU_ARCH_PROTOCOL *gCpu;
extern EFI_CPU_IO2_PROTOCOL  *gCpuIo2;

extern uc_engine *gUE;
extern EFI_PHYSICAL_ADDRESS UnicornCodeGenBuf;
extern EFI_PHYSICAL_ADDRESS UnicornCodeGenBufEnd;

typedef struct {
  LIST_ENTRY           Link;
  EFI_PHYSICAL_ADDRESS ImageBase;
  EFI_PHYSICAL_ADDRESS ImageEntry;
  UINT64               ImageSize;
} X86_IMAGE_RECORD;

typedef struct {
  EFI_VIRTUAL_ADDRESS ProgramCounter;
  UINT64              *Args;
  UINTN               Nesting;
  UINT64              Ret;
  EFI_TPL             Tpl;
  uc_context          *PrevContext;
} CpuRunFuncContext;

X86_IMAGE_RECORD *
FindImageRecord (
  IN  EFI_PHYSICAL_ADDRESS Address
  );

VOID
CpuDump (
  VOID
  );

VOID
CpuCleanup (
  VOID
  );

EFI_STATUS
CpuInit (
  VOID
  );

BOOLEAN
IsNativeCall (
  IN  UINT64 Pc
  );

UINT64
CpuRunFunc (
  IN  EFI_VIRTUAL_ADDRESS ProgramCounter,
  IN  UINT64              *Args
  );

VOID
CpuUnregisterCodeRange (
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  );

VOID
CpuRegisterCodeRange (
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  );

VOID
NativeThunk (
  IN  uc_engine *UE,
  IN  UINT64    Rip
  );

EFI_STATUS
TestProtocolInit (
  IN  EFI_HANDLE ImageHandle
  );

EFI_STATUS
ArchInit (
  VOID
  );

VOID
ArchCleanup (
  VOID
  );
