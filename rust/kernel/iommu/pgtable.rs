// SPDX-License-Identifier: GPL-2.0

//! IOMMU page table management.
//!
//! C header: [`include/linux/io-pgtable.h`](srctree/include/linux/io-pgtable.h)

use core::{
    marker::PhantomData,
    ptr::NonNull, //
};

use crate::{
    alloc,
    bindings,
    device::{
        Bound,
        Device, //
    },
    devres::Devres,
    error::to_result,
    io::PhysAddr,
    prelude::*, //
};

use bindings::io_pgtable_fmt;

/// Protection flags used with IOMMU mappings.
pub mod prot {
    /// Read access.
    pub const READ: u32 = bindings::IOMMU_READ;
    /// Write access.
    pub const WRITE: u32 = bindings::IOMMU_WRITE;
    /// Request cache coherency.
    pub const CACHE: u32 = bindings::IOMMU_CACHE;
    /// Request no-execute permission.
    pub const NOEXEC: u32 = bindings::IOMMU_NOEXEC;
    /// MMIO peripheral mapping.
    pub const MMIO: u32 = bindings::IOMMU_MMIO;
    /// Privileged mapping.
    pub const PRIVILEGED: u32 = bindings::IOMMU_PRIV;
}

/// Represents a requested `io_pgtable` configuration.
pub struct Config {
    /// Quirk bitmask (type-specific).
    pub quirks: usize,
    /// Valid page sizes, as a bitmask of powers of two.
    pub pgsize_bitmap: usize,
    /// Input address space size in bits.
    pub ias: u32,
    /// Output address space size in bits.
    pub oas: u32,
    /// IOMMU uses coherent accesses for page table walks.
    pub coherent_walk: bool,
}

/// An io page table using a specific format.
///
/// # Invariants
///
/// The pointer references a valid io page table.
pub struct IoPageTable<F: IoPageTableFmt> {
    ptr: NonNull<bindings::io_pgtable_ops>,
    _marker: PhantomData<F>,
}

// SAFETY: `struct io_pgtable_ops` is not restricted to a single thread.
unsafe impl<F: IoPageTableFmt> Send for IoPageTable<F> {}
// SAFETY: `struct io_pgtable_ops` may be accessed concurrently.
unsafe impl<F: IoPageTableFmt> Sync for IoPageTable<F> {}

/// The format used by this page table.
pub trait IoPageTableFmt: 'static {
    /// The value representing this format.
    const FORMAT: io_pgtable_fmt;
}

impl<F: IoPageTableFmt> IoPageTable<F> {
    /// Create a new `IoPageTable` as a device resource.
    #[inline]
    pub fn new(
        dev: &Device<Bound>,
        config: Config,
    ) -> impl PinInit<Devres<IoPageTable<F>>, Error> + '_ {
        // SAFETY: Devres ensures that the value is dropped during device unbind.
        Devres::new(dev, unsafe { Self::new_raw(dev, config) })
    }

    /// Create a new `IoPageTable`.
    ///
    /// # Safety
    ///
    /// If successful, then the returned `IoPageTable` must be dropped before the device is
    /// unbound.
    #[inline]
    pub unsafe fn new_raw(dev: &Device<Bound>, config: Config) -> Result<IoPageTable<F>> {
        let mut raw_cfg = bindings::io_pgtable_cfg {
            quirks: config.quirks,
            pgsize_bitmap: config.pgsize_bitmap,
            ias: config.ias,
            oas: config.oas,
            coherent_walk: config.coherent_walk,
            tlb: &raw const NOOP_FLUSH_OPS,
            iommu_dev: dev.as_raw(),
            // SAFETY: All zeroes is a valid value for `struct io_pgtable_cfg`.
            ..unsafe { core::mem::zeroed() }
        };

        // SAFETY:
        // * The raw_cfg pointer is valid for the duration of this call.
        // * The provided `FLUSH_OPS` contains valid function pointers that accept a null pointer
        //   as cookie.
        // * The caller ensures that the io pgtable does not outlive the device.
        let ops = unsafe {
            bindings::alloc_io_pgtable_ops(F::FORMAT, &mut raw_cfg, core::ptr::null_mut())
        };

        // INVARIANT: We successfully created a valid page table.
        Ok(IoPageTable {
            ptr: NonNull::new(ops).ok_or(ENOMEM)?,
            _marker: PhantomData,
        })
    }

    /// Obtain a raw pointer to the underlying `struct io_pgtable_ops`.
    #[inline]
    pub fn raw_ops(&self) -> *mut bindings::io_pgtable_ops {
        self.ptr.as_ptr()
    }

    /// Obtain a raw pointer to the underlying `struct io_pgtable`.
    #[inline]
    pub fn raw_pgtable(&self) -> *mut bindings::io_pgtable {
        // SAFETY: The io_pgtable_ops of an io-pgtable is always the ops field of a io_pgtable.
        unsafe { kernel::container_of!(self.raw_ops(), bindings::io_pgtable, ops) }
    }

    /// Obtain a raw pointer to the underlying `struct io_pgtable_cfg`.
    #[inline]
    pub fn raw_cfg(&self) -> *mut bindings::io_pgtable_cfg {
        // SAFETY: The `raw_pgtable()` method returns a valid pointer.
        unsafe { &raw mut (*self.raw_pgtable()).cfg }
    }

    /// Map a physically contiguous range of pages of the same size.
    ///
    /// Even if successful, this operation may not map the entire range. In that case, only a
    /// prefix of the range is mapped, and the returned integer indicates its length in bytes. In
    /// this case, the caller will usually call `map_pages` again for the remaining range.
    ///
    /// The returned [`Result`] indicates whether an error was encountered while mapping pages.
    /// Note that this may return a non-zero length even if an error was encountered. The caller
    /// will usually [unmap the relevant pages](Self::unmap_pages) on error.
    ///
    /// The caller must flush the TLB before using the pgtable to access the newly created mapping.
    ///
    /// # Safety
    ///
    /// * No other io-pgtable operation may access the range `iova .. iova+pgsize*pgcount` while
    ///   this `map_pages` operation executes.
    /// * This page table must not contain any mapping that overlaps with the mapping created by
    ///   this call.
    /// * If this page table is live, then the caller must ensure that it's okay to access the
    ///   physical address being mapped for the duration in which it is mapped.
    #[inline]
    pub unsafe fn map_pages(
        &self,
        iova: usize,
        paddr: PhysAddr,
        pgsize: usize,
        pgcount: usize,
        prot: u32,
        flags: alloc::Flags,
    ) -> (usize, Result) {
        let mut mapped: usize = 0;

        // SAFETY: The `map_pages` function in `io_pgtable_ops` is never null.
        let map_pages = unsafe { (*self.raw_ops()).map_pages.unwrap_unchecked() };

        // SAFETY: The safety requirements of this method are sufficient to call `map_pages`.
        let ret = to_result(unsafe {
            (map_pages)(
                self.raw_ops(),
                iova,
                paddr,
                pgsize,
                pgcount,
                prot as i32,
                flags.as_raw(),
                &mut mapped,
            )
        });

        (mapped, ret)
    }

    /// Unmap a range of virtually contiguous pages of the same size.
    ///
    /// This may not unmap the entire range, and returns the length of the unmapped prefix in
    /// bytes.
    ///
    /// # Safety
    ///
    /// * No other io-pgtable operation may access the range `iova .. iova+pgsize*pgcount` while
    ///   this `unmap_pages` operation executes.
    /// * This page table must contain one or more consecutive mappings starting at `iova` whose
    ///   total size is `pgcount * pgsize`.
    #[inline]
    #[must_use]
    pub unsafe fn unmap_pages(&self, iova: usize, pgsize: usize, pgcount: usize) -> usize {
        // SAFETY: The `unmap_pages` function in `io_pgtable_ops` is never null.
        let unmap_pages = unsafe { (*self.raw_ops()).unmap_pages.unwrap_unchecked() };

        // SAFETY: The safety requirements of this method are sufficient to call `unmap_pages`.
        unsafe { (unmap_pages)(self.raw_ops(), iova, pgsize, pgcount, core::ptr::null_mut()) }
    }
}

// For the initial users of these rust bindings, the GPU FW is managing the IOTLB and performs all
// required invalidations using a range. There is no need for it get ARM style invalidation
// instructions from the page table code.
//
// Support for flushing the TLB with ARM style invalidation instructions may be added in the
// future.
static NOOP_FLUSH_OPS: bindings::iommu_flush_ops = bindings::iommu_flush_ops {
    tlb_flush_all: Some(rust_tlb_flush_all_noop),
    tlb_flush_walk: Some(rust_tlb_flush_walk_noop),
    tlb_add_page: None,
};

#[no_mangle]
extern "C" fn rust_tlb_flush_all_noop(_cookie: *mut core::ffi::c_void) {}

#[no_mangle]
extern "C" fn rust_tlb_flush_walk_noop(
    _iova: usize,
    _size: usize,
    _granule: usize,
    _cookie: *mut core::ffi::c_void,
) {
}

impl<F: IoPageTableFmt> Drop for IoPageTable<F> {
    fn drop(&mut self) {
        // SAFETY: The caller of `Self::ttbr()` promised that the page table is not live when this
        // destructor runs.
        unsafe { bindings::free_io_pgtable_ops(self.raw_ops()) };
    }
}

/// The `ARM_64_LPAE_S1` page table format.
pub enum ARM64LPAES1 {}

impl IoPageTableFmt for ARM64LPAES1 {
    const FORMAT: io_pgtable_fmt = bindings::io_pgtable_fmt_ARM_64_LPAE_S1 as io_pgtable_fmt;
}

impl IoPageTable<ARM64LPAES1> {
    /// Access the `ttbr` field of the configuration.
    ///
    /// This is the physical address of the page table, which may be passed to the device that
    /// needs to use it.
    ///
    /// # Safety
    ///
    /// The caller must ensure that the device stops using the page table before dropping it.
    #[inline]
    pub unsafe fn ttbr(&self) -> u64 {
        // SAFETY: `arm_lpae_s1_cfg` is the right cfg type for `ARM64LPAES1`.
        unsafe { (*self.raw_cfg()).__bindgen_anon_1.arm_lpae_s1_cfg.ttbr }
    }

    /// Access the `mair` field of the configuration.
    #[inline]
    pub fn mair(&self) -> u64 {
        // SAFETY: `arm_lpae_s1_cfg` is the right cfg type for `ARM64LPAES1`.
        unsafe { (*self.raw_cfg()).__bindgen_anon_1.arm_lpae_s1_cfg.mair }
    }
}
