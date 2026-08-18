// SPDX-License-Identifier: GPL-2.0

//! Memory barriers.
//!
//! These primitives have the same semantics as their C counterparts: and the precise definitions
//! of semantics can be found at [`LKMM`].
//!
//! [`LKMM`]: srctree/tools/memory-model/

#![expect(private_bounds, reason = "sealed implementation")]

/// Memory barrier orderings.
///
/// The semantics of these orderings follows the [`LKMM`] definitions and rules.
///
/// - [`Read`] provides ordering between preceding load operations and succeeding load operations.
/// - [`Write`] provides ordering between preceding store operations and succeeding store
///   operations.
/// - [`Full`] provides ordering between all the preceding memory accesses and succeeding memory
///   accesses.
///
/// [`LKMM`]: srctree/tools/memory-model/
pub mod ordering {
    pub use crate::sync::atomic::ordering::Full;

    /// The annotation type for read-read barrier ordering.
    pub struct Read;

    /// The annotation type for write-write barrier ordering.
    pub struct Write;
}

pub use ordering::{
    Full,
    Read,
    Write, //
};

struct Smp;
struct Dma;

/// A compiler barrier.
///
/// A barrier that prevents compiler from reordering memory accesses across the barrier.
#[inline(always)]
pub(crate) fn barrier() {
    // By default, Rust inline asms are treated as being able to access any memory or flags, hence
    // it suffices as a compiler barrier.
    //
    // SAFETY: An empty asm block.
    unsafe { core::arch::asm!("") };
}

trait MemoryBarrier<Flavour = ()> {
    fn run();
}

macro_rules! define_barrier {
    ($([$flavour:ident])? $ordering:ident, $binding:ident) => {
        impl MemoryBarrier$(<$flavour>)? for $ordering {
            #[inline]
            fn run() {
                // SAFETY: barrier methods are safe to call.
                unsafe { bindings::$binding() };
            }
        }
    };
}

define_barrier!(Full, mb);
define_barrier!(Read, rmb);
define_barrier!(Write, wmb);
define_barrier!([Dma] Full, dma_mb);
define_barrier!([Dma] Read, dma_rmb);
define_barrier!([Dma] Write, dma_wmb);
define_barrier!([Smp] Full, smp_mb);
define_barrier!([Smp] Read, smp_rmb);
define_barrier!([Smp] Write, smp_wmb);

/// Memory barrier.
///
/// A barrier that prevents compiler and CPU from reordering memory accesses across the barrier.
///
/// The specific forms of reordering can be specified using the parameter.
/// - `mb(Read)` provides a read-read barrier.
/// - `mb(Write)` provides a write-write barrier.
/// - `mb(Full)` provides a full barrier.
///
/// # Examples
///
/// ```
/// # use kernel::sync::barrier::*;
/// mb(Read);
/// mb(Write);
/// mb(Full);
/// ```
#[inline]
#[doc(alias = "rmb")]
#[doc(alias = "wmb")]
pub fn mb<T: MemoryBarrier>(_: T) {
    T::run()
}

/// Memory barrier between CPUs.
///
/// A barrier that prevents compiler and CPU from reordering memory accesses across the barrier.
/// Does not prevent re-ordering with respect to other bus-mastering devices.
///
/// See [`mb`] for usage.
#[inline]
#[doc(alias = "smp_rmb")]
#[doc(alias = "smp_wmb")]
pub fn smp_mb<T: MemoryBarrier<Smp>>(_: T) {
    if cfg!(CONFIG_SMP) {
        T::run()
    } else {
        barrier()
    }
}

/// Memory barrier between local CPU and bus-mastering devices.
///
/// A barrier that prevents compiler and CPU from reordering memory accesses across the barrier.
/// Does not prevent re-ordering with respect to other CPUs.
///
/// See [`mb`] for usage.
#[inline]
#[doc(alias = "dma_rmb")]
#[doc(alias = "dma_wmb")]
pub fn dma_mb<T: MemoryBarrier<Dma>>(_: T) {
    T::run()
}
