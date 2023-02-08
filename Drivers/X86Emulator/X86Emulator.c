/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "X86Emulator.h"

EFI_CPU_ARCH_PROTOCOL     *gCpu;
EFI_CPU_IO2_PROTOCOL      *gCpuIo2;
EFI_LOADED_IMAGE_PROTOCOL *gDriverImage;

BOOLEAN
EmulatorIsNativeCall (
  IN  UINT64 ProgramCounter
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

STATIC EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL mEmulatorProtocol = {
  ImageProtocolSupported,
  ImageProtocolRegister,
  ImageProtocolUnregister,
  EDKII_PECOFF_IMAGE_EMULATOR_VERSION,
  EFI_IMAGE_MACHINE_X64
};

UINT64
EmulatorVmEntry (
  IN  UINT64      ProgramCounter,
  IN  UINT64      *Args,
  IN  ImageRecord *Record,
  IN  UINT64      Lr
  )
{
  return CpuRunFunc (Record->Cpu, ProgramCounter, (UINT64 *) Args);
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
EFIAPI
EmulatorDxeEntryPoint (
  IN  EFI_HANDLE       ImageHandle,
  IN  EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;

  Status = gBS->HandleProtocol (ImageHandle,
                                &gEfiLoadedImageProtocolGuid,
                                (VOID **)&gDriverImage);
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_ERROR, "Can't get driver LoadedImage: %r\n", Status));
    return Status;
  }

  EfiWrappersInit ();

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&gCpu);
  if (Status != EFI_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "EFI_CPU_ARCH_PROTOCOL is missing\n"));
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiCpuIo2ProtocolGuid, NULL,
                                (VOID **)&gCpuIo2);
  if (Status != EFI_SUCCESS) {
    DEBUG ((DEBUG_WARN, "EFI_CPU_IO2_PROTOCOL is missing\n"));
  }

  Status = CpuInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ArchInit ();
  if (EFI_ERROR (Status)) {
    CpuCleanup ();
    return Status;
  }


  Status = gBS->InstallProtocolInterface (&ImageHandle,
                                          &gEdkiiPeCoffImageEmulatorProtocolGuid,
                                          EFI_NATIVE_INTERFACE,
                                          &mEmulatorProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "InstallProtocolInterface failed: %r\n", Status));
    ArchCleanup ();
    CpuCleanup ();
    return Status;
  }

#ifndef NDEBUG
  Status = TestProtocolInit (ImageHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "InstallProtocolInterface failed: %r\n", Status));
    gBS->UninstallProtocolInterface (ImageHandle,
                                     &gEdkiiPeCoffImageEmulatorProtocolGuid,
                                     &mEmulatorProtocol);
    ArchCleanup ();
    CpuCleanup ();
    return Status;
  }
#endif

  return Status;
}
