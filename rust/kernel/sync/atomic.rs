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

mod internal;
pub mod ordering;
mod predefine;

pub use internal::AtomicImpl;
pub use ordering::{Acquire, Full, Relaxed, Release};

pub(crate) use internal::{AtomicArithmeticOps, AtomicBasicOps, AtomicExchangeOps};

use crate::build_error;
use internal::AtomicRepr;
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

// SAFETY: `Atomic<T>` is safe to transfer between execution contexts because of the safety
// requirement of `AtomicType`.
unsafe impl<T: AtomicType> Send for Atomic<T> {}

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
/// - [`Self`] must be safe to transfer between execution contexts, if it's [`Send`], this is
///   automatically satisfied. The exception is pointer types that are even though marked as
///   `!Send` (e.g. raw pointers and [`NonNull<T>`]) but requiring `unsafe` to do anything
///   meaningful on them. This is because transferring pointer values between execution contexts is
///   safe as long as the actual `unsafe` dereferencing is justified.
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
/// [`NonNull<T>`]: core::ptr::NonNull
pub unsafe trait AtomicType: Sized + Copy {
    /// The backing atomic implementation type.
    type Repr: AtomicImpl;
}

/// Types that support atomic add operations.
///
/// # Safety
///
// TODO: Properly defines `wrapping_add` in the following comment.
/// `wrapping_add` any value of type `Self::Repr::Delta` obtained by [`Self::rhs_into_delta()`] to
/// any value of type `Self::Repr` obtained through transmuting a value of type `Self` to must
/// yield a value with a bit pattern also valid for `Self`.
pub unsafe trait AtomicAdd<Rhs = Self>: AtomicType {
    /// Converts `Rhs` into the `Delta` type of the atomic implementation.
    fn rhs_into_delta(rhs: Rhs) -> <Self::Repr as AtomicImpl>::Delta;
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
    pub unsafe fn from_ptr<'a>(ptr: *mut T) -> &'a Self {
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
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Relaxed};
    ///
    /// let mut atomic_val = Atomic::new(0u32);
    /// let val_mut = atomic_val.get_mut();
    /// *val_mut = 101;
    /// assert_eq!(101, atomic_val.load(Relaxed));
    /// ```
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

impl<T: AtomicType + core::fmt::Debug> core::fmt::Debug for Atomic<T>
where
    T::Repr: AtomicBasicOps,
{
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        core::fmt::Debug::fmt(&self.load(Relaxed), f)
    }
}

impl<T: AtomicType> Atomic<T>
where
    T::Repr: AtomicExchangeOps,
{
    /// Atomic exchange.
    ///
    /// Atomically updates `*self` to `v` and returns the old value of `*self`.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Acquire, Relaxed};
    ///
    /// let x = Atomic::new(42);
    ///
    /// assert_eq!(42, x.xchg(52, Acquire));
    /// assert_eq!(52, x.load(Relaxed));
    /// ```
    #[doc(alias("atomic_xchg", "atomic64_xchg", "swap"))]
    #[inline(always)]
    pub fn xchg<Ordering: ordering::Ordering>(&self, v: T, _: Ordering) -> T {
        let v = into_repr(v);

        // INVARIANT: `self.0` is a valid `T` after `atomic_xchg*()` because `v` is transmutable to
        // `T`.
        let ret = {
            match Ordering::TYPE {
                OrderingType::Full => T::Repr::atomic_xchg(&self.0, v),
                OrderingType::Acquire => T::Repr::atomic_xchg_acquire(&self.0, v),
                OrderingType::Release => T::Repr::atomic_xchg_release(&self.0, v),
                OrderingType::Relaxed => T::Repr::atomic_xchg_relaxed(&self.0, v),
            }
        };

        // SAFETY: `ret` comes from reading `*self`, which is a valid `T` per type invariants.
        unsafe { from_repr(ret) }
    }

    /// Atomic compare and exchange.
    ///
    /// If `*self` == `old`, atomically updates `*self` to `new`. Otherwise, `*self` is not
    /// modified.
    ///
    /// Compare: The comparison is done via the byte level comparison between `*self` and `old`.
    ///
    /// Ordering: When succeeds, provides the corresponding ordering as the `Ordering` type
    /// parameter indicates, and a failed one doesn't provide any ordering, the load part of a
    /// failed cmpxchg is a [`Relaxed`] load.
    ///
    /// Returns `Ok(value)` if cmpxchg succeeds, and `value` is guaranteed to be equal to `old`,
    /// otherwise returns `Err(value)`, and `value` is the current value of `*self`.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Full, Relaxed};
    ///
    /// let x = Atomic::new(42);
    ///
    /// // Checks whether cmpxchg succeeded.
    /// let success = x.cmpxchg(52, 64, Relaxed).is_ok();
    /// # assert!(!success);
    ///
    /// // Checks whether cmpxchg failed.
    /// let failure = x.cmpxchg(52, 64, Relaxed).is_err();
    /// # assert!(failure);
    ///
    /// // Uses the old value if failed, probably re-try cmpxchg.
    /// match x.cmpxchg(52, 64, Relaxed) {
    ///     Ok(_) => { },
    ///     Err(old) => {
    ///         // do something with `old`.
    ///         # assert_eq!(old, 42);
    ///     }
    /// }
    ///
    /// // Uses the latest value regardlessly, same as atomic_cmpxchg() in C.
    /// let latest = x.cmpxchg(42, 64, Full).unwrap_or_else(|old| old);
    /// # assert_eq!(42, latest);
    /// assert_eq!(64, x.load(Relaxed));
    /// ```
    ///
    /// [`Relaxed`]: ordering::Relaxed
    #[doc(alias(
        "atomic_cmpxchg",
        "atomic64_cmpxchg",
        "atomic_try_cmpxchg",
        "atomic64_try_cmpxchg",
        "compare_exchange"
    ))]
    #[inline(always)]
    pub fn cmpxchg<Ordering: ordering::Ordering>(
        &self,
        mut old: T,
        new: T,
        o: Ordering,
    ) -> Result<T, T> {
        // Note on code generation:
        //
        // try_cmpxchg() is used to implement cmpxchg(), and if the helper functions are inlined,
        // the compiler is able to figure out that branch is not needed if the users don't care
        // about whether the operation succeeds or not. One exception is on x86, due to commit
        // 44fe84459faf ("locking/atomic: Fix atomic_try_cmpxchg() semantics"), the
        // atomic_try_cmpxchg() on x86 has a branch even if the caller doesn't care about the
        // success of cmpxchg and only wants to use the old value. For example, for code like:
        //
        //     let latest = x.cmpxchg(42, 64, Full).unwrap_or_else(|old| old);
        //
        // It will still generate code:
        //
        //     movl    $0x40, %ecx
        //     movl    $0x34, %eax
        //     lock
        //     cmpxchgl        %ecx, 0x4(%rsp)
        //     jne     1f
        //     2:
        //     ...
        //     1:  movl    %eax, %ecx
        //     jmp 2b
        //
        // This might be "fixed" by introducing a try_cmpxchg_exclusive() that knows the "*old"
        // location in the C function is always safe to write.
        if self.try_cmpxchg(&mut old, new, o) {
            Ok(old)
        } else {
            Err(old)
        }
    }

    /// Atomic compare and exchange and returns whether the operation succeeds.
    ///
    /// If `*self` == `old`, atomically updates `*self` to `new`. Otherwise, `*self` is not
    /// modified, `*old` is updated to the current value of `*self`.
    ///
    /// "Compare" and "Ordering" part are the same as [`Atomic::cmpxchg()`].
    ///
    /// Returns `true` means the cmpxchg succeeds otherwise returns `false`.
    #[inline(always)]
    fn try_cmpxchg<Ordering: ordering::Ordering>(&self, old: &mut T, new: T, _: Ordering) -> bool {
        let mut tmp = into_repr(*old);
        let new = into_repr(new);

        // INVARIANT: `self.0` is a valid `T` after `atomic_try_cmpxchg*()` because `new` is
        // transmutable to `T`.
        let ret = {
            match Ordering::TYPE {
                OrderingType::Full => T::Repr::atomic_try_cmpxchg(&self.0, &mut tmp, new),
                OrderingType::Acquire => {
                    T::Repr::atomic_try_cmpxchg_acquire(&self.0, &mut tmp, new)
                }
                OrderingType::Release => {
                    T::Repr::atomic_try_cmpxchg_release(&self.0, &mut tmp, new)
                }
                OrderingType::Relaxed => {
                    T::Repr::atomic_try_cmpxchg_relaxed(&self.0, &mut tmp, new)
                }
            }
        };

        // SAFETY: `tmp` comes from reading `*self`, which is a valid `T` per type invariants.
        *old = unsafe { from_repr(tmp) };

        ret
    }
}

impl<T: AtomicType> Atomic<T>
where
    T::Repr: AtomicArithmeticOps,
{
    /// Atomic add.
    ///
    /// Atomically updates `*self` to `(*self).wrapping_add(v)`.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Relaxed};
    ///
    /// let x = Atomic::new(42);
    ///
    /// assert_eq!(42, x.load(Relaxed));
    ///
    /// x.add(12, Relaxed);
    ///
    /// assert_eq!(54, x.load(Relaxed));
    /// ```
    #[inline(always)]
    pub fn add<Rhs>(&self, v: Rhs, _: ordering::Relaxed)
    where
        T: AtomicAdd<Rhs>,
    {
        let v = T::rhs_into_delta(v);

        // INVARIANT: `self.0` is a valid `T` after `atomic_add()` due to safety requirement of
        // `AtomicAdd`.
        T::Repr::atomic_add(&self.0, v);
    }

    /// Atomic fetch and add.
    ///
    /// Atomically updates `*self` to `(*self).wrapping_add(v)`, and returns the value of `*self`
    /// before the update.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Acquire, Full, Relaxed};
    ///
    /// let x = Atomic::new(42);
    /// assert_eq!(42, x.load(Relaxed));
    /// assert_eq!(42, x.fetch_add(12, Acquire));
    /// assert_eq!(54, x.load(Relaxed));
    ///
    /// let x = Atomic::new(42);
    /// assert_eq!(42, x.load(Relaxed));
    /// assert_eq!(42, x.fetch_add(12, Full));
    /// assert_eq!(54, x.load(Relaxed));
    /// ```
    #[inline(always)]
    pub fn fetch_add<Rhs, Ordering: ordering::Ordering>(&self, v: Rhs, _: Ordering) -> T
    where
        T: AtomicAdd<Rhs>,
    {
        let v = T::rhs_into_delta(v);

        // INVARIANT: `self.0` is a valid `T` after `atomic_fetch_add*()` due to safety requirement
        // of `AtomicAdd`.
        let ret = {
            match Ordering::TYPE {
                OrderingType::Full => T::Repr::atomic_fetch_add(&self.0, v),
                OrderingType::Acquire => T::Repr::atomic_fetch_add_acquire(&self.0, v),
                OrderingType::Release => T::Repr::atomic_fetch_add_release(&self.0, v),
                OrderingType::Relaxed => T::Repr::atomic_fetch_add_relaxed(&self.0, v),
            }
        };

        // SAFETY: `ret` comes from reading `self.0`, which is a valid `T` per type invariants.
        unsafe { from_repr(ret) }
    }

    /// Atomic fetch and subtract.
    ///
    /// Atomically updates `*self` to `(*self).wrapping_sub(v)`, and returns the value of `*self`
    /// before the update.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{Atomic, Acquire, Full, Relaxed};
    ///
    /// let x = Atomic::new(42);
    /// assert_eq!(42, x.load(Relaxed));
    /// assert_eq!(42, x.fetch_sub(12, Acquire));
    /// assert_eq!(30, x.load(Relaxed));
    ///
    /// let x = Atomic::new(42);
    /// assert_eq!(42, x.load(Relaxed));
    /// assert_eq!(42, x.fetch_sub(12, Full));
    /// assert_eq!(30, x.load(Relaxed));
    /// ```
    #[inline(always)]
    pub fn fetch_sub<Rhs, Ordering: ordering::Ordering>(&self, v: Rhs, _: Ordering) -> T
    where
        // Types that support addition also support subtraction.
        T: AtomicAdd<Rhs>,
    {
        let v = T::rhs_into_delta(v);

        // INVARIANT: `self.0` is a valid `T` after `atomic_fetch_sub*()` due to safety requirement
        // of `AtomicAdd`.
        let ret = {
            match Ordering::TYPE {
                OrderingType::Full => T::Repr::atomic_fetch_sub(&self.0, v),
                OrderingType::Acquire => T::Repr::atomic_fetch_sub_acquire(&self.0, v),
                OrderingType::Release => T::Repr::atomic_fetch_sub_release(&self.0, v),
                OrderingType::Relaxed => T::Repr::atomic_fetch_sub_relaxed(&self.0, v),
            }
        };

        // SAFETY: `ret` comes from reading `self.0`, which is a valid `T` per type invariants.
        unsafe { from_repr(ret) }
    }
}

#[cfg(any(CONFIG_X86_64, CONFIG_UML, CONFIG_ARM, CONFIG_ARM64))]
#[repr(C)]
#[derive(Clone, Copy)]
struct Flag {
    bool_field: bool,
}

/// # Invariants
///
/// `padding` must be all zeroes.
#[cfg(not(any(CONFIG_X86_64, CONFIG_UML, CONFIG_ARM, CONFIG_ARM64)))]
#[repr(C, align(4))]
#[derive(Clone, Copy)]
struct Flag {
    #[cfg(target_endian = "big")]
    padding: [u8; 3],
    bool_field: bool,
    #[cfg(target_endian = "little")]
    padding: [u8; 3],
}

impl Flag {
    #[inline(always)]
    const fn new(b: bool) -> Self {
        // INVARIANT: `padding` is all zeroes.
        Self {
            bool_field: b,
            #[cfg(not(any(CONFIG_X86_64, CONFIG_UML, CONFIG_ARM, CONFIG_ARM64)))]
            padding: [0; 3],
        }
    }
}

// SAFETY: `Flag` and `Repr` have the same size and alignment, and `Flag` is round-trip
// transmutable to the selected representation (`i8` or `i32`).
unsafe impl AtomicType for Flag {
    #[cfg(any(CONFIG_X86_64, CONFIG_UML, CONFIG_ARM, CONFIG_ARM64))]
    type Repr = i8;
    #[cfg(not(any(CONFIG_X86_64, CONFIG_UML, CONFIG_ARM, CONFIG_ARM64)))]
    type Repr = i32;
}

/// An atomic flag type intended to be backed by performance-optimal integer type.
///
/// The backing integer type is an implementation detail; it may vary by architecture and change
/// in the future.
///
/// [`AtomicFlag`] is generally preferable to [`Atomic<bool>`] when you need read-modify-write
/// (RMW) operations (e.g. [`Atomic::xchg()`]/[`Atomic::cmpxchg()`]) or when [`Atomic<bool>`] does
/// not save memory due to padding. On some architectures that do not support byte-sized atomic
/// RMW operations, RMW operations on [`Atomic<bool>`] are slower.
///
/// If you only use [`Atomic::load()`]/[`Atomic::store()`], [`Atomic<bool>`] is fine.
///
/// # Examples
///
/// ```
/// use kernel::sync::atomic::{AtomicFlag, Relaxed};
///
/// let flag = AtomicFlag::new(false);
/// assert_eq!(false, flag.load(Relaxed));
/// flag.store(true, Relaxed);
/// assert_eq!(true, flag.load(Relaxed));
/// ```
pub struct AtomicFlag(Atomic<Flag>);

impl AtomicFlag {
    /// Creates a new atomic flag.
    #[inline(always)]
    pub const fn new(b: bool) -> Self {
        Self(Atomic::new(Flag::new(b)))
    }

    /// Returns a mutable reference to the underlying flag as a [`bool`].
    ///
    /// This is safe because the mutable reference of the atomic flag guarantees exclusive access.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::sync::atomic::{AtomicFlag, Relaxed};
    ///
    /// let mut atomic_flag = AtomicFlag::new(false);
    /// assert_eq!(false, atomic_flag.load(Relaxed));
    /// *atomic_flag.get_mut() = true;
    /// assert_eq!(true, atomic_flag.load(Relaxed));
    /// ```
    #[inline(always)]
    pub fn get_mut(&mut self) -> &mut bool {
        &mut self.0.get_mut().bool_field
    }

    /// Loads the value from the atomic flag.
    #[inline(always)]
    pub fn load<Ordering: ordering::AcquireOrRelaxed>(&self, o: Ordering) -> bool {
        self.0.load(o).bool_field
    }

    /// Stores a value to the atomic flag.
    #[inline(always)]
    pub fn store<Ordering: ordering::ReleaseOrRelaxed>(&self, v: bool, o: Ordering) {
        self.0.store(Flag::new(v), o);
    }

    /// Stores a value to the atomic flag and returns the previous value.
    #[inline(always)]
    pub fn xchg<Ordering: ordering::Ordering>(&self, new: bool, o: Ordering) -> bool {
        self.0.xchg(Flag::new(new), o).bool_field
    }

    /// Store a value to the atomic flag if the current value is equal to `old`.
    #[inline(always)]
    pub fn cmpxchg<Ordering: ordering::Ordering>(
        &self,
        old: bool,
        new: bool,
        o: Ordering,
    ) -> Result<bool, bool> {
        match self.0.cmpxchg(Flag::new(old), Flag::new(new), o) {
            Ok(_) => Ok(old),
            Err(f) => Err(f.bool_field),
        }
    }
}

/// Atomic load over raw pointers.
///
/// This function provides a short-cut of `Atomic::from_ptr().load(..)`, and can be used to work
/// with C side on synchronizations:
///
/// - `atomic_load(.., Relaxed)` maps to `READ_ONCE()` when used for inter-thread communication.
/// - `atomic_load(.., Acquire)` maps to `smp_load_acquire()`.
///
/// # Safety
///
/// - `ptr` is a valid pointer to `T` and aligned to `align_of::<T>()`.
/// - If there is a concurrent store from kernel (C or Rust), it has to be atomic.
#[doc(alias("READ_ONCE", "smp_load_acquire"))]
#[inline(always)]
pub unsafe fn atomic_load<T: AtomicType, Ordering: ordering::AcquireOrRelaxed>(
    ptr: *mut T,
    o: Ordering,
) -> T
where
    T::Repr: AtomicBasicOps,
{
    // SAFETY: Per the function safety requirement, `ptr` is valid and aligned to
    // `align_of::<T>()`, and all concurrent stores from kernel are atomic, hence no data race per
    // LKMM.
    unsafe { Atomic::from_ptr(ptr) }.load(o)
}

/// Atomic store over raw pointers.
///
/// This function provides a short-cut of `Atomic::from_ptr().load(..)`, and can be used to work
/// with C side on synchronizations:
///
/// - `atomic_store(.., Relaxed)` maps to `WRITE_ONCE()` when used for inter-thread communication.
/// - `atomic_load(.., Release)` maps to `smp_store_release()`.
///
/// # Safety
///
/// - `ptr` is a valid pointer to `T` and aligned to `align_of::<T>()`.
/// - If there is a concurrent access from kernel (C or Rust), it has to be atomic.
#[doc(alias("WRITE_ONCE", "smp_store_release"))]
#[inline(always)]
pub unsafe fn atomic_store<T: AtomicType, Ordering: ordering::ReleaseOrRelaxed>(
    ptr: *mut T,
    v: T,
    o: Ordering,
) where
    T::Repr: AtomicBasicOps,
{
    // SAFETY: Per the function safety requirement, `ptr` is valid and aligned to
    // `align_of::<T>()`, and all concurrent accesses from kernel are atomic, hence no data race
    // per LKMM.
    unsafe { Atomic::from_ptr(ptr) }.store(v, o);
}

/// Atomic exchange over raw pointers.
///
/// This function provides a short-cut of `Atomic::from_ptr().xchg(..)`, and can be used to work
/// with C side on synchronizations.
///
/// # Safety
///
/// - `ptr` is a valid pointer to `T` and aligned to `align_of::<T>()`.
/// - If there is a concurrent access from kernel (C or Rust), it has to be atomic.
#[inline(always)]
pub unsafe fn xchg<T: AtomicType, Ordering: ordering::Ordering>(
    ptr: *mut T,
    new: T,
    o: Ordering,
) -> T
where
    T::Repr: AtomicExchangeOps,
{
    // SAFETY: Per the function safety requirement, `ptr` is valid and aligned to
    // `align_of::<T>()`, and all concurrent accesses from kernel are atomic, hence no data race
    // per LKMM.
    unsafe { Atomic::from_ptr(ptr) }.xchg(new, o)
}

/// Atomic compare and exchange over raw pointers.
///
/// This function provides a short-cut of `Atomic::from_ptr().cmpxchg(..)`, and can be used to work
/// with C side on synchronizations.
///
/// # Safety
///
/// - `ptr` is a valid pointer to `T` and aligned to `align_of::<T>()`.
/// - If there is a concurrent access from kernel (C or Rust), it has to be atomic.
#[doc(alias("try_cmpxchg"))]
#[inline(always)]
pub unsafe fn cmpxchg<T: AtomicType, Ordering: ordering::Ordering>(
    ptr: *mut T,
    old: T,
    new: T,
    o: Ordering,
) -> Result<T, T>
where
    T::Repr: AtomicExchangeOps,
{
    // SAFETY: Per the function safety requirement, `ptr` is valid and aligned to
    // `align_of::<T>()`, and all concurrent accesses from kernel are atomic, hence no data race
    // per LKMM.
    unsafe { Atomic::from_ptr(ptr) }.cmpxchg(old, new, o)
}
