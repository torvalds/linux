// SPDX-License-Identifier: GPL-2.0

//! Extensions to the [`alloc`] crate.

#[cfg(not(test))]
#[cfg(not(testlib))]
mod allocator;
pub mod box_ext;
pub mod vec_ext;

/// Indicates an allocation error.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct AllocError;

/// Flags to be used when allocating memory.
///
/// They can be combined with the operators `|`, `&`, and `!`.
///
/// Values can be used from the [`flags`] module.
#[derive(Clone, Copy)]
pub struct Flags(u32);

impl core::ops::BitOr for Flags {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl core::ops::BitAnd for Flags {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
}

impl core::ops::Not for Flags {
    type Output = Self;
    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}

/// Allocation flags.
///
/// These are meant to be used in functions that can allocate memory.
pub mod flags {
    use super::Flags;

    /// Zeroes out the allocated memory.
    ///
    /// This is normally or'd with other flags.
    pub const __GFP_ZERO: Flags = Flags(bindings::__GFP_ZERO);

    /// Users can not sleep and need the allocation to succeed.
    ///
    /// A lower watermark is applied to allow access to "atomic reserves". The current
    /// implementation doesn't support NMI and few other strict non-preemptive contexts (e.g.
    /// raw_spin_lock). The same applies to [`GFP_NOWAIT`].
    pub const GFP_ATOMIC: Flags = Flags(bindings::GFP_ATOMIC);

    /// Typical for kernel-internal allocations. The caller requires ZONE_NORMAL or a lower zone
    /// for direct access but can direct reclaim.
    pub const GFP_KERNEL: Flags = Flags(bindings::GFP_KERNEL);

    /// The same as [`GFP_KERNEL`], except the allocation is accounted to kmemcg.
    pub const GFP_KERNEL_ACCOUNT: Flags = Flags(bindings::GFP_KERNEL_ACCOUNT);

    /// Ror kernel allocations that should not stall for direct reclaim, start physical IO or
    /// use any filesystem callback.  It is very likely to fail to allocate memory, even for very
    /// small allocations.
    pub const GFP_NOWAIT: Flags = Flags(bindings::GFP_NOWAIT);
}
