/** @file

    Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

**/

#pragma once

#define X86_EMU_TEST_PROTOCOL_GUID                                      \
  { 0x9af2f62c, 0xac9b, 0xa821, { 0xa0, 0x8d, 0xec, 0x9e, 0xc4, 0x21, 0xb1, 0xb0 }};

#define ARG_VAL(x) ((x##UL << 56) | (x##UL))
#define RET_VAL ARG_VAL(0xFF)
#define FIELD_VAL(x) ((1UL << 63) | (x##UL << 56) | (x##UL))

typedef struct {
  UINTN CurrentContextCount;
  UINTN ExitPeriodMs;
  UINTN ExitPeriodTicks;
  UINTN ExitPeriodTbs;
} X86_EMU_TEST_DEBUG_STATE;

typedef struct {
  UINT16            HostMachineType;
  UINT64     EFIAPI (*TestRet)(VOID);
  EFI_STATUS EFIAPI (*TestArgs)(UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64);
  UINT64     EFIAPI (*TestCbArgs)(UINT64 EFIAPI (*Cb)(UINT64, UINT64, UINT64, UINT64,
                                                      UINT64, UINT64, UINT64, UINT64,
                                                      UINT64, UINT64, UINT64, UINT64,
                                                      UINT64, UINT64, UINT64, UINT64));
  EFI_STATUS EFIAPI (*TestGetDebugState)(X86_EMU_TEST_DEBUG_STATE *DebugState);
  UINT64     EFIAPI (*TestSj)(VOID EFIAPI (*Cb)(VOID *Buffer));
  VOID       EFIAPI (*TestLj)(VOID *Buffer);
} X86_EMU_TEST_PROTOCOL;
