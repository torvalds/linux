// SPDX-License-Identifier: GPL-2.0

//! Maple trees.
//!
//! C header: [`include/linux/maple_tree.h`](srctree/include/linux/maple_tree.h)
//!
//! Reference: <https://docs.kernel.org/core-api/maple_tree.html>

use core::{
    marker::PhantomData,
    ops::{Bound, RangeBounds},
    ptr,
};

use kernel::{
    alloc::Flags,
    error::to_result,
    prelude::*,
    types::{ForeignOwnable, Opaque},
};

/// A maple tree optimized for storing non-overlapping ranges.
///
/// # Invariants
///
/// Each range in the maple tree owns an instance of `T`.
#[pin_data(PinnedDrop)]
#[repr(transparent)]
pub struct MapleTree<T: ForeignOwnable> {
    #[pin]
    tree: Opaque<bindings::maple_tree>,
    _p: PhantomData<T>,
}

#[inline]
fn to_maple_range(range: impl RangeBounds<usize>) -> Option<(usize, usize)> {
    let first = match range.start_bound() {
        Bound::Included(start) => *start,
        Bound::Excluded(start) => start.checked_add(1)?,
        Bound::Unbounded => 0,
    };

    let last = match range.end_bound() {
        Bound::Included(end) => *end,
        Bound::Excluded(end) => end.checked_sub(1)?,
        Bound::Unbounded => usize::MAX,
    };

    if last < first {
        return None;
    }

    Some((first, last))
}

impl<T: ForeignOwnable> MapleTree<T> {
    /// Create a new maple tree.
    ///
    /// The tree will use the regular implementation with a higher branching factor, rather than
    /// the allocation tree.
    #[inline]
    pub fn new() -> impl PinInit<Self> {
        pin_init!(MapleTree {
            // SAFETY: This initializes a maple tree into a pinned slot. The maple tree will be
            // destroyed in Drop before the memory location becomes invalid.
            tree <- Opaque::ffi_init(|slot| unsafe { bindings::mt_init_flags(slot, 0) }),
            _p: PhantomData,
        })
    }

    /// Insert the value at the given index.
    ///
    /// # Errors
    ///
    /// If the maple tree already contains a range using the given index, then this call will
    /// return an [`InsertErrorKind::Occupied`]. It may also fail if memory allocation fails.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::maple_tree::{InsertErrorKind, MapleTree};
    ///
    /// let tree = KBox::pin_init(MapleTree::<KBox<i32>>::new(), GFP_KERNEL)?;
    ///
    /// let ten = KBox::new(10, GFP_KERNEL)?;
    /// let twenty = KBox::new(20, GFP_KERNEL)?;
    /// let the_answer = KBox::new(42, GFP_KERNEL)?;
    ///
    /// // These calls will succeed.
    /// tree.insert(100, ten, GFP_KERNEL)?;
    /// tree.insert(101, twenty, GFP_KERNEL)?;
    ///
    /// // This will fail because the index is already in use.
    /// assert_eq!(
    ///     tree.insert(100, the_answer, GFP_KERNEL).unwrap_err().cause,
    ///     InsertErrorKind::Occupied,
    /// );
    /// # Ok::<_, Error>(())
    /// ```
    #[inline]
    pub fn insert(&self, index: usize, value: T, gfp: Flags) -> Result<(), InsertError<T>> {
        self.insert_range(index..=index, value, gfp)
    }

    /// Insert a value to the specified range, failing on overlap.
    ///
    /// This accepts the usual types of Rust ranges using the `..` and `..=` syntax for exclusive
    /// and inclusive ranges respectively. The range must not be empty, and must not overlap with
    /// any existing range.
    ///
    /// # Errors
    ///
    /// If the maple tree already contains an overlapping range, then this call will return an
    /// [`InsertErrorKind::Occupied`]. It may also fail if memory allocation fails or if the
    /// requested range is invalid (e.g. empty).
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::maple_tree::{InsertErrorKind, MapleTree};
    ///
    /// let tree = KBox::pin_init(MapleTree::<KBox<i32>>::new(), GFP_KERNEL)?;
    ///
    /// let ten = KBox::new(10, GFP_KERNEL)?;
    /// let twenty = KBox::new(20, GFP_KERNEL)?;
    /// let the_answer = KBox::new(42, GFP_KERNEL)?;
    /// let hundred = KBox::new(100, GFP_KERNEL)?;
    ///
    /// // Insert the value 10 at the indices 100 to 499.
    /// tree.insert_range(100..500, ten, GFP_KERNEL)?;
    ///
    /// // Insert the value 20 at the indices 500 to 1000.
    /// tree.insert_range(500..=1000, twenty, GFP_KERNEL)?;
    ///
    /// // This will fail due to overlap with the previous range on index 1000.
    /// assert_eq!(
    ///     tree.insert_range(1000..1200, the_answer, GFP_KERNEL).unwrap_err().cause,
    ///     InsertErrorKind::Occupied,
    /// );
    ///
    /// // When using .. to specify the range, you must be careful to ensure that the range is
    /// // non-empty.
    /// assert_eq!(
    ///     tree.insert_range(72..72, hundred, GFP_KERNEL).unwrap_err().cause,
    ///     InsertErrorKind::InvalidRequest,
    /// );
    /// # Ok::<_, Error>(())
    /// ```
    pub fn insert_range<R>(&self, range: R, value: T, gfp: Flags) -> Result<(), InsertError<T>>
    where
        R: RangeBounds<usize>,
    {
        let Some((first, last)) = to_maple_range(range) else {
            return Err(InsertError {
                value,
                cause: InsertErrorKind::InvalidRequest,
            });
        };

        let ptr = T::into_foreign(value);

        // SAFETY: The tree is valid, and we are passing a pointer to an owned instance of `T`.
        let res = to_result(unsafe {
            bindings::mtree_insert_range(self.tree.get(), first, last, ptr, gfp.as_raw())
        });

        if let Err(err) = res {
            // SAFETY: As `mtree_insert_range` failed, it is safe to take back ownership.
            let value = unsafe { T::from_foreign(ptr) };

            let cause = if err == ENOMEM {
                InsertErrorKind::AllocError(kernel::alloc::AllocError)
            } else if err == EEXIST {
                InsertErrorKind::Occupied
            } else {
                InsertErrorKind::InvalidRequest
            };
            Err(InsertError { value, cause })
        } else {
            Ok(())
        }
    }

    /// Erase the range containing the given index.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::maple_tree::MapleTree;
    ///
    /// let tree = KBox::pin_init(MapleTree::<KBox<i32>>::new(), GFP_KERNEL)?;
    ///
    /// let ten = KBox::new(10, GFP_KERNEL)?;
    /// let twenty = KBox::new(20, GFP_KERNEL)?;
    ///
    /// tree.insert_range(100..500, ten, GFP_KERNEL)?;
    /// tree.insert(67, twenty, GFP_KERNEL)?;
    ///
    /// assert_eq!(tree.erase(67).map(|v| *v), Some(20));
    /// assert_eq!(tree.erase(275).map(|v| *v), Some(10));
    ///
    /// // The previous call erased the entire range, not just index 275.
    /// assert!(tree.erase(127).is_none());
    /// # Ok::<_, Error>(())
    /// ```
    #[inline]
    pub fn erase(&self, index: usize) -> Option<T> {
        // SAFETY: `self.tree` contains a valid maple tree.
        let ret = unsafe { bindings::mtree_erase(self.tree.get(), index) };

        // SAFETY: If the pointer is not null, then we took ownership of a valid instance of `T`
        // from the tree.
        unsafe { T::try_from_foreign(ret) }
    }

    /// Free all `T` instances in this tree.
    ///
    /// # Safety
    ///
    /// This frees Rust data referenced by the maple tree without removing it from the maple tree,
    /// leaving it in an invalid state. The caller must ensure that this invalid state cannot be
    /// observed by the end-user.
    unsafe fn free_all_entries(self: Pin<&mut Self>) {
        // SAFETY: The caller provides exclusive access to the entire maple tree, so we have
        // exclusive access to the entire maple tree despite not holding the lock.
        let mut ma_state = unsafe { MaState::new_raw(self.into_ref().get_ref(), 0, usize::MAX) };

        loop {
            // This uses the raw accessor because we're destroying pointers without removing them
            // from the maple tree, which is only valid because this is the destructor.
            let ptr = ma_state.mas_find_raw(usize::MAX);
            if ptr.is_null() {
                break;
            }
            // SAFETY: By the type invariants, this pointer references a valid value of type `T`.
            // By the safety requirements, it is okay to free it without removing it from the maple
            // tree.
            drop(unsafe { T::from_foreign(ptr) });
        }
    }
}

#[pinned_drop]
impl<T: ForeignOwnable> PinnedDrop for MapleTree<T> {
    #[inline]
    fn drop(mut self: Pin<&mut Self>) {
        // We only iterate the tree if the Rust value has a destructor.
        if core::mem::needs_drop::<T>() {
            // SAFETY: Other than the below `mtree_destroy` call, the tree will not be accessed
            // after this call.
            unsafe { self.as_mut().free_all_entries() };
        }

        // SAFETY: The tree is valid, and will not be accessed after this call.
        unsafe { bindings::mtree_destroy(self.tree.get()) };
    }
}

/// A helper type used for navigating a [`MapleTree`].
///
/// # Invariants
///
/// For the duration of `'tree`:
///
/// * The `ma_state` references a valid `MapleTree<T>`.
/// * The `ma_state` has read/write access to the tree.
pub struct MaState<'tree, T: ForeignOwnable> {
    state: bindings::ma_state,
    _phantom: PhantomData<&'tree mut MapleTree<T>>,
}

impl<'tree, T: ForeignOwnable> MaState<'tree, T> {
    /// Initialize a new `MaState` with the given tree.
    ///
    /// # Safety
    ///
    /// The caller must ensure that this `MaState` has read/write access to the maple tree.
    #[inline]
    unsafe fn new_raw(mt: &'tree MapleTree<T>, first: usize, end: usize) -> Self {
        // INVARIANT:
        // * Having a reference ensures that the `MapleTree<T>` is valid for `'tree`.
        // * The caller ensures that we have read/write access.
        Self {
            state: bindings::ma_state {
                tree: mt.tree.get(),
                index: first,
                last: end,
                node: ptr::null_mut(),
                status: bindings::maple_status_ma_start,
                min: 0,
                max: usize::MAX,
                alloc: ptr::null_mut(),
                mas_flags: 0,
                store_type: bindings::store_type_wr_invalid,
                ..Default::default()
            },
            _phantom: PhantomData,
        }
    }

    #[inline]
    fn as_raw(&mut self) -> *mut bindings::ma_state {
        &raw mut self.state
    }

    #[inline]
    fn mas_find_raw(&mut self, max: usize) -> *mut c_void {
        // SAFETY: By the type invariants, the `ma_state` is active and we have read/write access
        // to the tree.
        unsafe { bindings::mas_find(self.as_raw(), max) }
    }
}

/// Error type for failure to insert a new value.
pub struct InsertError<T> {
    /// The value that could not be inserted.
    pub value: T,
    /// The reason for the failure to insert.
    pub cause: InsertErrorKind,
}

/// The reason for the failure to insert.
#[derive(PartialEq, Eq, Copy, Clone, Debug)]
pub enum InsertErrorKind {
    /// There is already a value in the requested range.
    Occupied,
    /// Failure to allocate memory.
    AllocError(kernel::alloc::AllocError),
    /// The insertion request was invalid.
    InvalidRequest,
}

impl From<InsertErrorKind> for Error {
    #[inline]
    fn from(kind: InsertErrorKind) -> Error {
        match kind {
            InsertErrorKind::Occupied => EEXIST,
            InsertErrorKind::AllocError(kernel::alloc::AllocError) => ENOMEM,
            InsertErrorKind::InvalidRequest => EINVAL,
        }
    }
}

impl<T> From<InsertError<T>> for Error {
    #[inline]
    fn from(insert_err: InsertError<T>) -> Error {
        Error::from(insert_err.cause)
    }
}
