# Building MultiArchUefiPkg

MultiArchUefiPkg needs to be built using the TianoCore edk2 build
system, headers and libraries.

Last validated with TianoCore edk2 at Dec 7, 2023 commit [eccdab6](https://github.com/tianocore/edk2/commit/eccdab611c01aa40b6cefcfbcb4d23e54b4c0ec6).

## UEFI Requirements

- EFI_LOADED_IMAGE_PROTOCOL
- EFI_CPU_ARCH_PROTOCOL
- EFI_CPU_IO2_PROTOCOL
- EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL
- EFI_SERIAL_IO_PROTOCOL (see [note](#mau_standalone_logging-choices))
- MMU with support for no-execute mappings.
- AArch64 or RISCV64 UEFI implementation.

## Tested Compilers

Here's a rough idea of what's supposed to work. File bugs if MultiArchUefiPkg doesn't build for you.

### AArch64

- gcc version 9.4.0 (Ubuntu 9.4.0-1ubuntu1~20.04.1)

### RISC-V

- gcc version 9.4.0 (Ubuntu 9.4.0-1ubuntu1~20.04)
- gcc version 11.3.0 (Ubuntu 11.3.0-1ubuntu1~22.04.1)
- gcc version 12.2.0 (g2ee5e430018, from https://github.com/riscv-collab/riscv-gnu-toolchain)

### X64

- gcc version 9.4.0 (Ubuntu 9.4.0-1ubuntu1~20.04.1)

## Quick Start

### For AArch64

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git unicorn
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init
        $ export GCC_AARCH64_PREFIX=... (if you are on a non-AArch64 system)
        $ build -a AARCH64 -t GCC -p MultiArchUefiPkg/Emulator.dsc -b RELEASE

This will produce `Build/MultiArchUefiPkg/RELEASE_GCC/AARCH64/EmulatorDxe.efi`.

### For RISCV64

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git unicorn
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init
        $ export GCC_RISCV64_PREFIX=... (if you are on a non-RISCV64 system)
        $ build -a RISCV64 -t GCC -p MultiArchUefiPkg/Emulator.dsc -b RELEASE

This will produce `Build/MultiArchUefiPkg/RELEASE_GCC/RISCV64/EmulatorDxe.efi/`.

## Bundled Firmware Builds

There are two mechanisms to bundle the emulator driver with UEFI firmware.
- The recommended way is to consume the driver as a separately-built binary. This is called binary-included.
- The non-recommended way is build the emulator driver as part of a firmware build. This is called direct-included.

Binary-included builds are recommended to avoid conflicts between build options
and libraries used by the emulator and firmware builds. For example, as of
11/2023, RiscVVirt did not like getting built with `-Os` (optimize for size).

In some situations you may choose direct-included builds. For example, to debug some issue using your existing
serial port-based DebugLib. This may come with complications, include significantly larger binary size.

See [Docs/DirectlyIncluded](DirectlyIncluded/) for example
direct-included platform patches. See [Docs/BinaryIncluded](BinaryIncluded/) for example
binary-included platform patches. Note, for binary-included builds it is paramount to include $WORKSPACE
as part of your PACKAGES_PATH.

The examples below cover the binary-included case only.

### AArch64 Qemu Firmware

To quickly compile an ArmVirtPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git unicorn
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init
        $ git am MultiArchUefiPkg/Docs/BinaryIncluded/0002-ArmVirtPkg-bundle-MultiArchUefiPkg-driver-as-a-bin.patch

Be sure to comment out `INF OvmfPkg/VirtioNetDxe/VirtioNet.inf` in `ArmVirtPkg/ArmVirtQemuFvMain.fdf.inc` if you want to test with the x86_64 virtio iPXE OpRom.

        $ make -C BaseTools
        $ . edksetup.sh
        $ export PACKAGES_PATH=$WORKSPACE
        $ export GCC_AARCH64_PREFIX=... (if you are on a non-AArch64 system)
        $ build -a AARCH64 -t GCC -p MultiArchUefiPkg/Emulator.dsc -b RELEASE
        $ build -a AARCH64 -t GCC -p ArmVirtPkg/ArmVirtQemu.dsc -b RELEASE

You can then use QEMU to execute it:

        $ qemu-system-aarch64 -M virt -cpu cortex-a57 -m 2G -nographic -bios ./Build/ArmVirtQemu-AARCH64/RELEASE_GCC/FV/QEMU_EFI.fd

If you see dots on your screen, that is the x86_64 virtio iPXE OpRom in action!

### RISCV64 Qemu Firmware

To quickly compile a RiscVVirt OvmfPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel/unicorn-for-efi.git unicorn
        $ git submodule add https://github.com/intel/MultiArchUefiPkg.git
        $ git submodule update --init
        $ git am MultiArchUefiPkg/Docs/BinaryIncluded/0001-OvmfPkg-bundle-MultiArchUefiPkg-driver-as-a-bin.patch
        $ make -C BaseTools
        $ . edksetup.sh
        $ export PACKAGES_PATH=$WORKSPACE
        $ export GCC_RISCV64_PREFIX=... (if you are on a non-RISCV64 system)
        $ build -a RISCV64 -t GCC -p MultiArchUefiPkg/Emulator.dsc -b RELEASE

Follow further build and run instructions under `OvmfPkg/RiscVVirt/README.md` in the Tiano tree. This has been tested with PCIe pass-through and an AMD Radeon with x64 GOP.

## Unicorn Engine Dependency

MultiArchUefiPkg uses a port of Project Unicorn to UEFI which is not yet upstreamed. Beyond UEFI support, the [unicorn-for-efi](https://github.com/intel/unicorn-for-efi) repo also contains critical fixes to the operation of emulator. Beyond fixes, there are additional improvements that rely on additional new Unicorn APIs being made available.

## Special Builds

### Building With `MAU_ON_PRIVATE_STACK=YES`

If you build with `MAU_ON_PRIVATE_STACK=YES`, EmulatorDxe will use a dedicated
native stack for handling emulation. This has some runtime overhead and
is unneccesary for normal operation.

### Building with `MAU_TRY_WITHOUT_MMU=YES`

EmulatorDxe relies on MMU functionality to be present in the host UEFI implementation,
as page-based protection is crucial for seamless thunking from native to emulated code.
`MAU_TRY_WITHOUT_MMU=YES` enables a fallback path via the invalid instruction trap
handler. This works only if the emulated instruction is misaligned from RISC-V perspective
or is an invalid instruction. Needless to say, this is pretty flaky, so this build option
_also_ forces `MAU_WRAPPED_ENTRY_POINTS=YES`, which is necessary to allow certain, but
not all, 3rd party OpRoms and most (but not all) applications to function.

Consider this a non-production grade workaround. Fix your firmware to use the MMU instead.

### Building With `MAU_WRAPPED_ENTRY_POINTS=YES`

If you build with `MAU_WRAPPED_ENTRY_POINTS=YES`, EmulatorDxe will enable an additional
mechanism for invoking certain emulated client code from native code. It's a more
efficient mechanism than relying on no-executable protection and CPU traps, but it
only works for interfaces that EmulatorDxe is aware of.

Note: this is always forced on if you enable `MAU_TRY_WITHOUT_MMU=YES`.

#### UEFI Interfaces Supported

| Service | Comments |
| - | ------------ |
| EFI Events | Events with callbacks created via CreateEvent and CreateEventEx Boot Services |
| EMU_TEST_PROTOCOL | Emulated callbacks for the internal EmulatorDxe interface used by EmulatorTest |

#### RISC-V Notes

As of December 9th 2023, RISC-V builds still default to `MAU_TRY_WITHOUT_MMU=YES`
and `MAU_WRAPPED_ENTRY_POINTS=YES`. This is still necessary as not all
TianoCore EDK2 platforms enable the upstreamed MMU support.

### X64 Binaries

The `MAU_SUPPORTS_X64_BINS` option is set to `YES` by default.

Note: it doesn't make sense to set `MAU_SUPPORTS_X64_BINS=NO` on Aarch64
builds, as that is all the emulator supports on this architecture.

### AARCH64 Binaries On RISC-V

A RISC-V build can include AArch64 support when built with the
`MAU_SUPPORTS_AARCH64_BINS=YES` option.

Note: this will increase the binary size nearly 3x to 1.7MiB, so this option is off by default.

### Building With `MAU_CHECK_ORPHAN_CONTEXTS=YES`

If you build with `MAU_CHECK_ORPHAN_CONTEXTS=YES`, EmulatorDxe will perform
more runtime checks to handle unexpected/non-linear control flow from
native code, that can result in a resource leak inside the emulator.
This is enabled by default for DEBUG builds.

### Building With `MAU_EMU_TIMEOUT_NONE=YES`

Due to the way the Unicorn Engine operates (it is not fully reentrant),
client code is run in a critical section with timers disabled. To avoid hangs
due to emulated tight loops polling on some memory location updated by an
event, the code periodically bails out. If your client binaries are known good,
i.e.  they don't rely on tight polling loops without using UEFI services, you
can also try building with `MAU_EMU_TIMEOUT_NONE=YES` for maximum performance.

Note 1: EmulatorTest will _not_ correctly work with `MAU_EMU_TIMEOUT_NONE=YES`.

### Building With `MAU_EMU_X64_RAZ_WI_PIO=YES`

If you run a DEBUG build of a UEFI implementation that uses the
BaseIoLibIntrinsic (`IoLibNoIo.c`) implementation instead of a more
advanced variant forwarding I/O accesses to PCIe, you may see UEFI
assertions with emulated x64 drivers that attempt port I/O.

Building with `MAU_EMU_X64_RAZ_WI_PIO=YES` will ignore all port I/O writes
and return zeroes for all port I/O reads.

### `MAU_STANDALONE_LOGGING` choices.

You can choose different logging options for standalone builds via
the `MAU_STANDALONE_LOGGING` option in Emulator.dsc.
 * `SERIAL`: use EFI_SERIAL_IO_PROTOCOL. This is the most versatile choice, which works equally well for direct-included firmware builds and for side-loading the driver, but it assumes the presence of a single UART.
 * `CONOUT`: use Console Output device from EFI_SYSTEM_TABLE. Only useful for side-loading, not for direct-included firmware builds.
 * `SBI`:    use SBI console services. Only for RISC-V. 
 * `NONE`:   use nothing. Logging is for wimps - optimize for size and performance!

The default choice is `SERIAL`. If you change this and intend on a
binary-included build, don't forget to regenerate
`Drivers/EmulatorBin/EmulatorDxe.depex`. You can do this with the file
generated as part of a [standalone driver build](#quick-start):

        $ build -a RISCV64 -t GCC -p MultiArchUefiPkg/Emulator.dsc -b RELEASE
        $ cp  `find Build/MultiArchUefiPkg/ | grep depex | sed 1q` MultiArchUefiPkg/Drivers/EmulatorBin/EmulatorDxe.depex
