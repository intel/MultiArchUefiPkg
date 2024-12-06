## @file
#
#  Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
#  Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>
#
#  This library is free software; you can redistribute it and/or
#  modify it under the terms of the GNU Lesser General Public
#  License as published by the Free Software Foundation; either
#  version 2 of the License, or (at your option) any later version.
#
##

[Defines]
  PLATFORM_NAME                  = Emulator
  PLATFORM_GUID                  = 62ad1d2c-27bf-4021-b32d-268d0e71c032
  PLATFORM_VERSION               = 0.98
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/MultiArchUefiPkg
  SUPPORTED_ARCHITECTURES        = AARCH64|RISCV64|LOONGARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

  #
  # How do you want to log?
  # - SERIAL: use EFI_SERIAL_IO_PROTOCOL. This is the most versatile
  #           choice, which works equally well for direct-included
  #           firmware builds and for side-loading the driver, but
  #           it assumes the presence of a single UART.
  # - CONOUT: use Console Output device from EFI_SYSTEM_TABLE. Only
  #           useful for side-loading, not for direct-included
  #           firmware builds.
  # - SBI:    use SBI console services. Only for RISC-V.
  # - NONE:   use nothing. Logging is for wimps - optimize for size
  #           and performance!
  #
  MAU_STANDALONE_LOGGING         = SERIAL
  #
  # Use a dedicated native stack for handling emulation.
  #
  MAU_ON_PRIVATE_STACK           = NO
  #
  # Attempt some operation on UEFI implementations without
  # an enabled MMU, by relying on the illegal instruction
  # handler. It won't work well and is only supported on RISC-V.
  # Implies MAU_WRAPPED_ENTRY_POINTS=YES.
  #
  # On by default in RISC-V builds (via INF file).
  #
  MAU_TRY_WITHOUT_MMU            = NO
  #
  # Use an emulated entry point, instead of relying on
  # exception-driven thunking of native to emulated code.
  #
  # On by default in RISC-V builds (via INF file).
  #
  MAU_WRAPPED_ENTRY_POINTS       = NO
  #
  # Handle unexpected/non-linear control flow by native code,
  # that can result in a resource leak inside the emulator.
  # On by default in DEBUG builds (via INF file).
  #
  MAU_CHECK_ORPHAN_CONTEXTS      = NO
  #
  # For maximum performance, don't periodically bail out
  # of emulation. This is only useful for situations where
  # you know the executed code won't do tight loops polling
  # on some memory location updated by an event.
  #
  MAU_EMU_TIMEOUT_NONE           = NO
  #
  # If you want to support x64 UEFI boot service drivers
  # and applications, say YES. Saying NO doesn't make sense
  # for the AARCH64 build.
  #
  MAU_SUPPORTS_X64_BINS          = YES
  #
  # If you want to support AArch64 UEFI boot service drivers
  # and applications, say YES. Not available for the AARCH64
  # build.
  #
  MAU_SUPPORTS_AARCH64_BINS      = NO
  #
  # Say YES if you want to ignore all port I/O writes (reads
  # returning zero), instead of forwarding to EFI_CPU_IO2_PROTOCOL.
  #
  # Useful for testing on UEFI DEBUG builds that use the
  # BaseIoLibIntrinsic (IoLibNoIo.c) implementation.
  #
  MAU_EMU_X64_RAZ_WI_PIO         = NO
  #
  # Seems to work well even when building on small machines.
  #
  UC_LTO_JOBS                    = auto

!include MultiArchUefiPkg.dsc.inc

[PcdsFixedAtBuild.common]
  # DEBUG_ASSERT_ENABLED       0x01
  # DEBUG_PRINT_ENABLED        0x02
  # DEBUG_CODE_ENABLED         0x04
  # CLEAR_MEMORY_ENABLED       0x08
  # ASSERT_BREAKPOINT_ENABLED  0x10
  # ASSERT_DEADLOOP_ENABLED    0x20
!if $(TARGET) == RELEASE
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x23
!else
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2f
!endif

  #  DEBUG_INIT      0x00000001  // Initialization
  #  DEBUG_WARN      0x00000002  // Warnings
  #  DEBUG_LOAD      0x00000004  // Load events
  #  DEBUG_FS        0x00000008  // EFI File system
  #  DEBUG_POOL      0x00000010  // Alloc & Free (pool)
  #  DEBUG_PAGE      0x00000020  // Alloc & Free (page)
  #  DEBUG_INFO      0x00000040  // Informational debug messages
  #  DEBUG_DISPATCH  0x00000080  // PEI/DXE/SMM Dispatchers
  #  DEBUG_VARIABLE  0x00000100  // Variable
  #  DEBUG_BM        0x00000400  // Boot Manager
  #  DEBUG_BLKIO     0x00001000  // BlkIo Driver
  #  DEBUG_NET       0x00004000  // SNP Driver
  #  DEBUG_UNDI      0x00010000  // UNDI Driver
  #  DEBUG_LOADFILE  0x00020000  // LoadFile
  #  DEBUG_EVENT     0x00080000  // Event messages
  #  DEBUG_GCD       0x00100000  // Global Coherency Database changes
  #  DEBUG_CACHE     0x00200000  // Memory range cachability changes
  #  DEBUG_VERBOSE   0x00400000  // Detailed debug messages that may
  #                              // significantly impact boot performance
  #  DEBUG_ERROR     0x80000000  // Error
!if $(TARGET) == RELEASE
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000000
!else
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000004F
!endif

[LibraryClasses.common]
  #
  # Entry point
  #
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  #
  # Basic
  #
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  #
  # UEFI & PI
  #
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  #
  # Since software stack checking may be heuristically enabled by the compiler
  # include BaseStackCheckLib unconditionally.
  #
  NULL|MdePkg/Library/BaseStackCheckLib/BaseStackCheckLib.inf
  #
  # Misc.
  #
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  #
  # Logging choices.
  #
!if $(MAU_STANDALONE_LOGGING) == SERIAL
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  SerialPortLib|MultiArchUefiPkg/Library/BaseSerialPortLibSerialIoProtocol/BaseSerialPortLibSerialIoProtocol.inf
!endif
!if $(MAU_STANDALONE_LOGGING) == CONOUT
  SerialPortLib|MdePkg/Library/BaseSerialPortLibNull/BaseSerialPortLibNull.inf
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
!endif
!if $(MAU_STANDALONE_LOGGING) == SBI
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  SerialPortLib|MdePkg/Library/BaseSerialPortLibRiscVSbiLib/BaseSerialPortLibRiscVSbiLibRam.inf
  RiscVSbiLib|MdePkg/Library/BaseRiscVSbiLib/BaseRiscVSbiLib.inf
!endif
!if $(MAU_STANDALONE_LOGGING) == NONE
  SerialPortLib|MdePkg/Library/BaseSerialPortLibNull/BaseSerialPortLibNull.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!endif

[LibraryClasses.AARCH64]
  ArmDisassemblerLib|ArmPkg/Library/ArmDisassemblerLib/ArmDisassemblerLib.inf
  DefaultExceptionHandlerLib|ArmPkg/Library/DefaultExceptionHandlerLib/DefaultExceptionHandlerLib.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf
  ArmLib|ArmPkg/Library/ArmLib/ArmBaseLib.inf
  #
  # It is not possible to prevent ARM compiler calls to generic intrinsic functions.
  # This library provides the instrinsic functions generated by a given compiler.
  # [LibraryClasses.ARM] and NULL mean link this library into all ARM images.
  #
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf
  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf
  ArmGenericTimerCounterLib|ArmPkg/Library/ArmGenericTimerPhyCounterLib/ArmGenericTimerPhyCounterLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf

[LibraryClasses.RISCV64]
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  CpuExceptionHandlerLib|UefiCpuPkg/Library/BaseRiscV64CpuExceptionHandlerLib/BaseRiscV64CpuExceptionHandlerLib.inf
  TimerLib|UefiCpuPkg/Library/BaseRiscV64CpuTimerLib/BaseRiscV64CpuTimerLib.inf
  CacheMaintenanceLib|MdePkg/Library/BaseCacheMaintenanceLib/BaseCacheMaintenanceLib.inf

[LibraryClasses.LOONGARCH64]
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  CpuLib| MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
  CpuExceptionHandlerLib|UefiCpuPkg/Library/CpuExceptionHandlerLib/DxeCpuExceptionHandlerLib.inf
  SafeIntLib| MdePkg/Library/BaseSafeIntLib/BaseSafeIntLib.inf
  TimerLib|UefiCpuPkg/Library/CpuTimerLib/BaseCpuTimerLib.inf
  CacheMaintenanceLib|MdePkg/Library/BaseCacheMaintenanceLib/BaseCacheMaintenanceLib.inf

[BuildOptions]
!if $(ARCH) == RISCV64
  *_*_*_CC_FLAGS                       = -Os
!endif
