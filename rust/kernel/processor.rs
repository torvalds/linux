// SPDX-License-Identifier: GPL-2.0

//! Processor related primitives.
//!
//! C header: [`include/linux/processor.h`](srctree/include/linux/processor.h)

/// Lower CPU power consumption or yield to a hyperthreaded twin processor.
///
/// It also happens to serve as a compiler barrier.
#[inline]
pub fn cpu_relax() {
    // SAFETY: Always safe to call.
    unsafe { bindings::cpu_relax() }
}
