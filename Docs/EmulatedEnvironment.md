# UEFI Environment Modeled by MultiArchUefiPkg

The emulator presents UEFI Boot Services environment appropriate
for running Boot Service drivers (e.g. OpRom drivers for video cards, NICs)
and UEFI applications that aren't OS loaders.

EmulatorDxe traps accesses to certain UEFI APIs and provides
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

## Client Behavior

Use of SetJump and LongJump is discouraged, as this may incur a slow
leak for the lifetime of the client application / driver. Tracked in
https://github.com/intel/MultiArchUefiPkg/issues/13.

Reliance on architecture-specific functionality is discouraged, including but
not limited to:
- access to model ID registers (e.g. cpuid)
- raw access to the local timer / TSC instead of using UEFI services
- interrupt flag manipulation (e.g. cli/hlt) - these have no effect on the
  host or emulated environment.

Some client code may not be entirely 64-bit clean, making assumptions
about stack being located below 4GiB or allocated memory being below
4GiB (without explicitly requesting such mmemory). Such code will
quickly malfunction on systems where there no or little memory below
the 4GiB line. See https://github.com/intel/MultiArchUefiPkg/issues/16.

## Supported Boot Services (BS)

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

## Supported Runtime Services (RT)

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

## Other Protocols

### EFI_CPU_ARCH_PROTOCOL

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

### EFI_MEMORY_ATTRIBUTE_PROTOCOL

Note: empty comment field below indicates full support.

| Service | Comments |
| :-: | ------------ |
| GetMemoryAttributes | See [notes on memory attributes](#-notes-on-memory-attributes) |
| SetMemoryAttributes | See [notes on memory attributes](#-notes-on-memory-attributes) |
| ClearMemoryAttributes | See [notes on memory attributes](#-notes-on-memory-attributes) |

## Notes on Memory Allocation

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

Tracked in https://github.com/intel/MultiArchUefiPkg/issues/5.

## Notes on Memory Attributes

EmulatorDxe relies on client code being marked as non-executable using the
EFI_MEMORY_XP flag. EFI_MEMORY_ATTRIBUTE_PROTOCOL and EFI_CPU_ARCH_PROTOCOL's
SetMemoryAttributes service may be used to by a client program to strip
EFI_MEMORY_XP to client ranges, causing erroneous handling, such
as breaking control transfers from native to emulated client code.

Tracked in https://github.com/intel/MultiArchUefiPkg/issues/5.

## Notes on Self-Modifying Code

EmulatorDxe supports self-modifying code by detecting stores to executable
ranges and invalidating JITted blocks for the affected memory. What is not
supported is self-modifying code relying on native services to perform
the modifications. Today this includes the CopyMem and SetMem boot service.

Tracked in https://github.com/intel/MultiArchUefiPkg/issues/7.
