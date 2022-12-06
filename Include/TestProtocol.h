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
  UINT64 Field1;
  UINT64 Field2;
  UINT64 Field3;
  /*
   * Doesn't matter how many fields this has, so long as it
   * all adds up to being larger than RET16.
   */
  UINT64 Field4;
} RET_LARGE;

typedef struct {
  UINT64 Field1;
  UINT64 Field2;
} RET16;

typedef struct {
  UINT16            HostMachineType;
  UINT64     EFIAPI (*TestRet)(VOID);
  RET16      EFIAPI (*TestRet16)(UINT64);
  RET_LARGE  EFIAPI (*TestLargeRet)(UINT64);
  EFI_STATUS EFIAPI (*TestArgs)(UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64,
                                UINT64, UINT64, UINT64, UINT64);
  UINT64     EFIAPI (*TestCb)(UINT64, UINT64,
                              UINT64 EFIAPI (*Cb)(UINT64, UINT64));
} X86_EMU_TEST_PROTOCOL;

RET16 TestRet16 (
  IN  UINT64 Arg1
  );

RET_LARGE TestLargeRet (
  IN  UINT64 Arg1
  );
