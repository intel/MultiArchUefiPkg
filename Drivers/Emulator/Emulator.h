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

/*
 * Maximum # of arguments thunked between native and emulated code.
 */
#define MAX_ARGS  16

#ifdef MDE_CPU_AARCH64
#define NATIVE_INSN_ALIGNMENT  4
#elif defined (MDE_CPU_RISCV64)

/*
 * Built with compressed instructions, possibly.
 */
#define NATIVE_INSN_ALIGNMENT  2
#endif

#ifdef MDE_CPU_AARCH64
#define HOST_MACHINE_TYPE  EFI_IMAGE_MACHINE_AARCH64
#elif defined (MDE_CPU_RISCV64)
#define HOST_MACHINE_TYPE  EFI_IMAGE_MACHINE_RISCV64
#else
  #error
#endif

#define UNUSED  __attribute__((unused))

#define REG_READ(CpuContext, x)  ({                             \
      UINT64 Reg;                                               \
      UNUSED uc_err UcErr;                                      \
      UcErr = uc_reg_read ((CpuContext)->UE, x, &Reg);          \
      ASSERT (UcErr == UC_ERR_OK);                              \
      Reg;                                                      \
    })

#define REG_WRITE(CpuContext, x, Val)  ({                         \
      UNUSED uc_err UcErr;                                        \
      UINT64 Temp = Val;                                          \
      UcErr = uc_reg_write ((CpuContext)->UE, x, &Temp);          \
      ASSERT (UcErr == UC_ERR_OK);                                \
    })

typedef struct uc_struct   uc_engine;
typedef struct uc_context  uc_context;

typedef struct CpuRunContext CpuRunContext;

typedef struct CpuContext {
  UINT16        EmuMachineType;
  const CHAR8   *Name;
  int           StackReg;
  int           ProgramCounterReg;
  int           ReturnValueReg;
  int           Contexts;
  VOID                 (*Dump)(
    struct CpuContext *
    );
  VOID                 (*EmuThunkPre)(
    struct CpuContext *,
    UINT64  *Args
    );
  VOID                 (*EmuThunkPost)(
    struct CpuContext *,
    UINT64  *Args
    );
  UINT64               (*NativeThunk)(
    struct CpuRunContext *,
    UINT64  ProgramCounter
    );
  uc_engine               *UE;
  EFI_PHYSICAL_ADDRESS    UnicornCodeGenBuf;
  EFI_PHYSICAL_ADDRESS    UnicornCodeGenBufEnd;
  EFI_PHYSICAL_ADDRESS    EmuStackStart;
  EFI_PHYSICAL_ADDRESS    EmuStackTop;
  uc_context              *InitialState;
 #ifndef MAU_EMU_TIMEOUT_NONE
  UINT64                  TbCount;
  UINT64                  ExitPeriodTbs;
  UINT64                  ExitPeriodTicks;
  BOOLEAN                 StoppedOnTimeout;
 #endif /* MAU_EMU_TIMEOUT_NONE */
} CpuContext;

typedef struct {
  LIST_ENTRY                  Link;
  EFI_PHYSICAL_ADDRESS        ImageBase;
  EFI_PHYSICAL_ADDRESS        ImageEntry;
  UINT64                      ImageSize;
  EFI_HANDLE                  ImageHandle;

  /*
   * ISA-specific.
   */
  CpuContext                  *Cpu;

  /*
   * To support the Exit() boot service.
   */
  BASE_LIBRARY_JUMP_BUFFER    ImageExitJumpBuffer;
  EFI_STATUS                  ImageExitStatus;
  UINTN                       ImageExitDataSize;
  CHAR16                      *ImageExitData;
} ImageRecord;

typedef struct CpuRunContext {
  CpuContext              *Cpu;
  EFI_VIRTUAL_ADDRESS     ProgramCounter;
 #ifdef MAU_CHECK_ORPHAN_CONTEXTS
  UINT64                  LeakCookie;
 #endif /* MAU_CHECK_ORPHAN_CONTEXTS */
  UINT64                  *Args;
  UINT64                  Ret;
  BOOLEAN                 SavedInterruptState;

  uc_context              *PrevUcContext;
  struct CpuRunContext    *PrevContext;

  /*
   * Only set when we're invoking the entry point of an image.
   */
  ImageRecord             *ImageRecord;
} CpuRunContext;

#ifdef MAU_SUPPORTS_X64_BINS
extern CpuContext  CpuX64;
#endif /* MAU_SUPPORTS_X64_BINS */
#ifdef MAU_SUPPORTS_AARCH64_BINS
extern  CpuContext  CpuAArch64;
#endif /* MAU_SUPPORTS_AARCH64_BINS */
extern EFI_CPU_ARCH_PROTOCOL         *gCpu;
extern EFI_CPU_IO2_PROTOCOL          *gCpuIo2;
extern EFI_LOADED_IMAGE_PROTOCOL     *gDriverImage;
extern EFI_DRIVER_BINDING_PROTOCOL   gDriverBinding;
extern EFI_COMPONENT_NAME_PROTOCOL   gComponentName;
extern EFI_COMPONENT_NAME2_PROTOCOL  gComponentName2;

ImageRecord *
ImageFindByAddress (
  IN  EFI_PHYSICAL_ADDRESS  Address
  );

ImageRecord *
ImageFindByHandle (
  IN  EFI_HANDLE  Handle
  );

VOID
ImageDump (
  VOID
  );

BOOLEAN
EFIAPI
ImageProtocolSupported (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  *This,
  IN  UINT16                                ImageType,
  IN  EFI_DEVICE_PATH_PROTOCOL              *DevicePath OPTIONAL
  );

EFI_STATUS
EFIAPI
ImageProtocolUnregister (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  *This,
  IN  EFI_PHYSICAL_ADDRESS                  ImageBase
  );

EFI_STATUS
EFIAPI
ImageProtocolRegister (
  IN      EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  *This,
  IN      EFI_PHYSICAL_ADDRESS                  ImageBase,
  IN      UINT64                                ImageSize,
  IN  OUT EFI_IMAGE_ENTRY_POINT                 *EntryPoint
  );

UINT64
CpuStackPop64 (
  IN  CpuContext  *Cpu
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

EFI_STATUS
EmulatorStart (
  IN  EFI_HANDLE  ControllerHandle
  );

EFI_STATUS
EmulatorStop (
  IN  EFI_HANDLE  ControllerHandle
  );

VOID
EmulatorDump (
  VOID
  );

BOOLEAN
EmulatorIsNativeCall (
  IN  UINT64  ProgramCounter
  );

EFI_STATUS
CpuRunImage (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  );

VOID
CpuCompressLeakedContexts (
  IN  CpuRunContext  *CurrentContext,
  IN  BOOLEAN        OnImageExit
  );

EFI_STATUS
CpuExitImage (
  IN  UINT64  OriginalProgramCounter,
  IN  UINT64  ReturnAddress,
  IN  UINT64  *Args
  );

CpuRunContext *
CpuGetTopContext (
  VOID
  );

UINT64
CpuRunFunc (
  IN  CpuContext           *Cpu,
  IN  EFI_VIRTUAL_ADDRESS  ProgramCounter,
  IN  UINT64               *Args
  );

#ifndef NDEBUG
EFI_STATUS
EFIAPI
CpuGetDebugState (
  OUT EMU_TEST_DEBUG_STATE  *DebugState
  );

#endif

VOID
CpuUnregisterCodeRange (
  IN  CpuContext            *Cpu,
  IN  EFI_PHYSICAL_ADDRESS  ImageBase,
  IN  UINT64                ImageSize
  );

VOID
CpuRegisterCodeRange (
  IN  CpuContext            *Cpu,
  IN  EFI_PHYSICAL_ADDRESS  ImageBase,
  IN  UINT64                ImageSize
  );

BOOLEAN
CpuAddrIsCodeGen (
  IN  EFI_PHYSICAL_ADDRESS  Address
  );

#ifdef MAU_SUPPORTS_X64_BINS
UINT64
NativeThunkX64 (
  IN  CpuRunContext  *Context,
  IN  UINT64         ProgramCounter
  );

#endif /* MAU_SUPPORTS_X64_BINS */

#ifdef MAU_SUPPORTS_AARCH64_BINS
UINT64
NativeThunkAArch64 (
  IN  CpuRunContext  *Context,
  IN  UINT64         ProgramCounter
  );

#endif /* MAU_SUPPORTS_AARCH64_BINS */

EFI_STATUS
EFIAPI
NativeUnsupported (
  IN  UINT64  OriginalProgramCounter,
  IN  UINT64  ReturnAddress,
  IN  UINT64  *Args
  );

EFI_STATUS
TestProtocolInit (
  IN  EFI_HANDLE  ImageHandle
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
  IN  UINT64  ProgramCounter
  );

VOID
EfiWrappersDump (
  VOID
  );
