//
// Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//

        .text
        .align 3
        .global         X86EmulatorThunk
X86EmulatorThunk:
        addi            sp, sp, -80
        sd              ra, 0(sp)
        sd              s0, 8(sp)
        addi            s0, sp, 80

        sd              a0, 16(sp)
        sd              a1, 24(sp)
        sd              a3, 32(sp)
        sd              a4, 40(sp)
        sd              a5, 48(sp)
        sd              a6, 62(sp)
        sd              a7, 64(sp)

        mv              a0, t0
        addi            a1, sp, 16
        mv              a2, t1
        mv              a3, ra
        jal             X86EmulatorVmEntry

        ld              ra, 0(sp)
        ld              s0, 8(sp)
        addi            sp, sp, 80
        ret
