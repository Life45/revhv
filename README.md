# revhv

revhv is a type-2 Intel x86-64 hypervisor for modern Windows systems. The project exists to make heavily obfuscated or virtualized binaries, especially kernel drivers, easier to analyze when static reversing is too expensive or too blind. It also works well as an initial analysis tool before diving deep into the obfuscated binary. The implementation stays intentionally close to the Intel SDM and focuses on collecting valuable execution data.

The current tracing model is built around control-flow transitions between a monitored address range and the rest of the system. In practice, that means revhv can show which kernel APIs or other external code paths an obfuscated target actually reaches at runtime. 

It is best thought of as a dynamic stepping engine: It breaks in when execution is transferred from the target to any other code in the system in any way, logs it, resumes until execution reaches the target again, then repeats.

## Design goals

- Keep VMX-root state and execution isolated from the host OS as much as possible.
- Stay close to hardware behavior and the Intel SDM.
- Trace control-flow transitions with low enough overhead to be usable against real targets.
- Preserve enough crash state to make failure diagnosable instead of silently resetting the machine.
- Make captured data useful through symbol resolution, module snapshots, and configurable formatting.

## What the repo contains

- `revhv-km`: KMDF driver that enters VMX operation, virtualizes each logical processor, manages EPT state, handles VM-exits, exposes hypercalls, and emits logs and trace data.
- `revhv-um`: usermode controller for command dispatch, symbol resolution, module enumeration, log draining, trace polling, trace configuration, and offline trace parsing.
- `common`: shared trace, hypercall, logging, and export formats used by both components.

## Status and scope

This project is aimed at low-level reversing and runtime analysis for reverse-engineers with some prior knowledge. The current implementation is already useful for discovering cross-boundary execution from opaque targets, but the project is still far from complete and under active development.

I already have a write-up ready about an example analysis utilizing this project, as well as explaining the internals and my thought-process a bit more in detail. It just needs some polishing. When it's done, it'll be available on my github profile.

## Building the project

Other dependencies are added as git submodules under `external` folders per project component, therefore a recursive git clone is advised.

The project is provided as a Visual Studio solution. Only the x64 Debug configuration is currently configured. 

WDK is required to build `revhv-km`.

`revhv-km` driver can be loaded both by traditional methods(when DSE is disabled, eg. test-signing mode) and manual-mapping. However, the PE & NT headers of the driver are used by its memory manager for mapping host page tables, which most manual mappers strip/omit when mapping the driver, resulting in a system crash. Therefore, to use such a manual mapper, necessary implementation changes must be made either to the mapper or to `revhv-km`.

Admin rights are required to use the symbol resolution feature in `revhv-um`.

Both `revhv-km` and `revhv-um` have been tested in Windows 11 & Windows 10 VMWare nested virtualization VMs, as well as on Windows 11 bare metal.

## Isolation from the host OS

Although revhv is a type-2 hypervisor, the VMX-root environment is intentionally separated from the host OS as much as possible.

revhv does not share, for VMX-root execution:

- page tables (all levels)
- GDT
- IDT
- stack
- PAT
- EFER

Only the hypervisor image and the pools it allocates are mapped into the host VMX-root address space. NT kernel APIs are not used in VMX-root paths and are not mapped there.

Other choices:

- dedicated ISTs are used for NMI, `#DF`, and `#MC`
- `GSBASE` is set to zero in host state
- `FSBASE` is used as the current `vcpu` pointer

## Tracing model

The core tracing mechanism uses two EPTP configurations per vCPU:

- `normal execution`: used while code is running outside the monitored range.
- `target execution`: used while code is running inside the monitored range.

When auto-trace is enabled for an address range, revhv programs EPT permissions so an execution transition across that range boundary causes an EPT violation. The VM-exit handler then flips the current EPTP:

- normal -> target: when execution enters the monitored range
- target -> normal: when execution leaves it

On the target -> normal transition, revhv emits a binary trace record into a per-core ring buffer. This produces a control-flow trace of when the monitored code transfers execution to another module or region.

This is intentionally narrower than instruction-by-instruction tracing. The point is to capture boundaries that answer questions such as:

- Which kernel APIs does the packed or virtualized driver actually call?
- What arguments/guest state were present at the transfer point?

The same boundary concept can just as easily be used in reverse to uncover hooks or inbound control-flow into a region, although that is not the current implementation focus.

## Trace data path

Trace logging is split into two separate systems:

- Standard logs: formatted messages emitted through `LOG_INFO`, `LOG_ERROR`, and related macros. These go to a synchronized global ring buffer and optionally to serial COM1.
- Trace logs: high-rate binary entries emitted through `hv::trace::emit`. These are per-vCPU, lock-free, and unsynchronized across cores by design.

Both of these logging mechanisms can be used at any IRQL and from vmx-root.

Usermode starts one polling thread per logical processor and drains each per-core ring buffer through hypercalls into `trace_core_N.bin`. When auto-trace starts, the controller also writes:

- `modules.bin`: snapshot of loaded kernel modules at capture start
- `trace_cfg.bin`: exported capture configuration and optional formatting rules

Offline parsing then:

1. loads `modules.bin`
2. loads `trace_cfg.bin` if present
3. opens all `trace_core_N.bin` files
4. performs a timestamp-ordered k-way merge
5. resolves symbols lazily from module images on disk
6. writes a formatted combined log

### Limitation for offline parsing

Offline parsing has to be done on the same machine that auto-trace logs were captured on, simply because symbol parsing depends on being able to download the PDB of a module from the module list, and it does so by loading the module from disk using its stored full path in modules.bin. On a different machine, the path might not be present at all, or just simply be the wrong version of the module.

Future versions of revhv will address this limitation.

## Configurable capture

Trace entries are not hard-coded to a single layout. revhv supports:

- a generic transition configuration applied by default
- exact-address overrides keyed by guest RIP
- optional custom format strings for the offline parser

The default generic configuration captures:

- `rip`
- `retaddr`

Additional fields can be configured per capture point, including:

- `rsp`, `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`
- `r8` through `r15`
- `retaddr`

Example capture rule:

```text
at config exact nt!NtOpenFile rip retaddr rcx rdx r8 r9
```

Example offline formatting rule:

```text
at config fmt exact nt!ExFreePool "{retaddr} -> {rip}(pool = {rcx:x})"
```

That distinction is important: `revhv-km` records raw data only for performance reasons, `revhv-um` decides how to render it later. It can't render data that has not been recorded.

## Exception handling without SEH

`revhv-km` does not rely on SEH in VMX-root mode.

Instead, it uses a small explicit exception catcher:

- `R14` holds the address of an instruction expected to fault.
- `R15` holds the recovery address.
- when selected host exceptions occur, the trap handler checks whether `RIP == R14`
- if so, exception details are saved into the current `vcpu`
- execution resumes at `R15`

This is used for operations where the hypervisor deliberately attempts fault-prone instructions and needs exception handling.

At present, `#UD` and `#GP` are handled this way. Other unexpected host exceptions are treated as fatal.

## Unrecoverable host errors

When the hypervisor encounters an unrecoverable host error, the goal is to stop in a way that preserves diagnostic state and avoids triple-faults or resets.

The raising core does the following:

1. devirtualizes itself
2. switches back to a valid host code segment context
3. marks crash-in-progress atomically
4. sends NMIs to all other logical processors through x2APIC or xAPIC
5. waits for all other cores to acknowledge
6. calls `KeBugCheckEx(MANUALLY_INITIATED_CRASH, 'rvhv', ...)`

Responding cores do the following:

1. take the NMI on a dedicated IST (if executing in vmx-root at the time), or perform a guest NMI VMEXIT (if executing in vmx non-root at the time)
2. detect that crash handling is in progress
3. devirtualize themselves
4. increment the crash acknowledgement count
5. unblock NMIs with a crafted `IRETQ` frame
6. spin with interrupts enabled until the initiating core bugchecks

This behavior ensures a deterministic fail path where Windows can take control of all cores when `KeBugCheckEx` is called, and allows it to capture a crash dump. This dump also contains all regular logs of the hypervisor to provide valuable info for the reason of the crash (This is only the case when full memory dumps are enabled in Windows settings).

A known limitation is when an unrecoverable error happens at early startup stage. This specifically happens when a virtualized core raises an unrecoverable error while all cores have not launched yet. When the raising core sends the NMI, the core that did not launch yet receives the NMI through Windows' IDT. This usually results in a `NMI_HARDWARE_FAILURE` bugcheck, as Windows is not expecting an NMI at that time.

## Stealth and timing behavior

revhv's stealth implementation for timing checks is currently simple but enough for most uses.

Current measures include:

- VM-exit MSR-store and VM-entry MSR-load handling for `IA32_MPERF`, `IA32_APERF`, and `IA32_TIME_STAMP_COUNTER`
- VMCS handling for `IA32_PERF_GLOBAL_CTRL`
- TSC offset compensation to counter VM-exit and VM-entry overhead
- VMX preemption timer based resynchronization to limit cross-core TSC drift

`IA32_TIME_STAMP_COUNTER` is saved on VM-exit but not loaded on VM-entry. Instead, revhv measures the relevant overhead and adjusts the VMCS TSC offset before `VMRESUME`.

The core idea is:

```text
desired_tsc = stored_tsc + native_instruction_overhead - vmentry_overhead - vmexit_to_store_overhead
tsc_offset -= (rdtsc() - desired_tsc)
```

The benchmark path runs through a fast assembly path in the VM-exit stub so the C++ handler does not contaminate the measurement more than necessary.

Because invariant TSC still becomes desynchronized once each core is being adjusted independently, revhv periodically resynchronizes through the VMX preemption timer. Without that, Windows behavior becomes erratic.

A known limitation for `PMCs` and `MPERF/APERF` is the overhead of internal operations performed by the CPU itself until saving these MSRs on VMEXIT, and between loading them back on VMRESUME - landing on the next guest instruction boundary. This is handled by the benchmarking method and the provided formula for `TSC`, and it could theoretically be applied for others as well.

## Usermode controller

`revhv-um` provides most of the workflow around the hypervisor:

- detects whether the hypervisor is present on all cores
- resolves symbols and addresses
- enumerates loaded kernel modules even when the hypervisor is absent
- reads kernel memory through hypercalls when the hypervisor is active
- drains standard hypervisor logs into a local file
- controls auto-trace enable and disable
- snapshots module state and trace configuration when a trace starts
- drains per-core trace buffers into binary files
- parses raw traces offline with symbol resolution and custom formatting

A practical consequence is that some commands remain useful without VMX at all. Offline and symbol-oriented commands still work when the hypervisor is not present.

## Commands

Some commands are intentionally in *WinDbg fashion* for familiarity.

### General

- `help` or `?`
  - Show the general command list.

- `q`, `quit`, `exit`
  - Exit the controller.

### Symbol and module workflows

- `ln <address|symbol>`
  - Resolve an address to the nearest symbol, or a symbol expression to an address.
  - Supported forms include `module`, `module+offset`, `module!symbol+offset`, and `module:section+offset`.
  - Examples:

```text
ln nt!MmCopyMemory+0x100
ln nt:PAGE+0x123
ln 0xfffff80312345678
```

- `lm [filter]`
  - List loaded kernel modules.
  - Works even without the hypervisor.
  - Examples:

```text
lm
lm nt
```

- `lm export <filename>`
  - Export the current module list for offline use.
  - Example:

```text
lm export modules.bin
```

### Memory inspection

- `db`, `dw`, `dd`, `dq`, `dp`
  - Dump guest virtual memory through hypercalls.
  - `db`: bytes
  - `dw`: words
  - `dd`: dwords
  - `dq`: qwords
  - `dp`: pointers, with symbol resolution for pointed-to addresses
  - Forms:

```text
db <address|symbol> [count] [target_cr3]
dw <address|symbol> [count] [target_cr3]
dd <address|symbol> [count] [target_cr3]
dq <address|symbol> [count] [target_cr3]
dp <address|symbol> [count] [target_cr3]
```

Examples:

```text
db nt!MmCopyMemory 40
dd fffff80312340000 20
dp ntoskrnl!KeBugCheckEx+20 8
dq nt:PAGE+123
```

### Auto-trace

- `at enable <address|symbol> <size> [output_dir]`
  - Enable auto-trace for an address range.
  - Starts per-core trace polling threads.
  - Saves `modules.bin` and `trace_cfg.bin` into the output directory.
  - Examples:

```text
at enable nt!NtCreateFile 20
at enable fffff80312345678 100 C:\traces
```

- `at disable`
  - Stop polling, flush remaining entries, and restore normal EPT state.
  - Example:

```text
at disable
```

- `at config generic <f0> [f1] [f2] [f3] [f4] [f5]`
  - Set the default field map used for transition captures.
  - Example:

```text
at config generic rip retaddr
```

- `at config exact <address|symbol> <f0> [f1] [f2] [f3] [f4] [f5]`
  - Override the capture map for one exact guest RIP.
  - Example:

```text
at config exact nt!NtOpenFile rip retaddr rcx rdx r8 r9
```

- `at config fmt generic "<format>"`
  - Set the default output format for offline trace parsing.
  - Example:

```text
at config fmt generic "{rip} {retaddr}"
```

- `at config fmt exact <address|symbol> "<format>"`
  - Set a per-address formatting rule.
  - Example:

```text
at config fmt exact nt!ExFreePool "{retaddr} -> {rip}(pool = {rcx:x})"
```

- `at config fmt clear generic`
- `at config fmt clear exact <address|symbol>`
  - Remove previously configured formatting rules.
  - Example:

```text
at config fmt clear exact nt!ExFreePool
```

- `at config export <path>`
  - Write the current configuration to disk for offline parsing.
  - Example:

```text
at config export trace_cfg.bin
```

### Offline trace parsing

- `trace parse <modules.bin> <trace_dir> [output_file]`
  - Parse all `trace_core_N.bin` files in a directory.
  - Merge them by timestamp.
  - Resolve symbols from module files on disk.
  - Apply exported formatting rules.
  - Does not require the hypervisor.
  - Examples:

```text
trace parse modules.bin .\traces
trace parse modules.bin .\traces combined.log
```

### Miscellaneous

- `apic`
  - Query APIC information through the hypervisor.

- `df`
  - Deliberately trigger a host double fault path for testing.
  - This is expected to crash the system, but not freeze it. The goal is to test ISTs and the unrecoverable error mechanism.

## Typical workflow

One straightforward workflow looks like this:

1. Start `revhv-um` and verify the hypervisor is present.
2. Use `ln` and `lm` to identify the target module and address range.
3. Set a generic or exact capture configuration.
4. Enable auto-trace for the target range.
5. Exercise the target.
6. Disable auto-trace.
7. Parse the resulting trace directory.

The following is a partial extracted log(some parts removed for readability, formatting configured to only show RIP) from an example run on a heavily virtualized commercial anti-cheat driver, showing a small part of its unload routine:

```text
...
[core 8] ntoskrnl!KeAcquireGuardedMutex
[core 8] ntoskrnl!KeReleaseGuardedMutex
[core 8] ntoskrnl!NtClose
[core 8] ntoskrnl!ObfDereferenceObject
[core 8] ntoskrnl!ExFreePoolWithTag
[core 8] ntoskrnl!PsSetCreateProcessNotifyRoutineEx
[core 8] ntoskrnl!PsRemoveCreateThreadNotifyRoutine
[core 8] ntoskrnl!PsRemoveLoadImageNotifyRoutine
[core 8] ntoskrnl!ObUnRegisterCallbacks
...
[core 8] ntoskrnl!SeUnregisterImageVerificationCallback
[core 8] ntoskrnl!CmUnRegisterCallback
...
[core 8] ntoskrnl!KeSetEvent
[core 10] ntoskrnl!KeResetEvent
[core 8] ntoskrnl!KeSetEvent
[core 8] ntoskrnl!KeWaitForSingleObject
[core 10] ntoskrnl!KeAcquireGuardedMutex
[core 10] ntoskrnl!KeReleaseGuardedMutex
[core 10] ntoskrnl!PsTerminateSystemThread
...
```

## Acknowledgements

Idea of making an Intel hypervisor from scratch was inspired by `https://github.com/jonomango/hv`.