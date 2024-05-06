/** @file

    Copyright (c) 2024, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "Emulator.h"

/*
 * EfiHooks modify the UEFI environment presented to all UEFI code,
 * including ourselves. This is done by modifying the function
 * indirection tables (e.g. protocols).
 *
 * This is different from EfiWrappers, which only modify the
 * emulated UEFI environment, by redirecting or denying attempts to
 * execute certain native functions.
 */

UINTN                               gIgnoreInterruptManipulation;
BOOLEAN                             gApparentInterruptState;
STATIC EFI_CPU_ENABLE_INTERRUPT     mRealEnableInterrupt;
STATIC EFI_CPU_DISABLE_INTERRUPT    mRealDisableInterrupt;
STATIC EFI_CPU_GET_INTERRUPT_STATE  mRealGetInterruptState;

EFI_STATUS
EFIAPI
EfiHooksCpuEnableInterrupt (
  IN EFI_CPU_ARCH_PROTOCOL  *This
  )
{
  gApparentInterruptState = TRUE;

  if (gIgnoreInterruptManipulation == 0) {
    return mRealEnableInterrupt (This);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EfiHooksCpuDisableInterrupt (
  IN EFI_CPU_ARCH_PROTOCOL  *This
  )
{
  gApparentInterruptState = FALSE;

  if (gIgnoreInterruptManipulation == 0) {
    return mRealDisableInterrupt (This);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EfiHooksCpuGetInterruptState (
  IN  EFI_CPU_ARCH_PROTOCOL  *This,
  OUT BOOLEAN                *State
  )
{
  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (gIgnoreInterruptManipulation == 0) {
    return mRealGetInterruptState (This, State);
  }

  *State = gApparentInterruptState;
  return EFI_SUCCESS;
}

EFI_STATUS
EfiHooksInit (
  VOID
  )
{
  EFI_STATUS  Status;

  /*
   * The JIT engine is not reentrant, so it has to run
   * uninterrupted. This is done by disabling the interrupts.
   * Of course, we can't have the emulated code enable interrupts,
   * but it gets dicier than that, since the interrupt state is
   * also manipulated implicitly by manipulating the TPL (both
   * by emulated and native code). Thus we disconnect the notion
   * of real interrupt state (now owned by us) from the apparent
   * interrupt state (as seen by everything else).
   */
  Status = gCpu->GetInterruptState (gCpu, &gApparentInterruptState);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: GetInterruptState: %r\n",
      __FUNCTION__,
      Status
      ));
    return Status;
  }

  CriticalBegin ();
  mRealEnableInterrupt   = gCpu->EnableInterrupt;
  mRealDisableInterrupt  = gCpu->DisableInterrupt;
  mRealGetInterruptState = gCpu->GetInterruptState;

  /*
   * Tiano doesn't do anything crazy like protecting protocols
   * to be RO. A UEFI implementation might, in which case maybe
   * SetMemoryAttributes will help. Or maybe your SOL. Have fun!
   *
   * N.B. even if you can't hook this way, you could always
   * set a breakpoint exception. Ugh, though.
   */
  gCpu->EnableInterrupt   = EfiHooksCpuEnableInterrupt;
  gCpu->DisableInterrupt  = EfiHooksCpuDisableInterrupt;
  gCpu->GetInterruptState = EfiHooksCpuGetInterruptState;
  CriticalEnd ();

  return EFI_SUCCESS;
}

VOID
EfiHooksCleanup (
  VOID
  )
{
  CriticalBegin ();
  gCpu->EnableInterrupt   = mRealEnableInterrupt;
  gCpu->DisableInterrupt  = mRealDisableInterrupt;
  gCpu->GetInterruptState = mRealGetInterruptState;
  CriticalEnd ();
}
