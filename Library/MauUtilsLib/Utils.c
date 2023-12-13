/** @file

    Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>
    Copyright (C) 2017 Andrei Evgenievich Warkentin

    SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MauUtilsLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/EfiShellInterface.h>
#include <Protocol/ShellParameters.h>

EFI_STATUS
GetOpt (
  IN UINTN                Argc,
  IN CHAR16               **Argv,
  IN CHAR16               *OptionsWithArgs,
  IN OUT GET_OPT_CONTEXT  *Context
  )
{
  UINTN  Index;
  UINTN  SkipCount;

  if ((Context->OptIndex >= Argc) ||
      (*Argv[Context->OptIndex] != L'-'))
  {
    return EFI_END_OF_MEDIA;
  }

  if (*(Argv[Context->OptIndex] + 1) == L'\0') {
    /*
     * A lone dash is used to signify end of options list.
     *
     * Like above, but we want to skip the dash.
     */
    Context->OptIndex++;
    return EFI_END_OF_MEDIA;
  }

  SkipCount    = 1;
  Context->Opt = *(Argv[Context->OptIndex] + 1);

  if (OptionsWithArgs != NULL) {
    UINTN  ArgsLen = StrLen (OptionsWithArgs);

    for (Index = 0; Index < ArgsLen; Index++) {
      if (OptionsWithArgs[Index] == Context->Opt) {
        if (*(Argv[Context->OptIndex] + 2) != L'\0') {
          /*
           * Argument to the option may immediately follow
           * the option (not separated by space).
           */
          Context->OptArg = Argv[Context->OptIndex] + 2;
        } else if ((Context->OptIndex + 1 < Argc) &&
                   (*(Argv[Context->OptIndex + 1]) != L'-'))
        {
          /*
           * If argument is separated from option by space, it
           * cannot look like an option (i.e. begin with a dash).
           */
          Context->OptArg = Argv[Context->OptIndex + 1];
          SkipCount++;
        } else {
          /*
           * No argument. Maybe it was optional? Up to the caller
           * to decide.
           */
          Context->OptArg = NULL;
        }

        break;
      }
    }
  }

  Context->OptIndex += SkipCount;
  return EFI_SUCCESS;
}

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
