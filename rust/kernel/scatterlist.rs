// SPDX-License-Identifier: GPL-2.0

//! Abstractions for scatter-gather lists.
//!
//! C header: [`include/linux/scatterlist.h`](srctree/include/linux/scatterlist.h)
//!
//! Scatter-gather (SG) I/O is a memory access technique that allows devices to perform DMA
//! operations on data buffers that are not physically contiguous in memory. It works by creating a
//! "scatter-gather list", an array where each entry specifies the address and length of a
//! physically contiguous memory segment.
//!
//! The device's DMA controller can then read this list and process the segments sequentially as
//! part of one logical I/O request. This avoids the need for a single, large, physically contiguous
//! memory buffer, which can be difficult or impossible to allocate.
//!
//! This module provides safe Rust abstractions over the kernel's `struct scatterlist` and
//! `struct sg_table` types.
//!
//! The main entry point is the [`SGTable`] type, which represents a complete scatter-gather table.
//! It can be either:
//!
//! - An owned table ([`SGTable<Owned<P>>`]), created from a Rust memory buffer (e.g., [`VVec`]).
//!   This type manages the allocation of the `struct sg_table`, the DMA mapping of the buffer, and
//!   the automatic cleanup of all resources.
//! - A borrowed reference (&[`SGTable`]), which provides safe, read-only access to a table that was
//!   allocated by other (e.g., C) code.
//!
//! Individual entries in the table are represented by [`SGEntry`], which can be accessed by
//! iterating over an [`SGTable`].

use crate::{
    alloc,
    alloc::allocator::VmallocPageIter,
    bindings,
    device::{Bound, Device},
    devres::Devres,
    dma, error,
    io::resource::ResourceSize,
    page,
    prelude::*,
    types::{ARef, Opaque},
};
use core::{ops::Deref, ptr::NonNull};

/// A single entry in a scatter-gather list.
///
/// An `SGEntry` represents a single, physically contiguous segment of memory that has been mapped
/// for DMA.
///
/// Instances of this struct are obtained by iterating over an [`SGTable`]. Drivers do not create
/// or own [`SGEntry`] objects directly.
#[repr(transparent)]
pub struct SGEntry(Opaque<bindings::scatterlist>);

// SAFETY: `SGEntry` can be sent to any task.
unsafe impl Send for SGEntry {}

// SAFETY: `SGEntry` has no interior mutability and can be accessed concurrently.
unsafe impl Sync for SGEntry {}

impl SGEntry {
    /// Convert a raw `struct scatterlist *` to a `&'a SGEntry`.
    ///
    /// # Safety
    ///
    /// Callers must ensure that the `struct scatterlist` pointed to by `ptr` is valid for the
    /// lifetime `'a`.
    #[inline]
    unsafe fn from_raw<'a>(ptr: *mut bindings::scatterlist) -> &'a Self {
        // SAFETY: The safety requirements of this function guarantee that `ptr` is a valid pointer
        // to a `struct scatterlist` for the duration of `'a`.
        unsafe { &*ptr.cast() }
    }

    /// Obtain the raw `struct scatterlist *`.
    #[inline]
    fn as_raw(&self) -> *mut bindings::scatterlist {
        self.0.get()
    }

    /// Returns the DMA address of this SG entry.
    ///
    /// This is the address that the device should use to access the memory segment.
    #[inline]
    pub fn dma_address(&self) -> dma::DmaAddress {
        // SAFETY: `self.as_raw()` is a valid pointer to a `struct scatterlist`.
        unsafe { bindings::sg_dma_address(self.as_raw()) }
    }

    /// Returns the length of this SG entry in bytes.
    #[inline]
    pub fn dma_len(&self) -> ResourceSize {
        #[allow(clippy::useless_conversion)]
        // SAFETY: `self.as_raw()` is a valid pointer to a `struct scatterlist`.
        unsafe { bindings::sg_dma_len(self.as_raw()) }.into()
    }
}

/// The borrowed generic type of an [`SGTable`], representing a borrowed or externally managed
/// table.
#[repr(transparent)]
pub struct Borrowed(Opaque<bindings::sg_table>);

// SAFETY: `Borrowed` can be sent to any task.
unsafe impl Send for Borrowed {}

// SAFETY: `Borrowed` has no interior mutability and can be accessed concurrently.
unsafe impl Sync for Borrowed {}

/// A scatter-gather table.
///
/// This struct is a wrapper around the kernel's `struct sg_table`. It manages a list of DMA-mapped
/// memory segments that can be passed to a device for I/O operations.
///
/// The generic parameter `T` is used as a generic type to distinguish between owned and borrowed
/// tables.
///
///  - [`SGTable<Owned>`]: An owned table created and managed entirely by Rust code. It handles
///    allocation, DMA mapping, and cleanup of all associated resources. See [`SGTable::new`].
///  - [`SGTable<Borrowed>`} (or simply [`SGTable`]): Represents a table whose lifetime is managed
///    externally. It can be used safely via a borrowed reference `&'a SGTable`, where `'a` is the
///    external lifetime.
///
/// All [`SGTable`] variants can be iterated over the individual [`SGEntry`]s.
#[repr(transparent)]
#[pin_data]
pub struct SGTable<T: private::Sealed = Borrowed> {
    #[pin]
    inner: T,
}

impl SGTable {
    /// Creates a borrowed `&'a SGTable` from a raw `struct sg_table` pointer.
    ///
    /// This allows safe access to an `sg_table` that is managed elsewhere (for example, in C code).
    ///
    /// # Safety
    ///
    /// Callers must ensure that:
    ///
    /// - the `struct sg_table` pointed to by `ptr` is valid for the entire lifetime of `'a`,
    /// - the data behind `ptr` is not modified concurrently for the duration of `'a`.
    #[inline]
    pub unsafe fn from_raw<'a>(ptr: *mut bindings::sg_table) -> &'a Self {
        // SAFETY: The safety requirements of this function guarantee that `ptr` is a valid pointer
        // to a `struct sg_table` for the duration of `'a`.
        unsafe { &*ptr.cast() }
    }

    #[inline]
    fn as_raw(&self) -> *mut bindings::sg_table {
        self.inner.0.get()
    }

    /// Returns an [`SGTableIter`] bound to the lifetime of `self`.
    pub fn iter(&self) -> SGTableIter<'_> {
        // SAFETY: `self.as_raw()` is a valid pointer to a `struct sg_table`.
        let nents = unsafe { (*self.as_raw()).nents };

        let pos = if nents > 0 {
            // SAFETY: `self.as_raw()` is a valid pointer to a `struct sg_table`.
            let ptr = unsafe { (*self.as_raw()).sgl };

            // SAFETY: `ptr` is guaranteed to be a valid pointer to a `struct scatterlist`.
            Some(unsafe { SGEntry::from_raw(ptr) })
        } else {
            None
        };

        SGTableIter { pos, nents }
    }
}

/// Represents the DMA mapping state of a `struct sg_table`.
///
/// This is used as an inner type of [`Owned`] to manage the DMA mapping lifecycle.
///
/// # Invariants
///
/// - `sgt` is a valid pointer to a `struct sg_table` for the entire lifetime of the
///   [`DmaMappedSgt`].
/// - `sgt` is always DMA mapped.
struct DmaMappedSgt {
    sgt: NonNull<bindings::sg_table>,
    dev: ARef<Device>,
    dir: dma::DataDirection,
}

// SAFETY: `DmaMappedSgt` can be sent to any task.
unsafe impl Send for DmaMappedSgt {}

// SAFETY: `DmaMappedSgt` has no interior mutability and can be accessed concurrently.
unsafe impl Sync for DmaMappedSgt {}

impl DmaMappedSgt {
    /// # Safety
    ///
    /// - `sgt` must be a valid pointer to a `struct sg_table` for the entire lifetime of the
    ///   returned [`DmaMappedSgt`].
    /// - The caller must guarantee that `sgt` remains DMA mapped for the entire lifetime of
    ///   [`DmaMappedSgt`].
    unsafe fn new(
        sgt: NonNull<bindings::sg_table>,
        dev: &Device<Bound>,
        dir: dma::DataDirection,
    ) -> Result<Self> {
        // SAFETY:
        // - `dev.as_raw()` is a valid pointer to a `struct device`, which is guaranteed to be
        //   bound to a driver for the duration of this call.
        // - `sgt` is a valid pointer to a `struct sg_table`.
        error::to_result(unsafe {
            bindings::dma_map_sgtable(dev.as_raw(), sgt.as_ptr(), dir.into(), 0)
        })?;

        // INVARIANT: By the safety requirements of this function it is guaranteed that `sgt` is
        // valid for the entire lifetime of this object instance.
        Ok(Self {
            sgt,
            dev: dev.into(),
            dir,
        })
    }
}

impl Drop for DmaMappedSgt {
    #[inline]
    fn drop(&mut self) {
        // SAFETY:
        // - `self.dev.as_raw()` is a pointer to a valid `struct device`.
        // - `self.dev` is the same device the mapping has been created for in `Self::new()`.
        // - `self.sgt.as_ptr()` is a valid pointer to a `struct sg_table` by the type invariants
        //   of `Self`.
        // - `self.dir` is the same `dma::DataDirection` the mapping has been created with in
        //   `Self::new()`.
        unsafe {
            bindings::dma_unmap_sgtable(self.dev.as_raw(), self.sgt.as_ptr(), self.dir.into(), 0)
        };
    }
}

/// A transparent wrapper around a `struct sg_table`.
///
/// While we could also create the `struct sg_table` in the constructor of [`Owned`], we can't tear
/// down the `struct sg_table` in [`Owned::drop`]; the drop order in [`Owned`] matters.
#[repr(transparent)]
struct RawSGTable(Opaque<bindings::sg_table>);

// SAFETY: `RawSGTable` can be sent to any task.
unsafe impl Send for RawSGTable {}

// SAFETY: `RawSGTable` has no interior mutability and can be accessed concurrently.
unsafe impl Sync for RawSGTable {}

impl RawSGTable {
    /// # Safety
    ///
    /// - `pages` must be a slice of valid `struct page *`.
    /// - The pages pointed to by `pages` must remain valid for the entire lifetime of the returned
    ///   [`RawSGTable`].
    unsafe fn new(
        pages: &mut [*mut bindings::page],
        size: usize,
        max_segment: u32,
        flags: alloc::Flags,
    ) -> Result<Self> {
        // `sg_alloc_table_from_pages_segment()` expects at least one page, otherwise it
        // produces a NPE.
        if pages.is_empty() {
            return Err(EINVAL);
        }

        let sgt = Opaque::zeroed();
        // SAFETY:
        // - `sgt.get()` is a valid pointer to uninitialized memory.
        // - As by the check above, `pages` is not empty.
        error::to_result(unsafe {
            bindings::sg_alloc_table_from_pages_segment(
                sgt.get(),
                pages.as_mut_ptr(),
                pages.len().try_into()?,
                0,
                size,
                max_segment,
                flags.as_raw(),
            )
        })?;

        Ok(Self(sgt))
    }

    #[inline]
    fn as_raw(&self) -> *mut bindings::sg_table {
        self.0.get()
    }
}

impl Drop for RawSGTable {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: `sgt` is a valid and initialized `struct sg_table`.
        unsafe { bindings::sg_free_table(self.0.get()) };
    }
}

/// The [`Owned`] generic type of an [`SGTable`].
///
/// A [`SGTable<Owned>`] signifies that the [`SGTable`] owns all associated resources:
///
/// - The backing memory pages.
/// - The `struct sg_table` allocation (`sgt`).
/// - The DMA mapping, managed through a [`Devres`]-managed `DmaMappedSgt`.
///
/// Users interact with this type through the [`SGTable`] handle and do not need to manage
/// [`Owned`] directly.
#[pin_data]
pub struct Owned<P> {
    // Note: The drop order is relevant; we first have to unmap the `struct sg_table`, then free the
    // `struct sg_table` and finally free the backing pages.
    #[pin]
    dma: Devres<DmaMappedSgt>,
    sgt: RawSGTable,
    _pages: P,
}

// SAFETY: `Owned` can be sent to any task if `P` can be send to any task.
unsafe impl<P: Send> Send for Owned<P> {}

// SAFETY: `Owned` has no interior mutability and can be accessed concurrently if `P` can be
// accessed concurrently.
unsafe impl<P: Sync> Sync for Owned<P> {}

impl<P> Owned<P>
where
    for<'a> P: page::AsPageIter<Iter<'a> = VmallocPageIter<'a>> + 'static,
{
    fn new(
        dev: &Device<Bound>,
        mut pages: P,
        dir: dma::DataDirection,
        flags: alloc::Flags,
    ) -> Result<impl PinInit<Self, Error> + '_> {
        let page_iter = pages.page_iter();
        let size = page_iter.size();

        let mut page_vec: KVec<*mut bindings::page> =
            KVec::with_capacity(page_iter.page_count(), flags)?;

        for page in page_iter {
            page_vec.push(page.as_ptr(), flags)?;
        }

        // `dma_max_mapping_size` returns `size_t`, but `sg_alloc_table_from_pages_segment()` takes
        // an `unsigned int`.
        //
        // SAFETY: `dev.as_raw()` is a valid pointer to a `struct device`.
        let max_segment = match unsafe { bindings::dma_max_mapping_size(dev.as_raw()) } {
            0 => u32::MAX,
            max_segment => u32::try_from(max_segment).unwrap_or(u32::MAX),
        };

        Ok(try_pin_init!(&this in Self {
            // SAFETY:
            // - `page_vec` is a `KVec` of valid `struct page *` obtained from `pages`.
            // - The pages contained in `pages` remain valid for the entire lifetime of the
            //   `RawSGTable`.
            sgt: unsafe { RawSGTable::new(&mut page_vec, size, max_segment, flags) }?,
            dma <- {
                // SAFETY: `this` is a valid pointer to uninitialized memory.
                let sgt = unsafe { &raw mut (*this.as_ptr()).sgt }.cast();

                // SAFETY: `sgt` is guaranteed to be non-null.
                let sgt = unsafe { NonNull::new_unchecked(sgt) };

                // SAFETY:
                // - It is guaranteed that the object returned by `DmaMappedSgt::new` won't out-live
                //   `sgt`.
                // - `sgt` is never DMA unmapped manually.
                Devres::new(dev, unsafe { DmaMappedSgt::new(sgt, dev, dir) })
            },
            _pages: pages,
        }))
    }
}

impl<P> SGTable<Owned<P>>
where
    for<'a> P: page::AsPageIter<Iter<'a> = VmallocPageIter<'a>> + 'static,
{
    /// Allocates a new scatter-gather table from the given pages and maps it for DMA.
    ///
    /// This constructor creates a new [`SGTable<Owned>`] that takes ownership of `P`.
    /// It allocates a `struct sg_table`, populates it with entries corresponding to the physical
    /// pages of `P`, and maps the table for DMA with the specified [`Device`] and
    /// [`dma::DataDirection`].
    ///
    /// The DMA mapping is managed through [`Devres`], ensuring that the DMA mapping is unmapped
    /// once the associated [`Device`] is unbound, or when the [`SGTable<Owned>`] is dropped.
    ///
    /// # Parameters
    ///
    /// * `dev`: The [`Device`] that will be performing the DMA.
    /// * `pages`: The entity providing the backing pages. It must implement [`page::AsPageIter`].
    ///   The ownership of this entity is moved into the new [`SGTable<Owned>`].
    /// * `dir`: The [`dma::DataDirection`] of the DMA transfer.
    /// * `flags`: Allocation flags for internal allocations (e.g., [`GFP_KERNEL`]).
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::{
    ///     device::{Bound, Device},
    ///     dma, page,
    ///     prelude::*,
    ///     scatterlist::{SGTable, Owned},
    /// };
    ///
    /// fn test(dev: &Device<Bound>) -> Result {
    ///     let size = 4 * page::PAGE_SIZE;
    ///     let pages = VVec::<u8>::with_capacity(size, GFP_KERNEL)?;
    ///
    ///     let sgt = KBox::pin_init(SGTable::new(
    ///         dev,
    ///         pages,
    ///         dma::DataDirection::ToDevice,
    ///         GFP_KERNEL,
    ///     ), GFP_KERNEL)?;
    ///
    ///     Ok(())
    /// }
    /// ```
    pub fn new(
        dev: &Device<Bound>,
        pages: P,
        dir: dma::DataDirection,
        flags: alloc::Flags,
    ) -> impl PinInit<Self, Error> + '_ {
        try_pin_init!(Self {
            inner <- Owned::new(dev, pages, dir, flags)?
        })
    }
}

impl<P> Deref for SGTable<Owned<P>> {
    type Target = SGTable;

    #[inline]
    fn deref(&self) -> &Self::Target {
        // SAFETY:
        // - `self.inner.sgt.as_raw()` is a valid pointer to a `struct sg_table` for the entire
        //   lifetime of `self`.
        // - The backing `struct sg_table` is not modified for the entire lifetime of `self`.
        unsafe { SGTable::from_raw(self.inner.sgt.as_raw()) }
    }
}

mod private {
    pub trait Sealed {}

    impl Sealed for super::Borrowed {}
    impl<P> Sealed for super::Owned<P> {}
}

/// An [`Iterator`] over the DMA mapped [`SGEntry`] items of an [`SGTable`].
///
/// Note that the existence of an [`SGTableIter`] does not guarantee that the [`SGEntry`] items
/// actually remain DMA mapped; they are prone to be unmapped on device unbind.
pub struct SGTableIter<'a> {
    pos: Option<&'a SGEntry>,
    /// The number of DMA mapped entries in a `struct sg_table`.
    nents: c_uint,
}

impl<'a> Iterator for SGTableIter<'a> {
    type Item = &'a SGEntry;

    fn next(&mut self) -> Option<Self::Item> {
        let entry = self.pos?;
        self.nents = self.nents.saturating_sub(1);

        // SAFETY: `entry.as_raw()` is a valid pointer to a `struct scatterlist`.
        let next = unsafe { bindings::sg_next(entry.as_raw()) };

        self.pos = (!next.is_null() && self.nents > 0).then(|| {
            // SAFETY: If `next` is not NULL, `sg_next()` guarantees to return a valid pointer to
            // the next `struct scatterlist`.
            unsafe { SGEntry::from_raw(next) }
        });

        Some(entry)
    }
}
