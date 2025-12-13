// SPDX-License-Identifier: GPL-2.0

//! Tasks (threads and processes).
//!
//! C header: [`include/linux/sched.h`](srctree/include/linux/sched.h).

use crate::{
    bindings,
    ffi::{c_int, c_long, c_uint},
    mm::MmWithUser,
    pid_namespace::PidNamespace,
    sync::aref::ARef,
    types::{NotThreadSafe, Opaque},
};
use core::{
    cmp::{Eq, PartialEq},
    ops::Deref,
    ptr,
};

/// A sentinel value used for infinite timeouts.
pub const MAX_SCHEDULE_TIMEOUT: c_long = c_long::MAX;

/// Bitmask for tasks that are sleeping in an interruptible state.
pub const TASK_INTERRUPTIBLE: c_int = bindings::TASK_INTERRUPTIBLE as c_int;
/// Bitmask for tasks that are sleeping in an uninterruptible state.
pub const TASK_UNINTERRUPTIBLE: c_int = bindings::TASK_UNINTERRUPTIBLE as c_int;
/// Bitmask for tasks that are sleeping in a freezable state.
pub const TASK_FREEZABLE: c_int = bindings::TASK_FREEZABLE as c_int;
/// Convenience constant for waking up tasks regardless of whether they are in interruptible or
/// uninterruptible sleep.
pub const TASK_NORMAL: c_uint = bindings::TASK_NORMAL as c_uint;

/// Returns the currently running task.
#[macro_export]
macro_rules! current {
    () => {
        // SAFETY: This expression creates a temporary value that is dropped at the end of the
        // caller's scope. The following mechanisms ensure that the resulting `&CurrentTask` cannot
        // leave current task context:
        //
        // * To return to userspace, the caller must leave the current scope.
        // * Operations such as `begin_new_exec()` are necessarily unsafe and the caller of
        //   `begin_new_exec()` is responsible for safety.
        // * Rust abstractions for things such as a `kthread_use_mm()` scope must require the
        //   closure to be `Send`, so the `NotThreadSafe` field of `CurrentTask` ensures that the
        //   `&CurrentTask` cannot cross the scope in either direction.
        unsafe { &*$crate::task::Task::current() }
    };
}

/// Wraps the kernel's `struct task_struct`.
///
/// # Invariants
///
/// All instances are valid tasks created by the C portion of the kernel.
///
/// Instances of this type are always refcounted, that is, a call to `get_task_struct` ensures
/// that the allocation remains valid at least until the matching call to `put_task_struct`.
///
/// # Examples
///
/// The following is an example of getting the PID of the current thread with zero additional cost
/// when compared to the C version:
///
/// ```
/// let pid = current!().pid();
/// ```
///
/// Getting the PID of the current process, also zero additional cost:
///
/// ```
/// let pid = current!().group_leader().pid();
/// ```
///
/// Getting the current task and storing it in some struct. The reference count is automatically
/// incremented when creating `State` and decremented when it is dropped:
///
/// ```
/// use kernel::{task::Task, sync::aref::ARef};
///
/// struct State {
///     creator: ARef<Task>,
///     index: u32,
/// }
///
/// impl State {
///     fn new() -> Self {
///         Self {
///             creator: ARef::from(&**current!()),
///             index: 0,
///         }
///     }
/// }
/// ```
#[repr(transparent)]
pub struct Task(pub(crate) Opaque<bindings::task_struct>);

// SAFETY: By design, the only way to access a `Task` is via the `current` function or via an
// `ARef<Task>` obtained through the `AlwaysRefCounted` impl. This means that the only situation in
// which a `Task` can be accessed mutably is when the refcount drops to zero and the destructor
// runs. It is safe for that to happen on any thread, so it is ok for this type to be `Send`.
unsafe impl Send for Task {}

// SAFETY: It's OK to access `Task` through shared references from other threads because we're
// either accessing properties that don't change (e.g., `pid`, `group_leader`) or that are properly
// synchronised by C code (e.g., `signal_pending`).
unsafe impl Sync for Task {}

/// Represents the [`Task`] in the `current` global.
///
/// This type exists to provide more efficient operations that are only valid on the current task.
/// For example, to retrieve the pid-namespace of a task, you must use rcu protection unless it is
/// the current task.
///
/// # Invariants
///
/// Each value of this type must only be accessed from the task context it was created within.
///
/// Of course, every thread is in a different task context, but for the purposes of this invariant,
/// these operations also permanently leave the task context:
///
/// * Returning to userspace from system call context.
/// * Calling `release_task()`.
/// * Calling `begin_new_exec()` in a binary format loader.
///
/// Other operations temporarily create a new sub-context:
///
/// * Calling `kthread_use_mm()` creates a new context, and `kthread_unuse_mm()` returns to the
///   old context.
///
/// This means that a `CurrentTask` obtained before a `kthread_use_mm()` call may be used again
/// once `kthread_unuse_mm()` is called, but it must not be used between these two calls.
/// Conversely, a `CurrentTask` obtained between a `kthread_use_mm()`/`kthread_unuse_mm()` pair
/// must not be used after `kthread_unuse_mm()`.
#[repr(transparent)]
pub struct CurrentTask(Task, NotThreadSafe);

// Make all `Task` methods available on `CurrentTask`.
impl Deref for CurrentTask {
    type Target = Task;
    #[inline]
    fn deref(&self) -> &Task {
        &self.0
    }
}

/// The type of process identifiers (PIDs).
pub type Pid = bindings::pid_t;

/// The type of user identifiers (UIDs).
#[derive(Copy, Clone)]
pub struct Kuid {
    kuid: bindings::kuid_t,
}

impl Task {
    /// Returns a raw pointer to the current task.
    ///
    /// It is up to the user to use the pointer correctly.
    #[inline]
    pub fn current_raw() -> *mut bindings::task_struct {
        // SAFETY: Getting the current pointer is always safe.
        unsafe { bindings::get_current() }
    }

    /// Returns a task reference for the currently executing task/thread.
    ///
    /// The recommended way to get the current task/thread is to use the
    /// [`current`] macro because it is safe.
    ///
    /// # Safety
    ///
    /// Callers must ensure that the returned object is only used to access a [`CurrentTask`]
    /// within the task context that was active when this function was called. For more details,
    /// see the invariants section for [`CurrentTask`].
    #[inline]
    pub unsafe fn current() -> impl Deref<Target = CurrentTask> {
        struct TaskRef {
            task: *const CurrentTask,
        }

        impl Deref for TaskRef {
            type Target = CurrentTask;

            fn deref(&self) -> &Self::Target {
                // SAFETY: The returned reference borrows from this `TaskRef`, so it cannot outlive
                // the `TaskRef`, which the caller of `Task::current()` has promised will not
                // outlive the task/thread for which `self.task` is the `current` pointer. Thus, it
                // is okay to return a `CurrentTask` reference here.
                unsafe { &*self.task }
            }
        }

        TaskRef {
            // CAST: The layout of `struct task_struct` and `CurrentTask` is identical.
            task: Task::current_raw().cast(),
        }
    }

    /// Returns a raw pointer to the task.
    #[inline]
    pub fn as_ptr(&self) -> *mut bindings::task_struct {
        self.0.get()
    }

    /// Returns the group leader of the given task.
    pub fn group_leader(&self) -> &Task {
        // SAFETY: The group leader of a task never changes after initialization, so reading this
        // field is not a data race.
        let ptr = unsafe { *ptr::addr_of!((*self.as_ptr()).group_leader) };

        // SAFETY: The lifetime of the returned task reference is tied to the lifetime of `self`,
        // and given that a task has a reference to its group leader, we know it must be valid for
        // the lifetime of the returned task reference.
        unsafe { &*ptr.cast() }
    }

    /// Returns the PID of the given task.
    pub fn pid(&self) -> Pid {
        // SAFETY: The pid of a task never changes after initialization, so reading this field is
        // not a data race.
        unsafe { *ptr::addr_of!((*self.as_ptr()).pid) }
    }

    /// Returns the UID of the given task.
    #[inline]
    pub fn uid(&self) -> Kuid {
        // SAFETY: It's always safe to call `task_uid` on a valid task.
        Kuid::from_raw(unsafe { bindings::task_uid(self.as_ptr()) })
    }

    /// Returns the effective UID of the given task.
    #[inline]
    pub fn euid(&self) -> Kuid {
        // SAFETY: It's always safe to call `task_euid` on a valid task.
        Kuid::from_raw(unsafe { bindings::task_euid(self.as_ptr()) })
    }

    /// Determines whether the given task has pending signals.
    #[inline]
    pub fn signal_pending(&self) -> bool {
        // SAFETY: It's always safe to call `signal_pending` on a valid task.
        unsafe { bindings::signal_pending(self.as_ptr()) != 0 }
    }

    /// Returns task's pid namespace with elevated reference count
    #[inline]
    pub fn get_pid_ns(&self) -> Option<ARef<PidNamespace>> {
        // SAFETY: By the type invariant, we know that `self.0` is valid.
        let ptr = unsafe { bindings::task_get_pid_ns(self.as_ptr()) };
        if ptr.is_null() {
            None
        } else {
            // SAFETY: `ptr` is valid by the safety requirements of this function. And we own a
            // reference count via `task_get_pid_ns()`.
            // CAST: `Self` is a `repr(transparent)` wrapper around `bindings::pid_namespace`.
            Some(unsafe { ARef::from_raw(ptr::NonNull::new_unchecked(ptr.cast::<PidNamespace>())) })
        }
    }

    /// Returns the given task's pid in the provided pid namespace.
    #[doc(alias = "task_tgid_nr_ns")]
    #[inline]
    pub fn tgid_nr_ns(&self, pidns: Option<&PidNamespace>) -> Pid {
        let pidns = match pidns {
            Some(pidns) => pidns.as_ptr(),
            None => core::ptr::null_mut(),
        };
        // SAFETY: By the type invariant, we know that `self.0` is valid. We received a valid
        // PidNamespace that we can use as a pointer or we received an empty PidNamespace and
        // thus pass a null pointer. The underlying C function is safe to be used with NULL
        // pointers.
        unsafe { bindings::task_tgid_nr_ns(self.as_ptr(), pidns) }
    }

    /// Wakes up the task.
    #[inline]
    pub fn wake_up(&self) {
        // SAFETY: It's always safe to call `wake_up_process` on a valid task, even if the task
        // running.
        unsafe { bindings::wake_up_process(self.as_ptr()) };
    }
}

impl CurrentTask {
    /// Access the address space of the current task.
    ///
    /// This function does not touch the refcount of the mm.
    #[inline]
    pub fn mm(&self) -> Option<&MmWithUser> {
        // SAFETY: The `mm` field of `current` is not modified from other threads, so reading it is
        // not a data race.
        let mm = unsafe { (*self.as_ptr()).mm };

        if mm.is_null() {
            return None;
        }

        // SAFETY: If `current->mm` is non-null, then it references a valid mm with a non-zero
        // value of `mm_users`. Furthermore, the returned `&MmWithUser` borrows from this
        // `CurrentTask`, so it cannot escape the scope in which the current pointer was obtained.
        //
        // This is safe even if `kthread_use_mm()`/`kthread_unuse_mm()` are used. There are two
        // relevant cases:
        // * If the `&CurrentTask` was created before `kthread_use_mm()`, then it cannot be
        //   accessed during the `kthread_use_mm()`/`kthread_unuse_mm()` scope due to the
        //   `NotThreadSafe` field of `CurrentTask`.
        // * If the `&CurrentTask` was created within a `kthread_use_mm()`/`kthread_unuse_mm()`
        //   scope, then the `&CurrentTask` cannot escape that scope, so the returned `&MmWithUser`
        //   also cannot escape that scope.
        // In either case, it's not possible to read `current->mm` and keep using it after the
        // scope is ended with `kthread_unuse_mm()`.
        Some(unsafe { MmWithUser::from_raw(mm) })
    }

    /// Access the pid namespace of the current task.
    ///
    /// This function does not touch the refcount of the namespace or use RCU protection.
    ///
    /// To access the pid namespace of another task, see [`Task::get_pid_ns`].
    #[doc(alias = "task_active_pid_ns")]
    #[inline]
    pub fn active_pid_ns(&self) -> Option<&PidNamespace> {
        // SAFETY: It is safe to call `task_active_pid_ns` without RCU protection when calling it
        // on the current task.
        let active_ns = unsafe { bindings::task_active_pid_ns(self.as_ptr()) };

        if active_ns.is_null() {
            return None;
        }

        // The lifetime of `PidNamespace` is bound to `Task` and `struct pid`.
        //
        // The `PidNamespace` of a `Task` doesn't ever change once the `Task` is alive.
        //
        // From system call context retrieving the `PidNamespace` for the current task is always
        // safe and requires neither RCU locking nor a reference count to be held. Retrieving the
        // `PidNamespace` after `release_task()` for current will return `NULL` but no codepath
        // like that is exposed to Rust.
        //
        // SAFETY: If `current`'s pid ns is non-null, then it references a valid pid ns.
        // Furthermore, the returned `&PidNamespace` borrows from this `CurrentTask`, so it cannot
        // escape the scope in which the current pointer was obtained, e.g. it cannot live past a
        // `release_task()` call.
        Some(unsafe { PidNamespace::from_ptr(active_ns) })
    }
}

// SAFETY: The type invariants guarantee that `Task` is always refcounted.
unsafe impl crate::sync::aref::AlwaysRefCounted for Task {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::get_task_struct(self.as_ptr()) };
    }

    #[inline]
    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is nonzero.
        unsafe { bindings::put_task_struct(obj.cast().as_ptr()) }
    }
}

impl Kuid {
    /// Get the current euid.
    #[inline]
    pub fn current_euid() -> Kuid {
        // SAFETY: Just an FFI call.
        Self::from_raw(unsafe { bindings::current_euid() })
    }

    /// Create a `Kuid` given the raw C type.
    #[inline]
    pub fn from_raw(kuid: bindings::kuid_t) -> Self {
        Self { kuid }
    }

    /// Turn this kuid into the raw C type.
    #[inline]
    pub fn into_raw(self) -> bindings::kuid_t {
        self.kuid
    }

    /// Converts this kernel UID into a userspace UID.
    ///
    /// Uses the namespace of the current task.
    #[inline]
    pub fn into_uid_in_current_ns(self) -> bindings::uid_t {
        // SAFETY: Just an FFI call.
        unsafe { bindings::from_kuid(bindings::current_user_ns(), self.kuid) }
    }
}

impl PartialEq for Kuid {
    #[inline]
    fn eq(&self, other: &Kuid) -> bool {
        // SAFETY: Just an FFI call.
        unsafe { bindings::uid_eq(self.kuid, other.kuid) }
    }
}

impl Eq for Kuid {}

/// Annotation for functions that can sleep.
///
/// Equivalent to the C side [`might_sleep()`], this function serves as
/// a debugging aid and a potential scheduling point.
///
/// This function can only be used in a nonatomic context.
///
/// [`might_sleep()`]: https://docs.kernel.org/driver-api/basics.html#c.might_sleep
#[track_caller]
#[inline]
pub fn might_sleep() {
    #[cfg(CONFIG_DEBUG_ATOMIC_SLEEP)]
    {
        let loc = core::panic::Location::caller();
        let file = kernel::file_from_location(loc);

        // SAFETY: `file.as_ptr()` is valid for reading and guaranteed to be nul-terminated.
        unsafe { crate::bindings::__might_sleep(file.as_ptr().cast(), loc.line() as i32) }
    }

    // SAFETY: Always safe to call.
    unsafe { crate::bindings::might_resched() }
}
