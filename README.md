# MultiArchUefiPkg

This code implements a DXE driver for EDK2/Tianocore that allows
non-native UEFI boot service drivers and applications to be executed
in 64-bit UEFI environments. In AArch64 environments, EmulatorDxe
supports X64 (aka x86_64, AMD64) UEFI binaries. On RISCV64,
EmulatorDxe can be build with X64 and AARCH64 UEFI binary support.

Today, AArch64 and RISC-V are supported as host environments.

It's derived from https://github.com/ardbiesheuvel/X86EmulatorPkg, yet
is otherwise a reimplementation using https://www.unicorn-engine.org/.
It has competitive performance, portability, support for multiple
emulated ISAs, size (2/3rds the binary size on AArch64) and correctness
in modeling the emulated UEFI Boot Service environment,

## Theory of Operation

UEFI code uses a pretty narrowly-defined ABI, which makes it
easy to thunk x64/AArch64 client code making EFIAPI calls to
native code: no FP/SIMD, no returning large values, etc. E.g. calls like:

        UINT64
        EFIAPI
        Fn(UINT64, UINT64, UINT64, UINT64,
           UINT64, UINT64, UINT64, UINT64,
           UINT64, UINT64, UINT64, UINT64,
           UINT64, UINT64, UINT64, UINT64);

...with up to 16 arguments are supported both client -> native
and native -> client, which covers all UEFI needs.

The emulator presents an x64 and/or AArch64 UEFI Boot Services
environment appropriate for running Boot Service drivers (e.g. OpRom
drivers such as SNP, GOP) and UEFI applications (that aren't OS loaders).
Certain Boot, Runtime and DXE services are filtered or disabled.

## Quick Start

To quickly compile for AArch64:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/MultiArchUefiPkg.git
        $ git submodule update --init
        $ export GCC5_AARCH64_PREFIX=... (if you are on a non-AArch64 system)
        $ build -a AARCH64 -t GCC5 -p MultiArchUefiPkg/Emulator.dsc -b RELEASE

This will produce Build/MultiArchUefiPkg/RELEASE_GCC5/AARCH64/EmulatorDxe.efi

To quickly compile for RISCV64:

        $ git clone https://github.com/tianocore/edk2-staging.git
        $ cd edk2-staging
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/MultiArchUefiPkg.git
        $ git submodule update --init

        Apply any patches under MultiArchUefiPkg/edk2-staging-patches.

        $ export GCC5_RISCV64_PREFIX=... (if you are on a non-RISCV64 system)
        $ build -a RISCV64 -t GCC5 -p MultiArchUefiPkg/Emulator.dsc -b RELEASE

This will produce Build/MultiArchUefiPkg/RELEASE_GCC5/RISCV64/EmulatorDxe.efi

## ArmVirtPkg Firmware with the Bundled Emulator

To quickly compile an ArmVirtPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/MultiArchUefiPkg.git
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

## OvmfPkg RiscVVirt Firmware with the Bundled Emulator

To quickly compile a RiscVVirt OvmfPkg version that contains the emulator, run:

        $ git clone https://github.com/tianocore/edk2.git
        $ cd edk2
        $ git submodule add https://github.com/intel-sandbox/unicorn-for-efi.git
        $ git submodule add https://github.com/intel-sandbox/MultiArchUefiPkg.git
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

## Special Builds

MultiArchUefiPkg uses a port of Project Unicorn to UEFI which is not
yet upstreamed. Beyond UEFI support, the unicorn-for-efi repo also
contains critical fixes to the operation of emulator. Beyond fixes, there
are additional improvements that rely on additional new Unicorn APIs being
made available.

If you build with ON_PRIVATE_STACK=YES, EmulatorDxe will use a dedicated
native stack for handling x64 emulation. This has some runtime overhead and
is unneccesary for normal operation.

If you build with WRAPPED_ENTRY_POINTS=YES, EmulatorDxe will use a
different mechanism for invoking emulated client code, for debugging
situations where the EmulatorInterpreterSyncExceptionCallback machinery
(exception-driven detection of client code execution) doesn't work
(e.g. adding a new host CPU port, running certain x64 apps on RISC-V,
...). Due to maturity of RISC-V UEFI firmware at this time, all
RISC-V builds default to WRAPPED_ENTRY_POINTS=YES.

A RISC-V build can include AArch64 support when built with the
SUPPORTS_AARCH64_BINS=YES option. Note: this will increase the
binary size nearly 2x to 3MiB until LTO support for RISC-V is
added to edk2! This option is off by default.

If you build with CHECK_ORPHAN_CONTEXTS=YES, EmulatorDxe will perform
more runtime checks to handle unexpected/non-linear control flow from
native code, that can result in a resource leak inside the emulator.
This is enabled by default for DEBUG builds.

Due to the way the Unicorn Engine operates, client code is run in a
critical section with timers disabled. To avoid hangs due to emulated
tight loops polling on some memory location updated by an event, the code
periodically bails out. If your client binaries are known good, that is
they don't rely on tight polling loops without using UEFI services, you
can also try building with EMU_TIMEOUT_NONE for maximum performance.

Note 1: EmulatorTest will _not_ correctly work with EMU_TIMEOUT_NONE.

Finally, you can choose to build with BaseDebugLibNull. By default
UefiDebugLibConOut is used to get some reasonable debugging output, but
the extra code generated for DEBUG((...)) macros used for logging does
have some performance impact on the microbenchmarks.

## Modeled Environment

The emulator presents UEFI Boot Services environment appropriate
for running Boot Service drivers (e.g. OpRom drivers such as SNP,
GOP) and UEFI applications (that aren't OS loaders).

EmulatorDxe does trap accesses to certain UEFI APIs and provides
alternate implementations necessary for functionality/correctness,
but does this in a manner transparent to the client code. This can
include Boot/Runtime services and certain UEFI and PI protocols.
This is not exhaustive - raise a GitHub PR if you have a suggestion for
specific interfaces to be filtered.

EFI system tables are passed verbatim to the emulated client code.
For the EFI_SYSTEM_TABLE, the firmware vendor, revision as well
configuration table contents are left untouched. If a client
application accesses SMBIOS, ACPI or Device Tree configuration tables,
it may see unexpected contents (e.g. RISC-V ACPI MADT entries in an
x86-64 application).

### Client Behavior

Discourage use of SetJump and LongJump as this will incur a slow leak for
the lifetime of the client application / driver. Tracked in
https://github.com/intel-sandbox/MultiArchUefiPkg/issues/10.

Discourage reliance on architecture-specific functionality including, but
not limited to:
- access to model ID registers (e.g. cpuid)
- interrupt flag manipulation (e.g. cli/hlt) - these have no effect on the
  host or emulated environment.

Some client code may not be entirely 64-bit clean, making assumptions
about stack being located below 4GiB or allocated memory being below
4GiB (without explicitly requesting such mmemory). Such code will
quickly malfunction on systems where there no or little memory below
the 4GiB line.

### Supported Boot Services (BS)

The EFI_BOOT_SERVICES table is passed verbatim, and reports the host
UEFI Specification revision and host-specific function pointer addresses,
even for functionality that is filtered/adjusted/disabled.

Note: empty comment field below indicates full support.

| Service | Comments |
| :-: | ------------ |
| RaiseTPL | |
| RestoreTPL | |
| AllocatePages | See [notes on memory allocation](#-notes-on-memory-allocation) |
| FreePages | See [notes on memory allocation](#-notes-on-memory-allocation) |
| GetMemoryMap | |
| AllocatePool | See [notes on memory allocation](#-notes-on-memory-allocation) |
| FreePool | See [notes on memory allocation](#-notes-on-memory-allocation) |
| CreateEvent | |
| SetTimer | |
| WaitForEvent | |
| SignalEvent | |
| CloseEvent | |
| CheckEvent | |
| InstallProtocolInterface | |
| ReinstallProtocolInterface | |
| UninstallProtocolInterface | |
| HandleProtocol | |
| RegisterProtocolNotifiy | |
| LocateHandle | |
| LocateDevicePath | |
| InstallConfigurationTable | |
| LoadImage | |
| StartImage | |
| Exit | |
| UnloadImage | |
| ExitBootServices | Returns EFI_UNSUPPORTED |
| GetNextMonotonicCount | |
| Stall | |
| SetWatchdogTimer | |
| ConnectController | |
| DisconnectController | |
| OpenProtocol | |
| CloseProtocol | |
| OpenProtocolInformation | |
| ProtocolsPerHandle | |
| LocateHandleBuffer | |
| LocateProtocol | |
| InstallMultipleProtocolInterfaces | |
| UninstallMultipleProtocolInterfaces | |
| CalculateCrc32 | |
| CopyMem | See [notes on self-modifying code](#-notes-on-self-modifying-code) |
| SetMem | See [notes on self-modifying code](#-notes-on-self-modifying-code) |
| CreateEventEx | |

### Supported Runtime Services (RT)

Boot Service drivers and UEFI applications may make use of RT services
as well. The EFI_RUNTIMET_SERVICES table is passed verbatim, and reports
the host UEFI Specification revision and host-specific function pointer
addresses, even for functionality that is filtered/adjusted/disabled.

Note: empty comment field below indicates full support.

| Service | Comments |
| :-: | ------------ |
| GetTime | |
| SetTime | |
| GetWakeupTime | |
| SetWakeupTime | |
| SetVirtualAddressMap | |
| ConvertPointer | |
| GetVariable | |
| GetNextVariableName | |
| SetVariable | |
| GetNextHighMonotonicCount | |
| ResetSystem | |
| UpdateCapsule | |
| QueryCapsuleCapabilities | |
| QueryVariableInfo | |
### Other Protocols

#### EFI_CPU_ARCH_PROTOCOL

Note: empty comment field below indicates full support.

| Service | Comments |
| :-: | ------------ |
| FlushDataCache |
| EnableInterrupt | Returns EFI_UNSUPPORTED |
| DisableInterrupt | Returns EFI_UNSUPPORTED |
| GetInterruptState | Returns EFI_UNSUPPORTED |
| Init | Returns EFI_UNSUPPORTED |
| GetTimerValue | |
| SetMemoryAttributes | See [notes on memory attributes](#-notes-on-memory-attributes) |
| NumberOfTimers | |
| DmaBufferAlignment | |

#### EFI_MEMORY_ATTRIBUTE_PROTOCOL

Note: empty comment field below indicates full support.

| Service | Comments |
| :-: | ------------ |
| GetMemoryAttributes | See [notes on memory attributes](#-notes-on-memory-attributes) |
| SetMemoryAttributes | See [notes on memory attributes](#-notes-on-memory-attributes) |
| ClearMemoryAttributes | See [notes on memory attributes](#-notes-on-memory-attributes) |

### Notes on Memory Allocation

EmulatorDxe relies on client code being marked as non-executable and
on client code ranges being tracked internally to properly distinguish
between client to native, native to client and client to client control
transfers.

Memory allocated with EfiBootServicesCode/EfiRuntimeServicesCode cannot
be used for control transfers, as EmulatorDxe does not filter AllocatePages
and AllocatePool boot services, and will not add the new ranges to the
list of tracked client memory ranges and will not enforce no-execute
protection on such ranges. Attempts to peform control transfers to such
ranges will cause a crash, as such ranges will be treated as native code.

Tracked in https://github.com/intel-sandbox/MultiArchUefiPkg/issues/7.

### Notes on Memory Attributes

EmulatorDxe relies on client code being marked as non-executable using the
EFI_MEMORY_XP flag. EFI_MEMORY_ATTRIBUTE_PROTOCOL and EFI_CPU_ARCH_PROTOCOL's
SetMemoryAttributes service may be used to by a client program to strip
EFI_MEMORY_XP to client ranges, causing erroneous handling, such
as breaking control transfers from native to emulated client code.

Tracked in https://github.com/intel-sandbox/MultiArchUefiPkg/issues/7.

### Notes on Self-Modifying Code

EmulatorDxe supports self-modifying code by detecting stores to executable
ranges and invalidating JITted blocks for the affected memory. What is not
supported is self-modifying code relying on native services to perform
the modifications. Today this includes the CopyMem and SetMem boot service.

Tracked in https://github.com/intel-sandbox/MultiArchUefiPkg/issues/19.