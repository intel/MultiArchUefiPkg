# Running MultiArchUefiPkg

## How do I load a standalone built driver?

That is, what if EmulatorDxe.efi is not part of my firmware?

Put EmulatorDxe.efi on removable media and use the `load` Shell command. E.g.:

        Shell> fs0:
        FS0:\> load EmulatorDxe.efi

This should let you run non-native applications and load other drivers.

Note: this will *not* magically get a PCIe OpRom image loaded. See
[LoadOpRom.efi](#loadopromefi) for information on how to get a device
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

There are a few test applications. To build these:

        $ export GCC_X64_PREFIX=... (if you are on a non-X64 system)
        $ build -a X64 -t GCC -p MultiArchUefiPkg/EmulatorApps.dsc
        $ build -a AARCH64 -t GCC -p MultiArchUefiPkg/EmulatorApps.dsc
        $ build -a RISCV64 -t GCC -p MultiArchUefiPkg/EmulatorApps.dsc

### EmulatorTest.efi

Performs some regression and performance testing. Regression testing
relies on a DEBUG build of EmulatorDxe. The application can be run in
a native environment for overhead comparison purposes (e.g. a RISCV64
EmulatorTest vs an AARCH64 EmulatorTest in a RISCV64 environment).

### SetCon.efi

SetCon manipulates the console variables: `ConIn`, `ConOut`, `ErrOut`
and `ConInDev`, `ConOutDev`, `ErrOutDev`.

Particularly useful when loading graphics drivers using [LoadOpRom.efi](#loadopromefi).

#### Usage

        Shell> SetCon.efi [-A] [-i handle-index] [-h handle] [-p path] [-a] VariableName

Options:
* `-a`: append the new path to the existing set of device paths, instead of replacing.
* `-A`: automatically build console variables from currently available devices.
* `-i`: takes a handle index as produced by the UEFI Shell `dh` tool.
* `-h`: takes an actual EFI_HANDLE value.
* `-p`: takes a textual path representation.

Use it to see the current devices encoded:

        Shell> SetCon.efi ConOut
        ConOut:
           VenHw(D3987D4B-971A-435F-8CAF-4967EB627241)/Uart(38400,8,N,1)/VenMsg(DFA66065-B419-11D3-9A2D-0090273FC14D)
           PciRoot(0x0)/Pci(0x2,0x0)/AcpiAdr(0x80010100)

Use it to set variables:

        Shell> SetCon.efi -i AA ConIn
        Shell> SetCon.efi -h 1ABCDEF01 ErrOutDev
        Shell> SetConf.efi -a -p PciRoot(0x0)/Pci(0x2,0x0)/AcpiAdr(0x80010100) ConOut

Note: no validation is done on the device path. Yes, you can set `ConOut` to your disk drive, lol. No, it won't work or do anything.

Use it to automatically build console variables from available devices. This
is especially interesting in combination with LoadOpRom.efi. The example
below loads and starts OpRoms for all available devices, sets `ConOut`
to include the new video device, and forces UEFI to refresh active consoles
by disconnecting and reconnecting all drivers to devices.

        FS0:> load EmulatorDxe.efi
        FS0:> LoadOpRom.efi
        FS0:> SetCon -A ConOut
        FS0:> reconnect -r

### LoadOpRom.efi

A generic tool to load a PCI(e) OpRom driver. Particularly useful when
loading standalone EmulatorDxe.efi builds from UEFI Shell. That is,
when not bundling the emulator driver in a firmware build.

LoadOpRom.efi is UEFI application meant to run from the Shell. It takes
the Segment, Bus, Device and Function number of the PCI(e) function to
load the driver for.

The application only supports loading drivers of the same CPU architecture
as itself. This is a useful property, as OpRoms can contain many images,
e.g. an X64 and an AArch64 image.

#### Usage

        Shell> LoadOpRom.efi [-l] [-n] [seg bus dev func]

When run without a segment/bus/device/function tuple and extra
options, the tool will load compatible OpRom images for _all_
PCI(e) devices in the system. If you specify a particular SBDF,
the tool will process the OpRom for that SBDF alone.

By default, the tool will recursively connect the loaded image
(which it assumes to be a driver) to the controller where the
OpRom came from. This is usually what you'd want, anyway.

Options:
* `-l`: just list all ROM images, don't load anything.
* `-n`: when loading a driver, don't connect the driver to the controller.

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
        Recursive connect...

...at this point you _may_ have to adjust your Console Output device. You can do this manually:
* Go to Setup screen.
* Go to Boot Maintenance Manager.
* Go to Console Output Device Select.
* Put a checkbox next to the PCIe device(s) showing up.
* Save and exit back out to UEFI Shell.
* Run `connect -r` again.

You can also do this using the [SetCon.efi](#setconefi) tool.
