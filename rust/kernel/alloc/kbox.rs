// SPDX-License-Identifier: GPL-2.0

//! Implementation of [`Box`].

#[allow(unused_imports)] // Used in doc comments.
use super::allocator::{KVmalloc, Kmalloc, Vmalloc, VmallocPageIter};
use super::{AllocError, Allocator, Flags, NumaNode};
use core::alloc::Layout;
use core::borrow::{Borrow, BorrowMut};
use core::marker::PhantomData;
use core::mem::ManuallyDrop;
use core::mem::MaybeUninit;
use core::ops::{Deref, DerefMut};
use core::pin::Pin;
use core::ptr::NonNull;
use core::result::Result;

use crate::ffi::c_void;
use crate::fmt;
use crate::init::InPlaceInit;
use crate::page::AsPageIter;
use crate::types::ForeignOwnable;
use pin_init::{InPlaceWrite, Init, PinInit, ZeroableOption};

/// The kernel's [`Box`] type -- a heap allocation for a single value of type `T`.
///
/// This is the kernel's version of the Rust stdlib's `Box`. There are several differences,
/// for example no `noalias` attribute is emitted and partially moving out of a `Box` is not
/// supported. There are also several API differences, e.g. `Box` always requires an [`Allocator`]
/// implementation to be passed as generic, page [`Flags`] when allocating memory and all functions
/// that may allocate memory are fallible.
///
/// `Box` works with any of the kernel's allocators, e.g. [`Kmalloc`], [`Vmalloc`] or [`KVmalloc`].
/// There are aliases for `Box` with these allocators ([`KBox`], [`VBox`], [`KVBox`]).
///
/// When dropping a [`Box`], the value is also dropped and the heap memory is automatically freed.
///
/// # Examples
///
/// ```
/// let b = KBox::<u64>::new(24_u64, GFP_KERNEL)?;
///
/// assert_eq!(*b, 24_u64);
/// # Ok::<(), Error>(())
/// ```
///
/// ```
/// # use kernel::bindings;
/// const SIZE: usize = bindings::KMALLOC_MAX_SIZE as usize + 1;
/// struct Huge([u8; SIZE]);
///
/// assert!(KBox::<Huge>::new_uninit(GFP_KERNEL | __GFP_NOWARN).is_err());
/// ```
///
/// ```
/// # use kernel::bindings;
/// const SIZE: usize = bindings::KMALLOC_MAX_SIZE as usize + 1;
/// struct Huge([u8; SIZE]);
///
/// assert!(KVBox::<Huge>::new_uninit(GFP_KERNEL).is_ok());
/// ```
///
/// [`Box`]es can also be used to store trait objects by coercing their type:
///
/// ```
/// trait FooTrait {}
///
/// struct FooStruct;
/// impl FooTrait for FooStruct {}
///
/// let _ = KBox::new(FooStruct, GFP_KERNEL)? as KBox<dyn FooTrait>;
/// # Ok::<(), Error>(())
/// ```
///
/// # Invariants
///
/// `self.0` is always properly aligned and either points to memory allocated with `A` or, for
/// zero-sized types, is a dangling, well aligned pointer.
#[repr(transparent)]
#[cfg_attr(CONFIG_RUSTC_HAS_COERCE_POINTEE, derive(core::marker::CoercePointee))]
pub struct Box<#[cfg_attr(CONFIG_RUSTC_HAS_COERCE_POINTEE, pointee)] T: ?Sized, A: Allocator>(
    NonNull<T>,
    PhantomData<A>,
);

// This is to allow coercion from `Box<T, A>` to `Box<U, A>` if `T` can be converted to the
// dynamically-sized type (DST) `U`.
#[cfg(not(CONFIG_RUSTC_HAS_COERCE_POINTEE))]
impl<T, U, A> core::ops::CoerceUnsized<Box<U, A>> for Box<T, A>
where
    T: ?Sized + core::marker::Unsize<U>,
    U: ?Sized,
    A: Allocator,
{
}

// This is to allow `Box<U, A>` to be dispatched on when `Box<T, A>` can be coerced into `Box<U,
// A>`.
#[cfg(not(CONFIG_RUSTC_HAS_COERCE_POINTEE))]
impl<T, U, A> core::ops::DispatchFromDyn<Box<U, A>> for Box<T, A>
where
    T: ?Sized + core::marker::Unsize<U>,
    U: ?Sized,
    A: Allocator,
{
}

/// Type alias for [`Box`] with a [`Kmalloc`] allocator.
///
/// # Examples
///
/// ```
/// let b = KBox::new(24_u64, GFP_KERNEL)?;
///
/// assert_eq!(*b, 24_u64);
/// # Ok::<(), Error>(())
/// ```
pub type KBox<T> = Box<T, super::allocator::Kmalloc>;

/// Type alias for [`Box`] with a [`Vmalloc`] allocator.
///
/// # Examples
///
/// ```
/// let b = VBox::new(24_u64, GFP_KERNEL)?;
///
/// assert_eq!(*b, 24_u64);
/// # Ok::<(), Error>(())
/// ```
pub type VBox<T> = Box<T, super::allocator::Vmalloc>;

/// Type alias for [`Box`] with a [`KVmalloc`] allocator.
///
/// # Examples
///
/// ```
/// let b = KVBox::new(24_u64, GFP_KERNEL)?;
///
/// assert_eq!(*b, 24_u64);
/// # Ok::<(), Error>(())
/// ```
pub type KVBox<T> = Box<T, super::allocator::KVmalloc>;

// SAFETY: All zeros is equivalent to `None` (option layout optimization guarantee:
// <https://doc.rust-lang.org/stable/std/option/index.html#representation>).
unsafe impl<T, A: Allocator> ZeroableOption for Box<T, A> {}

// SAFETY: `Box` is `Send` if `T` is `Send` because the `Box` owns a `T`.
unsafe impl<T, A> Send for Box<T, A>
where
    T: Send + ?Sized,
    A: Allocator,
{
}

// SAFETY: `Box` is `Sync` if `T` is `Sync` because the `Box` owns a `T`.
unsafe impl<T, A> Sync for Box<T, A>
where
    T: Sync + ?Sized,
    A: Allocator,
{
}

impl<T, A> Box<T, A>
where
    T: ?Sized,
    A: Allocator,
{
    /// Creates a new `Box<T, A>` from a raw pointer.
    ///
    /// # Safety
    ///
    /// For non-ZSTs, `raw` must point at an allocation allocated with `A` that is sufficiently
    /// aligned for and holds a valid `T`. The caller passes ownership of the allocation to the
    /// `Box`.
    ///
    /// For ZSTs, `raw` must be a dangling, well aligned pointer.
    #[inline]
    pub const unsafe fn from_raw(raw: *mut T) -> Self {
        // INVARIANT: Validity of `raw` is guaranteed by the safety preconditions of this function.
        // SAFETY: By the safety preconditions of this function, `raw` is not a NULL pointer.
        Self(unsafe { NonNull::new_unchecked(raw) }, PhantomData)
    }

    /// Consumes the `Box<T, A>` and returns a raw pointer.
    ///
    /// This will not run the destructor of `T` and for non-ZSTs the allocation will stay alive
    /// indefinitely. Use [`Box::from_raw`] to recover the [`Box`], drop the value and free the
    /// allocation, if any.
    ///
    /// # Examples
    ///
    /// ```
    /// let x = KBox::new(24, GFP_KERNEL)?;
    /// let ptr = KBox::into_raw(x);
    /// // SAFETY: `ptr` comes from a previous call to `KBox::into_raw`.
    /// let x = unsafe { KBox::from_raw(ptr) };
    ///
    /// assert_eq!(*x, 24);
    /// # Ok::<(), Error>(())
    /// ```
    #[inline]
    pub fn into_raw(b: Self) -> *mut T {
        ManuallyDrop::new(b).0.as_ptr()
    }

    /// Consumes and leaks the `Box<T, A>` and returns a mutable reference.
    ///
    /// See [`Box::into_raw`] for more details.
    #[inline]
    pub fn leak<'a>(b: Self) -> &'a mut T {
        // SAFETY: `Box::into_raw` always returns a properly aligned and dereferenceable pointer
        // which points to an initialized instance of `T`.
        unsafe { &mut *Box::into_raw(b) }
    }
}

impl<T, A> Box<MaybeUninit<T>, A>
where
    A: Allocator,
{
    /// Converts a `Box<MaybeUninit<T>, A>` to a `Box<T, A>`.
    ///
    /// It is undefined behavior to call this function while the value inside of `b` is not yet
    /// fully initialized.
    ///
    /// # Safety
    ///
    /// Callers must ensure that the value inside of `b` is in an initialized state.
    pub unsafe fn assume_init(self) -> Box<T, A> {
        let raw = Self::into_raw(self);

        // SAFETY: `raw` comes from a previous call to `Box::into_raw`. By the safety requirements
        // of this function, the value inside the `Box` is in an initialized state. Hence, it is
        // safe to reconstruct the `Box` as `Box<T, A>`.
        unsafe { Box::from_raw(raw.cast()) }
    }

    /// Writes the value and converts to `Box<T, A>`.
    pub fn write(mut self, value: T) -> Box<T, A> {
        (*self).write(value);

        // SAFETY: We've just initialized `b`'s value.
        unsafe { self.assume_init() }
    }
}

impl<T, A> Box<T, A>
where
    A: Allocator,
{
    /// Creates a new `Box<T, A>` and initializes its contents with `x`.
    ///
    /// New memory is allocated with `A`. The allocation may fail, in which case an error is
    /// returned. For ZSTs no memory is allocated.
    pub fn new(x: T, flags: Flags) -> Result<Self, AllocError> {
        let b = Self::new_uninit(flags)?;
        Ok(Box::write(b, x))
    }

    /// Creates a new `Box<T, A>` with uninitialized contents.
    ///
    /// New memory is allocated with `A`. The allocation may fail, in which case an error is
    /// returned. For ZSTs no memory is allocated.
    ///
    /// # Examples
    ///
    /// ```
    /// let b = KBox::<u64>::new_uninit(GFP_KERNEL)?;
    /// let b = KBox::write(b, 24);
    ///
    /// assert_eq!(*b, 24_u64);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn new_uninit(flags: Flags) -> Result<Box<MaybeUninit<T>, A>, AllocError> {
        let layout = Layout::new::<MaybeUninit<T>>();
        let ptr = A::alloc(layout, flags, NumaNode::NO_NODE)?;

        // INVARIANT: `ptr` is either a dangling pointer or points to memory allocated with `A`,
        // which is sufficient in size and alignment for storing a `T`.
        Ok(Box(ptr.cast(), PhantomData))
    }

    /// Constructs a new `Pin<Box<T, A>>`. If `T` does not implement [`Unpin`], then `x` will be
    /// pinned in memory and can't be moved.
    #[inline]
    pub fn pin(x: T, flags: Flags) -> Result<Pin<Box<T, A>>, AllocError>
    where
        A: 'static,
    {
        Ok(Self::new(x, flags)?.into())
    }

    /// Construct a pinned slice of elements `Pin<Box<[T], A>>`.
    ///
    /// This is a convenient means for creation of e.g. slices of structrures containing spinlocks
    /// or mutexes.
    ///
    /// # Examples
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
    ///     fn new() -> impl PinInit<Self, Error> {
    ///         try_pin_init!(Self {
    ///             c: 10,
    ///             d <- new_spinlock!(Inner { a: 20, b: 30 }),
    ///         })
    ///     }
    /// }
    ///
    /// // Allocate a boxed slice of 10 `Example`s.
    /// let s = KBox::pin_slice(
    ///     | _i | Example::new(),
    ///     10,
    ///     GFP_KERNEL
    /// )?;
    ///
    /// assert_eq!(s[5].c, 10);
    /// assert_eq!(s[3].d.lock().a, 20);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn pin_slice<Func, Item, E>(
        mut init: Func,
        len: usize,
        flags: Flags,
    ) -> Result<Pin<Box<[T], A>>, E>
    where
        Func: FnMut(usize) -> Item,
        Item: PinInit<T, E>,
        E: From<AllocError>,
    {
        let mut buffer = super::Vec::<T, A>::with_capacity(len, flags)?;
        for i in 0..len {
            let ptr = buffer.spare_capacity_mut().as_mut_ptr().cast();
            // SAFETY:
            // - `ptr` is a valid pointer to uninitialized memory.
            // - `ptr` is not used if an error is returned.
            // - `ptr` won't be moved until it is dropped, i.e. it is pinned.
            unsafe { init(i).__pinned_init(ptr)? };

            // SAFETY:
            // - `i + 1 <= len`, hence we don't exceed the capacity, due to the call to
            //   `with_capacity()` above.
            // - The new value at index buffer.len() + 1 is the only element being added here, and
            //   it has been initialized above by `init(i).__pinned_init(ptr)`.
            unsafe { buffer.inc_len(1) };
        }

        let (ptr, _, _) = buffer.into_raw_parts();
        let slice = core::ptr::slice_from_raw_parts_mut(ptr, len);

        // SAFETY: `slice` points to an allocation allocated with `A` (`buffer`) and holds a valid
        // `[T]`.
        Ok(Pin::from(unsafe { Box::from_raw(slice) }))
    }

    /// Convert a [`Box<T,A>`] to a [`Pin<Box<T,A>>`]. If `T` does not implement
    /// [`Unpin`], then `x` will be pinned in memory and can't be moved.
    pub fn into_pin(this: Self) -> Pin<Self> {
        this.into()
    }

    /// Forgets the contents (does not run the destructor), but keeps the allocation.
    fn forget_contents(this: Self) -> Box<MaybeUninit<T>, A> {
        let ptr = Self::into_raw(this);

        // SAFETY: `ptr` is valid, because it came from `Box::into_raw`.
        unsafe { Box::from_raw(ptr.cast()) }
    }

    /// Drops the contents, but keeps the allocation.
    ///
    /// # Examples
    ///
    /// ```
    /// let value = KBox::new([0; 32], GFP_KERNEL)?;
    /// assert_eq!(*value, [0; 32]);
    /// let value = KBox::drop_contents(value);
    /// // Now we can re-use `value`:
    /// let value = KBox::write(value, [1; 32]);
    /// assert_eq!(*value, [1; 32]);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn drop_contents(this: Self) -> Box<MaybeUninit<T>, A> {
        let ptr = this.0.as_ptr();

        // SAFETY: `ptr` is valid, because it came from `this`. After this call we never access the
        // value stored in `this` again.
        unsafe { core::ptr::drop_in_place(ptr) };

        Self::forget_contents(this)
    }

    /// Moves the `Box`'s value out of the `Box` and consumes the `Box`.
    pub fn into_inner(b: Self) -> T {
        // SAFETY: By the type invariant `&*b` is valid for `read`.
        let value = unsafe { core::ptr::read(&*b) };
        let _ = Self::forget_contents(b);
        value
    }
}

impl<T, A> From<Box<T, A>> for Pin<Box<T, A>>
where
    T: ?Sized,
    A: Allocator,
{
    /// Converts a `Box<T, A>` into a `Pin<Box<T, A>>`. If `T` does not implement [`Unpin`], then
    /// `*b` will be pinned in memory and can't be moved.
    ///
    /// This moves `b` into `Pin` without moving `*b` or allocating and copying any memory.
    fn from(b: Box<T, A>) -> Self {
        // SAFETY: The value wrapped inside a `Pin<Box<T, A>>` cannot be moved or replaced as long
        // as `T` does not implement `Unpin`.
        unsafe { Pin::new_unchecked(b) }
    }
}

impl<T, A> InPlaceWrite<T> for Box<MaybeUninit<T>, A>
where
    A: Allocator + 'static,
{
    type Initialized = Box<T, A>;

    fn write_init<E>(mut self, init: impl Init<T, E>) -> Result<Self::Initialized, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid.
        unsafe { init.__init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { Box::assume_init(self) })
    }

    fn write_pin_init<E>(mut self, init: impl PinInit<T, E>) -> Result<Pin<Self::Initialized>, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid and will not be moved, because we pin it later.
        unsafe { init.__pinned_init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { Box::assume_init(self) }.into())
    }
}

impl<T, A> InPlaceInit<T> for Box<T, A>
where
    A: Allocator + 'static,
{
    type PinnedSelf = Pin<Self>;

    #[inline]
    fn try_pin_init<E>(init: impl PinInit<T, E>, flags: Flags) -> Result<Pin<Self>, E>
    where
        E: From<AllocError>,
    {
        Box::<_, A>::new_uninit(flags)?.write_pin_init(init)
    }

    #[inline]
    fn try_init<E>(init: impl Init<T, E>, flags: Flags) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        Box::<_, A>::new_uninit(flags)?.write_init(init)
    }
}

// SAFETY: The pointer returned by `into_foreign` comes from a well aligned
// pointer to `T` allocated by `A`.
unsafe impl<T: 'static, A> ForeignOwnable for Box<T, A>
where
    A: Allocator,
{
    const FOREIGN_ALIGN: usize = if core::mem::align_of::<T>() < A::MIN_ALIGN {
        A::MIN_ALIGN
    } else {
        core::mem::align_of::<T>()
    };

    type Borrowed<'a> = &'a T;
    type BorrowedMut<'a> = &'a mut T;

    fn into_foreign(self) -> *mut c_void {
        Box::into_raw(self).cast()
    }

    unsafe fn from_foreign(ptr: *mut c_void) -> Self {
        // SAFETY: The safety requirements of this function ensure that `ptr` comes from a previous
        // call to `Self::into_foreign`.
        unsafe { Box::from_raw(ptr.cast()) }
    }

    unsafe fn borrow<'a>(ptr: *mut c_void) -> &'a T {
        // SAFETY: The safety requirements of this method ensure that the object remains alive and
        // immutable for the duration of 'a.
        unsafe { &*ptr.cast() }
    }

    unsafe fn borrow_mut<'a>(ptr: *mut c_void) -> &'a mut T {
        let ptr = ptr.cast();
        // SAFETY: The safety requirements of this method ensure that the pointer is valid and that
        // nothing else will access the value for the duration of 'a.
        unsafe { &mut *ptr }
    }
}

// SAFETY: The pointer returned by `into_foreign` comes from a well aligned
// pointer to `T` allocated by `A`.
unsafe impl<T: 'static, A> ForeignOwnable for Pin<Box<T, A>>
where
    A: Allocator,
{
    const FOREIGN_ALIGN: usize = <Box<T, A> as ForeignOwnable>::FOREIGN_ALIGN;
    type Borrowed<'a> = Pin<&'a T>;
    type BorrowedMut<'a> = Pin<&'a mut T>;

    fn into_foreign(self) -> *mut c_void {
        // SAFETY: We are still treating the box as pinned.
        Box::into_raw(unsafe { Pin::into_inner_unchecked(self) }).cast()
    }

    unsafe fn from_foreign(ptr: *mut c_void) -> Self {
        // SAFETY: The safety requirements of this function ensure that `ptr` comes from a previous
        // call to `Self::into_foreign`.
        unsafe { Pin::new_unchecked(Box::from_raw(ptr.cast())) }
    }

    unsafe fn borrow<'a>(ptr: *mut c_void) -> Pin<&'a T> {
        // SAFETY: The safety requirements for this function ensure that the object is still alive,
        // so it is safe to dereference the raw pointer.
        // The safety requirements of `from_foreign` also ensure that the object remains alive for
        // the lifetime of the returned value.
        let r = unsafe { &*ptr.cast() };

        // SAFETY: This pointer originates from a `Pin<Box<T>>`.
        unsafe { Pin::new_unchecked(r) }
    }

    unsafe fn borrow_mut<'a>(ptr: *mut c_void) -> Pin<&'a mut T> {
        let ptr = ptr.cast();
        // SAFETY: The safety requirements for this function ensure that the object is still alive,
        // so it is safe to dereference the raw pointer.
        // The safety requirements of `from_foreign` also ensure that the object remains alive for
        // the lifetime of the returned value.
        let r = unsafe { &mut *ptr };

        // SAFETY: This pointer originates from a `Pin<Box<T>>`.
        unsafe { Pin::new_unchecked(r) }
    }
}

impl<T, A> Deref for Box<T, A>
where
    T: ?Sized,
    A: Allocator,
{
    type Target = T;

    fn deref(&self) -> &T {
        // SAFETY: `self.0` is always properly aligned, dereferenceable and points to an initialized
        // instance of `T`.
        unsafe { self.0.as_ref() }
    }
}

impl<T, A> DerefMut for Box<T, A>
where
    T: ?Sized,
    A: Allocator,
{
    fn deref_mut(&mut self) -> &mut T {
        // SAFETY: `self.0` is always properly aligned, dereferenceable and points to an initialized
        // instance of `T`.
        unsafe { self.0.as_mut() }
    }
}

/// # Examples
///
/// ```
/// # use core::borrow::Borrow;
/// # use kernel::alloc::KBox;
/// struct Foo<B: Borrow<u32>>(B);
///
/// // Owned instance.
/// let owned = Foo(1);
///
/// // Owned instance using `KBox`.
/// let owned_kbox = Foo(KBox::new(1, GFP_KERNEL)?);
///
/// let i = 1;
/// // Borrowed from `i`.
/// let borrowed = Foo(&i);
/// # Ok::<(), Error>(())
/// ```
impl<T, A> Borrow<T> for Box<T, A>
where
    T: ?Sized,
    A: Allocator,
{
    fn borrow(&self) -> &T {
        self.deref()
    }
}

/// # Examples
///
/// ```
/// # use core::borrow::BorrowMut;
/// # use kernel::alloc::KBox;
/// struct Foo<B: BorrowMut<u32>>(B);
///
/// // Owned instance.
/// let owned = Foo(1);
///
/// // Owned instance using `KBox`.
/// let owned_kbox = Foo(KBox::new(1, GFP_KERNEL)?);
///
/// let mut i = 1;
/// // Borrowed from `i`.
/// let borrowed = Foo(&mut i);
/// # Ok::<(), Error>(())
/// ```
impl<T, A> BorrowMut<T> for Box<T, A>
where
    T: ?Sized,
    A: Allocator,
{
    fn borrow_mut(&mut self) -> &mut T {
        self.deref_mut()
    }
}

impl<T, A> fmt::Display for Box<T, A>
where
    T: ?Sized + fmt::Display,
    A: Allocator,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        <T as fmt::Display>::fmt(&**self, f)
    }
}

impl<T, A> fmt::Debug for Box<T, A>
where
    T: ?Sized + fmt::Debug,
    A: Allocator,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        <T as fmt::Debug>::fmt(&**self, f)
    }
}

impl<T, A> Drop for Box<T, A>
where
    T: ?Sized,
    A: Allocator,
{
    fn drop(&mut self) {
        let layout = Layout::for_value::<T>(self);

        // SAFETY: The pointer in `self.0` is guaranteed to be valid by the type invariant.
        unsafe { core::ptr::drop_in_place::<T>(self.deref_mut()) };

        // SAFETY:
        // - `self.0` was previously allocated with `A`.
        // - `layout` is equal to the `Layout´ `self.0` was allocated with.
        unsafe { A::free(self.0.cast(), layout) };
    }
}

/// # Examples
///
/// ```
/// # use kernel::prelude::*;
/// use kernel::alloc::allocator::VmallocPageIter;
/// use kernel::page::{AsPageIter, PAGE_SIZE};
///
/// let mut vbox = VBox::new((), GFP_KERNEL)?;
///
/// assert!(vbox.page_iter().next().is_none());
///
/// let mut vbox = VBox::<[u8; PAGE_SIZE]>::new_uninit(GFP_KERNEL)?;
///
/// let page = vbox.page_iter().next().expect("At least one page should be available.\n");
///
/// // SAFETY: There is no concurrent read or write to the same page.
/// unsafe { page.fill_zero_raw(0, PAGE_SIZE)? };
/// # Ok::<(), Error>(())
/// ```
impl<T> AsPageIter for VBox<T> {
    type Iter<'a>
        = VmallocPageIter<'a>
    where
        T: 'a;

    fn page_iter(&mut self) -> Self::Iter<'_> {
        let ptr = self.0.cast();
        let size = core::mem::size_of::<T>();

        // SAFETY:
        // - `ptr` is a valid pointer to the beginning of a `Vmalloc` allocation.
        // - `ptr` is guaranteed to be valid for the lifetime of `'a`.
        // - `size` is the size of the `Vmalloc` allocation `ptr` points to.
        unsafe { VmallocPageIter::new(ptr, size) }
    }
}
