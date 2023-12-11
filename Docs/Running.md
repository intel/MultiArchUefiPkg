# Running MultiArchUefiPkg

## How do I load the driver if it's not part of my firmware?

Put EmulatorDxe.efi on removable media and use the `load` Shell command. E.g.:

        Shell> fs0:
        FS0:\> load EmulatorDxe.efi

This should let you run non-native applications and load other drivers.

Note: this will *not* magically get a PCIe OpRom image loaded. See
[LoadOpRom.efi](#loadoprom-efi) for information on how to get n device
OpRom driver working after manually loading the emulator.

## How do I know the emulator driver is present?

Use the `drivers` Shell command.

        Shell> drivers
                    T   D
        D           Y C I
        R           P F A
        V  VERSION  E G G #D #C DRIVER NAME                         IMAGE NAME
        == ======== = = = == == =================================== ==========
        ...
        7D 0000000A D - -  1  - MultiArchUefiPkg Emulator Driver    EmulatorDxe

## How do I know the emulator driver successfully loaded?

Use the 'devices' Shell command.

        Shell> devices
             T   D
             Y C I
             P F A
        CTRL E G G #P #D #C  Device Name
        ==== = = = == == === =========================================================
        7E R - -  0  1   0 MultiArchUefiPkg Emulator

## Can I stop or disconnect the driver?

Not today.

## Can I unload the driver?

Not today.

## Testing

There's a couple test applications. To build these:

        $ export GCC_X64_PREFIX=... (if you are on a non-X64 system)
        $ build -a X64 -t GCC -p MultiArchUefiPkg/EmulatorApps.dsc
        $ build -a AARCH64 -t GCC -p MultiArchUefiPkg/EmulatorApps.dsc
        $ build -a RISCV64 -t GCC -p MultiArchUefiPkg/EmulatorApps.dsc

### EmulatorTest.efi

When run against a DEBUG build of EmulatorDxe, will run further sanity tests. The application can be run in a native environment for overhead comparison purposes (e.g. a RISCV64 EmulatorTest vs an AARCH64 EmulatorTest in a RISCV64 environment).

### LoadOpRom.efi

A generic tool to load a PCI(e) OpRom driver. Particularly useful when
loading EmulatorDxe.efi from UEFI Shell (i.e. when not bundling the driver
in a firmware build).

LoadOpRom.efi is UEFI application meant to run from the Shell. It takes
the Segment, Bus, Device and Function number of the PCI(e) function to
load the driver for.

The application only supports loading drivers of the same CPU architecture
as itself. This is a useful property, as OpRoms can contain many images,
e.g. an X64 and an AArch64 image.

Here's an example session booting up a video card driver:

        FS0:\> pci
           Seg  Bus  Dev  Func
           ---  ---  ---  ----
           ...
            01   02   00    00 ==> Display Controller - VGA/8514 controller
                     Vendor 102B Device 2527 Prog Interface 0
        FS0:\> RISCV64LoadOpRom.efi 01 02 00 00
        ROM 0x00016A00 bytes
        --------------------
        +0x00000000: UNSUPPORTED BIOS image (0x9000 bytes)
        +0x00009000: UNSUPPORTED 0x8664 UEFI image (0xDA00 bytes)
        FS0:\> X64LoadOpRom.efi
        Image type X64 can't be loaded on RISCV64 UEFI system.
        FS0:\> load EmulatorDxe.efi
        ...
        FS0:\> X64LoadOpRom.efi 01 02 00 00
        ROM 0x00016A00 bytes
        --------------------
        +0x00000000: UNSUPPORTED BIOS image (0x9000 bytes)
        +0x00009000: SUPPORTED 0x8664 UEFI image (0xDA00 bytes)
        +0x00009000:    Subsystem: 0xB
        +0x00009000:    InitializationSize: 0xDA00 (bytes)
        +0x00009000:    EfiImageHeaderOffset: 0x38
        +0x00009000:    Compressed: yes
        ...
        Loading driver at 0x000F8840000 EntryPoint=0x000F88421A0
        ...
        FS0:\> connect -r
