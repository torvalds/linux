// SPDX-License-Identifier: GPL-2.0

//! Tasks (threads and processes).
//!
//! C header: [`include/linux/sched.h`](srctree/include/linux/sched.h).

use crate::{
    bindings,
    ffi::{c_int, c_long, c_uint},
    pid_namespace::PidNamespace,
    types::{ARef, NotThreadSafe, Opaque},
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
        // SAFETY: Deref + addr-of below create a temporary `TaskRef` that cannot outlive the
        // caller.
        unsafe { &*$crate::task::Task::current() }
    };
}

/// Returns the currently running task's pid namespace.
#[macro_export]
macro_rules! current_pid_ns {
    () => {
        // SAFETY: Deref + addr-of below create a temporary `PidNamespaceRef` that cannot outlive
        // the caller.
        unsafe { &*$crate::task::Task::current_pid_ns() }
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
/// use kernel::{task::Task, types::ARef};
///
/// struct State {
///     creator: ARef<Task>,
///     index: u32,
/// }
///
/// impl State {
///     fn new() -> Self {
///         Self {
///             creator: current!().into(),
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

/// The type of process identifiers (PIDs).
type Pid = bindings::pid_t;

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
    /// Callers must ensure that the returned object doesn't outlive the current task/thread.
    pub unsafe fn current() -> impl Deref<Target = Task> {
        struct TaskRef<'a> {
            task: &'a Task,
            _not_send: NotThreadSafe,
        }

        impl Deref for TaskRef<'_> {
            type Target = Task;

            fn deref(&self) -> &Self::Target {
                self.task
            }
        }

        let current = Task::current_raw();
        TaskRef {
            // SAFETY: If the current thread is still running, the current task is valid. Given
            // that `TaskRef` is not `Send`, we know it cannot be transferred to another thread
            // (where it could potentially outlive the caller).
            task: unsafe { &*current.cast() },
            _not_send: NotThreadSafe,
        }
    }

    /// Returns a PidNamespace reference for the currently executing task's/thread's pid namespace.
    ///
    /// This function can be used to create an unbounded lifetime by e.g., storing the returned
    /// PidNamespace in a global variable which would be a bug. So the recommended way to get the
    /// current task's/thread's pid namespace is to use the [`current_pid_ns`] macro because it is
    /// safe.
    ///
    /// # Safety
    ///
    /// Callers must ensure that the returned object doesn't outlive the current task/thread.
    pub unsafe fn current_pid_ns() -> impl Deref<Target = PidNamespace> {
        struct PidNamespaceRef<'a> {
            task: &'a PidNamespace,
            _not_send: NotThreadSafe,
        }

        impl Deref for PidNamespaceRef<'_> {
            type Target = PidNamespace;

            fn deref(&self) -> &Self::Target {
                self.task
            }
        }

        // The lifetime of `PidNamespace` is bound to `Task` and `struct pid`.
        //
        // The `PidNamespace` of a `Task` doesn't ever change once the `Task` is alive. A
        // `unshare(CLONE_NEWPID)` or `setns(fd_pidns/pidfd, CLONE_NEWPID)` will not have an effect
        // on the calling `Task`'s pid namespace. It will only effect the pid namespace of children
        // created by the calling `Task`. This invariant guarantees that after having acquired a
        // reference to a `Task`'s pid namespace it will remain unchanged.
        //
        // When a task has exited and been reaped `release_task()` will be called. This will set
        // the `PidNamespace` of the task to `NULL`. So retrieving the `PidNamespace` of a task
        // that is dead will return `NULL`. Note, that neither holding the RCU lock nor holding a
        // referencing count to
        // the `Task` will prevent `release_task()` being called.
        //
        // In order to retrieve the `PidNamespace` of a `Task` the `task_active_pid_ns()` function
        // can be used. There are two cases to consider:
        //
        // (1) retrieving the `PidNamespace` of the `current` task
        // (2) retrieving the `PidNamespace` of a non-`current` task
        //
        // From system call context retrieving the `PidNamespace` for case (1) is always safe and
        // requires neither RCU locking nor a reference count to be held. Retrieving the
        // `PidNamespace` after `release_task()` for current will return `NULL` but no codepath
        // like that is exposed to Rust.
        //
        // Retrieving the `PidNamespace` from system call context for (2) requires RCU protection.
        // Accessing `PidNamespace` outside of RCU protection requires a reference count that
        // must've been acquired while holding the RCU lock. Note that accessing a non-`current`
        // task means `NULL` can be returned as the non-`current` task could have already passed
        // through `release_task()`.
        //
        // To retrieve (1) the `current_pid_ns!()` macro should be used which ensure that the
        // returned `PidNamespace` cannot outlive the calling scope. The associated
        // `current_pid_ns()` function should not be called directly as it could be abused to
        // created an unbounded lifetime for `PidNamespace`. The `current_pid_ns!()` macro allows
        // Rust to handle the common case of accessing `current`'s `PidNamespace` without RCU
        // protection and without having to acquire a reference count.
        //
        // For (2) the `task_get_pid_ns()` method must be used. This will always acquire a
        // reference on `PidNamespace` and will return an `Option` to force the caller to
        // explicitly handle the case where `PidNamespace` is `None`, something that tends to be
        // forgotten when doing the equivalent operation in `C`. Missing RCU primitives make it
        // difficult to perform operations that are otherwise safe without holding a reference
        // count as long as RCU protection is guaranteed. But it is not important currently. But we
        // do want it in the future.
        //
        // Note for (2) the required RCU protection around calling `task_active_pid_ns()`
        // synchronizes against putting the last reference of the associated `struct pid` of
        // `task->thread_pid`. The `struct pid` stored in that field is used to retrieve the
        // `PidNamespace` of the caller. When `release_task()` is called `task->thread_pid` will be
        // `NULL`ed and `put_pid()` on said `struct pid` will be delayed in `free_pid()` via
        // `call_rcu()` allowing everyone with an RCU protected access to the `struct pid` acquired
        // from `task->thread_pid` to finish.
        //
        // SAFETY: The current task's pid namespace is valid as long as the current task is running.
        let pidns = unsafe { bindings::task_active_pid_ns(Task::current_raw()) };
        PidNamespaceRef {
            // SAFETY: If the current thread is still running, the current task and its associated
            // pid namespace are valid. `PidNamespaceRef` is not `Send`, so we know it cannot be
            // transferred to another thread (where it could potentially outlive the current
            // `Task`). The caller needs to ensure that the PidNamespaceRef doesn't outlive the
            // current task/thread.
            task: unsafe { PidNamespace::from_ptr(pidns) },
            _not_send: NotThreadSafe,
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
    pub fn uid(&self) -> Kuid {
        // SAFETY: It's always safe to call `task_uid` on a valid task.
        Kuid::from_raw(unsafe { bindings::task_uid(self.as_ptr()) })
    }

    /// Returns the effective UID of the given task.
    pub fn euid(&self) -> Kuid {
        // SAFETY: It's always safe to call `task_euid` on a valid task.
        Kuid::from_raw(unsafe { bindings::task_euid(self.as_ptr()) })
    }

    /// Determines whether the given task has pending signals.
    pub fn signal_pending(&self) -> bool {
        // SAFETY: It's always safe to call `signal_pending` on a valid task.
        unsafe { bindings::signal_pending(self.as_ptr()) != 0 }
    }

    /// Returns task's pid namespace with elevated reference count
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
    pub fn wake_up(&self) {
        // SAFETY: It's always safe to call `wake_up_process` on a valid task, even if the task
        // running.
        unsafe { bindings::wake_up_process(self.as_ptr()) };
    }
}

// SAFETY: The type invariants guarantee that `Task` is always refcounted.
unsafe impl crate::types::AlwaysRefCounted for Task {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::get_task_struct(self.as_ptr()) };
    }

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
