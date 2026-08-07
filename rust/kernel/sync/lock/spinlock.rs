// SPDX-License-Identifier: GPL-2.0

//! A kernel spinlock.
//!
//! This module allows Rust code to use the kernel's `spinlock_t`.
use super::*;
use crate::{
    interrupt::LocalInterruptDisabled,
    prelude::*, //
};

/// Creates a [`SpinLock`] initialiser with the given name and a newly-created lock class.
///
/// It uses the name if one is given, otherwise it generates one based on the file name and line
/// number.
#[macro_export]
macro_rules! new_spinlock {
    ($inner:expr $(, $name:literal)? $(,)?) => {
        $crate::sync::SpinLock::new(
            $inner, $crate::optional_name!($($name)?), $crate::static_lock_class!())
    };
}
pub use new_spinlock;

/// A spinlock.
///
/// Exposes the kernel's [`spinlock_t`]. When multiple CPUs attempt to lock the same spinlock, only
/// one at a time is allowed to progress, the others will block (spinning) until the spinlock is
/// unlocked, at which point another CPU will be allowed to make progress.
///
/// Instances of [`SpinLock`] need a lock class and to be pinned. The recommended way to create such
/// instances is with the [`pin_init`](pin_init::pin_init) and [`new_spinlock`] macros.
///
/// # Examples
///
/// The following example shows how to declare, allocate and initialise a struct (`Example`) that
/// contains an inner struct (`Inner`) that is protected by a spinlock.
///
/// ```
/// use kernel::sync::{new_spinlock, SpinLock};
///
/// struct Inner {
///     a: u32,
///     b: u32,
/// }
///
/// #[pin_data]
/// struct Example {
///     c: u32,
///     #[pin]
///     d: SpinLock<Inner>,
/// }
///
/// impl Example {
///     fn new() -> impl PinInit<Self> {
///         pin_init!(Self {
///             c: 10,
///             d <- new_spinlock!(Inner { a: 20, b: 30 }),
///         })
///     }
/// }
///
/// // Allocate a boxed `Example`.
/// let e = KBox::pin_init(Example::new(), GFP_KERNEL)?;
/// assert_eq!(e.c, 10);
/// assert_eq!(e.d.lock().a, 20);
/// assert_eq!(e.d.lock().b, 30);
/// # Ok::<(), Error>(())
/// ```
///
/// The following example shows how to use interior mutability to modify the contents of a struct
/// protected by a spinlock despite only having a shared reference:
///
/// ```
/// use kernel::sync::SpinLock;
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// fn example(m: &SpinLock<Example>) {
///     let mut guard = m.lock();
///     guard.a += 10;
///     guard.b += 20;
/// }
/// ```
///
/// [`spinlock_t`]: srctree/include/linux/spinlock.h
pub type SpinLock<T> = Lock<T, SpinLockBackend>;

/// A kernel `spinlock_t` lock backend.
pub struct SpinLockBackend;

/// A [`Guard`] acquired from locking a [`SpinLock`].
///
/// This is simply a type alias for a [`Guard`] returned from locking a [`SpinLock`]. It will unlock
/// the [`SpinLock`] upon being dropped.
pub type SpinLockGuard<'a, T> = Guard<'a, T, SpinLockBackend>;

// SAFETY: The underlying kernel `spinlock_t` object ensures mutual exclusion. `relock` uses the
// default implementation that always calls the same locking method.
unsafe impl Backend for SpinLockBackend {
    type State = bindings::spinlock_t;
    type GuardState = ();

    #[inline]
    unsafe fn init(
        ptr: *mut Self::State,
        name: *const crate::ffi::c_char,
        key: *mut bindings::lock_class_key,
    ) {
        // SAFETY: The safety requirements ensure that `ptr` is valid for writes, and `name` and
        // `key` are valid for read indefinitely.
        unsafe { bindings::__spin_lock_init(ptr, name, key) }
    }

    #[inline]
    unsafe fn lock(ptr: *mut Self::State) -> Self::GuardState {
        // SAFETY: The safety requirements of this function ensure that `ptr` points to valid
        // memory, and that it has been initialised before.
        unsafe { bindings::spin_lock(ptr) }
    }

    #[inline]
    unsafe fn unlock(ptr: *mut Self::State, _guard_state: &Self::GuardState) {
        // SAFETY: The safety requirements of this function ensure that `ptr` is valid and that the
        // caller is the owner of the spinlock.
        unsafe { bindings::spin_unlock(ptr) }
    }

    #[inline]
    unsafe fn try_lock(ptr: *mut Self::State) -> Option<Self::GuardState> {
        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use.
        let result = unsafe { bindings::spin_trylock(ptr) };

        if result != 0 {
            Some(())
        } else {
            None
        }
    }

    #[inline]
    unsafe fn assert_is_held(ptr: *mut Self::State) {
        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use.
        unsafe { bindings::spin_assert_is_held(ptr) }
    }
}

/// Creates a [`SpinLockIrq`] initialiser with the given name and a newly-created lock class.
///
/// It uses the name if one is given, otherwise it generates one based on the file name and line
/// number.
#[macro_export]
macro_rules! new_spinlock_irq {
    ($inner:expr $(, $name:literal)? $(,)?) => {
        $crate::sync::SpinLockIrq::new(
            $inner, $crate::optional_name!($($name)?), $crate::static_lock_class!())
    };
}
pub use new_spinlock_irq;

/// A variant of `SpinLock` that ensures interrupts are disabled in the critical section.
///
/// This lock can be acquired in two ways:
///
/// - Using [`lock()`] like any other type of lock, in which case the bindings will modify the
///   interrupt state to ensure that local processor interrupts remain disabled for at least as
///   long as the [`SpinLockIrqGuard`] exists.
/// - Using [`lock_with()`] in contexts where a [`LocalInterruptDisabled`] token is present and
///   local processor interrupts are already known to be disabled, in which case the local
///   interrupt state will not be touched. This method should be preferred if a
///   [`LocalInterruptDisabled`] token is present in the scope.
///
/// For more info on spinlocks, see [`SpinLock`]. For more information on interrupts,
/// [see the interrupt module](kernel::interrupt).
///
/// # Examples
///
/// The following example shows how to declare, allocate initialise and access a struct (`Example`)
/// that contains an inner struct (`Inner`) that is protected by a spinlock that requires local
/// processor interrupts to be disabled.
///
/// ```
/// use kernel::sync::{new_spinlock_irq, SpinLockIrq};
///
/// struct Inner {
///     a: u32,
///     b: u32,
/// }
///
/// #[pin_data]
/// struct Example {
///     #[pin]
///     c: SpinLockIrq<Inner>,
///     #[pin]
///     d: SpinLockIrq<Inner>,
/// }
///
/// impl Example {
///     fn new() -> impl PinInit<Self> {
///         pin_init!(Self {
///             c <- new_spinlock_irq!(Inner { a: 0, b: 10 }),
///             d <- new_spinlock_irq!(Inner { a: 20, b: 30 }),
///         })
///     }
/// }
///
/// // Allocate a boxed `Example`
/// let e = KBox::pin_init(Example::new(), GFP_KERNEL)?;
///
/// // Accessing an `Example` from a context where interrupts may not be disabled already.
/// let c_guard = e.c.lock(); // interrupts are disabled now, +1 interrupt disable refcount
/// let d_guard = e.d.lock(); // no interrupt state change, +1 interrupt disable refcount
///
/// assert_eq!(c_guard.a, 0);
/// assert_eq!(c_guard.b, 10);
/// assert_eq!(d_guard.a, 20);
/// assert_eq!(d_guard.b, 30);
///
/// drop(c_guard); // Dropping c_guard will not re-enable interrupts just yet, since d_guard is
///                // still in scope.
/// drop(d_guard); // Last interrupt disable reference dropped here, so interrupts are re-enabled
///                // now
/// # Ok::<(), Error>(())
/// ```
///
/// The next example demonstrates locking a [`SpinLockIrq`] using [`lock_with()`] in a function
/// which can only be called when local processor interrupts are already disabled.
///
/// ```
/// use kernel::sync::{new_spinlock_irq, SpinLockIrq};
/// use kernel::interrupt::*;
///
/// struct Inner {
///     a: u32,
/// }
///
/// #[pin_data]
/// struct Example {
///     #[pin]
///     inner: SpinLockIrq<Inner>,
/// }
///
/// impl Example {
///     fn new() -> impl PinInit<Self> {
///         pin_init!(Self {
///             inner <- new_spinlock_irq!(Inner { a: 20 }),
///         })
///     }
/// }
///
/// // Accessing an `Example` from a function that can only be called in no-interrupt contexts.
/// fn noirq_work(e: &Example, interrupt_disabled: &LocalInterruptDisabled) {
///     // Because we know interrupts are disabled from interrupt_disable, we can skip toggling
///     // interrupt state using lock_with() and the provided token
///     assert_eq!(e.inner.lock_with(interrupt_disabled).a, 20);
/// }
///
/// # let e = KBox::pin_init(Example::new(), GFP_KERNEL)?;
/// # let interrupt_guard = local_interrupt_disable();
/// # noirq_work(&e, &interrupt_guard);
/// #
/// # Ok::<(), Error>(())
/// ```
///
/// [`lock()`]: SpinLockIrq::lock
/// [`lock_with()`]: SpinLockIrq::lock_with
pub type SpinLockIrq<T> = super::Lock<T, SpinLockIrqBackend>;

/// A kernel `spinlock_t` lock backend that can only be acquired in interrupt disabled contexts.
pub struct SpinLockIrqBackend;

/// A [`Guard`] acquired from locking a [`SpinLockIrq`] using [`lock()`].
///
/// This is simply a type alias for a [`Guard`] returned from locking a [`SpinLockIrq`] using
/// [`lock()`]. It will unlock the [`SpinLockIrq`] and decrement the local processor's interrupt
/// disablement refcount upon being dropped.
///
/// [`lock()`]: SpinLockIrq::lock
pub type SpinLockIrqGuard<'a, T> = Guard<'a, T, SpinLockIrqBackend>;

// SAFETY: The underlying kernel `spinlock_t` object ensures mutual exclusion. `relock` uses the
// default implementation that always calls the same locking method.
unsafe impl Backend for SpinLockIrqBackend {
    type State = bindings::spinlock_t;
    type GuardState = ();

    #[inline]
    unsafe fn init(
        ptr: *mut Self::State,
        name: *const crate::ffi::c_char,
        key: *mut bindings::lock_class_key,
    ) {
        // SAFETY: The safety requirements ensure that `ptr` is valid for writes, and `name` and
        // `key` are valid for read indefinitely.
        unsafe { bindings::__spin_lock_init(ptr, name, key) }
    }

    #[inline]
    unsafe fn lock(ptr: *mut Self::State) -> Self::GuardState {
        // SAFETY: The safety requirements of this function ensure that `ptr` points to valid
        // memory, and that it has been initialised before.
        unsafe { bindings::spin_lock_irq_disable(ptr) }
    }

    #[inline]
    unsafe fn unlock(ptr: *mut Self::State, _guard_state: &Self::GuardState) {
        // SAFETY: The safety requirements of this function ensure that `ptr` is valid and that the
        // caller is the owner of the spinlock.
        unsafe { bindings::spin_unlock_irq_enable(ptr) }
    }

    #[inline]
    unsafe fn try_lock(ptr: *mut Self::State) -> Option<Self::GuardState> {
        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use.
        let result = unsafe { bindings::spin_trylock_irq_disable(ptr) };

        if result != 0 {
            Some(())
        } else {
            None
        }
    }

    #[inline]
    unsafe fn assert_is_held(ptr: *mut Self::State) {
        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use.
        unsafe { bindings::spin_assert_is_held(ptr) }
    }
}

impl<T: ?Sized> Lock<T, SpinLockIrqBackend> {
    /// Casts the lock as a `Lock<T, SpinLockBackend>`.
    #[inline]
    fn as_lock_in_interrupt<'a>(&'a self, _context: &'a LocalInterruptDisabled) -> &'a SpinLock<T> {
        // SAFETY:
        // - `Lock<T, SpinLockBackend>` and `Lock<T, SpinLockIrqBackend>` both have identical data
        //   layouts.
        // - As long as local interrupts are disabled (which is proven to be true by _context), it
        //   is safe to treat a lock with SpinLockIrqBackend as a SpinLockBackend lock.
        unsafe { core::mem::transmute(self) }
    }

    /// Acquires the lock without modifying local interrupt state.
    ///
    /// This function should be used in place of the more expensive [`Lock::lock()`] function when
    /// possible for [`SpinLockIrq`] locks.
    #[inline]
    pub fn lock_with<'a>(&'a self, context: &'a LocalInterruptDisabled) -> SpinLockGuard<'a, T> {
        self.as_lock_in_interrupt(context).lock()
    }

    /// Tries to acquire the lock without modifying local interrupt state.
    ///
    /// This function should be used in place of the more expensive [`Lock::try_lock()`] function
    /// when possible for [`SpinLockIrq`] locks.
    ///
    /// Returns a guard that can be used to access the data protected by the lock if successful.
    #[must_use = "if unused, the lock will be immediately unlocked"]
    #[inline]
    pub fn try_lock_with<'a>(
        &'a self,
        context: &'a LocalInterruptDisabled,
    ) -> Option<SpinLockGuard<'a, T>> {
        self.as_lock_in_interrupt(context).try_lock()
    }
}

#[kunit_tests(rust_spinlock_irq_condvar)]
mod tests {
    use super::*;
    use crate::{
        sync::*,
        workqueue::{
            self,
            impl_has_work,
            new_work,
            Work,
            WorkItem, //
        },
    };

    struct TestState {
        value: u32,
        waiter_ready: bool,
    }

    #[pin_data]
    struct Test {
        #[pin]
        state: SpinLockIrq<TestState>,

        #[pin]
        state_changed: CondVar,

        #[pin]
        waiter_state_changed: CondVar,

        #[pin]
        wait_work: Work<Self>,
    }

    impl_has_work! {
        impl HasWork<Self> for Test { self.wait_work }
    }

    impl Test {
        pub(crate) fn new() -> Result<Arc<Self>> {
            Arc::try_pin_init(
                try_pin_init!(
                    Self {
                        state <- new_spinlock_irq!(TestState {
                            value: 1,
                            waiter_ready: false
                        }),
                        state_changed <- new_condvar!(),
                        waiter_state_changed <- new_condvar!(),
                        wait_work <- new_work!("IrqCondvarTest::wait_work")
                    }
                ),
                GFP_KERNEL,
            )
        }
    }

    impl WorkItem for Test {
        type Pointer = Arc<Self>;

        fn run(this: Arc<Self>) {
            // Wait for the test to be ready to wait for us
            let mut state = this.state.lock();

            // Make sure the interrupts actually turned off
            // SAFETY: It's always safe to call `lockdep_assert_irqs_disabled()`
            unsafe { bindings::lockdep_assert_irqs_disabled() };

            while !state.waiter_ready {
                this.waiter_state_changed.wait(&mut state);
            }

            // Deliver the exciting value update our test has been waiting for
            state.value += 1;
            this.state_changed.notify_sync();
        }
    }

    #[test]
    fn spinlock_irq_condvar() -> Result {
        let testdata = Test::new()?;

        let _ = workqueue::system().enqueue(testdata.clone());

        // Let the updater know when we're ready to wait
        let mut state = testdata.state.lock();
        state.waiter_ready = true;
        testdata.waiter_state_changed.notify_sync();

        // Wait for the exciting value update
        testdata.state_changed.wait(&mut state);
        assert_eq!(state.value, 2);
        Ok(())
    }
}
