# OS Kernel Enhancements in xv6

Kernel-level memory management extensions to the MIT xv6 teaching operating system, developed as part of COL331 (Operating Systems) at IIT Delhi.

## Overview

This project extends xv6 with a page swapping subsystem that enables processes to use more virtual memory than the available physical RAM. When physical memory runs low, the kernel evicts pages to a swap region on disk and brings them back on demand via page fault handling.

Key components:

- **Page swap infrastructure** -- A swap slot table (`pageswap.c`) manages 800 disk-backed slots. Each slot stores one 4 KB page across 8 contiguous 512-byte disk sectors.
- **Victim selection with accessed-bit aging** -- On every timer interrupt, the kernel clears the PTE accessed bit (`PTE_A`) for all user pages, enabling clock-style approximation of LRU. A victim process is selected based on resident set size (`rss`) when free memory falls below a configurable threshold.
- **Page fault handler** -- Trap handler (`trap.c`, case `T_PGFLT`) detects swapped-out pages by reading the swap slot index stored in the PTE, reads the page back from disk, and remaps it into the process address space.
- **RSS tracking** -- Each process tracks its resident set size in `proc.rss`, updated on swap-in and during periodic PTE scans.

## Build and Run

Prerequisites: GCC cross-compiler toolchain for i386 and QEMU.

```bash
cd xv6-public

# Build the kernel and filesystem image
make

# Run in QEMU
make qemu

# Run in QEMU without a graphical window (serial only)
make qemu-nox

# Clean build artifacts
make clean
```

To run the memory stress test:

```
$ memtest
Memtest Passed
```

## Project Structure

```
xv6-public/
  pageswap.c / pageswap.h   Swap slot management and victim selection
  trap.c                     Page fault handler and PTE_A aging on timer tick
  kalloc.c                   Physical page allocator (triggers swap-out when low)
  vm.c                       Virtual memory utilities (walkpgdir, mappages)
  proc.h                     Per-process state (includes rss field)
  memtest.c                  Userspace memory stress test
  Makefile                   Build system targeting i386 / QEMU
```

## Course

COL331 -- Operating Systems, IIT Delhi

## Tech Stack

C, x86 assembly, QEMU, MIT xv6
