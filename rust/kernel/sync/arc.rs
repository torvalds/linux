// SPDX-License-Identifier: GPL-2.0

//! A reference-counted pointer.
//!
//! This module implements a way for users to create reference-counted objects and pointers to
//! them. Such a pointer automatically increments and decrements the count, and drops the
//! underlying object when it reaches zero. It is also safe to use concurrently from multiple
//! threads.
//!
//! It is different from the standard library's [`Arc`] in a few ways:
//! 1. It is backed by the kernel's `refcount_t` type.
//! 2. It does not support weak references, which allows it to be half the size.
//! 3. It saturates the reference count instead of aborting when it goes over a threshold.
//! 4. It does not provide a `get_mut` method, so the ref counted object is pinned.
//!
//! [`Arc`]: https://doc.rust-lang.org/std/sync/struct.Arc.html

use crate::{
    bindings,
    error::Result,
    types::{ForeignOwnable, Opaque},
};
use alloc::boxed::Box;
use core::{
    marker::{PhantomData, Unsize},
    mem::{ManuallyDrop, MaybeUninit},
    ops::{Deref, DerefMut},
    pin::Pin,
    ptr::NonNull,
};

/// A reference-counted pointer to an instance of `T`.
///
/// The reference count is incremented when new instances of [`Arc`] are created, and decremented
/// when they are dropped. When the count reaches zero, the underlying `T` is also dropped.
///
/// # Invariants
///
/// The reference count on an instance of [`Arc`] is always non-zero.
/// The object pointed to by [`Arc`] is always pinned.
///
/// # Examples
///
/// ```
/// use kernel::sync::Arc;
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// // Create a ref-counted instance of `Example`.
/// let obj = Arc::try_new(Example { a: 10, b: 20 })?;
///
/// // Get a new pointer to `obj` and increment the refcount.
/// let cloned = obj.clone();
///
/// // Assert that both `obj` and `cloned` point to the same underlying object.
/// assert!(core::ptr::eq(&*obj, &*cloned));
///
/// // Destroy `obj` and decrement its refcount.
/// drop(obj);
///
/// // Check that the values are still accessible through `cloned`.
/// assert_eq!(cloned.a, 10);
/// assert_eq!(cloned.b, 20);
///
/// // The refcount drops to zero when `cloned` goes out of scope, and the memory is freed.
/// ```
///
/// Using `Arc<T>` as the type of `self`:
///
/// ```
/// use kernel::sync::Arc;
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// impl Example {
///     fn take_over(self: Arc<Self>) {
///         // ...
///     }
///
///     fn use_reference(self: &Arc<Self>) {
///         // ...
///     }
/// }
///
/// let obj = Arc::try_new(Example { a: 10, b: 20 })?;
/// obj.use_reference();
/// obj.take_over();
/// ```
///
/// Coercion from `Arc<Example>` to `Arc<dyn MyTrait>`:
///
/// ```
/// use kernel::sync::{Arc, ArcBorrow};
///
/// trait MyTrait {
///     // Trait has a function whose `self` type is `Arc<Self>`.
///     fn example1(self: Arc<Self>) {}
///
///     // Trait has a function whose `self` type is `ArcBorrow<'_, Self>`.
///     fn example2(self: ArcBorrow<'_, Self>) {}
/// }
///
/// struct Example;
/// impl MyTrait for Example {}
///
/// // `obj` has type `Arc<Example>`.
/// let obj: Arc<Example> = Arc::try_new(Example)?;
///
/// // `coerced` has type `Arc<dyn MyTrait>`.
/// let coerced: Arc<dyn MyTrait> = obj;
/// ```
pub struct Arc<T: ?Sized> {
    ptr: NonNull<ArcInner<T>>,
    _p: PhantomData<ArcInner<T>>,
}

#[repr(C)]
struct ArcInner<T: ?Sized> {
    refcount: Opaque<bindings::refcount_t>,
    data: T,
}

// This is to allow [`Arc`] (and variants) to be used as the type of `self`.
impl<T: ?Sized> core::ops::Receiver for Arc<T> {}

// This is to allow coercion from `Arc<T>` to `Arc<U>` if `T` can be converted to the
// dynamically-sized type (DST) `U`.
impl<T: ?Sized + Unsize<U>, U: ?Sized> core::ops::CoerceUnsized<Arc<U>> for Arc<T> {}

// This is to allow `Arc<U>` to be dispatched on when `Arc<T>` can be coerced into `Arc<U>`.
impl<T: ?Sized + Unsize<U>, U: ?Sized> core::ops::DispatchFromDyn<Arc<U>> for Arc<T> {}

// SAFETY: It is safe to send `Arc<T>` to another thread when the underlying `T` is `Sync` because
// it effectively means sharing `&T` (which is safe because `T` is `Sync`); additionally, it needs
// `T` to be `Send` because any thread that has an `Arc<T>` may ultimately access `T` directly, for
// example, when the reference count reaches zero and `T` is dropped.
unsafe impl<T: ?Sized + Sync + Send> Send for Arc<T> {}

// SAFETY: It is safe to send `&Arc<T>` to another thread when the underlying `T` is `Sync` for the
// same reason as above. `T` needs to be `Send` as well because a thread can clone an `&Arc<T>`
// into an `Arc<T>`, which may lead to `T` being accessed by the same reasoning as above.
unsafe impl<T: ?Sized + Sync + Send> Sync for Arc<T> {}

impl<T> Arc<T> {
    /// Constructs a new reference counted instance of `T`.
    pub fn try_new(contents: T) -> Result<Self> {
        // INVARIANT: The refcount is initialised to a non-zero value.
        let value = ArcInner {
            // SAFETY: There are no safety requirements for this FFI call.
            refcount: Opaque::new(unsafe { bindings::REFCOUNT_INIT(1) }),
            data: contents,
        };

        let inner = Box::try_new(value)?;

        // SAFETY: We just created `inner` with a reference count of 1, which is owned by the new
        // `Arc` object.
        Ok(unsafe { Self::from_inner(Box::leak(inner).into()) })
    }
}

impl<T: ?Sized> Arc<T> {
    /// Constructs a new [`Arc`] from an existing [`ArcInner`].
    ///
    /// # Safety
    ///
    /// The caller must ensure that `inner` points to a valid location and has a non-zero reference
    /// count, one of which will be owned by the new [`Arc`] instance.
    unsafe fn from_inner(inner: NonNull<ArcInner<T>>) -> Self {
        // INVARIANT: By the safety requirements, the invariants hold.
        Arc {
            ptr: inner,
            _p: PhantomData,
        }
    }

    /// Returns an [`ArcBorrow`] from the given [`Arc`].
    ///
    /// This is useful when the argument of a function call is an [`ArcBorrow`] (e.g., in a method
    /// receiver), but we have an [`Arc`] instead. Getting an [`ArcBorrow`] is free when optimised.
    #[inline]
    pub fn as_arc_borrow(&self) -> ArcBorrow<'_, T> {
        // SAFETY: The constraint that the lifetime of the shared reference must outlive that of
        // the returned `ArcBorrow` ensures that the object remains alive and that no mutable
        // reference can be created.
        unsafe { ArcBorrow::new(self.ptr) }
    }
}

impl<T: 'static> ForeignOwnable for Arc<T> {
    type Borrowed<'a> = ArcBorrow<'a, T>;

    fn into_foreign(self) -> *const core::ffi::c_void {
        ManuallyDrop::new(self).ptr.as_ptr() as _
    }

    unsafe fn borrow<'a>(ptr: *const core::ffi::c_void) -> ArcBorrow<'a, T> {
        // SAFETY: By the safety requirement of this function, we know that `ptr` came from
        // a previous call to `Arc::into_foreign`.
        let inner = NonNull::new(ptr as *mut ArcInner<T>).unwrap();

        // SAFETY: The safety requirements of `from_foreign` ensure that the object remains alive
        // for the lifetime of the returned value. Additionally, the safety requirements of
        // `ForeignOwnable::borrow_mut` ensure that no new mutable references are created.
        unsafe { ArcBorrow::new(inner) }
    }

    unsafe fn from_foreign(ptr: *const core::ffi::c_void) -> Self {
        // SAFETY: By the safety requirement of this function, we know that `ptr` came from
        // a previous call to `Arc::into_foreign`, which guarantees that `ptr` is valid and
        // holds a reference count increment that is transferrable to us.
        unsafe { Self::from_inner(NonNull::new(ptr as _).unwrap()) }
    }
}

impl<T: ?Sized> Deref for Arc<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: By the type invariant, there is necessarily a reference to the object, so it is
        // safe to dereference it.
        unsafe { &self.ptr.as_ref().data }
    }
}

impl<T: ?Sized> Clone for Arc<T> {
    fn clone(&self) -> Self {
        // INVARIANT: C `refcount_inc` saturates the refcount, so it cannot overflow to zero.
        // SAFETY: By the type invariant, there is necessarily a reference to the object, so it is
        // safe to increment the refcount.
        unsafe { bindings::refcount_inc(self.ptr.as_ref().refcount.get()) };

        // SAFETY: We just incremented the refcount. This increment is now owned by the new `Arc`.
        unsafe { Self::from_inner(self.ptr) }
    }
}

impl<T: ?Sized> Drop for Arc<T> {
    fn drop(&mut self) {
        // SAFETY: By the type invariant, there is necessarily a reference to the object. We cannot
        // touch `refcount` after it's decremented to a non-zero value because another thread/CPU
        // may concurrently decrement it to zero and free it. It is ok to have a raw pointer to
        // freed/invalid memory as long as it is never dereferenced.
        let refcount = unsafe { self.ptr.as_ref() }.refcount.get();

        // INVARIANT: If the refcount reaches zero, there are no other instances of `Arc`, and
        // this instance is being dropped, so the broken invariant is not observable.
        // SAFETY: Also by the type invariant, we are allowed to decrement the refcount.
        let is_zero = unsafe { bindings::refcount_dec_and_test(refcount) };
        if is_zero {
            // The count reached zero, we must free the memory.
            //
            // SAFETY: The pointer was initialised from the result of `Box::leak`.
            unsafe { Box::from_raw(self.ptr.as_ptr()) };
        }
    }
}

impl<T: ?Sized> From<UniqueArc<T>> for Arc<T> {
    fn from(item: UniqueArc<T>) -> Self {
        item.inner
    }
}

impl<T: ?Sized> From<Pin<UniqueArc<T>>> for Arc<T> {
    fn from(item: Pin<UniqueArc<T>>) -> Self {
        // SAFETY: The type invariants of `Arc` guarantee that the data is pinned.
        unsafe { Pin::into_inner_unchecked(item).inner }
    }
}

/// A borrowed reference to an [`Arc`] instance.
///
/// For cases when one doesn't ever need to increment the refcount on the allocation, it is simpler
/// to use just `&T`, which we can trivially get from an `Arc<T>` instance.
///
/// However, when one may need to increment the refcount, it is preferable to use an `ArcBorrow<T>`
/// over `&Arc<T>` because the latter results in a double-indirection: a pointer (shared reference)
/// to a pointer (`Arc<T>`) to the object (`T`). An [`ArcBorrow`] eliminates this double
/// indirection while still allowing one to increment the refcount and getting an `Arc<T>` when/if
/// needed.
///
/// # Invariants
///
/// There are no mutable references to the underlying [`Arc`], and it remains valid for the
/// lifetime of the [`ArcBorrow`] instance.
///
/// # Example
///
/// ```
/// use crate::sync::{Arc, ArcBorrow};
///
/// struct Example;
///
/// fn do_something(e: ArcBorrow<'_, Example>) -> Arc<Example> {
///     e.into()
/// }
///
/// let obj = Arc::try_new(Example)?;
/// let cloned = do_something(obj.as_arc_borrow());
///
/// // Assert that both `obj` and `cloned` point to the same underlying object.
/// assert!(core::ptr::eq(&*obj, &*cloned));
/// ```
///
/// Using `ArcBorrow<T>` as the type of `self`:
///
/// ```
/// use crate::sync::{Arc, ArcBorrow};
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// impl Example {
///     fn use_reference(self: ArcBorrow<'_, Self>) {
///         // ...
///     }
/// }
///
/// let obj = Arc::try_new(Example { a: 10, b: 20 })?;
/// obj.as_arc_borrow().use_reference();
/// ```
pub struct ArcBorrow<'a, T: ?Sized + 'a> {
    inner: NonNull<ArcInner<T>>,
    _p: PhantomData<&'a ()>,
}

// This is to allow [`ArcBorrow`] (and variants) to be used as the type of `self`.
impl<T: ?Sized> core::ops::Receiver for ArcBorrow<'_, T> {}

// This is to allow `ArcBorrow<U>` to be dispatched on when `ArcBorrow<T>` can be coerced into
// `ArcBorrow<U>`.
impl<T: ?Sized + Unsize<U>, U: ?Sized> core::ops::DispatchFromDyn<ArcBorrow<'_, U>>
    for ArcBorrow<'_, T>
{
}

impl<T: ?Sized> Clone for ArcBorrow<'_, T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T: ?Sized> Copy for ArcBorrow<'_, T> {}

impl<T: ?Sized> ArcBorrow<'_, T> {
    /// Creates a new [`ArcBorrow`] instance.
    ///
    /// # Safety
    ///
    /// Callers must ensure the following for the lifetime of the returned [`ArcBorrow`] instance:
    /// 1. That `inner` remains valid;
    /// 2. That no mutable references to `inner` are created.
    unsafe fn new(inner: NonNull<ArcInner<T>>) -> Self {
        // INVARIANT: The safety requirements guarantee the invariants.
        Self {
            inner,
            _p: PhantomData,
        }
    }
}

impl<T: ?Sized> From<ArcBorrow<'_, T>> for Arc<T> {
    fn from(b: ArcBorrow<'_, T>) -> Self {
        // SAFETY: The existence of `b` guarantees that the refcount is non-zero. `ManuallyDrop`
        // guarantees that `drop` isn't called, so it's ok that the temporary `Arc` doesn't own the
        // increment.
        ManuallyDrop::new(unsafe { Arc::from_inner(b.inner) })
            .deref()
            .clone()
    }
}

impl<T: ?Sized> Deref for ArcBorrow<'_, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: By the type invariant, the underlying object is still alive with no mutable
        // references to it, so it is safe to create a shared reference.
        unsafe { &self.inner.as_ref().data }
    }
}

/// A refcounted object that is known to have a refcount of 1.
///
/// It is mutable and can be converted to an [`Arc`] so that it can be shared.
///
/// # Invariants
///
/// `inner` always has a reference count of 1.
///
/// # Examples
///
/// In the following example, we make changes to the inner object before turning it into an
/// `Arc<Test>` object (after which point, it cannot be mutated directly). Note that `x.into()`
/// cannot fail.
///
/// ```
/// use kernel::sync::{Arc, UniqueArc};
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// fn test() -> Result<Arc<Example>> {
///     let mut x = UniqueArc::try_new(Example { a: 10, b: 20 })?;
///     x.a += 1;
///     x.b += 1;
///     Ok(x.into())
/// }
///
/// # test().unwrap();
/// ```
///
/// In the following example we first allocate memory for a ref-counted `Example` but we don't
/// initialise it on allocation. We do initialise it later with a call to [`UniqueArc::write`],
/// followed by a conversion to `Arc<Example>`. This is particularly useful when allocation happens
/// in one context (e.g., sleepable) and initialisation in another (e.g., atomic):
///
/// ```
/// use kernel::sync::{Arc, UniqueArc};
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// fn test() -> Result<Arc<Example>> {
///     let x = UniqueArc::try_new_uninit()?;
///     Ok(x.write(Example { a: 10, b: 20 }).into())
/// }
///
/// # test().unwrap();
/// ```
///
/// In the last example below, the caller gets a pinned instance of `Example` while converting to
/// `Arc<Example>`; this is useful in scenarios where one needs a pinned reference during
/// initialisation, for example, when initialising fields that are wrapped in locks.
///
/// ```
/// use kernel::sync::{Arc, UniqueArc};
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// fn test() -> Result<Arc<Example>> {
///     let mut pinned = Pin::from(UniqueArc::try_new(Example { a: 10, b: 20 })?);
///     // We can modify `pinned` because it is `Unpin`.
///     pinned.as_mut().a += 1;
///     Ok(pinned.into())
/// }
///
/// # test().unwrap();
/// ```
pub struct UniqueArc<T: ?Sized> {
    inner: Arc<T>,
}

impl<T> UniqueArc<T> {
    /// Tries to allocate a new [`UniqueArc`] instance.
    pub fn try_new(value: T) -> Result<Self> {
        Ok(Self {
            // INVARIANT: The newly-created object has a ref-count of 1.
            inner: Arc::try_new(value)?,
        })
    }

    /// Tries to allocate a new [`UniqueArc`] instance whose contents are not initialised yet.
    pub fn try_new_uninit() -> Result<UniqueArc<MaybeUninit<T>>> {
        Ok(UniqueArc::<MaybeUninit<T>> {
            // INVARIANT: The newly-created object has a ref-count of 1.
            inner: Arc::try_new(MaybeUninit::uninit())?,
        })
    }
}

impl<T> UniqueArc<MaybeUninit<T>> {
    /// Converts a `UniqueArc<MaybeUninit<T>>` into a `UniqueArc<T>` by writing a value into it.
    pub fn write(mut self, value: T) -> UniqueArc<T> {
        self.deref_mut().write(value);
        let inner = ManuallyDrop::new(self).inner.ptr;
        UniqueArc {
            // SAFETY: The new `Arc` is taking over `ptr` from `self.inner` (which won't be
            // dropped). The types are compatible because `MaybeUninit<T>` is compatible with `T`.
            inner: unsafe { Arc::from_inner(inner.cast()) },
        }
    }
}

impl<T: ?Sized> From<UniqueArc<T>> for Pin<UniqueArc<T>> {
    fn from(obj: UniqueArc<T>) -> Self {
        // SAFETY: It is not possible to move/replace `T` inside a `Pin<UniqueArc<T>>` (unless `T`
        // is `Unpin`), so it is ok to convert it to `Pin<UniqueArc<T>>`.
        unsafe { Pin::new_unchecked(obj) }
    }
}

impl<T: ?Sized> Deref for UniqueArc<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.inner.deref()
    }
}

impl<T: ?Sized> DerefMut for UniqueArc<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: By the `Arc` type invariant, there is necessarily a reference to the object, so
        // it is safe to dereference it. Additionally, we know there is only one reference when
        // it's inside a `UniqueArc`, so it is safe to get a mutable reference.
        unsafe { &mut self.inner.ptr.as_mut().data }
    }
}
