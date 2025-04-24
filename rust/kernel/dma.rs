// SPDX-License-Identifier: GPL-2.0

//! Direct memory access (DMA).
//!
//! C header: [`include/linux/dma-mapping.h`](srctree/include/linux/dma-mapping.h)

use crate::{
    bindings, build_assert,
    device::Device,
    error::code::*,
    error::Result,
    transmute::{AsBytes, FromBytes},
    types::ARef,
};

/// Possible attributes associated with a DMA mapping.
///
/// They can be combined with the operators `|`, `&`, and `!`.
///
/// Values can be used from the [`attrs`] module.
///
/// # Examples
///
/// ```
/// use kernel::device::Device;
/// use kernel::dma::{attrs::*, CoherentAllocation};
///
/// # fn test(dev: &Device) -> Result {
/// let attribs = DMA_ATTR_FORCE_CONTIGUOUS | DMA_ATTR_NO_WARN;
/// let c: CoherentAllocation<u64> =
///     CoherentAllocation::alloc_attrs(dev, 4, GFP_KERNEL, attribs)?;
/// # Ok::<(), Error>(()) }
/// ```
#[derive(Clone, Copy, PartialEq)]
#[repr(transparent)]
pub struct Attrs(u32);

impl Attrs {
    /// Get the raw representation of this attribute.
    pub(crate) fn as_raw(self) -> crate::ffi::c_ulong {
        self.0 as _
    }

    /// Check whether `flags` is contained in `self`.
    pub fn contains(self, flags: Attrs) -> bool {
        (self & flags) == flags
    }
}

impl core::ops::BitOr for Attrs {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl core::ops::BitAnd for Attrs {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
}

impl core::ops::Not for Attrs {
    type Output = Self;
    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}

/// DMA mapping attributes.
pub mod attrs {
    use super::Attrs;

    /// Specifies that reads and writes to the mapping may be weakly ordered, that is that reads
    /// and writes may pass each other.
    pub const DMA_ATTR_WEAK_ORDERING: Attrs = Attrs(bindings::DMA_ATTR_WEAK_ORDERING);

    /// Specifies that writes to the mapping may be buffered to improve performance.
    pub const DMA_ATTR_WRITE_COMBINE: Attrs = Attrs(bindings::DMA_ATTR_WRITE_COMBINE);

    /// Lets the platform to avoid creating a kernel virtual mapping for the allocated buffer.
    pub const DMA_ATTR_NO_KERNEL_MAPPING: Attrs = Attrs(bindings::DMA_ATTR_NO_KERNEL_MAPPING);

    /// Allows platform code to skip synchronization of the CPU cache for the given buffer assuming
    /// that it has been already transferred to 'device' domain.
    pub const DMA_ATTR_SKIP_CPU_SYNC: Attrs = Attrs(bindings::DMA_ATTR_SKIP_CPU_SYNC);

    /// Forces contiguous allocation of the buffer in physical memory.
    pub const DMA_ATTR_FORCE_CONTIGUOUS: Attrs = Attrs(bindings::DMA_ATTR_FORCE_CONTIGUOUS);

    /// This is a hint to the DMA-mapping subsystem that it's probably not worth the time to try
    /// to allocate memory to in a way that gives better TLB efficiency.
    pub const DMA_ATTR_ALLOC_SINGLE_PAGES: Attrs = Attrs(bindings::DMA_ATTR_ALLOC_SINGLE_PAGES);

    /// This tells the DMA-mapping subsystem to suppress allocation failure reports (similarly to
    /// __GFP_NOWARN).
    pub const DMA_ATTR_NO_WARN: Attrs = Attrs(bindings::DMA_ATTR_NO_WARN);

    /// Used to indicate that the buffer is fully accessible at an elevated privilege level (and
    /// ideally inaccessible or at least read-only at lesser-privileged levels).
    pub const DMA_ATTR_PRIVILEGED: Attrs = Attrs(bindings::DMA_ATTR_PRIVILEGED);
}

/// An abstraction of the `dma_alloc_coherent` API.
///
/// This is an abstraction around the `dma_alloc_coherent` API which is used to allocate and map
/// large consistent DMA regions.
///
/// A [`CoherentAllocation`] instance contains a pointer to the allocated region (in the
/// processor's virtual address space) and the device address which can be given to the device
/// as the DMA address base of the region. The region is released once [`CoherentAllocation`]
/// is dropped.
///
/// # Invariants
///
/// For the lifetime of an instance of [`CoherentAllocation`], the `cpu_addr` is a valid pointer
/// to an allocated region of consistent memory and `dma_handle` is the DMA address base of
/// the region.
// TODO
//
// DMA allocations potentially carry device resources (e.g.IOMMU mappings), hence for soundness
// reasons DMA allocation would need to be embedded in a `Devres` container, in order to ensure
// that device resources can never survive device unbind.
//
// However, it is neither desirable nor necessary to protect the allocated memory of the DMA
// allocation from surviving device unbind; it would require RCU read side critical sections to
// access the memory, which may require subsequent unnecessary copies.
//
// Hence, find a way to revoke the device resources of a `CoherentAllocation`, but not the
// entire `CoherentAllocation` including the allocated memory itself.
pub struct CoherentAllocation<T: AsBytes + FromBytes> {
    dev: ARef<Device>,
    dma_handle: bindings::dma_addr_t,
    count: usize,
    cpu_addr: *mut T,
    dma_attrs: Attrs,
}

impl<T: AsBytes + FromBytes> CoherentAllocation<T> {
    /// Allocates a region of `size_of::<T> * count` of consistent memory.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::device::Device;
    /// use kernel::dma::{attrs::*, CoherentAllocation};
    ///
    /// # fn test(dev: &Device) -> Result {
    /// let c: CoherentAllocation<u64> =
    ///     CoherentAllocation::alloc_attrs(dev, 4, GFP_KERNEL, DMA_ATTR_NO_WARN)?;
    /// # Ok::<(), Error>(()) }
    /// ```
    pub fn alloc_attrs(
        dev: &Device,
        count: usize,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<CoherentAllocation<T>> {
        build_assert!(
            core::mem::size_of::<T>() > 0,
            "It doesn't make sense for the allocated type to be a ZST"
        );

        let size = count
            .checked_mul(core::mem::size_of::<T>())
            .ok_or(EOVERFLOW)?;
        let mut dma_handle = 0;
        // SAFETY: Device pointer is guaranteed as valid by the type invariant on `Device`.
        let ret = unsafe {
            bindings::dma_alloc_attrs(
                dev.as_raw(),
                size,
                &mut dma_handle,
                gfp_flags.as_raw(),
                dma_attrs.as_raw(),
            )
        };
        if ret.is_null() {
            return Err(ENOMEM);
        }
        // INVARIANT: We just successfully allocated a coherent region which is accessible for
        // `count` elements, hence the cpu address is valid. We also hold a refcounted reference
        // to the device.
        Ok(Self {
            dev: dev.into(),
            dma_handle,
            count,
            cpu_addr: ret as *mut T,
            dma_attrs,
        })
    }

    /// Performs the same functionality as [`CoherentAllocation::alloc_attrs`], except the
    /// `dma_attrs` is 0 by default.
    pub fn alloc_coherent(
        dev: &Device,
        count: usize,
        gfp_flags: kernel::alloc::Flags,
    ) -> Result<CoherentAllocation<T>> {
        CoherentAllocation::alloc_attrs(dev, count, gfp_flags, Attrs(0))
    }

    /// Returns the base address to the allocated region in the CPU's virtual address space.
    pub fn start_ptr(&self) -> *const T {
        self.cpu_addr
    }

    /// Returns the base address to the allocated region in the CPU's virtual address space as
    /// a mutable pointer.
    pub fn start_ptr_mut(&mut self) -> *mut T {
        self.cpu_addr
    }

    /// Returns a DMA handle which may given to the device as the DMA address base of
    /// the region.
    pub fn dma_handle(&self) -> bindings::dma_addr_t {
        self.dma_handle
    }

    /// Returns a pointer to an element from the region with bounds checking. `offset` is in
    /// units of `T`, not the number of bytes.
    ///
    /// Public but hidden since it should only be used from [`dma_read`] and [`dma_write`] macros.
    #[doc(hidden)]
    pub fn item_from_index(&self, offset: usize) -> Result<*mut T> {
        if offset >= self.count {
            return Err(EINVAL);
        }
        // SAFETY:
        // - The pointer is valid due to type invariant on `CoherentAllocation`
        // and we've just checked that the range and index is within bounds.
        // - `offset` can't overflow since it is smaller than `self.count` and we've checked
        // that `self.count` won't overflow early in the constructor.
        Ok(unsafe { self.cpu_addr.add(offset) })
    }

    /// Reads the value of `field` and ensures that its type is [`FromBytes`].
    ///
    /// # Safety
    ///
    /// This must be called from the [`dma_read`] macro which ensures that the `field` pointer is
    /// validated beforehand.
    ///
    /// Public but hidden since it should only be used from [`dma_read`] macro.
    #[doc(hidden)]
    pub unsafe fn field_read<F: FromBytes>(&self, field: *const F) -> F {
        // SAFETY:
        // - By the safety requirements field is valid.
        // - Using read_volatile() here is not sound as per the usual rules, the usage here is
        // a special exception with the following notes in place. When dealing with a potential
        // race from a hardware or code outside kernel (e.g. user-space program), we need that
        // read on a valid memory is not UB. Currently read_volatile() is used for this, and the
        // rationale behind is that it should generate the same code as READ_ONCE() which the
        // kernel already relies on to avoid UB on data races. Note that the usage of
        // read_volatile() is limited to this particular case, it cannot be used to prevent
        // the UB caused by racing between two kernel functions nor do they provide atomicity.
        unsafe { field.read_volatile() }
    }

    /// Writes a value to `field` and ensures that its type is [`AsBytes`].
    ///
    /// # Safety
    ///
    /// This must be called from the [`dma_write`] macro which ensures that the `field` pointer is
    /// validated beforehand.
    ///
    /// Public but hidden since it should only be used from [`dma_write`] macro.
    #[doc(hidden)]
    pub unsafe fn field_write<F: AsBytes>(&self, field: *mut F, val: F) {
        // SAFETY:
        // - By the safety requirements field is valid.
        // - Using write_volatile() here is not sound as per the usual rules, the usage here is
        // a special exception with the following notes in place. When dealing with a potential
        // race from a hardware or code outside kernel (e.g. user-space program), we need that
        // write on a valid memory is not UB. Currently write_volatile() is used for this, and the
        // rationale behind is that it should generate the same code as WRITE_ONCE() which the
        // kernel already relies on to avoid UB on data races. Note that the usage of
        // write_volatile() is limited to this particular case, it cannot be used to prevent
        // the UB caused by racing between two kernel functions nor do they provide atomicity.
        unsafe { field.write_volatile(val) }
    }
}

/// Note that the device configured to do DMA must be halted before this object is dropped.
impl<T: AsBytes + FromBytes> Drop for CoherentAllocation<T> {
    fn drop(&mut self) {
        let size = self.count * core::mem::size_of::<T>();
        // SAFETY: Device pointer is guaranteed as valid by the type invariant on `Device`.
        // The cpu address, and the dma handle are valid due to the type invariants on
        // `CoherentAllocation`.
        unsafe {
            bindings::dma_free_attrs(
                self.dev.as_raw(),
                size,
                self.cpu_addr as _,
                self.dma_handle,
                self.dma_attrs.as_raw(),
            )
        }
    }
}

// SAFETY: It is safe to send a `CoherentAllocation` to another thread if `T`
// can be sent to another thread.
unsafe impl<T: AsBytes + FromBytes + Send> Send for CoherentAllocation<T> {}

/// Reads a field of an item from an allocated region of structs.
///
/// # Examples
///
/// ```
/// use kernel::device::Device;
/// use kernel::dma::{attrs::*, CoherentAllocation};
///
/// struct MyStruct { field: u32, }
///
/// // SAFETY: All bit patterns are acceptable values for `MyStruct`.
/// unsafe impl kernel::transmute::FromBytes for MyStruct{};
/// // SAFETY: Instances of `MyStruct` have no uninitialized portions.
/// unsafe impl kernel::transmute::AsBytes for MyStruct{};
///
/// # fn test(alloc: &kernel::dma::CoherentAllocation<MyStruct>) -> Result {
/// let whole = kernel::dma_read!(alloc[2]);
/// let field = kernel::dma_read!(alloc[1].field);
/// # Ok::<(), Error>(()) }
/// ```
#[macro_export]
macro_rules! dma_read {
    ($dma:expr, $idx: expr, $($field:tt)*) => {{
        let item = $crate::dma::CoherentAllocation::item_from_index(&$dma, $idx)?;
        // SAFETY: `item_from_index` ensures that `item` is always a valid pointer and can be
        // dereferenced. The compiler also further validates the expression on whether `field`
        // is a member of `item` when expanded by the macro.
        unsafe {
            let ptr_field = ::core::ptr::addr_of!((*item) $($field)*);
            $crate::dma::CoherentAllocation::field_read(&$dma, ptr_field)
        }
    }};
    ($dma:ident [ $idx:expr ] $($field:tt)* ) => {
        $crate::dma_read!($dma, $idx, $($field)*);
    };
    ($($dma:ident).* [ $idx:expr ] $($field:tt)* ) => {
        $crate::dma_read!($($dma).*, $idx, $($field)*);
    };
}

/// Writes to a field of an item from an allocated region of structs.
///
/// # Examples
///
/// ```
/// use kernel::device::Device;
/// use kernel::dma::{attrs::*, CoherentAllocation};
///
/// struct MyStruct { member: u32, }
///
/// // SAFETY: All bit patterns are acceptable values for `MyStruct`.
/// unsafe impl kernel::transmute::FromBytes for MyStruct{};
/// // SAFETY: Instances of `MyStruct` have no uninitialized portions.
/// unsafe impl kernel::transmute::AsBytes for MyStruct{};
///
/// # fn test(alloc: &kernel::dma::CoherentAllocation<MyStruct>) -> Result {
/// kernel::dma_write!(alloc[2].member = 0xf);
/// kernel::dma_write!(alloc[1] = MyStruct { member: 0xf });
/// # Ok::<(), Error>(()) }
/// ```
#[macro_export]
macro_rules! dma_write {
    ($dma:ident [ $idx:expr ] $($field:tt)*) => {{
        $crate::dma_write!($dma, $idx, $($field)*);
    }};
    ($($dma:ident).* [ $idx:expr ] $($field:tt)* ) => {{
        $crate::dma_write!($($dma).*, $idx, $($field)*);
    }};
    ($dma:expr, $idx: expr, = $val:expr) => {
        let item = $crate::dma::CoherentAllocation::item_from_index(&$dma, $idx)?;
        // SAFETY: `item_from_index` ensures that `item` is always a valid item.
        unsafe { $crate::dma::CoherentAllocation::field_write(&$dma, item, $val) }
    };
    ($dma:expr, $idx: expr, $(.$field:ident)* = $val:expr) => {
        let item = $crate::dma::CoherentAllocation::item_from_index(&$dma, $idx)?;
        // SAFETY: `item_from_index` ensures that `item` is always a valid pointer and can be
        // dereferenced. The compiler also further validates the expression on whether `field`
        // is a member of `item` when expanded by the macro.
        unsafe {
            let ptr_field = ::core::ptr::addr_of_mut!((*item) $(.$field)*);
            $crate::dma::CoherentAllocation::field_write(&$dma, ptr_field, $val)
        }
    };
}
