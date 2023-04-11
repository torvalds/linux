// SPDX-License-Identifier: GPL-2.0

//! Tasks (threads and processes).
//!
//! C header: [`include/linux/sched.h`](../../../../include/linux/sched.h).

use crate::{bindings, types::Opaque};
use core::ptr;

/// Wraps the kernel's `struct task_struct`.
///
/// # Invariants
///
/// All instances are valid tasks created by the C portion of the kernel.
///
/// Instances of this type are always ref-counted, that is, a call to `get_task_struct` ensures
/// that the allocation remains valid at least until the matching call to `put_task_struct`.
#[repr(transparent)]
pub struct Task(pub(crate) Opaque<bindings::task_struct>);

// SAFETY: It's OK to access `Task` through references from other threads because we're either
// accessing properties that don't change (e.g., `pid`, `group_leader`) or that are properly
// synchronised by C code (e.g., `signal_pending`).
unsafe impl Sync for Task {}

/// The type of process identifiers (PIDs).
type Pid = bindings::pid_t;

impl Task {
    /// Returns the group leader of the given task.
    pub fn group_leader(&self) -> &Task {
        // SAFETY: By the type invariant, we know that `self.0` is a valid task. Valid tasks always
        // have a valid group_leader.
        let ptr = unsafe { *ptr::addr_of!((*self.0.get()).group_leader) };

        // SAFETY: The lifetime of the returned task reference is tied to the lifetime of `self`,
        // and given that a task has a reference to its group leader, we know it must be valid for
        // the lifetime of the returned task reference.
        unsafe { &*ptr.cast() }
    }

    /// Returns the PID of the given task.
    pub fn pid(&self) -> Pid {
        // SAFETY: By the type invariant, we know that `self.0` is a valid task. Valid tasks always
        // have a valid pid.
        unsafe { *ptr::addr_of!((*self.0.get()).pid) }
    }

    /// Determines whether the given task has pending signals.
    pub fn signal_pending(&self) -> bool {
        // SAFETY: By the type invariant, we know that `self.0` is valid.
        unsafe { bindings::signal_pending(self.0.get()) != 0 }
    }

    /// Wakes up the task.
    pub fn wake_up(&self) {
        // SAFETY: By the type invariant, we know that `self.0.get()` is non-null and valid.
        // And `wake_up_process` is safe to be called for any valid task, even if the task is
        // running.
        unsafe { bindings::wake_up_process(self.0.get()) };
    }
}

// SAFETY: The type invariants guarantee that `Task` is always ref-counted.
unsafe impl crate::types::AlwaysRefCounted for Task {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::get_task_struct(self.0.get()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is nonzero.
        unsafe { bindings::put_task_struct(obj.cast().as_ptr()) }
    }
}
