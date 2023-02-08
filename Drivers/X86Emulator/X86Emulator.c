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
STATIC LIST_ENTRY         mX86ImageList;

VOID
DumpImageRecords (
  VOID
  )
{
  LIST_ENTRY       *Entry;
  X86_IMAGE_RECORD *Record;

  DEBUG ((DEBUG_ERROR, "x64 images:\n"));
  for (Entry = GetFirstNode (&mX86ImageList);
       !IsNull (&mX86ImageList, Entry);
       Entry = GetNextNode (&mX86ImageList, Entry)) {

    Record = BASE_CR (Entry, X86_IMAGE_RECORD, Link);

    DEBUG ((DEBUG_ERROR, "\tImage 0x%lx-0x%lx (Entry 0x%lx)\n",
            Record->ImageBase, Record->ImageBase +
            Record->ImageSize - 1, Record->ImageEntry));
  }
}

X86_IMAGE_RECORD *
FindImageRecordByAddress (
  IN  EFI_PHYSICAL_ADDRESS Address
  )
{
  LIST_ENTRY       *Entry;
  X86_IMAGE_RECORD *Record;

  for (Entry = GetFirstNode (&mX86ImageList);
       !IsNull (&mX86ImageList, Entry);
       Entry = GetNextNode (&mX86ImageList, Entry)) {

    Record = BASE_CR (Entry, X86_IMAGE_RECORD, Link);

    if (Address >= Record->ImageBase &&
        Address < Record->ImageBase + Record->ImageSize) {
      return Record;
    }
  }
  return NULL;
}

X86_IMAGE_RECORD *
FindImageRecordByHandle (
  IN  EFI_HANDLE Handle
  )
{
  LIST_ENTRY       *Entry;
  X86_IMAGE_RECORD *Record;

  for (Entry = GetFirstNode (&mX86ImageList);
       !IsNull (&mX86ImageList, Entry);
       Entry = GetNextNode (&mX86ImageList, Entry)) {

    Record = BASE_CR (Entry, X86_IMAGE_RECORD, Link);

    if (Handle == Record->ImageHandle) {
      return Record;
    }
  }
  return NULL;
}

BOOLEAN
IsNativeCall (
  IN  UINT64 Pc
  )
{
  if ((Pc & (NATIVE_INSN_ALIGNMENT - 1)) != 0) {
    return FALSE;
  }

  if (Pc < EFI_PAGE_SIZE) {
    return TRUE;
  }

  if (FindImageRecordByAddress ((EFI_PHYSICAL_ADDRESS) Pc) != NULL) {
    return FALSE;
  }

  return TRUE;
}

STATIC
BOOLEAN
EFIAPI
IsX86ImageSupported (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL *This,
  IN  UINT16                               ImageType,
  IN  EFI_DEVICE_PATH_PROTOCOL             *DevicePath OPTIONAL
  )
{
  if (ImageType != EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION &&
      ImageType != EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER) {
    return FALSE;
  }

  return TRUE;
}

STATIC
EFI_STATUS
EFIAPI
RegisterX86Image (
  IN      EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL *This,
  IN      EFI_PHYSICAL_ADDRESS                 ImageBase,
  IN      UINT64                               ImageSize,
  IN  OUT EFI_IMAGE_ENTRY_POINT                *EntryPoint
  )
{
  EFI_STATUS                   Status;
  X86_IMAGE_RECORD             *Record;
  PE_COFF_LOADER_IMAGE_CONTEXT ImageContext;

  ZeroMem (&ImageContext, sizeof (ImageContext));

  ImageContext.Handle    = (VOID *)(UINTN)ImageBase;
  ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

  Status = PeCoffLoaderGetImageInfo (&ImageContext);
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_ERROR, "PeCoffLoaderGetImageInfo failed: %r\n", Status));
    return Status;
  }

  ASSERT (ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION ||
          ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER);

  Record = AllocatePool (sizeof (*Record));
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (ImageContext.Machine == EFI_IMAGE_MACHINE_X64) {
    Record->Cpu = &CpuX86;
  } else {
    Record->Cpu = NULL;
  }

  ASSERT (Record->Cpu != NULL);
  Record->ImageBase = ImageBase;
  Record->ImageEntry = (UINT64) *EntryPoint;
  Record->ImageSize = ImageSize;

  CpuRegisterCodeRange (Record->Cpu, ImageBase, ImageSize);

  InsertTailList (&mX86ImageList, &Record->Link);

  /*
   * On AArch64, X86Emulator relies on no-execute protection of the "foreign"
   * binary for seamless thunking to emulated code. Any attempt by native code
   * to call into the emulated code will be patched up by the installed
   * exception handler (X86InterpreterSyncExceptionCallback) to invoke
   * X86EmulatorVmEntry instead.
   *
   * Exception-driven detection of emulated code execution is the key
   * feature enabling emulated drivers to work, as their protocols can thus
   * be used from native code.
   *
   * RISC-V systems should operate similarly, but as of 12/2022 SetMemoryAttributes
   * is a no-op due to the early stage of RISC-V ports (no MMU enabled yet in
   * UEFI). Fortunately, a mix of using an illegal instruction exception trap with
   * trapping EFI calls that take callbacks works reasonably well.
   */
  Status = gCpu->SetMemoryAttributes (gCpu, ImageBase, ImageSize, EFI_MEMORY_XP);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /*
   * Entry point is not entered via exception handler - some special handling is
   * necesssary to support proper emulation of the Exit UEFI Boot Service.
   */
  *EntryPoint = CpuRunImage;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UnregisterX86Image (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL *This,
  IN  EFI_PHYSICAL_ADDRESS                 ImageBase
  )
{
  X86_IMAGE_RECORD *Record;
  EFI_STATUS       Status;

  Record = FindImageRecordByAddress (ImageBase);
  if (Record == NULL) {
    return EFI_NOT_FOUND;
  }

  CpuUnregisterCodeRange (Record->Cpu, Record->ImageBase, Record->ImageSize);

  /*
   * Remove non-exec protection installed by RegisterX86Image.
   */
  Status = gCpu->SetMemoryAttributes (gCpu, Record->ImageBase,
                                      Record->ImageSize, 0);

  RemoveEntryList (&Record->Link);
  FreePool (Record);

  return Status;
}

STATIC EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL mX86EmulatorProtocol = {
  IsX86ImageSupported,
  RegisterX86Image,
  UnregisterX86Image,
  EDKII_PECOFF_IMAGE_EMULATOR_VERSION,
  EFI_IMAGE_MACHINE_X64
};

UINT64
X86EmulatorVmEntry (
  IN  UINT64           Pc,
  IN  UINT64           *Args,
  IN  X86_IMAGE_RECORD *Record,
  IN  UINT64           Lr
  )
{
  return CpuRunFunc (Record->Cpu, Pc, (UINT64 *) Args);
}

VOID
X86EmulatorDump (
  VOID
  )
{
  DumpImageRecords ();
  EfiWrappersDump ();
  CpuDump ();
}

EFI_STATUS
EFIAPI
X86EmulatorDxeEntryPoint (
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

  InitializeListHead (&mX86ImageList);

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
                                          &mX86EmulatorProtocol);
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
                                     &mX86EmulatorProtocol);
    ArchCleanup ();
    CpuCleanup ();
    return Status;
  }
#endif

  return Status;
}
