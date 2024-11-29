//
// Copyright (c) 2022-2023, Intel Corporation. All rights reserved.<BR>
// Copyright (c) 2024 Loongson Technology Corporation Limited. All rights reserved.<BR>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//

#define STORE    st.d      /* 64 bit mode regsave instruction */
#define LOAD     ld.d      /* 64 bit mode regload instruction */
#define RSIZE    8         /* 64 bit mode register size */

ASM_GLOBAL ASM_PFX(EmulatorThunk)
ASM_PFX(EmulatorThunk):
  STORE  $sp, $sp, -80
  addi.d $sp, $sp, -80
  STORE  $ra, $sp, 1 * RSIZE
  STORE  $a0, $sp, 2 * RSIZE
  STORE  $a1, $sp, 3 * RSIZE
  STORE  $a2, $sp, 4 * RSIZE
  STORE  $a3, $sp, 5 * RSIZE
  STORE  $a4, $sp, 6 * RSIZE
  STORE  $a5, $sp, 7 * RSIZE
  STORE  $a6, $sp, 8 * RSIZE
  STORE  $a7, $sp, 9 * RSIZE

  move     $a0, $t0
  addi.d   $a1, $sp, 2 * RSIZE
  move     $a2, $t1
  move     $a3, $ra
  la.pcrel $ra, EmulatorVmEntry
  jirl     $ra, $ra, 0

  LOAD   $ra, $sp, 1 * RSIZE
  LOAD   $sp, $sp, 0x0
  jirl   $zero, $ra, 0
.end
