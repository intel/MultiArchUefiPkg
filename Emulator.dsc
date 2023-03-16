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
  SUPPORTED_ARCHITECTURES        = AARCH64|RISCV64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  #
  # Use a dedicated native stack for handling emulation.
  #
  ON_PRIVATE_STACK               = NO
  #
  # Use an emulated entry point, instead of relying on
  # exception-driven thunking of native to emulated code.
  # On by default in RISC-V builds (via INF file).
  #
  WRAPPED_ENTRY_POINTS           = NO
  #
  # Handle unexpected/non-linear control flow by native code,
  # that can result in a resource leak inside the emulator.
  # On by default in DEBUG builds (via INF file).
  #
  CHECK_ORPHAN_CONTEXTS          = NO
  #
  # For maximum performance, don't periodically bail out
  # of emulation. This is only useful for situations where
  # you know the executed code won't do tight loops polling
  # on some memory location updated by an event.
  #
  EMU_TIMEOUT_NONE               = NO
  #
  # If you want to support AArch64 UEFI boot service drivers
  # and applications, say YES. Not available for the AARCH64
  # build.
  #
  SUPPORTS_AARCH64_BINS          = NO

!include unicorn/efi/UnicornPkg.dsc.inc

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
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  #
  # For absolute performance, build with BaseDebugLibNull at the
  # expense of no error messages from anything.
  #
  # DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf

  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  SerialPortLib|MdePkg/Library/BaseSerialPortLibNull/BaseSerialPortLibNull.inf
  #
  # The emulator cannot depend on emulated code.
  #
  # The emulator needs to protect itself from emulated apps/drivers that modify boot/runtime
  # services the emulator uses. A great example of this is the Shell replacing ST->ConOut.
  #
  UefiDriverEntryPoint|MultiArchUefiPkg/Library/CachedSTDriverEntryPoint/UefiDriverEntryPoint.inf

[LibraryClasses.AARCH64]
  ArmDisassemblerLib|ArmPkg/Library/ArmDisassemblerLib/ArmDisassemblerLib.inf
  DefaultExceptionHandlerLib|ArmPkg/Library/DefaultExceptionHandlerLib/DefaultExceptionHandlerLib.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[LibraryClasses.RISCV64]
!if $(SUPPORTS_AARCH64_BINS) == YES
  NULL|unicorn/efi/UnicornArm64Lib.inf
!endif
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  CpuExceptionHandlerLib|UefiCpuPkg/Library/BaseRiscV64CpuExceptionHandlerLib/BaseRiscV64CpuExceptionHandlerLib.inf

[BuildOptions]
  *_*_*_CC_FLAGS                       = -DDISABLE_NEW_DEPRECATED_INTERFACES
!if $(ON_PRIVATE_STACK) == YES
  *_*_*_CC_FLAGS                       = -DMAU_ON_PRIVATE_STACK
!endif
!if $(WRAPPED_ENTRY_POINTS) == YES
  *_*_*_CC_FLAGS                       = -DMAU_WRAPPED_ENTRY_POINTS
!endif
!if $(CHECK_ORPHAN_CONTEXTS) == YES
  *_*_*_CC_FLAGS                       = -DMAU_CHECK_ORPHAN_CONTEXTS
!endif
!if $(EMU_TIMEOUT_NONE) == YES
  *_*_*_CC_FLAGS                       = -DMAU_EMU_TIMEOUT_NONE
!endif
!if $(ARCH) != AARCH64
!if $(SUPPORTS_AARCH64_BINS) == YES
  *_*_*_CC_FLAGS                       = -DMAU_SUPPORTS_AARCH64_BINS
!endif
!endif

[Components]
  MultiArchUefiPkg/Drivers/Emulator/Emulator.inf
