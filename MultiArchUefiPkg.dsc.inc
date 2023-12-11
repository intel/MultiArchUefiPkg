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

!include unicorn/efi/UnicornPkg.dsc.inc

[LibraryClasses.common]
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf

!if $(MAU_SUPPORTS_X64_BINS) == YES
  NULL|unicorn/efi/UnicornX86Lib.inf
!endif

[LibraryClasses.AARCH64]
  ArmDisassemblerLib|ArmPkg/Library/ArmDisassemblerLib/ArmDisassemblerLib.inf
  DefaultExceptionHandlerLib|ArmPkg/Library/DefaultExceptionHandlerLib/DefaultExceptionHandlerLib.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[LibraryClasses.RISCV64]
!if $(MAU_SUPPORTS_AARCH64_BINS) == YES
  NULL|unicorn/efi/UnicornArm64Lib.inf
!endif
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  CpuExceptionHandlerLib|UefiCpuPkg/Library/BaseRiscV64CpuExceptionHandlerLib/BaseRiscV64CpuExceptionHandlerLib.inf

[BuildOptions]
  *_*_*_CC_FLAGS                       = -DDISABLE_NEW_DEPRECATED_INTERFACES
!if $(MAU_ON_PRIVATE_STACK) == YES
  *_*_*_CC_FLAGS                       = -DMAU_ON_PRIVATE_STACK
!endif
!if $(MAU_TRY_WITHOUT_MMU) == YES
  *_*_*_CC_FLAGS                       = -DMAU_TRY_WITHOUT_MMU
  *_*_*_CC_FLAGS                       = -DMAU_WRAPPED_ENTRY_POINTS
!endif
!if $(MAU_WRAPPED_ENTRY_POINTS) == YES
  *_*_*_CC_FLAGS                       = -DMAU_WRAPPED_ENTRY_POINTS
!endif
!if $(MAU_CHECK_ORPHAN_CONTEXTS) == YES
  *_*_*_CC_FLAGS                       = -DMAU_CHECK_ORPHAN_CONTEXTS
!endif
!if $(MAU_EMU_TIMEOUT_NONE) == YES
  *_*_*_CC_FLAGS                       = -DMAU_EMU_TIMEOUT_NONE
!endif
!if $(MAU_SUPPORTS_X64_BINS) == YES
  *_*_*_CC_FLAGS                       = -DMAU_SUPPORTS_X64_BINS
!endif
!if $(ARCH) != AARCH64
!if $(MAU_SUPPORTS_AARCH64_BINS) == YES
  *_*_*_CC_FLAGS                       = -DMAU_SUPPORTS_AARCH64_BINS
!endif
!endif
!if $(MAU_EMU_X64_RAZ_WI_PIO) == YES
  *_*_*_CC_FLAGS                       = -DMAU_EMU_X64_RAZ_WI_PIO
!endif

[Components]
  MultiArchUefiPkg/Drivers/Emulator/Emulator.inf {
    <LibraryClasses>
      #
      # The emulator cannot depend on emulated code.
      #
      # The emulator needs to protect itself from emulated apps/drivers that modify boot/runtime
      # services the emulator uses. A great example of this is the Shell replacing ST->ConOut
      # (when building Emulator.dsc with MAU_STANDALONE_LOGGING = CONOUT).
      #
      UefiDriverEntryPoint|MultiArchUefiPkg/Library/CachedSTDriverEntryPoint/UefiDriverEntryPoint.inf
  }
