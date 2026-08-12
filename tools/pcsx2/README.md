# PCSX2 patches

`ee-tlb-fixes.patch` applies to PCSX2 (developed against `v2.7.159-3-g337daf7ed`)
and is what gets PS2 Linux past `Freeing unused kernel memory: 88k freed` under
emulation. Nothing in it is specific to kernelloader; it is all R5900 MMU
behaviour that PCSX2 had wrong.

```sh
git -C /path/to/pcsx2 apply /path/to/ee-tlb-fixes.patch
cmake --build build --parallel "$(nproc)"
```

## Why any of this was needed

Games map their memory up front, with `tlbwi`, at indices they pick, and never
demand-page. That is the only path PCSX2 had ever been exercised on. Linux uses
the other one — `tlbwr`, demand paging, per-process address spaces — and every
part of it was broken, each fault hidden behind the previous one.

| # | Fault | Effect |
|---|---|---|
| 1 | `COP0.n.Random` was read but never written | Stuck at 0, so every `tlbwr` replaced entry 0. Under the PS2 BIOS `Wired` is 31, so that is a wired entry — the refill handler was destroying the scratchpad mapping on every miss. |
| 2 | No TLB Invalid exception | `TLBL`/`TLBS` always vectored to the refill handler at `0x000`. An entry that is present but has `V=0` must vector to `0x180` instead. |
| 3 | `TLBP` read `EntryHi` through an inverted bitfield union | `VPN2` was taken from the ASID end and `ASID` from the VPN2 end, then compared against a byte address. A probe of a live entry reported "not found". |
| 4 | `MapTLB`/`UnmapTLB` ignored the ASID | The vtlb is one flat space, so every process's mappings went in together and the last writer won — one process read another's pages. |
| 5 | Writes to a `D=0` page raised nothing | `MapTLB` mapped every page read-write regardless of `EntryLo.D`, and the vtlb has no read-only state, so **TLB Modified was an exception PCSX2 could never raise**. Copy-on-write depends on it entirely. |

Numbers 2 and 5 are the same fault in two places: the vtlb records a page as
either mapped or not, and both demand paging and copy-on-write need the states
in between — *present but invalid*, and *present but read-only*. Adding those
is what `vtlb_VMapWriteProtected()` and the refill/invalid split are for.

Number 2 is the one that matters most. Demand paging is built entirely on the
distinction PCSX2 could not represent: the refill handler installs the invalid
PTE it finds, and the *retry* is meant to trap to `0x180` and reach
`do_page_fault`, which allocates the page. Sent back to `0x000` instead, the
handler reinstalled the same zero PTE forever. Measured at the stall: ~10
million faults a second on one address, `EntryLo0`/`EntryLo1` both zero, and
**not one valid entry in the whole 48-entry TLB**.

Each fix only exposed the next. In order, the boot went: stuck in the refill
vector → userspace instructions executing → `INIT: version 2.78 booting`, then
a segmentation violation once init forked → copy-on-write faulting properly
(86 TLB Modified exceptions, at ASID 1, 2 and 3 in succession, on consecutive
pages, with `pc` in `ld.so`) and the kernel idle with 44/48 entries mapped.

## Which address space the vtlb holds

The vtlb can only ever hold one address space, so something has to decide which
one. Reading the ASID out of `EntryHi` at the moment of each map or unmap looks
right and is not: `EntryHi` is not a statement of the running process. `TLBR`
loads it from an entry, and Linux's flush loops put the ASID of whichever `mm`
they are flushing into it while they probe. Filter on it directly and the vtlb
ends up holding a mixture of address spaces with no record of which pages came
from where.

That showed up as a store to address `0x3` — a pointer read out of the wrong
process's page — faulting 76,356 times in a single sample, after the boot had
otherwise reached userspace. `tlbInstalledASID` tracks what is actually
installed instead, and only ever moves through a full 48-entry rebuild, so
whatever is in the vtlb is always exactly one address space.

## Write protection

`vtlb_VMapWriteProtected()` installs a handler for the page rather than a
direct RAM pointer, because a direct pointer serves reads and writes alike.
The handler resolves reads through the guest TLB itself and raises
`EXC_CODE(1)` on any store. Pages are mapped `vaddr`→`vaddr` so the handler
receives the virtual address the exception needs. It also drops any fastmem
mapping for the page: fastmem hands stores straight to host memory with no
handler in the way, so a page that must trap on write cannot have one.

## PCSX2_STRICT_USEG

`memReset()` identity-maps virtual `0x00000000`–`0x1FFFFFFF` onto physical
memory, so a user address the guest has *not* mapped still resolves instead of
faulting. `/sbin/init` is linked at `0x00400000` with its data at `0x10000000`,
both inside that window.

Setting `PCSX2_STRICT_USEG=1` makes the whole user segment fault unless the
guest's own TLB maps it. It is gated on the variable rather than replaced
outright because the identity map is what every game has been tested against.
The PS2 BIOS maps the user segment itself (39 entries, covering `0x00080000`
upwards), so the loader and games still run with it set.

Whether it is still *required* now that the four faults above are fixed has not
been retested — every run since has had it on.

## Before sending any of this upstream

The diff also carries the diagnostic instrumentation that found all of it: a
periodic EE state dump (pc with disassembly, every GPR, CP0, stack, all 48 TLB
entries) and a bounded TLB event trace. That has to be split out first — it is
a debugging aid, not a fix.

One trap worth keeping: `cpuRegs.cycle` is declared `u64` but wraps at 32 bits
in practice. A dump scheduled on an absolute deadline past 2^32 fires once and
never again, which silently produced no post-handoff output at all. Compare a
32-bit delta.
