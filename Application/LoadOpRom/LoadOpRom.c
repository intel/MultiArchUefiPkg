/** @file

    Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>
    Copyright (C) 2017 Andrei Evgenievich Warkentin

    SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/DevicePathLib.h>
#include <Protocol/PciIo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/EfiShellInterface.h>
#include <Protocol/ShellParameters.h>
#include <Protocol/Decompress.h>
#include <IndustryStandard/Pci.h>

#ifdef MDE_CPU_AARCH64
#define HOST_MACHINE_TYPE  EFI_IMAGE_MACHINE_AARCH64
#elif defined (MDE_CPU_RISCV64)
#define HOST_MACHINE_TYPE  EFI_IMAGE_MACHINE_RISCV64
#elif defined (MDE_CPU_X64)
#define HOST_MACHINE_TYPE  EFI_IMAGE_MACHINE_X64
#else
  #error
#endif

STATIC EFI_HANDLE  mImageHandle;

STATIC
EFI_STATUS
Usage (
  IN CHAR16  *Name
  )
{
  Print (L"Usage: %s seg bus dev func\n", Name);
  return EFI_INVALID_PARAMETER;
}

STATIC
EFI_STATUS
GetShellArgcArgv (
  IN  EFI_HANDLE  ImageHandle,
  OUT UINTN       *Argcp,
  OUT CHAR16      ***Argvp
  )
{
  EFI_STATUS                     Status;
  EFI_SHELL_PARAMETERS_PROTOCOL  *EfiShellParametersProtocol;
  EFI_SHELL_INTERFACE            *EfiShellInterface;

  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID **)&EfiShellParametersProtocol,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (!EFI_ERROR (Status)) {
    //
    // Shell 2.0 interface.
    //
    *Argcp = EfiShellParametersProtocol->Argc;
    *Argvp = EfiShellParametersProtocol->Argv;
    return EFI_SUCCESS;
  }

  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiShellInterfaceGuid,
                  (VOID **)&EfiShellInterface,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (!EFI_ERROR (Status)) {
    //
    // 1.0 interface.
    //
    *Argcp = EfiShellInterface->Argc;
    *Argvp = EfiShellInterface->Argv;
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

STATIC
VOID
LoadImage (
  IN EFI_DEVICE_PATH_PROTOCOL  *PciPath,
  IN VOID                      *RomHeader
  )
{
  VOID                                     *Image;
  UINTN                                    ImageSize;
  EFI_STATUS                               Status;
  EFI_HANDLE                               ImageHandle;
  UINTN                                    InitializationSize;
  EFI_PCI_EXPANSION_ROM_HEADER             *EfiRomHeader;
  MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH  DeviceNode;
  EFI_DEVICE_PATH_PROTOCOL                 *DevicePath;

  EfiRomHeader       = RomHeader;
  Image              = (UINT8 *)EfiRomHeader + EfiRomHeader->EfiImageHeaderOffset;
  InitializationSize = EfiRomHeader->InitializationSize * 512;
  ImageSize          = InitializationSize - EfiRomHeader->EfiImageHeaderOffset;

  DeviceNode.Header.Type    = MEDIA_DEVICE_PATH;
  DeviceNode.Header.SubType = MEDIA_RELATIVE_OFFSET_RANGE_DP;
  SetDevicePathNodeLength (&DeviceNode.Header, sizeof (DeviceNode));
  DeviceNode.StartingOffset = EfiRomHeader->EfiImageHeaderOffset;
  DeviceNode.EndingOffset   = InitializationSize - 1;

  if (EfiRomHeader->CompressionType != 0) {
    UINT32                   DestinationSize;
    UINT32                   ScratchSize;
    EFI_DECOMPRESS_PROTOCOL  *Decompress;
    VOID                     *Destination;
    VOID                     *Scratch;

    Status = gBS->LocateProtocol (&gEfiDecompressProtocolGuid, NULL, (VOID **)&Decompress);
    if (EFI_ERROR (Status)) {
      Print (L"Can't find EFI_DECOMPRESS_PROTOCOL: %r\n", Status);
      return;
    }

    Status = Decompress->GetInfo (
                           Decompress,
                           Image,
                           ImageSize,
                           &DestinationSize,
                           &ScratchSize
                           );
    if (EFI_ERROR (Status)) {
      Print (L"Decompress->GetInfo failed: %r\n", Status);
      return;
    }

    Destination = AllocateZeroPool (DestinationSize);
    if (Destination == NULL) {
      Print (
        L"Failed to allocate 0x%x bytes for decompressed image\n",
        DestinationSize
        );
      return;
    }

    Scratch = AllocateZeroPool (ScratchSize);
    if (Scratch == NULL) {
      Print (
        L"Failed to allocate 0x%x bytes for scratch\n",
        ScratchSize
        );
      FreePool (Scratch);
      return;
    }

    Status = Decompress->Decompress (
                           Decompress,
                           Image,
                           ImageSize,
                           Destination,
                           DestinationSize,
                           Scratch,
                           ScratchSize
                           );
    FreePool (Scratch);

    if (EFI_ERROR (Status)) {
      Print (L"Failed to decompress: %r\n", Status);
      FreePool (Destination);
      return;
    }

    Image     = Destination;
    ImageSize = DestinationSize;
  }

  DevicePath = AppendDevicePathNode (PciPath, (VOID *)&DeviceNode);
  if (DevicePath == NULL) {
    Print (L"AppendDevicePathNode failed\n");
    goto done;
  }

  ImageHandle = NULL;
  Status      = gBS->LoadImage (
                       TRUE,
                       mImageHandle,
                       DevicePath,
                       Image,
                       ImageSize,
                       &ImageHandle
                       );
  if (EFI_ERROR (Status)) {
    Print (L"LoadImage failed: %r\n", Status);
    if (Status == EFI_SECURITY_VIOLATION) {
      gBS->UnloadImage (ImageHandle);
    }

    goto done;
  }

  Status = gBS->StartImage (ImageHandle, NULL, NULL);
  if (EFI_ERROR (Status)) {
    Print (L"StartImage failed: %r\n", Status);
    gBS->UnloadImage (ImageHandle);
  }

done:
  if (EFI_ERROR (Status)) {
    if (EfiRomHeader->CompressionType != 0) {
      FreePool (Image);
    }
  }

  if (DevicePath != NULL) {
    FreePool (DevicePath);
  }
}

STATIC
BOOLEAN
ParseImage (
  IN VOID                *RomImage,
  IN VOID                *RomHeader,
  IN UINTN               Length,
  IN PCI_DATA_STRUCTURE  *Pcir
  )
{
  CHAR16                        *Type;
  UINTN                         RomOffset;
  UINTN                         InitializationSize;
  BOOLEAN                       Supported;
  EFI_PCI_EXPANSION_ROM_HEADER  *EfiRomHeader;

  RomOffset = (UINTN)RomHeader - (UINTN)RomImage;
  if (Pcir->CodeType == PCI_CODE_TYPE_EFI_IMAGE) {
    Type = L"UEFI";
  } else if (Pcir->CodeType == PCI_CODE_TYPE_PCAT_IMAGE) {
    Type = L"BIOS";
  } else if (Pcir->CodeType == 1) {
    Type = L"1275";
  } else if (Pcir->CodeType == 2) {
    Type = L"HPPA";
  } else {
    Type = L"????";
  }

  Supported = FALSE;
  if (Pcir->CodeType != PCI_CODE_TYPE_EFI_IMAGE) {
    Print (L"+0x%08x: UNSUPPORTED %s image (0x%x bytes)\n", RomOffset, Type, Length);
  } else {
    EfiRomHeader       = (VOID *)RomHeader;
    InitializationSize = EfiRomHeader->InitializationSize * 512;

    Print (
      L"+0x%08x: %sSUPPORTED 0x%x %s image (0x%x bytes)\n",
      RomOffset,
      EfiRomHeader->EfiMachineType == HOST_MACHINE_TYPE ?
      L"" : L"UN",
      EfiRomHeader->EfiMachineType,
      Type,
      Length
      );

    if (EfiRomHeader->EfiMachineType == HOST_MACHINE_TYPE) {
      Print (
        L"+0x%08x:\tSubsystem: 0x%x\n",
        RomOffset,
        EfiRomHeader->EfiSubsystem
        );
      Print (
        L"+0x%08x:\tInitializationSize: 0x%x (bytes)\n",
        RomOffset,
        InitializationSize
        );
      Print (
        L"+0x%08x:\tEfiImageHeaderOffset: 0x%x\n",
        RomOffset,
        EfiRomHeader->EfiImageHeaderOffset
        );
      Print (
        L"+0x%08x:\tCompressed: %s\n",
        RomOffset,
        EfiRomHeader->CompressionType == 0 ? L"no" : L"yes"
        );

      if ((Length < InitializationSize) ||
          (EfiRomHeader->EfiImageHeaderOffset > InitializationSize))
      {
        Print (L"+0x%08x: Image is CORRUPT and UNSUPPORTED\n", RomOffset);
      } else {
        Supported = TRUE;
      }
    }
  }

  if (!Supported) {
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
ParseImages (
  IN EFI_DEVICE_PATH_PROTOCOL  *PciPath,
  IN VOID                      *RomImage,
  IN UINT64                    RomSize
  )
{
  PCI_EXPANSION_ROM_HEADER  *RomHeader;
  PCI_DATA_STRUCTURE        *RomPcir;
  UINT8                     Indicator;

  Indicator = 0;
  RomHeader = RomImage;
  if (RomHeader == NULL) {
    return;
  }

  do {
    UINTN  ImageLength;

    if (RomHeader->Signature != PCI_EXPANSION_ROM_HEADER_SIGNATURE) {
      RomHeader = (PCI_EXPANSION_ROM_HEADER *)((UINT8 *)RomHeader + 512);
      continue;
    }

    //
    // The PCI Data Structure must be DWORD aligned.
    //
    if ((RomHeader->PcirOffset == 0) ||
        ((RomHeader->PcirOffset & 3) != 0) ||
        ((UINT8 *)RomHeader + RomHeader->PcirOffset + sizeof (PCI_DATA_STRUCTURE) > (UINT8 *)RomImage + RomSize))
    {
      break;
    }

    RomPcir = (PCI_DATA_STRUCTURE *)((UINT8 *)RomHeader + RomHeader->PcirOffset);
    if (RomPcir->Signature != PCI_DATA_STRUCTURE_SIGNATURE) {
      break;
    }

    ImageLength = RomPcir->ImageLength;
    if (RomPcir->CodeType == PCI_CODE_TYPE_PCAT_IMAGE) {
      EFI_LEGACY_EXPANSION_ROM_HEADER  *Legacy = (void *)RomHeader;
      //
      // Some legacy cards do not report the correct ImageLength so use
      // the maximum of the legacy length and the PCIR Image Length.
      //
      ImageLength = MAX (ImageLength, Legacy->Size512);
    }

    if (ParseImage (RomImage, RomHeader, ImageLength * 512, RomPcir)) {
      LoadImage (PciPath, RomHeader);
      return;
    }

    Indicator = RomPcir->Indicator;
    RomHeader = (PCI_EXPANSION_ROM_HEADER *)
                ((UINT8 *)RomHeader + ImageLength * 512);
  } while (((UINT8 *)RomHeader < (UINT8 *)RomImage + RomSize) &&
           ((Indicator & 0x80) == 0x00));
}

STATIC
EFI_STATUS
AnalyzeROM (
  IN EFI_PCI_IO_PROTOCOL       *PciIo,
  IN EFI_DEVICE_PATH_PROTOCOL  *PciPath
  )
{
  if (PciIo->RomSize == 0) {
    Print (L"No ROM\n");
    return EFI_SUCCESS;
  }

  Print (L"ROM 0x%08x bytes\n", PciIo->RomSize);
  Print (L"--------------------\n");
  ParseImages (PciPath, PciIo->RomImage, PciIo->RomSize);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EntryPoint (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN                     Argc;
  CHAR16                    **Argv;
  UINTN                     WantSeg;
  UINTN                     WantBus;
  UINTN                     WantDev;
  UINTN                     WantFunc;
  UINTN                     PciIndex;
  UINTN                     PciCount;
  EFI_STATUS                Status;
  EFI_HANDLE                *PciHandles;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  EFI_DEVICE_PATH_PROTOCOL  *PciPath;

  mImageHandle = ImageHandle;
  Status       = GetShellArgcArgv (ImageHandle, &Argc, &Argv);
  if (Status != EFI_SUCCESS) {
    Print (
      L"This program requires Microsoft Windows.\n"
      "Just kidding...only the UEFI Shell!\n"
      );
    return EFI_ABORTED;
  }

  if (Argc < 5) {
    return Usage (Argv[0]);
  }

  WantSeg  = StrHexToUintn (Argv[1]);
  WantBus  = StrHexToUintn (Argv[2]);
  WantDev  = StrHexToUintn (Argv[3]);
  WantFunc = StrHexToUintn (Argv[4]);

  PciCount   = 0;
  PciHandles = NULL;
  Status     = gBS->LocateHandleBuffer (
                      ByProtocol,
                      &gEfiPciIoProtocolGuid,
                      NULL,
                      &PciCount,
                      &PciHandles
                      );
  if (Status != EFI_SUCCESS) {
    Print (L"No PCI devices found\n");
    return EFI_SUCCESS;
  }

  for (PciIndex = 0; PciIndex < PciCount; PciIndex++) {
    UINTN  Seg;
    UINTN  Bus;
    UINTN  Dev;
    UINTN  Func;

    Status = gBS->HandleProtocol (
                    PciHandles[PciIndex],
                    &gEfiPciIoProtocolGuid,
                    (VOID *)&PciIo
                    );

    if (Status != EFI_SUCCESS) {
      Print (L"Coudn't get EFI_PCI_IO_PROTOCOL: %r\n", Status);
      continue;
    }

    Status = gBS->HandleProtocol (
                    PciHandles[PciIndex],
                    &gEfiDevicePathProtocolGuid,
                    (VOID *)&PciPath
                    );
    if (Status != EFI_SUCCESS) {
      Print (L"Coudn't get EFI_DEVICE_PATH_PROTOCOL: %r\n", Status);
      continue;
    }

    Status = PciIo->GetLocation (PciIo, &Seg, &Bus, &Dev, &Func);
    if (Status != EFI_SUCCESS) {
      Print (L"GetLocation failed: %r\n", Status);
      continue;
    }

    if ((WantSeg != Seg) ||
        (WantBus != Bus) ||
        (WantDev != Dev) ||
        (WantFunc != Func))
    {
      continue;
    }

    break;
  }

  if (PciIndex == PciCount) {
    Print (
      L"SBDF 0x%02x%02x%02x%02x not found\n",
      WantSeg,
      WantBus,
      WantDev,
      WantFunc
      );
    Status = EFI_NOT_FOUND;
  } else {
    Status = AnalyzeROM (PciIo, PciPath);
  }

  gBS->FreePool (PciHandles);
  return Status;
}
