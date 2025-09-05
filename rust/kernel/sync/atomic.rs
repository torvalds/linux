// SPDX-License-Identifier: GPL-2.0

//! Atomic primitives.
//!
//! These primitives have the same semantics as their C counterparts: and the precise definitions of
//! semantics can be found at [`LKMM`]. Note that Linux Kernel Memory (Consistency) Model is the
//! only model for Rust code in kernel, and Rust's own atomics should be avoided.
//!
//! # Data races
//!
//! [`LKMM`] atomics have different rules regarding data races:
//!
//! - A normal write from C side is treated as an atomic write if
//!   CONFIG_KCSAN_ASSUME_PLAIN_WRITES_ATOMIC=y.
//! - Mixed-size atomic accesses don't cause data races.
//!
//! [`LKMM`]: srctree/tools/memory-model/

#[allow(dead_code, unreachable_pub)]
mod internal;
pub mod ordering;
mod predefine;

pub use internal::AtomicImpl;
pub use ordering::{Acquire, Full, Relaxed, Release};

use crate::build_error;
use internal::{AtomicBasicOps, AtomicRepr};
use ordering::OrderingType;

/// A memory location which can be safely modified from multiple execution contexts.
///
/// This has the same size, alignment and bit validity as the underlying type `T`. And it disables
/// niche optimization for the same reason as [`UnsafeCell`].
///
/// The atomic operations are implemented in a way that is fully compatible with the [Linux Kernel
/// Memory (Consistency) Model][LKMM], hence they should be modeled as the corresponding
/// [`LKMM`][LKMM] atomic primitives. With the help of [`Atomic::from_ptr()`] and
/// [`Atomic::as_ptr()`], this provides a way to interact with [C-side atomic operations]
/// (including those without the `atomic` prefix, e.g. `READ_ONCE()`, `WRITE_ONCE()`,
/// `smp_load_acquire()` and `smp_store_release()`).
///
/// # Invariants
///
/// `self.0` is a valid `T`.
///
/// [`UnsafeCell`]: core::cell::UnsafeCell
/// [LKMM]: srctree/tools/memory-model/
/// [C-side atomic operations]: srctree/Documentation/atomic_t.txt
#[repr(transparent)]
pub struct Atomic<T: AtomicType>(AtomicRepr<T::Repr>);

// SAFETY: `Atomic<T>` is safe to share among execution contexts because all accesses are atomic.
unsafe impl<T: AtomicType> Sync for Atomic<T> {}

/// Types that support basic atomic operations.
///
/// # Round-trip transmutability
///
/// `T` is round-trip transmutable to `U` if and only if both of these properties hold:
///
/// - Any valid bit pattern for `T` is also a valid bit pattern for `U`.
/// - Transmuting (e.g. using [`transmute()`]) a value of type `T` to `U` and then to `T` again
///   yields a value that is in all aspects equivalent to the original value.
///
/// # Safety
///
/// - [`Self`] must have the same size and alignment as [`Self::Repr`].
/// - [`Self`] must be [round-trip transmutable] to  [`Self::Repr`].
///
/// Note that this is more relaxed than requiring the bi-directional transmutability (i.e.
/// [`transmute()`] is always sound between `U` and `T`) because of the support for atomic
/// variables over unit-only enums, see [Examples].
///
/// # Limitations
///
/// Because C primitives are used to implement the atomic operations, and a C function requires a
/// valid object of a type to operate on (i.e. no `MaybeUninit<_>`), hence at the Rust <-> C
/// surface, only types with all the bits initialized can be passed. As a result, types like `(u8,
/// u16)` (padding bytes are uninitialized) are currently not supported.
///
/// # Examples
///
/// A unit-only enum that implements [`AtomicType`]:
///
/// ```
/// use kernel::sync::atomic::{AtomicType, Atomic, Relaxed};
///
/// #[derive(Clone, Copy, PartialEq, Eq)]
/// #[repr(i32)]
/// enum State {
///     Uninit = 0,
///     Working = 1,
///     Done = 2,
/// };
///
/// // SAFETY: `State` and `i32` has the same size and alignment, and it's round-trip
/// // transmutable to `i32`.
/// unsafe impl AtomicType for State {
///     type Repr = i32;
/// }
///
/// let s = Atomic::new(State::Uninit);
///
/// assert_eq!(State::Uninit, s.load(Relaxed));
/// ```
/// [`transmute()`]: core::mem::transmute
/// [round-trip transmutable]: AtomicType#round-trip-transmutability
/// [Examples]: AtomicType#examples
pub unsafe trait AtomicType: Sized + Send + Copy {
    /// The backing atomic implementation type.
    type Repr: AtomicImpl;
}

#[inline(always)]
const fn into_repr<T: AtomicType>(v: T) -> T::Repr {
    // SAFETY: Per the safety requirement of `AtomicType`, `T` is round-trip transmutable to
    // `T::Repr`, therefore the transmute operation is sound.
    unsafe { core::mem::transmute_copy(&v) }
}

/// # Safety
///
/// `r` must be a valid bit pattern of `T`.
#[inline(always)]
const unsafe fn from_repr<T: AtomicType>(r: T::Repr) -> T {
    // SAFETY: Per the safety requirement of the function, the transmute operation is sound.
    unsafe { core::mem::transmute_copy(&r) }
}

impl<T: AtomicType> Atomic<T> {
    /// Creates a new atomic `T`.
    pub const fn new(v: T) -> Self {
        // INVARIANT: Per the safety requirement of `AtomicType`, `into_repr(v)` is a valid `T`.
        Self(AtomicRepr::new(into_repr(v)))
    }

    /// Creates a reference to an atomic `T` from a pointer of `T`.
    ///
    /// This usually is used when communicating with C side or manipulating a C struct, see
    /// examples below.
    ///
    /// # Safety
    ///
    /// - `ptr` is aligned to `align_of::<T>()`.
    /// - `ptr` is valid for reads and writes for `'a`.
    /// - For the duration of `'a`, other accesses to `*ptr` must not cause data races (defined
    ///   by [`LKMM`]) against atomic operations on the returned reference. Note that if all other
    ///   accesses are atomic, then this safety requirement is trivially fulfilled.
    ///
    /// [`LKMM`]: srctree/tools/memory-model
    ///
    /// # Examples
    ///
    /// Using [`Atomic::from_ptr()`] combined with [`Atomic::load()`] or [`Atomic::store()`] can
    /// achieve the same functionality as `READ_ONCE()`/`smp_load_acquire()` or
    /// `WRITE_ONCE()`/`smp_store_release()` in C side:
    ///
    /// ```
    /// # use kernel::types::Opaque;
    /// use kernel::sync::atomic::{Atomic, Relaxed, Release};
    ///
    /// // Assume there is a C struct `foo`.
    /// mod cbindings {
    ///     #[repr(C)]
    ///     pub(crate) struct foo {
    ///         pub(crate) a: i32,
    ///         pub(crate) b: i32
    ///     }
    /// }
    ///
    /// let tmp = Opaque::new(cbindings::foo { a: 1, b: 2 });
    ///
    /// // struct foo *foo_ptr = ..;
    /// let foo_ptr = tmp.get();
    ///
    /// // SAFETY: `foo_ptr` is valid, and `.a` is in bounds.
    /// let foo_a_ptr = unsafe { &raw mut (*foo_ptr).a };
    ///
    /// // a = READ_ONCE(foo_ptr->a);
    /// //
    /// // SAFETY: `foo_a_ptr` is valid for read, and all other accesses on it is atomic, so no
    /// // data race.
    /// let a = unsafe { Atomic::from_ptr(foo_a_ptr) }.load(Relaxed);
    /// # assert_eq!(a, 1);
    ///
    /// // smp_store_release(&foo_ptr->a, 2);
    /// //
    /// // SAFETY: `foo_a_ptr` is valid for writes, and all other accesses on it is atomic, so
    /// // no data race.
    /// unsafe { Atomic::from_ptr(foo_a_ptr) }.store(2, Release);
    /// ```
    pub unsafe fn from_ptr<'a>(ptr: *mut T) -> &'a Self
    where
        T: Sync,
    {
        // CAST: `T` and `Atomic<T>` have the same size, alignment and bit validity.
        // SAFETY: Per function safety requirement, `ptr` is a valid pointer and the object will
        // live long enough. It's safe to return a `&Atomic<T>` because function safety requirement
        // guarantees other accesses won't cause data races.
        unsafe { &*ptr.cast::<Self>() }
    }

    /// Returns a pointer to the underlying atomic `T`.
    ///
    /// Note that use of the return pointer must not cause data races defined by [`LKMM`].
    ///
    /// # Guarantees
    ///
    /// The returned pointer is valid and properly aligned (i.e. aligned to [`align_of::<T>()`]).
    ///
    /// [`LKMM`]: srctree/tools/memory-model
    /// [`align_of::<T>()`]: core::mem::align_of
    pub const fn as_ptr(&self) -> *mut T {
        // GUARANTEE: Per the function guarantee of `AtomicRepr::as_ptr()`, the `self.0.as_ptr()`
        // must be a valid and properly aligned pointer for `T::Repr`, and per the safety guarantee
        // of `AtomicType`, it's a valid and properly aligned pointer of `T`.
        self.0.as_ptr().cast()
    }

    /// Returns a mutable reference to the underlying atomic `T`.
    ///
    /// This is safe because the mutable reference of the atomic `T` guarantees exclusive access.
    pub fn get_mut(&mut self) -> &mut T {
        // CAST: `T` and `T::Repr` has the same size and alignment per the safety requirement of
        // `AtomicType`, and per the type invariants `self.0` is a valid `T`, therefore the casting
        // result is a valid pointer of `T`.
        // SAFETY: The pointer is valid per the CAST comment above, and the mutable reference
        // guarantees exclusive access.
        unsafe { &mut *self.0.as_ptr().cast() }
    }
}

impl<T: AtomicType> Atomic<T>
where
    T::Repr: AtomicBasicOps,
{
    /// Loads the value from the atomic `T`.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Relaxed};
    ///
    /// let x = Atomic::new(42i32);
    ///
    /// assert_eq!(42, x.load(Relaxed));
    ///
    /// let x = Atomic::new(42i64);
    ///
    /// assert_eq!(42, x.load(Relaxed));
    /// ```
    #[doc(alias("atomic_read", "atomic64_read"))]
    #[inline(always)]
    pub fn load<Ordering: ordering::AcquireOrRelaxed>(&self, _: Ordering) -> T {
        let v = {
            match Ordering::TYPE {
                OrderingType::Relaxed => T::Repr::atomic_read(&self.0),
                OrderingType::Acquire => T::Repr::atomic_read_acquire(&self.0),
                _ => build_error!("Wrong ordering"),
            }
        };

        // SAFETY: `v` comes from reading `self.0`, which is a valid `T` per the type invariants.
        unsafe { from_repr(v) }
    }

    /// Stores a value to the atomic `T`.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Relaxed};
    ///
    /// let x = Atomic::new(42i32);
    ///
    /// assert_eq!(42, x.load(Relaxed));
    ///
    /// x.store(43, Relaxed);
    ///
    /// assert_eq!(43, x.load(Relaxed));
    /// ```
    #[doc(alias("atomic_set", "atomic64_set"))]
    #[inline(always)]
    pub fn store<Ordering: ordering::ReleaseOrRelaxed>(&self, v: T, _: Ordering) {
        let v = into_repr(v);

        // INVARIANT: `v` is a valid `T`, and is stored to `self.0` by `atomic_set*()`.
        match Ordering::TYPE {
            OrderingType::Relaxed => T::Repr::atomic_set(&self.0, v),
            OrderingType::Release => T::Repr::atomic_set_release(&self.0, v),
            _ => build_error!("Wrong ordering"),
        }
    }
}
