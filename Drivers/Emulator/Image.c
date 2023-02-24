/** @file

    Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
    Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "Emulator.h"

STATIC LIST_ENTRY  mImageList = INITIALIZE_LIST_HEAD_VARIABLE (mImageList);

VOID
ImageDump (
  VOID
  )
{
  LIST_ENTRY   *Entry;
  ImageRecord  *Record;

  DEBUG ((DEBUG_ERROR, "Emulated images:\n"));
  for (Entry = GetFirstNode (&mImageList);
       !IsNull (&mImageList, Entry);
       Entry = GetNextNode (&mImageList, Entry))
  {
    Record = BASE_CR (Entry, ImageRecord, Link);

    DEBUG ((
      DEBUG_ERROR,
      "\t%7a Image 0x%lx-0x%lx (Entry 0x%lx)\n",
      Record->Cpu->Name,
      Record->ImageBase,
      Record->ImageBase + Record->ImageSize - 1,
      Record->ImageEntry
      ));
  }
}

ImageRecord *
ImageFindByAddress (
  IN  EFI_PHYSICAL_ADDRESS  Address
  )
{
  LIST_ENTRY   *Entry;
  ImageRecord  *Record;

  for (Entry = GetFirstNode (&mImageList);
       !IsNull (&mImageList, Entry);
       Entry = GetNextNode (&mImageList, Entry))
  {
    Record = BASE_CR (Entry, ImageRecord, Link);

    if ((Address >= Record->ImageBase) &&
        (Address < Record->ImageBase + Record->ImageSize))
    {
      return Record;
    }
  }

  return NULL;
}

ImageRecord *
ImageFindByHandle (
  IN  EFI_HANDLE  Handle
  )
{
  LIST_ENTRY   *Entry;
  ImageRecord  *Record;

  for (Entry = GetFirstNode (&mImageList);
       !IsNull (&mImageList, Entry);
       Entry = GetNextNode (&mImageList, Entry))
  {
    Record = BASE_CR (Entry, ImageRecord, Link);

    if (Handle == Record->ImageHandle) {
      return Record;
    }
  }

  return NULL;
}

EFI_STATUS
EFIAPI
ImageProtocolRegister (
  IN      EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  *This,
  IN      EFI_PHYSICAL_ADDRESS                  ImageBase,
  IN      UINT64                                ImageSize,
  IN  OUT EFI_IMAGE_ENTRY_POINT                 *EntryPoint
  )
{
  EFI_STATUS                    Status;
  ImageRecord                   *Record;
  PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;

  ZeroMem (&ImageContext, sizeof (ImageContext));

  ImageContext.Handle    = (VOID *)(UINTN)ImageBase;
  ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

  Status = PeCoffLoaderGetImageInfo (&ImageContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PeCoffLoaderGetImageInfo failed: %r\n", Status));
    return Status;
  }

  ASSERT (
    ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION ||
    ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER
    );

  Record = AllocatePool (sizeof (*Record));
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (ImageContext.Machine == EFI_IMAGE_MACHINE_X64) {
    Record->Cpu = &CpuX64;
 #ifdef SUPPORTS_AARCH64_BINS
  } else if (ImageContext.Machine == EFI_IMAGE_MACHINE_AARCH64) {
    Record->Cpu = &CpuAArch64;
 #endif /* SUPPORTS_AARCH64_BINS */
  } else {
    Record->Cpu = NULL;
  }

  ASSERT (Record->Cpu != NULL);
  Record->ImageBase  = ImageBase;
  Record->ImageEntry = (UINT64)*EntryPoint;
  Record->ImageSize  = ImageSize;

  CpuRegisterCodeRange (Record->Cpu, ImageBase, ImageSize);

  InsertTailList (&mImageList, &Record->Link);

  /*
   * On AArch64, this code relies on no-execute protection of the "foreign"
   * binary for seamless thunking to emulated code. Any attempt by native code
   * to call into the emulated code will be patched up by the installed
   * exception handler (InterpreterSyncExceptionCallback) to invoke
   * EmulatorVmEntry instead.
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

EFI_STATUS
EFIAPI
ImageProtocolUnregister (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  *This,
  IN  EFI_PHYSICAL_ADDRESS                  ImageBase
  )
{
  ImageRecord  *Record;
  EFI_STATUS   Status;

  Record = ImageFindByAddress (ImageBase);
  if (Record == NULL) {
    return EFI_NOT_FOUND;
  }

  CpuUnregisterCodeRange (Record->Cpu, Record->ImageBase, Record->ImageSize);

  /*
   * Remove non-exec protection installed by RegisterImage.
   */
  Status = gCpu->SetMemoryAttributes (
                   gCpu,
                   Record->ImageBase,
                   Record->ImageSize,
                   0
                   );

  RemoveEntryList (&Record->Link);
  FreePool (Record);

  return Status;
}

BOOLEAN
EFIAPI
ImageProtocolSupported (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL  *This,
  IN  UINT16                                ImageType,
  IN  EFI_DEVICE_PATH_PROTOCOL              *DevicePath OPTIONAL
  )
{
  if ((ImageType != EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION) &&
      (ImageType != EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER))
  {
    return FALSE;
  }

  return TRUE;
}
