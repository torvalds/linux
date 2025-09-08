// SPDX-License-Identifier: GPL-2.0

//! Kernel types.

use crate::ffi::c_void;
use core::{
    cell::UnsafeCell,
    marker::{PhantomData, PhantomPinned},
    mem::MaybeUninit,
    ops::{Deref, DerefMut},
};
use pin_init::{PinInit, Wrapper, Zeroable};

pub use crate::sync::aref::{ARef, AlwaysRefCounted};

/// Used to transfer ownership to and from foreign (non-Rust) languages.
///
/// Ownership is transferred from Rust to a foreign language by calling [`Self::into_foreign`] and
/// later may be transferred back to Rust by calling [`Self::from_foreign`].
///
/// This trait is meant to be used in cases when Rust objects are stored in C objects and
/// eventually "freed" back to Rust.
///
/// # Safety
///
/// - Implementations must satisfy the guarantees of [`Self::into_foreign`].
pub unsafe trait ForeignOwnable: Sized {
    /// The alignment of pointers returned by `into_foreign`.
    const FOREIGN_ALIGN: usize;

    /// Type used to immutably borrow a value that is currently foreign-owned.
    type Borrowed<'a>;

    /// Type used to mutably borrow a value that is currently foreign-owned.
    type BorrowedMut<'a>;

    /// Converts a Rust-owned object to a foreign-owned one.
    ///
    /// The foreign representation is a pointer to void. Aside from the guarantees listed below,
    /// there are no other guarantees for this pointer. For example, it might be invalid, dangling
    /// or pointing to uninitialized memory. Using it in any way except for [`from_foreign`],
    /// [`try_from_foreign`], [`borrow`], or [`borrow_mut`] can result in undefined behavior.
    ///
    /// # Guarantees
    ///
    /// - Minimum alignment of returned pointer is [`Self::FOREIGN_ALIGN`].
    /// - The returned pointer is not null.
    ///
    /// [`from_foreign`]: Self::from_foreign
    /// [`try_from_foreign`]: Self::try_from_foreign
    /// [`borrow`]: Self::borrow
    /// [`borrow_mut`]: Self::borrow_mut
    fn into_foreign(self) -> *mut c_void;

    /// Converts a foreign-owned object back to a Rust-owned one.
    ///
    /// # Safety
    ///
    /// The provided pointer must have been returned by a previous call to [`into_foreign`], and it
    /// must not be passed to `from_foreign` more than once.
    ///
    /// [`into_foreign`]: Self::into_foreign
    unsafe fn from_foreign(ptr: *mut c_void) -> Self;

    /// Tries to convert a foreign-owned object back to a Rust-owned one.
    ///
    /// A convenience wrapper over [`ForeignOwnable::from_foreign`] that returns [`None`] if `ptr`
    /// is null.
    ///
    /// # Safety
    ///
    /// `ptr` must either be null or satisfy the safety requirements for [`from_foreign`].
    ///
    /// [`from_foreign`]: Self::from_foreign
    unsafe fn try_from_foreign(ptr: *mut c_void) -> Option<Self> {
        if ptr.is_null() {
            None
        } else {
            // SAFETY: Since `ptr` is not null here, then `ptr` satisfies the safety requirements
            // of `from_foreign` given the safety requirements of this function.
            unsafe { Some(Self::from_foreign(ptr)) }
        }
    }

    /// Borrows a foreign-owned object immutably.
    ///
    /// This method provides a way to access a foreign-owned value from Rust immutably. It provides
    /// you with exactly the same abilities as an `&Self` when the value is Rust-owned.
    ///
    /// # Safety
    ///
    /// The provided pointer must have been returned by a previous call to [`into_foreign`], and if
    /// the pointer is ever passed to [`from_foreign`], then that call must happen after the end of
    /// the lifetime `'a`.
    ///
    /// [`into_foreign`]: Self::into_foreign
    /// [`from_foreign`]: Self::from_foreign
    unsafe fn borrow<'a>(ptr: *mut c_void) -> Self::Borrowed<'a>;

    /// Borrows a foreign-owned object mutably.
    ///
    /// This method provides a way to access a foreign-owned value from Rust mutably. It provides
    /// you with exactly the same abilities as an `&mut Self` when the value is Rust-owned, except
    /// that the address of the object must not be changed.
    ///
    /// Note that for types like [`Arc`], an `&mut Arc<T>` only gives you immutable access to the
    /// inner value, so this method also only provides immutable access in that case.
    ///
    /// In the case of `Box<T>`, this method gives you the ability to modify the inner `T`, but it
    /// does not let you change the box itself. That is, you cannot change which allocation the box
    /// points at.
    ///
    /// # Safety
    ///
    /// The provided pointer must have been returned by a previous call to [`into_foreign`], and if
    /// the pointer is ever passed to [`from_foreign`], then that call must happen after the end of
    /// the lifetime `'a`.
    ///
    /// The lifetime `'a` must not overlap with the lifetime of any other call to [`borrow`] or
    /// `borrow_mut` on the same object.
    ///
    /// [`into_foreign`]: Self::into_foreign
    /// [`from_foreign`]: Self::from_foreign
    /// [`borrow`]: Self::borrow
    /// [`Arc`]: crate::sync::Arc
    unsafe fn borrow_mut<'a>(ptr: *mut c_void) -> Self::BorrowedMut<'a>;
}

// SAFETY: The pointer returned by `into_foreign` comes from a well aligned
// pointer to `()`.
unsafe impl ForeignOwnable for () {
    const FOREIGN_ALIGN: usize = core::mem::align_of::<()>();
    type Borrowed<'a> = ();
    type BorrowedMut<'a> = ();

    fn into_foreign(self) -> *mut c_void {
        core::ptr::NonNull::dangling().as_ptr()
    }

    unsafe fn from_foreign(_: *mut c_void) -> Self {}

    unsafe fn borrow<'a>(_: *mut c_void) -> Self::Borrowed<'a> {}
    unsafe fn borrow_mut<'a>(_: *mut c_void) -> Self::BorrowedMut<'a> {}
}

/// Runs a cleanup function/closure when dropped.
///
/// The [`ScopeGuard::dismiss`] function prevents the cleanup function from running.
///
/// # Examples
///
/// In the example below, we have multiple exit paths and we want to log regardless of which one is
/// taken:
///
/// ```
/// # use kernel::types::ScopeGuard;
/// fn example1(arg: bool) {
///     let _log = ScopeGuard::new(|| pr_info!("example1 completed\n"));
///
///     if arg {
///         return;
///     }
///
///     pr_info!("Do something...\n");
/// }
///
/// # example1(false);
/// # example1(true);
/// ```
///
/// In the example below, we want to log the same message on all early exits but a different one on
/// the main exit path:
///
/// ```
/// # use kernel::types::ScopeGuard;
/// fn example2(arg: bool) {
///     let log = ScopeGuard::new(|| pr_info!("example2 returned early\n"));
///
///     if arg {
///         return;
///     }
///
///     // (Other early returns...)
///
///     log.dismiss();
///     pr_info!("example2 no early return\n");
/// }
///
/// # example2(false);
/// # example2(true);
/// ```
///
/// In the example below, we need a mutable object (the vector) to be accessible within the log
/// function, so we wrap it in the [`ScopeGuard`]:
///
/// ```
/// # use kernel::types::ScopeGuard;
/// fn example3(arg: bool) -> Result {
///     let mut vec =
///         ScopeGuard::new_with_data(KVec::new(), |v| pr_info!("vec had {} elements\n", v.len()));
///
///     vec.push(10u8, GFP_KERNEL)?;
///     if arg {
///         return Ok(());
///     }
///     vec.push(20u8, GFP_KERNEL)?;
///     Ok(())
/// }
///
/// # assert_eq!(example3(false), Ok(()));
/// # assert_eq!(example3(true), Ok(()));
/// ```
///
/// # Invariants
///
/// The value stored in the struct is nearly always `Some(_)`, except between
/// [`ScopeGuard::dismiss`] and [`ScopeGuard::drop`]: in this case, it will be `None` as the value
/// will have been returned to the caller. Since  [`ScopeGuard::dismiss`] consumes the guard,
/// callers won't be able to use it anymore.
pub struct ScopeGuard<T, F: FnOnce(T)>(Option<(T, F)>);

impl<T, F: FnOnce(T)> ScopeGuard<T, F> {
    /// Creates a new guarded object wrapping the given data and with the given cleanup function.
    pub fn new_with_data(data: T, cleanup_func: F) -> Self {
        // INVARIANT: The struct is being initialised with `Some(_)`.
        Self(Some((data, cleanup_func)))
    }

    /// Prevents the cleanup function from running and returns the guarded data.
    pub fn dismiss(mut self) -> T {
        // INVARIANT: This is the exception case in the invariant; it is not visible to callers
        // because this function consumes `self`.
        self.0.take().unwrap().0
    }
}

impl ScopeGuard<(), fn(())> {
    /// Creates a new guarded object with the given cleanup function.
    pub fn new(cleanup: impl FnOnce()) -> ScopeGuard<(), impl FnOnce(())> {
        ScopeGuard::new_with_data((), move |()| cleanup())
    }
}

impl<T, F: FnOnce(T)> Deref for ScopeGuard<T, F> {
    type Target = T;

    fn deref(&self) -> &T {
        // The type invariants guarantee that `unwrap` will succeed.
        &self.0.as_ref().unwrap().0
    }
}

impl<T, F: FnOnce(T)> DerefMut for ScopeGuard<T, F> {
    fn deref_mut(&mut self) -> &mut T {
        // The type invariants guarantee that `unwrap` will succeed.
        &mut self.0.as_mut().unwrap().0
    }
}

impl<T, F: FnOnce(T)> Drop for ScopeGuard<T, F> {
    fn drop(&mut self) {
        // Run the cleanup function if one is still present.
        if let Some((data, cleanup)) = self.0.take() {
            cleanup(data)
        }
    }
}

/// Stores an opaque value.
///
/// [`Opaque<T>`] is meant to be used with FFI objects that are never interpreted by Rust code.
///
/// It is used to wrap structs from the C side, like for example `Opaque<bindings::mutex>`.
/// It gets rid of all the usual assumptions that Rust has for a value:
///
/// * The value is allowed to be uninitialized (for example have invalid bit patterns: `3` for a
///   [`bool`]).
/// * The value is allowed to be mutated, when a `&Opaque<T>` exists on the Rust side.
/// * No uniqueness for mutable references: it is fine to have multiple `&mut Opaque<T>` point to
///   the same value.
/// * The value is not allowed to be shared with other threads (i.e. it is `!Sync`).
///
/// This has to be used for all values that the C side has access to, because it can't be ensured
/// that the C side is adhering to the usual constraints that Rust needs.
///
/// Using [`Opaque<T>`] allows to continue to use references on the Rust side even for values shared
/// with C.
///
/// # Examples
///
/// ```
/// # #![expect(unreachable_pub, clippy::disallowed_names)]
/// use kernel::types::Opaque;
/// # // Emulate a C struct binding which is from C, maybe uninitialized or not, only the C side
/// # // knows.
/// # mod bindings {
/// #     pub struct Foo {
/// #         pub val: u8,
/// #     }
/// # }
///
/// // `foo.val` is assumed to be handled on the C side, so we use `Opaque` to wrap it.
/// pub struct Foo {
///     foo: Opaque<bindings::Foo>,
/// }
///
/// impl Foo {
///     pub fn get_val(&self) -> u8 {
///         let ptr = Opaque::get(&self.foo);
///
///         // SAFETY: `Self` is valid from C side.
///         unsafe { (*ptr).val }
///     }
/// }
///
/// // Create an instance of `Foo` with the `Opaque` wrapper.
/// let foo = Foo {
///     foo: Opaque::new(bindings::Foo { val: 0xdb }),
/// };
///
/// assert_eq!(foo.get_val(), 0xdb);
/// ```
#[repr(transparent)]
pub struct Opaque<T> {
    value: UnsafeCell<MaybeUninit<T>>,
    _pin: PhantomPinned,
}

// SAFETY: `Opaque<T>` allows the inner value to be any bit pattern, including all zeros.
unsafe impl<T> Zeroable for Opaque<T> {}

impl<T> Opaque<T> {
    /// Creates a new opaque value.
    pub const fn new(value: T) -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::new(value)),
            _pin: PhantomPinned,
        }
    }

    /// Creates an uninitialised value.
    pub const fn uninit() -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::uninit()),
            _pin: PhantomPinned,
        }
    }

    /// Creates a new zeroed opaque value.
    pub const fn zeroed() -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::zeroed()),
            _pin: PhantomPinned,
        }
    }

    /// Creates a pin-initializer from the given initializer closure.
    ///
    /// The returned initializer calls the given closure with the pointer to the inner `T` of this
    /// `Opaque`. Since this memory is uninitialized, the closure is not allowed to read from it.
    ///
    /// This function is safe, because the `T` inside of an `Opaque` is allowed to be
    /// uninitialized. Additionally, access to the inner `T` requires `unsafe`, so the caller needs
    /// to verify at that point that the inner value is valid.
    pub fn ffi_init(init_func: impl FnOnce(*mut T)) -> impl PinInit<Self> {
        // SAFETY: We contain a `MaybeUninit`, so it is OK for the `init_func` to not fully
        // initialize the `T`.
        unsafe {
            pin_init::pin_init_from_closure::<_, ::core::convert::Infallible>(move |slot| {
                init_func(Self::cast_into(slot));
                Ok(())
            })
        }
    }

    /// Creates a fallible pin-initializer from the given initializer closure.
    ///
    /// The returned initializer calls the given closure with the pointer to the inner `T` of this
    /// `Opaque`. Since this memory is uninitialized, the closure is not allowed to read from it.
    ///
    /// This function is safe, because the `T` inside of an `Opaque` is allowed to be
    /// uninitialized. Additionally, access to the inner `T` requires `unsafe`, so the caller needs
    /// to verify at that point that the inner value is valid.
    pub fn try_ffi_init<E>(
        init_func: impl FnOnce(*mut T) -> Result<(), E>,
    ) -> impl PinInit<Self, E> {
        // SAFETY: We contain a `MaybeUninit`, so it is OK for the `init_func` to not fully
        // initialize the `T`.
        unsafe {
            pin_init::pin_init_from_closure::<_, E>(move |slot| init_func(Self::cast_into(slot)))
        }
    }

    /// Returns a raw pointer to the opaque data.
    pub const fn get(&self) -> *mut T {
        UnsafeCell::get(&self.value).cast::<T>()
    }

    /// Gets the value behind `this`.
    ///
    /// This function is useful to get access to the value without creating intermediate
    /// references.
    pub const fn cast_into(this: *const Self) -> *mut T {
        UnsafeCell::raw_get(this.cast::<UnsafeCell<MaybeUninit<T>>>()).cast::<T>()
    }

    /// The opposite operation of [`Opaque::cast_into`].
    pub const fn cast_from(this: *const T) -> *const Self {
        this.cast()
    }
}

impl<T> Wrapper<T> for Opaque<T> {
    /// Create an opaque pin-initializer from the given pin-initializer.
    fn pin_init<E>(slot: impl PinInit<T, E>) -> impl PinInit<Self, E> {
        Self::try_ffi_init(|ptr: *mut T| {
            // SAFETY:
            //   - `ptr` is a valid pointer to uninitialized memory,
            //   - `slot` is not accessed on error,
            //   - `slot` is pinned in memory.
            unsafe { PinInit::<T, E>::__pinned_init(slot, ptr) }
        })
    }
}

/// Zero-sized type to mark types not [`Send`].
///
/// Add this type as a field to your struct if your type should not be sent to a different task.
/// Since [`Send`] is an auto trait, adding a single field that is `!Send` will ensure that the
/// whole type is `!Send`.
///
/// If a type is `!Send` it is impossible to give control over an instance of the type to another
/// task. This is useful to include in types that store or reference task-local information. A file
/// descriptor is an example of such task-local information.
///
/// This type also makes the type `!Sync`, which prevents immutable access to the value from
/// several threads in parallel.
pub type NotThreadSafe = PhantomData<*mut ()>;

/// Used to construct instances of type [`NotThreadSafe`] similar to how `PhantomData` is
/// constructed.
///
/// [`NotThreadSafe`]: type@NotThreadSafe
#[allow(non_upper_case_globals)]
pub const NotThreadSafe: NotThreadSafe = PhantomData;
