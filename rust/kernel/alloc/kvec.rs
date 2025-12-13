// SPDX-License-Identifier: GPL-2.0

//! Implementation of [`Vec`].

use super::{
    allocator::{KVmalloc, Kmalloc, Vmalloc, VmallocPageIter},
    layout::ArrayLayout,
    AllocError, Allocator, Box, Flags, NumaNode,
};
use crate::{
    fmt,
    page::AsPageIter, //
};
use core::{
    borrow::{Borrow, BorrowMut},
    marker::PhantomData,
    mem::{ManuallyDrop, MaybeUninit},
    ops::Deref,
    ops::DerefMut,
    ops::Index,
    ops::IndexMut,
    ptr,
    ptr::NonNull,
    slice,
    slice::SliceIndex,
};

mod errors;
pub use self::errors::{InsertError, PushError, RemoveError};

/// Create a [`KVec`] containing the arguments.
///
/// New memory is allocated with `GFP_KERNEL`.
///
/// # Examples
///
/// ```
/// let mut v = kernel::kvec![];
/// v.push(1, GFP_KERNEL)?;
/// assert_eq!(v, [1]);
///
/// let mut v = kernel::kvec![1; 3]?;
/// v.push(4, GFP_KERNEL)?;
/// assert_eq!(v, [1, 1, 1, 4]);
///
/// let mut v = kernel::kvec![1, 2, 3]?;
/// v.push(4, GFP_KERNEL)?;
/// assert_eq!(v, [1, 2, 3, 4]);
///
/// # Ok::<(), Error>(())
/// ```
#[macro_export]
macro_rules! kvec {
    () => (
        $crate::alloc::KVec::new()
    );
    ($elem:expr; $n:expr) => (
        $crate::alloc::KVec::from_elem($elem, $n, GFP_KERNEL)
    );
    ($($x:expr),+ $(,)?) => (
        match $crate::alloc::KBox::new_uninit(GFP_KERNEL) {
            Ok(b) => Ok($crate::alloc::KVec::from($crate::alloc::KBox::write(b, [$($x),+]))),
            Err(e) => Err(e),
        }
    );
}

/// The kernel's [`Vec`] type.
///
/// A contiguous growable array type with contents allocated with the kernel's allocators (e.g.
/// [`Kmalloc`], [`Vmalloc`] or [`KVmalloc`]), written `Vec<T, A>`.
///
/// For non-zero-sized values, a [`Vec`] will use the given allocator `A` for its allocation. For
/// the most common allocators the type aliases [`KVec`], [`VVec`] and [`KVVec`] exist.
///
/// For zero-sized types the [`Vec`]'s pointer must be `dangling_mut::<T>`; no memory is allocated.
///
/// Generally, [`Vec`] consists of a pointer that represents the vector's backing buffer, the
/// capacity of the vector (the number of elements that currently fit into the vector), its length
/// (the number of elements that are currently stored in the vector) and the `Allocator` type used
/// to allocate (and free) the backing buffer.
///
/// A [`Vec`] can be deconstructed into and (re-)constructed from its previously named raw parts
/// and manually modified.
///
/// [`Vec`]'s backing buffer gets, if required, automatically increased (re-allocated) when elements
/// are added to the vector.
///
/// # Invariants
///
/// - `self.ptr` is always properly aligned and either points to memory allocated with `A` or, for
///   zero-sized types, is a dangling, well aligned pointer.
///
/// - `self.len` always represents the exact number of elements stored in the vector.
///
/// - `self.layout` represents the absolute number of elements that can be stored within the vector
///   without re-allocation. For ZSTs `self.layout`'s capacity is zero. However, it is legal for the
///   backing buffer to be larger than `layout`.
///
/// - `self.len()` is always less than or equal to `self.capacity()`.
///
/// - The `Allocator` type `A` of the vector is the exact same `Allocator` type the backing buffer
///   was allocated with (and must be freed with).
pub struct Vec<T, A: Allocator> {
    ptr: NonNull<T>,
    /// Represents the actual buffer size as `cap` times `size_of::<T>` bytes.
    ///
    /// Note: This isn't quite the same as `Self::capacity`, which in contrast returns the number of
    /// elements we can still store without reallocating.
    layout: ArrayLayout<T>,
    len: usize,
    _p: PhantomData<A>,
}

/// Type alias for [`Vec`] with a [`Kmalloc`] allocator.
///
/// # Examples
///
/// ```
/// let mut v = KVec::new();
/// v.push(1, GFP_KERNEL)?;
/// assert_eq!(&v, &[1]);
///
/// # Ok::<(), Error>(())
/// ```
pub type KVec<T> = Vec<T, Kmalloc>;

/// Type alias for [`Vec`] with a [`Vmalloc`] allocator.
///
/// # Examples
///
/// ```
/// let mut v = VVec::new();
/// v.push(1, GFP_KERNEL)?;
/// assert_eq!(&v, &[1]);
///
/// # Ok::<(), Error>(())
/// ```
pub type VVec<T> = Vec<T, Vmalloc>;

/// Type alias for [`Vec`] with a [`KVmalloc`] allocator.
///
/// # Examples
///
/// ```
/// let mut v = KVVec::new();
/// v.push(1, GFP_KERNEL)?;
/// assert_eq!(&v, &[1]);
///
/// # Ok::<(), Error>(())
/// ```
pub type KVVec<T> = Vec<T, KVmalloc>;

// SAFETY: `Vec` is `Send` if `T` is `Send` because `Vec` owns its elements.
unsafe impl<T, A> Send for Vec<T, A>
where
    T: Send,
    A: Allocator,
{
}

// SAFETY: `Vec` is `Sync` if `T` is `Sync` because `Vec` owns its elements.
unsafe impl<T, A> Sync for Vec<T, A>
where
    T: Sync,
    A: Allocator,
{
}

impl<T, A> Vec<T, A>
where
    A: Allocator,
{
    #[inline]
    const fn is_zst() -> bool {
        core::mem::size_of::<T>() == 0
    }

    /// Returns the number of elements that can be stored within the vector without allocating
    /// additional memory.
    pub const fn capacity(&self) -> usize {
        if const { Self::is_zst() } {
            usize::MAX
        } else {
            self.layout.len()
        }
    }

    /// Returns the number of elements stored within the vector.
    #[inline]
    pub const fn len(&self) -> usize {
        self.len
    }

    /// Increments `self.len` by `additional`.
    ///
    /// # Safety
    ///
    /// - `additional` must be less than or equal to `self.capacity - self.len`.
    /// - All elements within the interval [`self.len`,`self.len + additional`) must be initialized.
    #[inline]
    pub const unsafe fn inc_len(&mut self, additional: usize) {
        // Guaranteed by the type invariant to never underflow.
        debug_assert!(additional <= self.capacity() - self.len());
        // INVARIANT: By the safety requirements of this method this represents the exact number of
        // elements stored within `self`.
        self.len += additional;
    }

    /// Decreases `self.len` by `count`.
    ///
    /// Returns a mutable slice to the elements forgotten by the vector. It is the caller's
    /// responsibility to drop these elements if necessary.
    ///
    /// # Safety
    ///
    /// - `count` must be less than or equal to `self.len`.
    unsafe fn dec_len(&mut self, count: usize) -> &mut [T] {
        debug_assert!(count <= self.len());
        // INVARIANT: We relinquish ownership of the elements within the range `[self.len - count,
        // self.len)`, hence the updated value of `set.len` represents the exact number of elements
        // stored within `self`.
        self.len -= count;
        // SAFETY: The memory after `self.len()` is guaranteed to contain `count` initialized
        // elements of type `T`.
        unsafe { slice::from_raw_parts_mut(self.as_mut_ptr().add(self.len), count) }
    }

    /// Returns a slice of the entire vector.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = KVec::new();
    /// v.push(1, GFP_KERNEL)?;
    /// v.push(2, GFP_KERNEL)?;
    /// assert_eq!(v.as_slice(), &[1, 2]);
    /// # Ok::<(), Error>(())
    /// ```
    #[inline]
    pub fn as_slice(&self) -> &[T] {
        self
    }

    /// Returns a mutable slice of the entire vector.
    #[inline]
    pub fn as_mut_slice(&mut self) -> &mut [T] {
        self
    }

    /// Returns a mutable raw pointer to the vector's backing buffer, or, if `T` is a ZST, a
    /// dangling raw pointer.
    #[inline]
    pub fn as_mut_ptr(&mut self) -> *mut T {
        self.ptr.as_ptr()
    }

    /// Returns a raw pointer to the vector's backing buffer, or, if `T` is a ZST, a dangling raw
    /// pointer.
    #[inline]
    pub const fn as_ptr(&self) -> *const T {
        self.ptr.as_ptr()
    }

    /// Returns `true` if the vector contains no elements, `false` otherwise.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = KVec::new();
    /// assert!(v.is_empty());
    ///
    /// v.push(1, GFP_KERNEL);
    /// assert!(!v.is_empty());
    /// ```
    #[inline]
    pub const fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Creates a new, empty `Vec<T, A>`.
    ///
    /// This method does not allocate by itself.
    #[inline]
    pub const fn new() -> Self {
        // INVARIANT: Since this is a new, empty `Vec` with no backing memory yet,
        // - `ptr` is a properly aligned dangling pointer for type `T`,
        // - `layout` is an empty `ArrayLayout` (zero capacity)
        // - `len` is zero, since no elements can be or have been stored,
        // - `A` is always valid.
        Self {
            ptr: NonNull::dangling(),
            layout: ArrayLayout::empty(),
            len: 0,
            _p: PhantomData::<A>,
        }
    }

    /// Returns a slice of `MaybeUninit<T>` for the remaining spare capacity of the vector.
    pub fn spare_capacity_mut(&mut self) -> &mut [MaybeUninit<T>] {
        // SAFETY:
        // - `self.len` is smaller than `self.capacity` by the type invariant and hence, the
        //   resulting pointer is guaranteed to be part of the same allocated object.
        // - `self.len` can not overflow `isize`.
        let ptr = unsafe { self.as_mut_ptr().add(self.len) }.cast::<MaybeUninit<T>>();

        // SAFETY: The memory between `self.len` and `self.capacity` is guaranteed to be allocated
        // and valid, but uninitialized.
        unsafe { slice::from_raw_parts_mut(ptr, self.capacity() - self.len) }
    }

    /// Appends an element to the back of the [`Vec`] instance.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = KVec::new();
    /// v.push(1, GFP_KERNEL)?;
    /// assert_eq!(&v, &[1]);
    ///
    /// v.push(2, GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 2]);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn push(&mut self, v: T, flags: Flags) -> Result<(), AllocError> {
        self.reserve(1, flags)?;
        // SAFETY: The call to `reserve` was successful, so the capacity is at least one greater
        // than the length.
        unsafe { self.push_within_capacity_unchecked(v) };
        Ok(())
    }

    /// Appends an element to the back of the [`Vec`] instance without reallocating.
    ///
    /// Fails if the vector does not have capacity for the new element.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = KVec::with_capacity(10, GFP_KERNEL)?;
    /// for i in 0..10 {
    ///     v.push_within_capacity(i)?;
    /// }
    ///
    /// assert!(v.push_within_capacity(10).is_err());
    /// # Ok::<(), Error>(())
    /// ```
    pub fn push_within_capacity(&mut self, v: T) -> Result<(), PushError<T>> {
        if self.len() < self.capacity() {
            // SAFETY: The length is less than the capacity.
            unsafe { self.push_within_capacity_unchecked(v) };
            Ok(())
        } else {
            Err(PushError(v))
        }
    }

    /// Appends an element to the back of the [`Vec`] instance without reallocating.
    ///
    /// # Safety
    ///
    /// The length must be less than the capacity.
    unsafe fn push_within_capacity_unchecked(&mut self, v: T) {
        let spare = self.spare_capacity_mut();

        // SAFETY: By the safety requirements, `spare` is non-empty.
        unsafe { spare.get_unchecked_mut(0) }.write(v);

        // SAFETY: We just initialised the first spare entry, so it is safe to increase the length
        // by 1. We also know that the new length is <= capacity because the caller guarantees that
        // the length is less than the capacity at the beginning of this function.
        unsafe { self.inc_len(1) };
    }

    /// Inserts an element at the given index in the [`Vec`] instance.
    ///
    /// Fails if the vector does not have capacity for the new element. Panics if the index is out
    /// of bounds.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::alloc::kvec::InsertError;
    ///
    /// let mut v = KVec::with_capacity(5, GFP_KERNEL)?;
    /// for i in 0..5 {
    ///     v.insert_within_capacity(0, i)?;
    /// }
    ///
    /// assert!(matches!(v.insert_within_capacity(0, 5), Err(InsertError::OutOfCapacity(_))));
    /// assert!(matches!(v.insert_within_capacity(1000, 5), Err(InsertError::IndexOutOfBounds(_))));
    /// assert_eq!(v, [4, 3, 2, 1, 0]);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn insert_within_capacity(
        &mut self,
        index: usize,
        element: T,
    ) -> Result<(), InsertError<T>> {
        let len = self.len();
        if index > len {
            return Err(InsertError::IndexOutOfBounds(element));
        }

        if len >= self.capacity() {
            return Err(InsertError::OutOfCapacity(element));
        }

        // SAFETY: This is in bounds since `index <= len < capacity`.
        let p = unsafe { self.as_mut_ptr().add(index) };
        // INVARIANT: This breaks the Vec invariants by making `index` contain an invalid element,
        // but we restore the invariants below.
        // SAFETY: Both the src and dst ranges end no later than one element after the length.
        // Since the length is less than the capacity, both ranges are in bounds of the allocation.
        unsafe { ptr::copy(p, p.add(1), len - index) };
        // INVARIANT: This restores the Vec invariants.
        // SAFETY: The pointer is in-bounds of the allocation.
        unsafe { ptr::write(p, element) };
        // SAFETY: Index `len` contains a valid element due to the above copy and write.
        unsafe { self.inc_len(1) };
        Ok(())
    }

    /// Removes the last element from a vector and returns it, or `None` if it is empty.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = KVec::new();
    /// v.push(1, GFP_KERNEL)?;
    /// v.push(2, GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 2]);
    ///
    /// assert_eq!(v.pop(), Some(2));
    /// assert_eq!(v.pop(), Some(1));
    /// assert_eq!(v.pop(), None);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn pop(&mut self) -> Option<T> {
        if self.is_empty() {
            return None;
        }

        let removed: *mut T = {
            // SAFETY: We just checked that the length is at least one.
            let slice = unsafe { self.dec_len(1) };
            // SAFETY: The argument to `dec_len` was 1 so this returns a slice of length 1.
            unsafe { slice.get_unchecked_mut(0) }
        };

        // SAFETY: The guarantees of `dec_len` allow us to take ownership of this value.
        Some(unsafe { removed.read() })
    }

    /// Removes the element at the given index.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = kernel::kvec![1, 2, 3]?;
    /// assert_eq!(v.remove(1)?, 2);
    /// assert_eq!(v, [1, 3]);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn remove(&mut self, i: usize) -> Result<T, RemoveError> {
        let value = {
            let value_ref = self.get(i).ok_or(RemoveError)?;
            // INVARIANT: This breaks the invariants by invalidating the value at index `i`, but we
            // restore the invariants below.
            // SAFETY: The value at index `i` is valid, because otherwise we would have already
            // failed with `RemoveError`.
            unsafe { ptr::read(value_ref) }
        };

        // SAFETY: We checked that `i` is in-bounds.
        let p = unsafe { self.as_mut_ptr().add(i) };

        // INVARIANT: After this call, the invalid value is at the last slot, so the Vec invariants
        // are restored after the below call to `dec_len(1)`.
        // SAFETY: `p.add(1).add(self.len - i - 1)` is `i+1+len-i-1 == len` elements after the
        // beginning of the vector, so this is in-bounds of the vector's allocation.
        unsafe { ptr::copy(p.add(1), p, self.len - i - 1) };

        // SAFETY: Since the check at the beginning of this call did not fail with `RemoveError`,
        // the length is at least one.
        unsafe { self.dec_len(1) };

        Ok(value)
    }

    /// Creates a new [`Vec`] instance with at least the given capacity.
    ///
    /// # Examples
    ///
    /// ```
    /// let v = KVec::<u32>::with_capacity(20, GFP_KERNEL)?;
    ///
    /// assert!(v.capacity() >= 20);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn with_capacity(capacity: usize, flags: Flags) -> Result<Self, AllocError> {
        let mut v = Vec::new();

        v.reserve(capacity, flags)?;

        Ok(v)
    }

    /// Creates a `Vec<T, A>` from a pointer, a length and a capacity using the allocator `A`.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = kernel::kvec![1, 2, 3]?;
    /// v.reserve(1, GFP_KERNEL)?;
    ///
    /// let (mut ptr, mut len, cap) = v.into_raw_parts();
    ///
    /// // SAFETY: We've just reserved memory for another element.
    /// unsafe { ptr.add(len).write(4) };
    /// len += 1;
    ///
    /// // SAFETY: We only wrote an additional element at the end of the `KVec`'s buffer and
    /// // correspondingly increased the length of the `KVec` by one. Otherwise, we construct it
    /// // from the exact same raw parts.
    /// let v = unsafe { KVec::from_raw_parts(ptr, len, cap) };
    ///
    /// assert_eq!(v, [1, 2, 3, 4]);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    ///
    /// # Safety
    ///
    /// If `T` is a ZST:
    ///
    /// - `ptr` must be a dangling, well aligned pointer.
    ///
    /// Otherwise:
    ///
    /// - `ptr` must have been allocated with the allocator `A`.
    /// - `ptr` must satisfy or exceed the alignment requirements of `T`.
    /// - `ptr` must point to memory with a size of at least `size_of::<T>() * capacity` bytes.
    /// - The allocated size in bytes must not be larger than `isize::MAX`.
    /// - `length` must be less than or equal to `capacity`.
    /// - The first `length` elements must be initialized values of type `T`.
    ///
    /// It is also valid to create an empty `Vec` passing a dangling pointer for `ptr` and zero for
    /// `cap` and `len`.
    pub unsafe fn from_raw_parts(ptr: *mut T, length: usize, capacity: usize) -> Self {
        let layout = if Self::is_zst() {
            ArrayLayout::empty()
        } else {
            // SAFETY: By the safety requirements of this function, `capacity * size_of::<T>()` is
            // smaller than `isize::MAX`.
            unsafe { ArrayLayout::new_unchecked(capacity) }
        };

        // INVARIANT: For ZSTs, we store an empty `ArrayLayout`, all other type invariants are
        // covered by the safety requirements of this function.
        Self {
            // SAFETY: By the safety requirements, `ptr` is either dangling or pointing to a valid
            // memory allocation, allocated with `A`.
            ptr: unsafe { NonNull::new_unchecked(ptr) },
            layout,
            len: length,
            _p: PhantomData::<A>,
        }
    }

    /// Consumes the `Vec<T, A>` and returns its raw components `pointer`, `length` and `capacity`.
    ///
    /// This will not run the destructor of the contained elements and for non-ZSTs the allocation
    /// will stay alive indefinitely. Use [`Vec::from_raw_parts`] to recover the [`Vec`], drop the
    /// elements and free the allocation, if any.
    pub fn into_raw_parts(self) -> (*mut T, usize, usize) {
        let mut me = ManuallyDrop::new(self);
        let len = me.len();
        let capacity = me.capacity();
        let ptr = me.as_mut_ptr();
        (ptr, len, capacity)
    }

    /// Clears the vector, removing all values.
    ///
    /// Note that this method has no effect on the allocated capacity
    /// of the vector.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = kernel::kvec![1, 2, 3]?;
    ///
    /// v.clear();
    ///
    /// assert!(v.is_empty());
    /// # Ok::<(), Error>(())
    /// ```
    #[inline]
    pub fn clear(&mut self) {
        self.truncate(0);
    }

    /// Ensures that the capacity exceeds the length by at least `additional` elements.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = KVec::new();
    /// v.push(1, GFP_KERNEL)?;
    ///
    /// v.reserve(10, GFP_KERNEL)?;
    /// let cap = v.capacity();
    /// assert!(cap >= 10);
    ///
    /// v.reserve(10, GFP_KERNEL)?;
    /// let new_cap = v.capacity();
    /// assert_eq!(new_cap, cap);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    pub fn reserve(&mut self, additional: usize, flags: Flags) -> Result<(), AllocError> {
        let len = self.len();
        let cap = self.capacity();

        if cap - len >= additional {
            return Ok(());
        }

        if Self::is_zst() {
            // The capacity is already `usize::MAX` for ZSTs, we can't go higher.
            return Err(AllocError);
        }

        // We know that `cap <= isize::MAX` because of the type invariants of `Self`. So the
        // multiplication by two won't overflow.
        let new_cap = core::cmp::max(cap * 2, len.checked_add(additional).ok_or(AllocError)?);
        let layout = ArrayLayout::new(new_cap).map_err(|_| AllocError)?;

        // SAFETY:
        // - `ptr` is valid because it's either `None` or comes from a previous call to
        //   `A::realloc`.
        // - `self.layout` matches the `ArrayLayout` of the preceding allocation.
        let ptr = unsafe {
            A::realloc(
                Some(self.ptr.cast()),
                layout.into(),
                self.layout.into(),
                flags,
                NumaNode::NO_NODE,
            )?
        };

        // INVARIANT:
        // - `layout` is some `ArrayLayout::<T>`,
        // - `ptr` has been created by `A::realloc` from `layout`.
        self.ptr = ptr.cast();
        self.layout = layout;

        Ok(())
    }

    /// Shortens the vector, setting the length to `len` and drops the removed values.
    /// If `len` is greater than or equal to the current length, this does nothing.
    ///
    /// This has no effect on the capacity and will not allocate.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = kernel::kvec![1, 2, 3]?;
    /// v.truncate(1);
    /// assert_eq!(v.len(), 1);
    /// assert_eq!(&v, &[1]);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    pub fn truncate(&mut self, len: usize) {
        if let Some(count) = self.len().checked_sub(len) {
            // SAFETY: `count` is `self.len() - len` so it is guaranteed to be less than or
            // equal to `self.len()`.
            let ptr: *mut [T] = unsafe { self.dec_len(count) };

            // SAFETY: the contract of `dec_len` guarantees that the elements in `ptr` are
            // valid elements whose ownership has been transferred to the caller.
            unsafe { ptr::drop_in_place(ptr) };
        }
    }

    /// Takes ownership of all items in this vector without consuming the allocation.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = kernel::kvec![0, 1, 2, 3]?;
    ///
    /// for (i, j) in v.drain_all().enumerate() {
    ///     assert_eq!(i, j);
    /// }
    ///
    /// assert!(v.capacity() >= 4);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn drain_all(&mut self) -> DrainAll<'_, T> {
        // SAFETY: This does not underflow the length.
        let elems = unsafe { self.dec_len(self.len()) };
        // INVARIANT: The first `len` elements of the spare capacity are valid values, and as we
        // just set the length to zero, we may transfer ownership to the `DrainAll` object.
        DrainAll {
            elements: elems.iter_mut(),
        }
    }

    /// Removes all elements that don't match the provided closure.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = kernel::kvec![1, 2, 3, 4]?;
    /// v.retain(|i| *i % 2 == 0);
    /// assert_eq!(v, [2, 4]);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn retain(&mut self, mut f: impl FnMut(&mut T) -> bool) {
        let mut num_kept = 0;
        let mut next_to_check = 0;
        while let Some(to_check) = self.get_mut(next_to_check) {
            if f(to_check) {
                self.swap(num_kept, next_to_check);
                num_kept += 1;
            }
            next_to_check += 1;
        }
        self.truncate(num_kept);
    }
}

impl<T: Clone, A: Allocator> Vec<T, A> {
    /// Extend the vector by `n` clones of `value`.
    pub fn extend_with(&mut self, n: usize, value: T, flags: Flags) -> Result<(), AllocError> {
        if n == 0 {
            return Ok(());
        }

        self.reserve(n, flags)?;

        let spare = self.spare_capacity_mut();

        for item in spare.iter_mut().take(n - 1) {
            item.write(value.clone());
        }

        // We can write the last element directly without cloning needlessly.
        spare[n - 1].write(value);

        // SAFETY:
        // - `self.len() + n < self.capacity()` due to the call to reserve above,
        // - the loop and the line above initialized the next `n` elements.
        unsafe { self.inc_len(n) };

        Ok(())
    }

    /// Pushes clones of the elements of slice into the [`Vec`] instance.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = KVec::new();
    /// v.push(1, GFP_KERNEL)?;
    ///
    /// v.extend_from_slice(&[20, 30, 40], GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 20, 30, 40]);
    ///
    /// v.extend_from_slice(&[50, 60], GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 20, 30, 40, 50, 60]);
    /// # Ok::<(), Error>(())
    /// ```
    pub fn extend_from_slice(&mut self, other: &[T], flags: Flags) -> Result<(), AllocError> {
        self.reserve(other.len(), flags)?;
        for (slot, item) in core::iter::zip(self.spare_capacity_mut(), other) {
            slot.write(item.clone());
        }

        // SAFETY:
        // - `other.len()` spare entries have just been initialized, so it is safe to increase
        //   the length by the same number.
        // - `self.len() + other.len() <= self.capacity()` is guaranteed by the preceding `reserve`
        //   call.
        unsafe { self.inc_len(other.len()) };
        Ok(())
    }

    /// Create a new `Vec<T, A>` and extend it by `n` clones of `value`.
    pub fn from_elem(value: T, n: usize, flags: Flags) -> Result<Self, AllocError> {
        let mut v = Self::with_capacity(n, flags)?;

        v.extend_with(n, value, flags)?;

        Ok(v)
    }

    /// Resizes the [`Vec`] so that `len` is equal to `new_len`.
    ///
    /// If `new_len` is smaller than `len`, the `Vec` is [`Vec::truncate`]d.
    /// If `new_len` is larger, each new slot is filled with clones of `value`.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = kernel::kvec![1, 2, 3]?;
    /// v.resize(1, 42, GFP_KERNEL)?;
    /// assert_eq!(&v, &[1]);
    ///
    /// v.resize(3, 42, GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 42, 42]);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    pub fn resize(&mut self, new_len: usize, value: T, flags: Flags) -> Result<(), AllocError> {
        match new_len.checked_sub(self.len()) {
            Some(n) => self.extend_with(n, value, flags),
            None => {
                self.truncate(new_len);
                Ok(())
            }
        }
    }
}

impl<T, A> Drop for Vec<T, A>
where
    A: Allocator,
{
    fn drop(&mut self) {
        // SAFETY: `self.as_mut_ptr` is guaranteed to be valid by the type invariant.
        unsafe {
            ptr::drop_in_place(core::ptr::slice_from_raw_parts_mut(
                self.as_mut_ptr(),
                self.len,
            ))
        };

        // SAFETY:
        // - `self.ptr` was previously allocated with `A`.
        // - `self.layout` matches the `ArrayLayout` of the preceding allocation.
        unsafe { A::free(self.ptr.cast(), self.layout.into()) };
    }
}

impl<T, A, const N: usize> From<Box<[T; N], A>> for Vec<T, A>
where
    A: Allocator,
{
    fn from(b: Box<[T; N], A>) -> Vec<T, A> {
        let len = b.len();
        let ptr = Box::into_raw(b);

        // SAFETY:
        // - `b` has been allocated with `A`,
        // - `ptr` fulfills the alignment requirements for `T`,
        // - `ptr` points to memory with at least a size of `size_of::<T>() * len`,
        // - all elements within `b` are initialized values of `T`,
        // - `len` does not exceed `isize::MAX`.
        unsafe { Vec::from_raw_parts(ptr.cast(), len, len) }
    }
}

impl<T, A: Allocator> Default for Vec<T, A> {
    #[inline]
    fn default() -> Self {
        Self::new()
    }
}

impl<T: fmt::Debug, A: Allocator> fmt::Debug for Vec<T, A> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&**self, f)
    }
}

impl<T, A> Deref for Vec<T, A>
where
    A: Allocator,
{
    type Target = [T];

    #[inline]
    fn deref(&self) -> &[T] {
        // SAFETY: The memory behind `self.as_ptr()` is guaranteed to contain `self.len`
        // initialized elements of type `T`.
        unsafe { slice::from_raw_parts(self.as_ptr(), self.len) }
    }
}

impl<T, A> DerefMut for Vec<T, A>
where
    A: Allocator,
{
    #[inline]
    fn deref_mut(&mut self) -> &mut [T] {
        // SAFETY: The memory behind `self.as_ptr()` is guaranteed to contain `self.len`
        // initialized elements of type `T`.
        unsafe { slice::from_raw_parts_mut(self.as_mut_ptr(), self.len) }
    }
}

/// # Examples
///
/// ```
/// # use core::borrow::Borrow;
/// struct Foo<B: Borrow<[u32]>>(B);
///
/// // Owned array.
/// let owned_array = Foo([1, 2, 3]);
///
/// // Owned vector.
/// let owned_vec = Foo(KVec::from_elem(0, 3, GFP_KERNEL)?);
///
/// let arr = [1, 2, 3];
/// // Borrowed slice from `arr`.
/// let borrowed_slice = Foo(&arr[..]);
/// # Ok::<(), Error>(())
/// ```
impl<T, A> Borrow<[T]> for Vec<T, A>
where
    A: Allocator,
{
    fn borrow(&self) -> &[T] {
        self.as_slice()
    }
}

/// # Examples
///
/// ```
/// # use core::borrow::BorrowMut;
/// struct Foo<B: BorrowMut<[u32]>>(B);
///
/// // Owned array.
/// let owned_array = Foo([1, 2, 3]);
///
/// // Owned vector.
/// let owned_vec = Foo(KVec::from_elem(0, 3, GFP_KERNEL)?);
///
/// let mut arr = [1, 2, 3];
/// // Borrowed slice from `arr`.
/// let borrowed_slice = Foo(&mut arr[..]);
/// # Ok::<(), Error>(())
/// ```
impl<T, A> BorrowMut<[T]> for Vec<T, A>
where
    A: Allocator,
{
    fn borrow_mut(&mut self) -> &mut [T] {
        self.as_mut_slice()
    }
}

impl<T: Eq, A> Eq for Vec<T, A> where A: Allocator {}

impl<T, I: SliceIndex<[T]>, A> Index<I> for Vec<T, A>
where
    A: Allocator,
{
    type Output = I::Output;

    #[inline]
    fn index(&self, index: I) -> &Self::Output {
        Index::index(&**self, index)
    }
}

impl<T, I: SliceIndex<[T]>, A> IndexMut<I> for Vec<T, A>
where
    A: Allocator,
{
    #[inline]
    fn index_mut(&mut self, index: I) -> &mut Self::Output {
        IndexMut::index_mut(&mut **self, index)
    }
}

macro_rules! impl_slice_eq {
    ($([$($vars:tt)*] $lhs:ty, $rhs:ty,)*) => {
        $(
            impl<T, U, $($vars)*> PartialEq<$rhs> for $lhs
            where
                T: PartialEq<U>,
            {
                #[inline]
                fn eq(&self, other: &$rhs) -> bool { self[..] == other[..] }
            }
        )*
    }
}

impl_slice_eq! {
    [A1: Allocator, A2: Allocator] Vec<T, A1>, Vec<U, A2>,
    [A: Allocator] Vec<T, A>, &[U],
    [A: Allocator] Vec<T, A>, &mut [U],
    [A: Allocator] &[T], Vec<U, A>,
    [A: Allocator] &mut [T], Vec<U, A>,
    [A: Allocator] Vec<T, A>, [U],
    [A: Allocator] [T], Vec<U, A>,
    [A: Allocator, const N: usize] Vec<T, A>, [U; N],
    [A: Allocator, const N: usize] Vec<T, A>, &[U; N],
}

impl<'a, T, A> IntoIterator for &'a Vec<T, A>
where
    A: Allocator,
{
    type Item = &'a T;
    type IntoIter = slice::Iter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'a, T, A: Allocator> IntoIterator for &'a mut Vec<T, A>
where
    A: Allocator,
{
    type Item = &'a mut T;
    type IntoIter = slice::IterMut<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter_mut()
    }
}

/// # Examples
///
/// ```
/// # use kernel::prelude::*;
/// use kernel::alloc::allocator::VmallocPageIter;
/// use kernel::page::{AsPageIter, PAGE_SIZE};
///
/// let mut vec = VVec::<u8>::new();
///
/// assert!(vec.page_iter().next().is_none());
///
/// vec.reserve(PAGE_SIZE, GFP_KERNEL)?;
///
/// let page = vec.page_iter().next().expect("At least one page should be available.\n");
///
/// // SAFETY: There is no concurrent read or write to the same page.
/// unsafe { page.fill_zero_raw(0, PAGE_SIZE)? };
/// # Ok::<(), Error>(())
/// ```
impl<T> AsPageIter for VVec<T> {
    type Iter<'a>
        = VmallocPageIter<'a>
    where
        T: 'a;

    fn page_iter(&mut self) -> Self::Iter<'_> {
        let ptr = self.ptr.cast();
        let size = self.layout.size();

        // SAFETY:
        // - `ptr` is a valid pointer to the beginning of a `Vmalloc` allocation.
        // - `ptr` is guaranteed to be valid for the lifetime of `'a`.
        // - `size` is the size of the `Vmalloc` allocation `ptr` points to.
        unsafe { VmallocPageIter::new(ptr, size) }
    }
}

/// An [`Iterator`] implementation for [`Vec`] that moves elements out of a vector.
///
/// This structure is created by the [`Vec::into_iter`] method on [`Vec`] (provided by the
/// [`IntoIterator`] trait).
///
/// # Examples
///
/// ```
/// let v = kernel::kvec![0, 1, 2]?;
/// let iter = v.into_iter();
///
/// # Ok::<(), Error>(())
/// ```
pub struct IntoIter<T, A: Allocator> {
    ptr: *mut T,
    buf: NonNull<T>,
    len: usize,
    layout: ArrayLayout<T>,
    _p: PhantomData<A>,
}

impl<T, A> IntoIter<T, A>
where
    A: Allocator,
{
    fn into_raw_parts(self) -> (*mut T, NonNull<T>, usize, usize) {
        let me = ManuallyDrop::new(self);
        let ptr = me.ptr;
        let buf = me.buf;
        let len = me.len;
        let cap = me.layout.len();
        (ptr, buf, len, cap)
    }

    /// Same as `Iterator::collect` but specialized for `Vec`'s `IntoIter`.
    ///
    /// # Examples
    ///
    /// ```
    /// let v = kernel::kvec![1, 2, 3]?;
    /// let mut it = v.into_iter();
    ///
    /// assert_eq!(it.next(), Some(1));
    ///
    /// let v = it.collect(GFP_KERNEL);
    /// assert_eq!(v, [2, 3]);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    ///
    /// # Implementation details
    ///
    /// Currently, we can't implement `FromIterator`. There are a couple of issues with this trait
    /// in the kernel, namely:
    ///
    /// - Rust's specialization feature is unstable. This prevents us to optimize for the special
    ///   case where `I::IntoIter` equals `Vec`'s `IntoIter` type.
    /// - We also can't use `I::IntoIter`'s type ID either to work around this, since `FromIterator`
    ///   doesn't require this type to be `'static`.
    /// - `FromIterator::from_iter` does return `Self` instead of `Result<Self, AllocError>`, hence
    ///   we can't properly handle allocation failures.
    /// - Neither `Iterator::collect` nor `FromIterator::from_iter` can handle additional allocation
    ///   flags.
    ///
    /// Instead, provide `IntoIter::collect`, such that we can at least convert a `IntoIter` into a
    /// `Vec` again.
    ///
    /// Note that `IntoIter::collect` doesn't require `Flags`, since it re-uses the existing backing
    /// buffer. However, this backing buffer may be shrunk to the actual count of elements.
    pub fn collect(self, flags: Flags) -> Vec<T, A> {
        let old_layout = self.layout;
        let (mut ptr, buf, len, mut cap) = self.into_raw_parts();
        let has_advanced = ptr != buf.as_ptr();

        if has_advanced {
            // Copy the contents we have advanced to at the beginning of the buffer.
            //
            // SAFETY:
            // - `ptr` is valid for reads of `len * size_of::<T>()` bytes,
            // - `buf.as_ptr()` is valid for writes of `len * size_of::<T>()` bytes,
            // - `ptr` and `buf.as_ptr()` are not be subject to aliasing restrictions relative to
            //   each other,
            // - both `ptr` and `buf.ptr()` are properly aligned.
            unsafe { ptr::copy(ptr, buf.as_ptr(), len) };
            ptr = buf.as_ptr();

            // SAFETY: `len` is guaranteed to be smaller than `self.layout.len()` by the type
            // invariant.
            let layout = unsafe { ArrayLayout::<T>::new_unchecked(len) };

            // SAFETY: `buf` points to the start of the backing buffer and `len` is guaranteed by
            // the type invariant to be smaller than `cap`. Depending on `realloc` this operation
            // may shrink the buffer or leave it as it is.
            ptr = match unsafe {
                A::realloc(
                    Some(buf.cast()),
                    layout.into(),
                    old_layout.into(),
                    flags,
                    NumaNode::NO_NODE,
                )
            } {
                // If we fail to shrink, which likely can't even happen, continue with the existing
                // buffer.
                Err(_) => ptr,
                Ok(ptr) => {
                    cap = len;
                    ptr.as_ptr().cast()
                }
            };
        }

        // SAFETY: If the iterator has been advanced, the advanced elements have been copied to
        // the beginning of the buffer and `len` has been adjusted accordingly.
        //
        // - `ptr` is guaranteed to point to the start of the backing buffer.
        // - `cap` is either the original capacity or, after shrinking the buffer, equal to `len`.
        // - `alloc` is guaranteed to be unchanged since `into_iter` has been called on the original
        //   `Vec`.
        unsafe { Vec::from_raw_parts(ptr, len, cap) }
    }
}

impl<T, A> Iterator for IntoIter<T, A>
where
    A: Allocator,
{
    type Item = T;

    /// # Examples
    ///
    /// ```
    /// let v = kernel::kvec![1, 2, 3]?;
    /// let mut it = v.into_iter();
    ///
    /// assert_eq!(it.next(), Some(1));
    /// assert_eq!(it.next(), Some(2));
    /// assert_eq!(it.next(), Some(3));
    /// assert_eq!(it.next(), None);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    fn next(&mut self) -> Option<T> {
        if self.len == 0 {
            return None;
        }

        let current = self.ptr;

        // SAFETY: We can't overflow; decreasing `self.len` by one every time we advance `self.ptr`
        // by one guarantees that.
        unsafe { self.ptr = self.ptr.add(1) };

        self.len -= 1;

        // SAFETY: `current` is guaranteed to point at a valid element within the buffer.
        Some(unsafe { current.read() })
    }

    /// # Examples
    ///
    /// ```
    /// let v: KVec<u32> = kernel::kvec![1, 2, 3]?;
    /// let mut iter = v.into_iter();
    /// let size = iter.size_hint().0;
    ///
    /// iter.next();
    /// assert_eq!(iter.size_hint().0, size - 1);
    ///
    /// iter.next();
    /// assert_eq!(iter.size_hint().0, size - 2);
    ///
    /// iter.next();
    /// assert_eq!(iter.size_hint().0, size - 3);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len, Some(self.len))
    }
}

impl<T, A> Drop for IntoIter<T, A>
where
    A: Allocator,
{
    fn drop(&mut self) {
        // SAFETY: `self.ptr` is guaranteed to be valid by the type invariant.
        unsafe { ptr::drop_in_place(ptr::slice_from_raw_parts_mut(self.ptr, self.len)) };

        // SAFETY:
        // - `self.buf` was previously allocated with `A`.
        // - `self.layout` matches the `ArrayLayout` of the preceding allocation.
        unsafe { A::free(self.buf.cast(), self.layout.into()) };
    }
}

impl<T, A> IntoIterator for Vec<T, A>
where
    A: Allocator,
{
    type Item = T;
    type IntoIter = IntoIter<T, A>;

    /// Consumes the `Vec<T, A>` and creates an `Iterator`, which moves each value out of the
    /// vector (from start to end).
    ///
    /// # Examples
    ///
    /// ```
    /// let v = kernel::kvec![1, 2]?;
    /// let mut v_iter = v.into_iter();
    ///
    /// let first_element: Option<u32> = v_iter.next();
    ///
    /// assert_eq!(first_element, Some(1));
    /// assert_eq!(v_iter.next(), Some(2));
    /// assert_eq!(v_iter.next(), None);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    ///
    /// ```
    /// let v = kernel::kvec![];
    /// let mut v_iter = v.into_iter();
    ///
    /// let first_element: Option<u32> = v_iter.next();
    ///
    /// assert_eq!(first_element, None);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        let buf = self.ptr;
        let layout = self.layout;
        let (ptr, len, _) = self.into_raw_parts();

        IntoIter {
            ptr,
            buf,
            len,
            layout,
            _p: PhantomData::<A>,
        }
    }
}

/// An iterator that owns all items in a vector, but does not own its allocation.
///
/// # Invariants
///
/// Every `&mut T` returned by the iterator references a `T` that the iterator may take ownership
/// of.
pub struct DrainAll<'vec, T> {
    elements: slice::IterMut<'vec, T>,
}

impl<'vec, T> Iterator for DrainAll<'vec, T> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        let elem: *mut T = self.elements.next()?;
        // SAFETY: By the type invariants, we may take ownership of this value.
        Some(unsafe { elem.read() })
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.elements.size_hint()
    }
}

impl<'vec, T> Drop for DrainAll<'vec, T> {
    fn drop(&mut self) {
        if core::mem::needs_drop::<T>() {
            let iter = core::mem::take(&mut self.elements);
            let ptr: *mut [T] = iter.into_slice();
            // SAFETY: By the type invariants, we own these values so we may destroy them.
            unsafe { ptr::drop_in_place(ptr) };
        }
    }
}

#[macros::kunit_tests(rust_kvec)]
mod tests {
    use super::*;
    use crate::prelude::*;

    #[test]
    fn test_kvec_retain() {
        /// Verify correctness for one specific function.
        #[expect(clippy::needless_range_loop)]
        fn verify(c: &[bool]) {
            let mut vec1: KVec<usize> = KVec::with_capacity(c.len(), GFP_KERNEL).unwrap();
            let mut vec2: KVec<usize> = KVec::with_capacity(c.len(), GFP_KERNEL).unwrap();

            for i in 0..c.len() {
                vec1.push_within_capacity(i).unwrap();
                if c[i] {
                    vec2.push_within_capacity(i).unwrap();
                }
            }

            vec1.retain(|i| c[*i]);

            assert_eq!(vec1, vec2);
        }

        /// Add one to a binary integer represented as a boolean array.
        fn add(value: &mut [bool]) {
            let mut carry = true;
            for v in value {
                let new_v = carry != *v;
                carry = carry && *v;
                *v = new_v;
            }
        }

        // This boolean array represents a function from index to boolean. We check that `retain`
        // behaves correctly for all possible boolean arrays of every possible length less than
        // ten.
        let mut func = KVec::with_capacity(10, GFP_KERNEL).unwrap();
        for len in 0..10 {
            for _ in 0u32..1u32 << len {
                verify(&func);
                add(&mut func);
            }
            func.push_within_capacity(false).unwrap();
        }
    }
}
