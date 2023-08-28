// SPDX-License-Identifier: GPL-2.0

//! Work queues.
//!
//! C header: [`include/linux/workqueue.h`](../../../../include/linux/workqueue.h)

use crate::{bindings, types::Opaque};

/// A kernel work queue.
///
/// Wraps the kernel's C `struct workqueue_struct`.
///
/// It allows work items to be queued to run on thread pools managed by the kernel. Several are
/// always available, for example, `system`, `system_highpri`, `system_long`, etc.
#[repr(transparent)]
pub struct Queue(Opaque<bindings::workqueue_struct>);

// SAFETY: Accesses to workqueues used by [`Queue`] are thread-safe.
unsafe impl Send for Queue {}
// SAFETY: Accesses to workqueues used by [`Queue`] are thread-safe.
unsafe impl Sync for Queue {}

impl Queue {
    /// Use the provided `struct workqueue_struct` with Rust.
    ///
    /// # Safety
    ///
    /// The caller must ensure that the provided raw pointer is not dangling, that it points at a
    /// valid workqueue, and that it remains valid until the end of 'a.
    pub unsafe fn from_raw<'a>(ptr: *const bindings::workqueue_struct) -> &'a Queue {
        // SAFETY: The `Queue` type is `#[repr(transparent)]`, so the pointer cast is valid. The
        // caller promises that the pointer is not dangling.
        unsafe { &*(ptr as *const Queue) }
    }

    /// Enqueues a work item.
    ///
    /// This may fail if the work item is already enqueued in a workqueue.
    ///
    /// The work item will be submitted using `WORK_CPU_UNBOUND`.
    pub fn enqueue<W, const ID: u64>(&self, w: W) -> W::EnqueueOutput
    where
        W: RawWorkItem<ID> + Send + 'static,
    {
        let queue_ptr = self.0.get();

        // SAFETY: We only return `false` if the `work_struct` is already in a workqueue. The other
        // `__enqueue` requirements are not relevant since `W` is `Send` and static.
        //
        // The call to `bindings::queue_work_on` will dereference the provided raw pointer, which
        // is ok because `__enqueue` guarantees that the pointer is valid for the duration of this
        // closure.
        //
        // Furthermore, if the C workqueue code accesses the pointer after this call to
        // `__enqueue`, then the work item was successfully enqueued, and `bindings::queue_work_on`
        // will have returned true. In this case, `__enqueue` promises that the raw pointer will
        // stay valid until we call the function pointer in the `work_struct`, so the access is ok.
        unsafe {
            w.__enqueue(move |work_ptr| {
                bindings::queue_work_on(bindings::WORK_CPU_UNBOUND as _, queue_ptr, work_ptr)
            })
        }
    }
}

/// A raw work item.
///
/// This is the low-level trait that is designed for being as general as possible.
///
/// The `ID` parameter to this trait exists so that a single type can provide multiple
/// implementations of this trait. For example, if a struct has multiple `work_struct` fields, then
/// you will implement this trait once for each field, using a different id for each field. The
/// actual value of the id is not important as long as you use different ids for different fields
/// of the same struct. (Fields of different structs need not use different ids.)
///
/// Note that the id is used only to select the right method to call during compilation. It wont be
/// part of the final executable.
///
/// # Safety
///
/// Implementers must ensure that any pointers passed to a `queue_work_on` closure by `__enqueue`
/// remain valid for the duration specified in the guarantees section of the documentation for
/// `__enqueue`.
pub unsafe trait RawWorkItem<const ID: u64> {
    /// The return type of [`Queue::enqueue`].
    type EnqueueOutput;

    /// Enqueues this work item on a queue using the provided `queue_work_on` method.
    ///
    /// # Guarantees
    ///
    /// If this method calls the provided closure, then the raw pointer is guaranteed to point at a
    /// valid `work_struct` for the duration of the call to the closure. If the closure returns
    /// true, then it is further guaranteed that the pointer remains valid until someone calls the
    /// function pointer stored in the `work_struct`.
    ///
    /// # Safety
    ///
    /// The provided closure may only return `false` if the `work_struct` is already in a workqueue.
    ///
    /// If the work item type is annotated with any lifetimes, then you must not call the function
    /// pointer after any such lifetime expires. (Never calling the function pointer is okay.)
    ///
    /// If the work item type is not [`Send`], then the function pointer must be called on the same
    /// thread as the call to `__enqueue`.
    unsafe fn __enqueue<F>(self, queue_work_on: F) -> Self::EnqueueOutput
    where
        F: FnOnce(*mut bindings::work_struct) -> bool;
}

/// Returns the system work queue (`system_wq`).
///
/// It is the one used by `schedule[_delayed]_work[_on]()`. Multi-CPU multi-threaded. There are
/// users which expect relatively short queue flush time.
///
/// Callers shouldn't queue work items which can run for too long.
pub fn system() -> &'static Queue {
    // SAFETY: `system_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_wq) }
}

/// Returns the system high-priority work queue (`system_highpri_wq`).
///
/// It is similar to the one returned by [`system`] but for work items which require higher
/// scheduling priority.
pub fn system_highpri() -> &'static Queue {
    // SAFETY: `system_highpri_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_highpri_wq) }
}

/// Returns the system work queue for potentially long-running work items (`system_long_wq`).
///
/// It is similar to the one returned by [`system`] but may host long running work items. Queue
/// flushing might take relatively long.
pub fn system_long() -> &'static Queue {
    // SAFETY: `system_long_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_long_wq) }
}

/// Returns the system unbound work queue (`system_unbound_wq`).
///
/// Workers are not bound to any specific CPU, not concurrency managed, and all queued work items
/// are executed immediately as long as `max_active` limit is not reached and resources are
/// available.
pub fn system_unbound() -> &'static Queue {
    // SAFETY: `system_unbound_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_unbound_wq) }
}

/// Returns the system freezable work queue (`system_freezable_wq`).
///
/// It is equivalent to the one returned by [`system`] except that it's freezable.
///
/// A freezable workqueue participates in the freeze phase of the system suspend operations. Work
/// items on the workqueue are drained and no new work item starts execution until thawed.
pub fn system_freezable() -> &'static Queue {
    // SAFETY: `system_freezable_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_freezable_wq) }
}

/// Returns the system power-efficient work queue (`system_power_efficient_wq`).
///
/// It is inclined towards saving power and is converted to "unbound" variants if the
/// `workqueue.power_efficient` kernel parameter is specified; otherwise, it is similar to the one
/// returned by [`system`].
pub fn system_power_efficient() -> &'static Queue {
    // SAFETY: `system_power_efficient_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_power_efficient_wq) }
}

/// Returns the system freezable power-efficient work queue (`system_freezable_power_efficient_wq`).
///
/// It is similar to the one returned by [`system_power_efficient`] except that is freezable.
///
/// A freezable workqueue participates in the freeze phase of the system suspend operations. Work
/// items on the workqueue are drained and no new work item starts execution until thawed.
pub fn system_freezable_power_efficient() -> &'static Queue {
    // SAFETY: `system_freezable_power_efficient_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_freezable_power_efficient_wq) }
}
