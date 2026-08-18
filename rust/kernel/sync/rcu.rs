// SPDX-License-Identifier: GPL-2.0

//! RCU support.
//!
//! C header: [`include/linux/rcupdate.h`](srctree/include/linux/rcupdate.h)

use crate::{bindings, types::NotThreadSafe};

/// Evidence that the RCU read side lock is held on the current thread/CPU.
///
/// The type is explicitly not `Send` because this property is per-thread/CPU.
///
/// # Invariants
///
/// The RCU read side lock is actually held while instances of this guard exist.
pub struct Guard(NotThreadSafe);

impl Guard {
    /// Acquires the RCU read side lock and returns a guard.
    #[inline]
    pub fn new() -> Self {
        // SAFETY: An FFI call with no additional requirements.
        unsafe { bindings::rcu_read_lock() };
        // INVARIANT: The RCU read side lock was just acquired above.
        Self(NotThreadSafe)
    }

    /// Explicitly releases the RCU read side lock.
    #[inline]
    pub fn unlock(self) {}
}

impl Default for Guard {
    #[inline]
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Guard {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: By the type invariants, the RCU read side is locked, so it is ok to unlock it.
        unsafe { bindings::rcu_read_unlock() };
    }
}

/// Acquires the RCU read side lock.
#[inline]
pub fn read_lock() -> Guard {
    Guard::new()
}

/// Wait until all in-flight `call_rcu()` callbacks complete.
///
/// Note that this primitive does not necessarily wait for an RCU grace period
/// to complete. For example, if there are no RCU callbacks queued anywhere
/// in the system, then [`rcu_barrier()`] is within its rights to return
/// immediately, without waiting for anything, much less an RCU grace period.
/// In fact, [`rcu_barrier()`] will normally not result in any RCU grace periods
/// beyond those that were already destined to be executed.
///
/// In kernels built with `CONFIG_RCU_LAZY=y`, this function also hurries all
/// pending lazy RCU callbacks.
///
/// Note that this is one of the RCU primitives which must not be called in
/// atomic context.
#[inline]
pub fn rcu_barrier() {
    // SAFETY: `rcu_barrier()` is always safe to be called. It just might wait for a grace period.
    unsafe { bindings::rcu_barrier() };
}

/// Wait for one RCU grace period.
///
/// Waits for all RCU read-side critical sections (such as those established by
/// a [`Guard`]) at the moment of the function call to finish.
///
/// Does not prevent new read-side critical sections from starting, which may
/// begin and run while this call is blocking.
///
/// Note that this is one of the RCU primitives which must not be called in
/// atomic context.
#[inline]
pub fn synchronize_rcu() {
    // SAFETY: `synchronize_rcu()` is always safe to be called from process context.
    unsafe { bindings::synchronize_rcu() };
}
