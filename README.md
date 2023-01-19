# UCX86EmulatorPkg

This code implements a DXE driver for EDK2/Tianocore that allows
64-bit x86 UEFI drivers (aka x86_64, X64, AMD64) to be executed
on other 64-bit UEFI environments. Today, AArch64 and RISC-V
are supported.

It's derived from https://github.com/ardbiesheuvel/X86EmulatorPkg, yet
is otherwise a reimplementation using https://www.unicorn-engine.org/.
Same performance, half the binary size (on AArch64) and portable.
You can have all three.

## How does it work?

UEFI code uses a pretty narrowly-defined ABI, which makes it
easy to thunk x64 code making EFIAPI calls to native code -
no FP/SIMD, no returning large values, etc. E.g. calls like:

UINT64
EFIAPI
Fn(UINT64, UINT64, UINT64, UINT64,
   UINT64, UINT64, UINT64, UINT64,
   UINT64, UINT64, UINT64, UINT64,
   UINT64, UINT64, UINT64, UINT64);

...with up to 16 arguments are supported both x64 -> native
and native -> x64, which covers all UEFI needs.

The emulator presents an x64 UEFI Boot Services environment,
appropriate for running Boot Servces drivers (e.g. OpRom drivers
such as SNP, GOP) and EFI applications (that aren't OS loaders).
Certain Boot, Runtime and DXE services are filtered or disabled.

## Quick Start

To quickly compile for AArch64:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/UCX86EmulatorPkg.git
        $ git submodule update --init
        $ export GCC5_AARCH64_PREFIX=... (if you are on a non-AArch64 system)
        $ build -a AARCH64 -t GCC5 -p UCX86EmulatorPkg/X86Emulator.dsc -b RELEASE

This will produce Build/UCX86Emulator/RELEASE_GCC5/AARCH64/X86EmulatorDxe.efi

To quickly compile for RISCV64:

        $ git clone https://github.com/tianocore/edk2-staging.git
        $ cd edk2-staging
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/UCX86EmulatorPkg.git
        $ git submodule update --init

        Apply the patches under UCX86EmulatorPkg/edk2-staging-patches, except for
        0006-ArmVirtPkg-bundle-UCX86EmulatorPkg-driver.patch. These patches will also
        need to be applied to the UEFI firmware used for testing.

        $ export GCC5_RISCV64_PREFIX=... (if you are on a non-RISCV64 system)
        $ build -a RISCV64 -t GCC5 -p UCX86EmulatorPkg/X86Emulator.dsc -b RELEASE

This will produce Build/UCX86Emulator/RELEASE_GCC5/RISCV64/X86EmulatorDxe.efi

## ArmVirtPkg firmware with the bundled emulator

To quickly compile an ArmVirtPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/UCX86EmulatorPkg.git
        $ git submodule update --init

        Apply edk2-staging-patches/0006-ArmVirtPkg-bundle-UCX86EmulatorPkg-driver.patch

        Be sure to comment out "INF OvmfPkg/VirtioNetDxe/VirtioNet.inf" in ArmVirtPkg/ArmVirtQemuFvMain.fdf.inc
        if you want to test with the x86_64 virtio iPXE OpRom.

        $ make -C BaseTools
        $ . edksetup.sh
        $ export GCC5_AARCH64_PREFIX=... (if you are on a non-aarch64 system)
        $ build -a AARCH64 -t GCC5 -p ArmVirtPkg/ArmVirtQemu.dsc -b RELEASE (-b DEBUG for debug build)

You can then use QEMU to execute it:

        $ qemu-system-aarch64 -M virt -cpu cortex-a57 -m 2G -nographic -bios ./Build/ArmVirtQemu-AARCH64/RELEASE_GCC5/FV/QEMU_EFI.fd

If you see dots on your screen, that is the x86_64 virtio iPXE OpRom in action!

## OvmfPkg RiscVVirt firmware with the bundled emulator

To quickly compile a RiscVVirt OvmfPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/UCX86EmulatorPkg.git
        $ git submodule update --init

        Apply the patches under UCX86EmulatorPkg/edk2-staging-patches, except for
        0006-ArmVirtPkg-bundle-UCX86EmulatorPkg-driver.patch.

        $ make -C BaseTools
        $ . edksetup.sh
        $ export GCC5_RISCV64_PREFIX=... (if you are on a non-RISCV64 system)
        $ build -a RISCV64 -t GCC5 -p OvmfPkg/RiscVVirt/RiscVVirtQemu.dsc -b RELEASE (-b DEBUG for debug build)
        $ dd if=/dev/zero of=flash1.img bs=1M count=32
        $ dd if=Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT.fd of=flash1.img conv=notrunc

You can then use QEMU to execute it:

        $ qemu-system-riscv64 -drive file=flash1.img,if=pflash,format=raw,unit=1 -machine virt,aia=aplic,acpi=on -m 1G -smp 2 -nographic

This has been tested with PCIe pass-through and an AMD Radeon with x64 GOP.

## Further testing

There's a small test application:

        $ export GCC5_X64_PREFIX=... (if you are on a non-X64 system)
        $ build -a X64 -t GCC5 -p UCX86EmulatorPkg/X86EmulatorTest.dsc

When run against a DEBUG build of X86EmulatorDxe, will run further sanity tests.

## Special builds

UCX86EmulatorPkg uses a port of Project Unicorn to UEFI which is not
yet upstreamed. Beyond UEFI support, the unicorn-for-efi repo also
contains critical fixes to the operation of emulator. Beyond fixes, there
are additional improvements that rely on additional new Unicorn APIs being
made available.

If you build with UPSTREAM_UC=YES, you will get a version of UCX86EmulatorPkg
that does not rely on the existence of these additional APIs. Such a build
could be interesting as part of upstreaming efforts for the UEFI port of
Project Unicorn. Building with UPSTREAM_UC=YES does *not* exclude changes
without an API impact.

If you build with ON_PRIVATE_STACK=YES, X86EmulatorDxe will use a dedicated
native stack for handling x64 emulation. This has some runtime overhead and
is unneccesary for normal operation.

If you build with WRAPPED_ENTRY_POINTS=YES, X86EmulatorDxe will use a
different mechanism for invoking emulated code, for debugging situations
where the X86InterpreterSyncExceptionCallback machinery (exception-driven
detection of emulated code execution) doesn't work (e.g. adding a new host
CPU port, running certain x64 apps on RISC-V, ...).

Finally, you can choose to build with BaseDebugLibNull. By default
UefiDebugLibConOut is used to get some reasonable debugging output, but
the extra code generated for DEBUG((...)) macros used for logging does
have some performance impact on the microbenchmarks.
