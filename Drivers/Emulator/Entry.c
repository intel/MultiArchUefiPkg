/** @file

    Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "Emulator.h"

EFI_CPU_ARCH_PROTOCOL      *gCpu;
EFI_CPU_IO2_PROTOCOL       *gCpuIo2;
EFI_LOADED_IMAGE_PROTOCOL  *gDriverImage;

typedef struct {
  VENDOR_DEVICE_PATH          Custom;
  EFI_DEVICE_PATH_PROTOCOL    EndDevicePath;
} EFI_EMU_DEVICE_PATH;

STATIC EFI_EMU_DEVICE_PATH  mDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8),
      }
    },
    EFI_CALLER_ID_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      sizeof (EFI_DEVICE_PATH_PROTOCOL),
      0
    }
  }
};

STATIC EFI_HANDLE  mDevice;

EFI_STATUS
EFIAPI
DriverEntry (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&gDriverImage
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Can't get driver LoadedImage: %r\n", Status));
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&gCpu);
  if (Status != EFI_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "EFI_CPU_ARCH_PROTOCOL is missing\n"));
    return Status;
  }

 #ifndef MAU_EMU_X64_RAZ_WI_PIO
  Status = gBS->LocateProtocol (
                  &gEfiCpuIo2ProtocolGuid,
                  NULL,
                  (VOID **)&gCpuIo2
                  );
  if (Status != EFI_SUCCESS) {
    DEBUG ((DEBUG_WARN, "EFI_CPU_IO2_PROTOCOL is missing\n"));
  }

 #endif /* MAU_EMU_X64_RAZ_WI_PIO */

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mDevice,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiCallerIdGuid,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "InstallMultipleProtocolInterfaces: %r\n", Status));
    return Status;
  }

  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gDriverBinding,
             ImageHandle,
             &gComponentName,
             &gComponentName2
             );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "EfiLibInstallDriverBindingComponentName2: %r\n", Status));
    gBS->UninstallMultipleProtocolInterfaces (
           mDevice,
           &gEfiDevicePathProtocolGuid,
           &mDevicePath,
           &gEfiCallerIdGuid,
           NULL,
           NULL
           );
  }

  return Status;
}
