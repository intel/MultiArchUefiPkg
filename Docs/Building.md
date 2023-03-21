# Building MultiArchUefiPkg

## Quick Start

### For AArch64

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init
        $ export GCC5_AARCH64_PREFIX=... (if you are on a non-AArch64 system)
        $ build -a AARCH64 -t GCC5 -p MultiArchUefiPkg/Emulator.dsc -b RELEASE

This will produce Build/MultiArchUefiPkg/RELEASE_GCC5/AARCH64/EmulatorDxe.efi

### For RISCV64

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init
        $ export GCC5_RISCV64_PREFIX=... (if you are on a non-RISCV64 system)
        $ build -a RISCV64 -t GCC5 -p MultiArchUefiPkg/Emulator.dsc -b RELEASE

This will produce Build/MultiArchUefiPkg/RELEASE_GCC5/RISCV64/EmulatorDxe.efi

## Bundled Firmware Builds

### AArch64 Qemu Firmware

To quickly compile an ArmVirtPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init

        Apply any patches under MultiArchUefiPkg/edk2-staging-patches.

        Be sure to comment out "INF OvmfPkg/VirtioNetDxe/VirtioNet.inf" in
        ArmVirtPkg/ArmVirtQemuFvMain.fdf.inc if you want to test with the
        x86_64 virtio iPXE OpRom.

        $ make -C BaseTools
        $ . edksetup.sh
        $ export GCC5_AARCH64_PREFIX=... (if you are on a non-aarch64 system)
        $ build -a AARCH64 -t GCC5 -p ArmVirtPkg/ArmVirtQemu.dsc -b RELEASE (-b DEBUG for debug build)

You can then use QEMU to execute it:

        $ qemu-system-aarch64 -M virt -cpu cortex-a57 -m 2G -nographic -bios ./Build/ArmVirtQemu-AARCH64/RELEASE_GCC5/FV/QEMU_EFI.fd

If you see dots on your screen, that is the x86_64 virtio iPXE OpRom in action!

### RISCV64 Qemu Firmware

To quickly compile a RiscVVirt OvmfPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init

        Apply any patches under MultiArchUefiPkg/edk2-staging-patches.

        $ make -C BaseTools
        $ . edksetup.sh
        $ export GCC5_RISCV64_PREFIX=... (if you are on a non-RISCV64 system)
        $ build -a RISCV64 -t GCC5 -p OvmfPkg/RiscVVirt/RiscVVirtQemu.dsc -b RELEASE (-b DEBUG for debug build)
        $ dd if=/dev/zero of=flash1.img bs=1M count=32
        $ dd if=Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT.fd of=flash1.img conv=notrunc

You can then use QEMU to execute it:

        $ qemu-system-riscv64 -drive file=flash1.img,if=pflash,format=raw,unit=1 -machine virt,aia=aplic,acpi=on -m 1G -smp 2 -nographic

This has been tested with PCIe pass-through and an AMD Radeon with x64 GOP.

## Further Testing

There's a small test application:

        $ export GCC5_X64_PREFIX=... (if you are on a non-X64 system)
        $ build -a X64 -t GCC5 -p MultiArchUefiPkg/EmulatorTest.dsc
        $ build -a AARCH64 -t GCC5 -p MultiArchUefiPkg/EmulatorTest.dsc
        $ build -a RISCV64 -t GCC5 -p MultiArchUefiPkg/EmulatorTest.dsc

When run against a DEBUG build of EmulatorDxe, will run further sanity tests.
The application can be run in a native environment for overhead comparison
purposes (e.g. a RISCV64 EmulatorTest vs an AARCH64 EmulatorTest in a
RISCV64 environment).

## Unicorn Engine Dependency

MultiArchUefiPkg uses a port of Project Unicorn to UEFI which is not
yet upstreamed. Beyond UEFI support, the [unicorn-for-efi](https://github.com/intel/unicorn-for-efi) repo also contains critical fixes to the operation of emulator. Beyond fixes, there
are additional improvements that rely on additional new Unicorn APIs being
made available.

## Special Builds

### Building With ON_PRIVATE_STACK=YES

If you build with ON_PRIVATE_STACK=YES, EmulatorDxe will use a dedicated
native stack for handling x64 emulation. This has some runtime overhead and
is unneccesary for normal operation.

### Building With WRAPPED_ENTRY_POINTS=YES

EmulatorDxe relies on MMU functionality to be present in the host UEFI implementation.
If you build with WRAPPED_ENTRY_POINTS=YES, EmulatorDxe will enable an additional
mechanism for invoking certain emulated client code from native code. It's a more
efficient mechanism than relying on no-executable protection and CPU traps, but it
only works for interfaces that EmulatorDxe is aware of.

#### RISC-V Notes

As of March 21st 2023, RISC-V builds still default to WRAPPED_ENTRY_POINTS=YES. This is
still necessary for the upstream TianoCore EDK2 on RISC-V, which does
not have MMU support and does not allow configuring page-based protection.
WARPPED_ENTRY_POINTS=YES allow certain, but not all, 3rd party OpRoms
and most (but not all) applications to function.

The fallback on RISC-V is the invalid instruction trap handler, which works if you're lucky
(emulated instruction is misaligned from RISC-V perspective or an invalid instruction)
and doesn't work if you're not lucky - this is why WRAPPED_ENTRY_POINTS was initially added!

A [MMU support patchset from Ventana Microsystems](https://github.com/pttuan/edk2/tree/tphan/riscv_mmu)
is in review and has been tested to work well with MultiArchUefiPkg.

### AARCH64 Binaries On RISC-V

A RISC-V build can include AArch64 support when built with the
SUPPORTS_AARCH64_BINS=YES option.

Note: this will increase the
binary size nearly 2x to 3MiB until LTO support for RISC-V is
added to TianoCore EDK2, so this option is off by default.

### Building With CHECK_ORPHAN_CONTEXTS=YES

If you build with CHECK_ORPHAN_CONTEXTS=YES, EmulatorDxe will perform
more runtime checks to handle unexpected/non-linear control flow from
native code, that can result in a resource leak inside the emulator.
This is enabled by default for DEBUG builds.

### Building With EMU_TIMEOUT_NONE

Due to the way the Unicorn Engine operates (it is not fully reentrant),
client code is run in a critical section with timers disabled. To avoid hangs
due to emulated tight loops polling on some memory location updated by an
event, the code periodically bails out. If your client binaries are known good,
i.e.  they don't rely on tight polling loops without using UEFI services, you
can also try building with EMU_TIMEOUT_NONE for maximum performance.

Note 1: EmulatorTest will _not_ correctly work with EMU_TIMEOUT_NONE.

### Building Without Any Logging

Finally, you can choose to build with BaseDebugLibNull. By default
UefiDebugLibConOut is used to get some reasonable debugging output, but
the extra code generated for DEBUG((...)) macros used for logging does
have some performance impact on the microbenchmarks.
