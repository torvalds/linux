// SPDX-License-Identifier: GPL-2.0
/*
 * Non-trivial C macros cannot be used in Rust. Similarly, inlined C functions
 * cannot be called either. This file explicitly creates functions ("helpers")
 * that wrap those so that they can be called from Rust.
 *
 * Sorted alphabetically.
 */

#include <linux/compiler_types.h>

#ifdef __BINDGEN__
// Omit `inline` for bindgen as it ignores inline functions.
#define __rust_helper
#else
// The helper functions are all inline functions.
//
// We use `__always_inline` here to bypass LLVM inlining checks, in case the
// helpers are inlined directly into Rust CGUs.
//
// The LLVM inlining checks are false positives:
// * LLVM doesn't want to inline functions compiled with
//   `-fno-delete-null-pointer-checks` with code compiled without.
//   The C CGUs all have this enabled and Rust CGUs don't. Inlining is okay
//   since this is one of the hardening features that does not change the ABI,
//   and we shouldn't have null pointer dereferences in these helpers.
// * LLVM doesn't want to inline functions with different list of builtins. C
//   side has `-fno-builtin-wcslen`; `wcslen` is not a Rust builtin, so they
//   should be compatible, but LLVM does not perform inlining due to attributes
//   mismatch.
// * clang and Rust doesn't have the exact target string. Clang generates
//   `+cmov,+cx8,+fxsr` but Rust doesn't enable them (in fact, Rust will
//   complain if `-Ctarget-feature=+cmov,+cx8,+fxsr` is used). x86-64 always
//   enable these features, so they are in fact the same target string, but
//   LLVM doesn't understand this and so inlining is inhibited. This can be
//   bypassed with `--ignore-tti-inline-compatible`, but this is a hidden
//   option.
#define __rust_helper __always_inline
#endif

#include "atomic.c"
#include "atomic_ext.c"
#include "auxiliary.c"
#include "barrier.c"
#include "binder.c"
#include "bitmap.c"
#include "bitops.c"
#include "blk.c"
#include "bug.c"
#include "build_assert.c"
#include "build_bug.c"
#include "clk.c"
#include "completion.c"
#include "cpu.c"
#include "cpufreq.c"
#include "cpumask.c"
#include "cred.c"
#include "device.c"
#include "dma.c"
#include "dma-resv.c"
#include "drm.c"
#include "err.c"
#include "irq.c"
#include "fs.c"
#include "gpu.c"
#include "io.c"
#include "jump_label.c"
#include "kunit.c"
#include "list.c"
#include "maple_tree.c"
#include "mm.c"
#include "mutex.c"
#include "of.c"
#include "page.c"
#include "pci.c"
#include "pid_namespace.c"
#include "platform.c"
#include "poll.c"
#include "processor.c"
#include "property.c"
#include "pwm.c"
#include "rbtree.c"
#include "rcu.c"
#include "refcount.c"
#include "regulator.c"
#include "scatterlist.c"
#include "security.c"
#include "signal.c"
#include "slab.c"
#include "spinlock.c"
#include "sync.c"
#include "task.c"
#include "time.c"
#include "uaccess.c"
#include "usb.c"
#include "vmalloc.c"
#include "wait.c"
#include "workqueue.c"
#include "xarray.c"
