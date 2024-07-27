/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "Emulator.h"

BOOLEAN
EmulatorIsNativeCall (
  IN  UINT64  ProgramCounter
  )
{
  if ((ProgramCounter & (NATIVE_INSN_ALIGNMENT - 1)) != 0) {
    return FALSE;
  }

  if (ProgramCounter < EFI_PAGE_SIZE) {
    return TRUE;
  }

  if (ImageFindByAddress (ProgramCounter) != NULL) {
    return FALSE;
  }

  return TRUE;
}

#ifdef MAU_SUPPORTS_X64_BINS
STATIC EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  mEmulatorProtocolX64 = {
  ImageProtocolSupported,
  ImageProtocolRegister,
  ImageProtocolUnregister,
  EDKII_PECOFF_IMAGE_EMULATOR_VERSION,
  EFI_IMAGE_MACHINE_X64
};
#endif /* MAU_SUPPORTS_X64_BINS */

#ifdef MAU_SUPPORTS_AARCH64_BINS
STATIC EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  mEmulatorProtocolAArch64 = {
  ImageProtocolSupported,
  ImageProtocolRegister,
  ImageProtocolUnregister,
  EDKII_PECOFF_IMAGE_EMULATOR_VERSION,
  EFI_IMAGE_MACHINE_AARCH64
};
#endif /* MAU_SUPPORTS_AARCH64_BINS */

UINT64
EmulatorVmEntry (
  IN  UINT64       ProgramCounter,
  IN  UINT64       *Args,
  IN  ImageRecord  *Record,
  IN  UINT64       Lr
  )
{
  return CpuRunFunc (Record->Cpu, ProgramCounter, (UINT64 *)Args);
}

VOID
EmulatorDump (
  VOID
  )
{
  ImageDump ();
  EfiWrappersDump ();
  CpuDump ();
}

EFI_STATUS
EmulatorStop (
  IN  EFI_HANDLE  ControllerHandle
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EmulatorStart (
  IN  EFI_HANDLE  ControllerHandle
  )
{
  EFI_STATUS  Status;

 #ifdef MAU_SUPPORTS_X64_BINS
  EFI_HANDLE  EmuHandleX64 = NULL;
 #endif /* MAU_SUPPORTS_X64_BINS */

 #ifdef MAU_SUPPORTS_AARCH64_BINS
  EFI_HANDLE  EmuHandleAArch64 = NULL;
 #endif /* MAU_SUPPORTS_AARCH64_BINS */

  Status = EfiHooksInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  EfiWrappersInit ();

  Status = CpuInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ArchInit ();
  if (EFI_ERROR (Status)) {
    CpuCleanup ();
    EfiHooksCleanup ();
    return Status;
  }

 #ifdef MAU_SUPPORTS_X64_BINS
  Status = gBS->InstallProtocolInterface (
                  &EmuHandleX64,
                  &gEdkiiPeCoffImageEmulatorProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mEmulatorProtocolX64
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "InstallProtocolInterface mEmulatorProtocolX64 failed: %r\n", Status));
    goto done;
  }

 #endif /* MAU_SUPPORTS_X64_BINS */

 #ifdef MAU_SUPPORTS_AARCH64_BINS
  Status = gBS->InstallProtocolInterface (
                  &EmuHandleAArch64,
                  &gEdkiiPeCoffImageEmulatorProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mEmulatorProtocolAArch64
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "InstallProtocolInterface mEmulatorProtocolAArch64 failed: %r\n", Status));
    goto done;
  }

 #endif /* MAU_SUPPORTS_AARCH64_BINS */

 #ifndef NDEBUG
  Status = TestProtocolInit (ControllerHandle);
 #endif

 #if defined (MAU_SUPPORTS_AARCH64_BINS) || \
  defined (MAU_SUPPORTS_X64_BINS)
done:
 #endif

  if (EFI_ERROR (Status)) {
 #ifdef MAU_SUPPORTS_X64_BINS
    if (EmuHandleX64 != NULL) {
      gBS->UninstallProtocolInterface (
             EmuHandleX64,
             &gEdkiiPeCoffImageEmulatorProtocolGuid,
             &mEmulatorProtocolX64
             );
    }

 #endif /* MAU_SUPPORTS_X64_BINS */

 #ifdef MAU_SUPPORTS_AARCH64_BINS
    if (EmuHandleAArch64 != NULL) {
      gBS->UninstallProtocolInterface (
             EmuHandleAArch64,
             &gEdkiiPeCoffImageEmulatorProtocolGuid,
             &mEmulatorProtocolAArch64
             );
    }

 #endif /* MAU_SUPPORTS_AARCH64_BINS */
    ArchCleanup ();
    CpuCleanup ();
    EfiHooksCleanup ();
  }

  return Status;
}
