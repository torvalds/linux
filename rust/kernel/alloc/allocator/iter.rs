// SPDX-License-Identifier: GPL-2.0

use super::Vmalloc;
use crate::page;
use core::marker::PhantomData;
use core::ptr::NonNull;

/// An [`Iterator`] of [`page::BorrowedPage`] items owned by a [`Vmalloc`] allocation.
///
/// # Guarantees
///
/// The pages iterated by the [`Iterator`] appear in the order as they are mapped in the CPU's
/// virtual address space ascendingly.
///
/// # Invariants
///
/// - `buf` is a valid and [`page::PAGE_SIZE`] aligned pointer into a [`Vmalloc`] allocation.
/// - `size` is the number of bytes from `buf` until the end of the [`Vmalloc`] allocation `buf`
///   points to.
pub struct VmallocPageIter<'a> {
    /// The base address of the [`Vmalloc`] buffer.
    buf: NonNull<u8>,
    /// The size of the buffer pointed to by `buf` in bytes.
    size: usize,
    /// The current page index of the [`Iterator`].
    index: usize,
    _p: PhantomData<page::BorrowedPage<'a>>,
}

impl<'a> Iterator for VmallocPageIter<'a> {
    type Item = page::BorrowedPage<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        let offset = self.index.checked_mul(page::PAGE_SIZE)?;

        // Even though `self.size()` may be smaller than `Self::page_count() * page::PAGE_SIZE`, it
        // is always a number between `(Self::page_count() - 1) * page::PAGE_SIZE` and
        // `Self::page_count() * page::PAGE_SIZE`, hence the check below is sufficient.
        if offset < self.size() {
            self.index += 1;
        } else {
            return None;
        }

        // TODO: Use `NonNull::add()` instead, once the minimum supported compiler version is
        // bumped to 1.80 or later.
        //
        // SAFETY: `offset` is in the interval `[0, (self.page_count() - 1) * page::PAGE_SIZE]`,
        // hence the resulting pointer is guaranteed to be within the same allocation.
        let ptr = unsafe { self.buf.as_ptr().add(offset) };

        // SAFETY: `ptr` is guaranteed to be non-null given that it is derived from `self.buf`.
        let ptr = unsafe { NonNull::new_unchecked(ptr) };

        // SAFETY:
        // - `ptr` is a valid pointer to a `Vmalloc` allocation.
        // - `ptr` is valid for the duration of `'a`.
        Some(unsafe { Vmalloc::to_page(ptr) })
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.page_count().saturating_sub(self.index);

        (remaining, Some(remaining))
    }
}

impl<'a> VmallocPageIter<'a> {
    /// Creates a new [`VmallocPageIter`] instance.
    ///
    /// # Safety
    ///
    /// - `buf` must be a [`page::PAGE_SIZE`] aligned pointer into a [`Vmalloc`] allocation.
    /// - `buf` must be valid for at least the lifetime of `'a`.
    /// - `size` must be the number of bytes from `buf` until the end of the [`Vmalloc`] allocation
    ///   `buf` points to.
    pub unsafe fn new(buf: NonNull<u8>, size: usize) -> Self {
        // INVARIANT: By the safety requirements, `buf` is a valid and `page::PAGE_SIZE` aligned
        // pointer into a [`Vmalloc`] allocation.
        Self {
            buf,
            size,
            index: 0,
            _p: PhantomData,
        }
    }

    /// Returns the size of the backing [`Vmalloc`] allocation in bytes.
    ///
    /// Note that this is the size the [`Vmalloc`] allocation has been allocated with. Hence, this
    /// number may be smaller than `[`Self::page_count`] * [`page::PAGE_SIZE`]`.
    #[inline]
    pub fn size(&self) -> usize {
        self.size
    }

    /// Returns the number of pages owned by the backing [`Vmalloc`] allocation.
    #[inline]
    pub fn page_count(&self) -> usize {
        self.size().div_ceil(page::PAGE_SIZE)
    }
}
