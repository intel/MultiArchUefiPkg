/** @file

    Copyright (c) 2024, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#include "Emulator.h"

EFI_STATUS
ObjectAllocCreate (
  IN  ObjectAllocConfig   *Config,
  OUT ObjectAllocContext  **Context
  )
{
  UINTN               Index;
  UINTN               Pages;
  UINTN               ObjectSize;
  EFI_STATUS          Status;
  ObjectHeader        *Object;
  ObjectAllocContext  *ObjectContext;

  ObjectContext = AllocatePool (sizeof (*ObjectContext));
  if (ObjectContext == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ObjectContext->Config = *Config;
  Config                = &ObjectContext->Config;

  ASSERT (Config->OnCreate == NULL || IsDriverImagePointer (Config->OnCreate));
  ASSERT (Config->OnDestroy == NULL || IsDriverImagePointer (Config->OnDestroy));
  ASSERT (Config->OnAlloc == NULL || IsDriverImagePointer (Config->OnAlloc));
  ASSERT (Config->OnFree == NULL || IsDriverImagePointer (Config->OnFree));

  ObjectSize = ROUND_UP (Config->ObjectSize, Config->ObjectAlignment);

  if (ObjectSize < sizeof (ObjectHeader)) {
    return EFI_INVALID_PARAMETER;
  }

  Pages                    = EFI_SIZE_TO_PAGES (ObjectSize * Config->ObjectCount);
  ObjectContext->ArenaSize = EFI_PAGES_TO_SIZE (Pages);
  Config->ObjectCount      = ObjectContext->ArenaSize / ObjectSize;

  ObjectContext->Arena = AllocatePages (Pages);
  if (ObjectContext->Arena == NULL) {
    FreePool (ObjectContext);
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (ObjectContext->Arena, ObjectContext->ArenaSize);
  InitializeListHead (&ObjectContext->List);

  for (Object = ObjectContext->Arena, Index = 0;
       Index < Config->ObjectCount;
       Object = (VOID *)(((UINTN)Object) + ObjectSize), Index++)
  {
    Object->Signature = Config->Signature;

    if (Config->OnCreate != NULL) {
      Status = Config->OnCreate (Object, Config->CbContext);
      if (EFI_ERROR (Status)) {
        break;
      }
    }

    InsertTailList (&ObjectContext->List, &Object->Link);
  }

  if (EFI_ERROR (Status)) {
    /*
     * Object points to the first failed object.
     */
    while (Index-- != 0) {
      /*
       * Undo OnCreate for successfully created objects.
       */
      Object = (VOID *)(((UINTN)Object) - ObjectSize);
      if (Config->OnDestroy != NULL) {
        Config->OnDestroy (Object, Config->CbContext);
      }
    }

    FreePages (ObjectContext->Arena, Pages);
    FreePool (ObjectContext);
    return Status;
  }

  *Context = ObjectContext;
  return EFI_SUCCESS;
}

VOID
ObjectAllocDestroy (
  IN  ObjectAllocContext  *Context
  )
{
  UINTN              Index;
  UINTN              ObjectSize;
  ObjectHeader       *Object;
  ObjectAllocConfig  *Config;

  Config     = &Context->Config;
  ObjectSize = ROUND_UP (Config->ObjectSize, Config->ObjectAlignment);

  for (Object = Context->Arena, Index = 0;
       Index < Config->ObjectCount;
       Object = (VOID *)(((UINTN)Object) + ObjectSize), Index++)
  {
    /*
     * IsListEmpty is true only for allocated objects. No objects
     * ought to be allocated at this time.
     */
    ASSERT (!IsListEmpty (&Object->Link));
    Config->OnDestroy (Object, Config->CbContext);
  }

  FreePages (Context->Arena, EFI_SIZE_TO_PAGES (ObjectSize * Config->ObjectCount));
  FreePool (Context);
}

EFI_STATUS
ObjectAlloc (
  IN  ObjectAllocContext  *Context,
  OUT ObjectHeader        **ObjectReturned
  )
{
  EFI_STATUS         Status;
  ObjectHeader       *Object;
  LIST_ENTRY         *ObjectEntry;
  ObjectAllocConfig  *Config;

  ASSERT (Context != NULL);
  ASSERT (Context->Arena != NULL);

  Config = &Context->Config;
  if (IsListEmpty (&Context->List)) {
    return EFI_OUT_OF_RESOURCES;
  }

  ObjectEntry = GetFirstNode (&Context->List);
  Object      = CR (ObjectEntry, ObjectHeader, Link, Config->Signature);

  if (Config->OnAlloc != NULL) {
    Status = Config->OnAlloc (Object, Config->CbContext);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  RemoveEntryList (ObjectEntry);

  /*
   * This allows us to detect still-allocated objects in ObjectAllocDestroy.
   */
  InitializeListHead (ObjectEntry);

  *ObjectReturned = Object;
  return EFI_SUCCESS;
}

VOID
ObjectFree (
  IN  ObjectAllocContext  *Context,
  IN  ObjectHeader        *Object
  )
{
  ObjectAllocConfig  *Config;

  ASSERT (Context != NULL);
  ASSERT (Context->Arena != NULL);

  Config = &Context->Config;

  ASSERT (IsListEmpty (&Object->Link));
  if (Config->OnFree != NULL) {
    Config->OnFree (Object, Config->CbContext);
  }

  InsertTailList (&Context->List, &Object->Link);
}
