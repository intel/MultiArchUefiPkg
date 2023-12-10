# Running MultiArchUefiPkg

## How do I load the driver if it's not part of my firmware?

Put EmulatorDxe.efi on removable media and use the `load` Shell command. E.g.:

        Shell> fs0:
        Shell> load EmulatorDxe.efi

This should let you run non-native applications and load other drivers.

Note: this will *not* magically get a PCIe OpRom image loaded. In fact,
there is no mechanism to do that, short of embedding EmulatorDxe into
your firmware for this to work. Connecting/reconnecting handles won't help
and the `loadpcirom` Shell command expects a file, not a PCI(e) S/B/D/F.

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

There's a small test application:

        $ export GCC_X64_PREFIX=... (if you are on a non-X64 system)
        $ build -a X64 -t GCC -p MultiArchUefiPkg/EmulatorTest.dsc
        $ build -a AARCH64 -t GCC -p MultiArchUefiPkg/EmulatorTest.dsc
        $ build -a RISCV64 -t GCC -p MultiArchUefiPkg/EmulatorTest.dsc

When run against a DEBUG build of EmulatorDxe, will run further sanity tests. The application can be run in a native environment for overhead comparison purposes (e.g. a RISCV64 EmulatorTest vs an AARCH64 EmulatorTest in a RISCV64 environment).
