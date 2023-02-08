/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

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
#include <Library/TimerLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PeCoffLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/Cpu.h>
#include <Protocol/CpuIo2.h>
#include <Protocol/DebugSupport.h>
#include <Protocol/PeCoffImageEmulator.h>
#include <Protocol/LoadedImage.h>

#include "TestProtocol.h"

#ifdef MDE_CPU_AARCH64
#define NATIVE_INSN_ALIGNMENT 4
#elif defined (MDE_CPU_RISCV64)
/*
 * Built with compressed instructions, possibly.
 */
#define NATIVE_INSN_ALIGNMENT 2
#endif

#define UNUSED __attribute__((unused))

#define REG_READ(CpuEmu, x) ({                                  \
      UINT64 Reg;                                               \
      UNUSED uc_err UcErr;                                      \
      UcErr = uc_reg_read ((CpuEmu)->UE, x, &Reg);              \
      ASSERT (UcErr == UC_ERR_OK);                              \
      Reg;                                                      \
    })

#define REG_WRITE(CpuEmu, x, Val) ({                              \
      UNUSED uc_err UcErr;                                        \
      UINT64 Temp = Val;                                          \
      UcErr = uc_reg_write ((CpuEmu)->UE, x, &Temp);              \
      ASSERT (UcErr == UC_ERR_OK);                                \
    })

typedef struct uc_struct uc_engine;
typedef struct uc_context uc_context;

typedef struct CpuRunContext CpuRunContext;

typedef struct CpuEmu {
  int                  StackReg;
  int                  Contexts;
  VOID                 (*Dump) (struct CpuEmu *);
  UINT64               (*RunCtxInternal) (CpuRunContext *);
  VOID                 (*NativeThunk) (struct CpuEmu *, UINT64 *ProgramCounter);
  uc_engine            *UE;
  EFI_PHYSICAL_ADDRESS UnicornCodeGenBuf;
  EFI_PHYSICAL_ADDRESS UnicornCodeGenBufEnd;
  EFI_PHYSICAL_ADDRESS EmuStackStart;
  EFI_PHYSICAL_ADDRESS EmuStackTop;
  uc_context           *InitialState;
#ifndef EMU_TIMEOUT_NONE
  UINT64               TbCount;
  UINT64               ExitPeriodTbs;
  UINT64               ExitPeriodTicks;
#endif /* EMU_TIMEOUT_NONE */
} CpuEmu;

typedef struct {
  LIST_ENTRY               Link;
  EFI_PHYSICAL_ADDRESS     ImageBase;
  EFI_PHYSICAL_ADDRESS     ImageEntry;
  UINT64                   ImageSize;
  EFI_HANDLE               ImageHandle;
  /*
   * ISA-specific.
   */
  CpuEmu                   *Cpu;
  /*
   * To support the Exit() boot service.
   */
  BASE_LIBRARY_JUMP_BUFFER ImageExitJumpBuffer;
  EFI_STATUS               ImageExitStatus;
  UINTN                    ImageExitDataSize;
  CHAR16                   *ImageExitData;
} X86_IMAGE_RECORD;

typedef struct CpuRunContext {
  CpuEmu               *Cpu;
  EFI_VIRTUAL_ADDRESS  ProgramCounter;
#ifdef CHECK_ORPHAN_CONTEXTS
  UINT64               LeakCookie;
#endif /* CHECK_ORPHAN_CONTEXTS */
  UINT64               *Args;
  UINT64               Ret;
  EFI_TPL              Tpl;

  UINT64               TimeoutAbsTicks;
  BOOLEAN              StoppedOnTimeout;
  uc_context           *PrevUcContext;
  struct CpuRunContext *PrevContext;
  /*
   * Only set when we're invoking the entry point of an image.
   */
  X86_IMAGE_RECORD     *ImageRecord;
} CpuRunContext;

extern CpuEmu                    CpuX86;
extern EFI_CPU_ARCH_PROTOCOL     *gCpu;
extern EFI_CPU_IO2_PROTOCOL      *gCpuIo2;
extern EFI_LOADED_IMAGE_PROTOCOL *gDriverImage;

VOID
X86EmulatorDump (
  VOID
  );

X86_IMAGE_RECORD *
FindImageRecordByAddress (
  IN  EFI_PHYSICAL_ADDRESS Address
  );

X86_IMAGE_RECORD *
FindImageRecordByHandle (
  IN  EFI_HANDLE Handle
  );

VOID
DumpImageRecords (
  VOID
  );

UINT64
CpuStackPop64 (
  IN  CpuEmu *Cpu
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

EFI_STATUS
CpuRunImage (
  IN  EFI_HANDLE       ImageHandle,
  IN  EFI_SYSTEM_TABLE *SystemTable
  );

VOID
CpuCompressLeakedContexts (
  IN  CpuRunContext *CurrentContext,
  IN  BOOLEAN       OnImageExit
  );

EFI_STATUS
CpuExitImage (
  IN  CpuEmu *Cpu,
  IN  UINT64 OriginalRip,
  IN  UINT64 ReturnAddress,
  IN  UINT64 *Args
  );

CpuRunContext *
CpuGetTopContext (
  VOID
  );

UINT64
CpuRunFunc (
  IN  CpuEmu              *Cpu,
  IN  EFI_VIRTUAL_ADDRESS ProgramCounter,
  IN  UINT64              *Args
  );

#ifndef NDEBUG
EFI_STATUS
EFIAPI
CpuGetDebugState (
  OUT X86_EMU_TEST_DEBUG_STATE *DebugState
  );
#endif

VOID
CpuUnregisterCodeRange (
  IN  CpuEmu               *Cpu,
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  );

VOID
CpuRegisterCodeRange (
  IN  CpuEmu               *Cpu,
  IN  EFI_PHYSICAL_ADDRESS ImageBase,
  IN  UINT64               ImageSize
  );

BOOLEAN
CpuAddrIsCodeGen (
  IN  EFI_PHYSICAL_ADDRESS Address
  );

VOID
NativeThunkX86 (
  IN  CpuEmu *Cpu,
  IN  UINT64 *Rip
  );

EFI_STATUS
EFIAPI
NativeUnsupported (
  IN  CpuEmu *Cpu,
  IN  UINT64 OriginalRip,
  IN  UINT64 ReturnAddress,
  IN  UINT64 *Args
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

VOID
EfiWrappersInit (
  VOID
  );

UINT64
EfiWrappersOverride (
  IN  UINT64 Rip
  );

VOID
EfiWrappersDump (
  VOID
  );
