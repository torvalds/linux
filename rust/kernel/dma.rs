// SPDX-License-Identifier: GPL-2.0

//! Direct memory access (DMA).
//!
//! C header: [`include/linux/dma-mapping.h`](srctree/include/linux/dma-mapping.h)

use crate::{
    bindings,
    debugfs,
    device::{
        self,
        Bound,
        Core, //
    },
    error::to_result,
    fs::file,
    prelude::*,
    ptr::KnownSize,
    sync::aref::ARef,
    transmute::{
        AsBytes,
        FromBytes, //
    }, //
    uaccess::UserSliceWriter,
};
use core::{
    ops::{
        Deref,
        DerefMut, //
    },
    ptr::NonNull, //
};

/// DMA address type.
///
/// Represents a bus address used for Direct Memory Access (DMA) operations.
///
/// This is an alias of the kernel's `dma_addr_t`, which may be `u32` or `u64` depending on
/// `CONFIG_ARCH_DMA_ADDR_T_64BIT`.
///
/// Note that this may be `u64` even on 32-bit architectures.
pub type DmaAddress = bindings::dma_addr_t;

/// Trait to be implemented by DMA capable bus devices.
///
/// The [`dma::Device`](Device) trait should be implemented by bus specific device representations,
/// where the underlying bus is DMA capable, such as:
#[cfg_attr(CONFIG_PCI, doc = "* [`pci::Device`](kernel::pci::Device)")]
/// * [`platform::Device`](::kernel::platform::Device)
pub trait Device: AsRef<device::Device<Core>> {
    /// Set up the device's DMA streaming addressing capabilities.
    ///
    /// This method is usually called once from `probe()` as soon as the device capabilities are
    /// known.
    ///
    /// # Safety
    ///
    /// This method must not be called concurrently with any DMA allocation or mapping primitives,
    /// such as [`Coherent::zeroed`].
    unsafe fn dma_set_mask(&self, mask: DmaMask) -> Result {
        // SAFETY:
        // - By the type invariant of `device::Device`, `self.as_ref().as_raw()` is valid.
        // - The safety requirement of this function guarantees that there are no concurrent calls
        //   to DMA allocation and mapping primitives using this mask.
        to_result(unsafe { bindings::dma_set_mask(self.as_ref().as_raw(), mask.value()) })
    }

    /// Set up the device's DMA coherent addressing capabilities.
    ///
    /// This method is usually called once from `probe()` as soon as the device capabilities are
    /// known.
    ///
    /// # Safety
    ///
    /// This method must not be called concurrently with any DMA allocation or mapping primitives,
    /// such as [`Coherent::zeroed`].
    unsafe fn dma_set_coherent_mask(&self, mask: DmaMask) -> Result {
        // SAFETY:
        // - By the type invariant of `device::Device`, `self.as_ref().as_raw()` is valid.
        // - The safety requirement of this function guarantees that there are no concurrent calls
        //   to DMA allocation and mapping primitives using this mask.
        to_result(unsafe { bindings::dma_set_coherent_mask(self.as_ref().as_raw(), mask.value()) })
    }

    /// Set up the device's DMA addressing capabilities.
    ///
    /// This is a combination of [`Device::dma_set_mask`] and [`Device::dma_set_coherent_mask`].
    ///
    /// This method is usually called once from `probe()` as soon as the device capabilities are
    /// known.
    ///
    /// # Safety
    ///
    /// This method must not be called concurrently with any DMA allocation or mapping primitives,
    /// such as [`Coherent::zeroed`].
    unsafe fn dma_set_mask_and_coherent(&self, mask: DmaMask) -> Result {
        // SAFETY:
        // - By the type invariant of `device::Device`, `self.as_ref().as_raw()` is valid.
        // - The safety requirement of this function guarantees that there are no concurrent calls
        //   to DMA allocation and mapping primitives using this mask.
        to_result(unsafe {
            bindings::dma_set_mask_and_coherent(self.as_ref().as_raw(), mask.value())
        })
    }

    /// Set the maximum size of a single DMA segment the device may request.
    ///
    /// This method is usually called once from `probe()` as soon as the device capabilities are
    /// known.
    ///
    /// # Safety
    ///
    /// This method must not be called concurrently with any DMA allocation or mapping primitives,
    /// such as [`Coherent::zeroed`].
    unsafe fn dma_set_max_seg_size(&self, size: u32) {
        // SAFETY:
        // - By the type invariant of `device::Device`, `self.as_ref().as_raw()` is valid.
        // - The safety requirement of this function guarantees that there are no concurrent calls
        //   to DMA allocation and mapping primitives using this parameter.
        unsafe { bindings::dma_set_max_seg_size(self.as_ref().as_raw(), size) }
    }
}

/// A DMA mask that holds a bitmask with the lowest `n` bits set.
///
/// Use [`DmaMask::new`] or [`DmaMask::try_new`] to construct a value. Values
/// are guaranteed to never exceed the bit width of `u64`.
///
/// This is the Rust equivalent of the C macro `DMA_BIT_MASK()`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DmaMask(u64);

impl DmaMask {
    /// Constructs a `DmaMask` with the lowest `n` bits set to `1`.
    ///
    /// For `n <= 64`, sets exactly the lowest `n` bits.
    /// For `n > 64`, results in a build error.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::dma::DmaMask;
    ///
    /// let mask0 = DmaMask::new::<0>();
    /// assert_eq!(mask0.value(), 0);
    ///
    /// let mask1 = DmaMask::new::<1>();
    /// assert_eq!(mask1.value(), 0b1);
    ///
    /// let mask64 = DmaMask::new::<64>();
    /// assert_eq!(mask64.value(), u64::MAX);
    ///
    /// // Build failure.
    /// // let mask_overflow = DmaMask::new::<100>();
    /// ```
    #[inline]
    pub const fn new<const N: u32>() -> Self {
        let Ok(mask) = Self::try_new(N) else {
            build_error!("Invalid DMA Mask.");
        };

        mask
    }

    /// Constructs a `DmaMask` with the lowest `n` bits set to `1`.
    ///
    /// For `n <= 64`, sets exactly the lowest `n` bits.
    /// For `n > 64`, returns [`EINVAL`].
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::dma::DmaMask;
    ///
    /// let mask0 = DmaMask::try_new(0)?;
    /// assert_eq!(mask0.value(), 0);
    ///
    /// let mask1 = DmaMask::try_new(1)?;
    /// assert_eq!(mask1.value(), 0b1);
    ///
    /// let mask64 = DmaMask::try_new(64)?;
    /// assert_eq!(mask64.value(), u64::MAX);
    ///
    /// let mask_overflow = DmaMask::try_new(100);
    /// assert!(mask_overflow.is_err());
    /// # Ok::<(), Error>(())
    /// ```
    #[inline]
    pub const fn try_new(n: u32) -> Result<Self> {
        Ok(Self(match n {
            0 => 0,
            1..=64 => u64::MAX >> (64 - n),
            _ => return Err(EINVAL),
        }))
    }

    /// Returns the underlying `u64` bitmask value.
    #[inline]
    pub const fn value(&self) -> u64 {
        self.0
    }
}

/// Possible attributes associated with a DMA mapping.
///
/// They can be combined with the operators `|`, `&`, and `!`.
///
/// Values can be used from the [`attrs`] module.
///
/// # Examples
///
/// ```
/// # use kernel::device::{Bound, Device};
/// use kernel::dma::{attrs::*, Coherent};
///
/// # fn test(dev: &Device<Bound>) -> Result {
/// let attribs = DMA_ATTR_FORCE_CONTIGUOUS | DMA_ATTR_NO_WARN;
/// let c: Coherent<[u64]> =
///     Coherent::zeroed_slice_with_attrs(dev, 4, GFP_KERNEL, attribs)?;
/// # Ok::<(), Error>(()) }
/// ```
#[derive(Clone, Copy, PartialEq)]
#[repr(transparent)]
pub struct Attrs(u32);

impl Attrs {
    /// Get the raw representation of this attribute.
    pub(crate) fn as_raw(self) -> crate::ffi::c_ulong {
        self.0 as crate::ffi::c_ulong
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

    /// Allows platform code to skip synchronization of the CPU cache for the given buffer assuming
    /// that it has been already transferred to 'device' domain.
    pub const DMA_ATTR_SKIP_CPU_SYNC: Attrs = Attrs(bindings::DMA_ATTR_SKIP_CPU_SYNC);

    /// Forces contiguous allocation of the buffer in physical memory.
    pub const DMA_ATTR_FORCE_CONTIGUOUS: Attrs = Attrs(bindings::DMA_ATTR_FORCE_CONTIGUOUS);

    /// Hints DMA-mapping subsystem that it's probably not worth the time to try
    /// to allocate memory to in a way that gives better TLB efficiency.
    pub const DMA_ATTR_ALLOC_SINGLE_PAGES: Attrs = Attrs(bindings::DMA_ATTR_ALLOC_SINGLE_PAGES);

    /// This tells the DMA-mapping subsystem to suppress allocation failure reports (similarly to
    /// `__GFP_NOWARN`).
    pub const DMA_ATTR_NO_WARN: Attrs = Attrs(bindings::DMA_ATTR_NO_WARN);

    /// Indicates that the buffer is fully accessible at an elevated privilege level (and
    /// ideally inaccessible or at least read-only at lesser-privileged levels).
    pub const DMA_ATTR_PRIVILEGED: Attrs = Attrs(bindings::DMA_ATTR_PRIVILEGED);

    /// Indicates that the buffer is MMIO memory.
    pub const DMA_ATTR_MMIO: Attrs = Attrs(bindings::DMA_ATTR_MMIO);
}

/// DMA data direction.
///
/// Corresponds to the C [`enum dma_data_direction`].
///
/// [`enum dma_data_direction`]: srctree/include/linux/dma-direction.h
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
#[repr(u32)]
pub enum DataDirection {
    /// The DMA mapping is for bidirectional data transfer.
    ///
    /// This is used when the buffer can be both read from and written to by the device.
    /// The cache for the corresponding memory region is both flushed and invalidated.
    Bidirectional = Self::const_cast(bindings::dma_data_direction_DMA_BIDIRECTIONAL),

    /// The DMA mapping is for data transfer from memory to the device (write).
    ///
    /// The CPU has prepared data in the buffer, and the device will read it.
    /// The cache for the corresponding memory region is flushed before device access.
    ToDevice = Self::const_cast(bindings::dma_data_direction_DMA_TO_DEVICE),

    /// The DMA mapping is for data transfer from the device to memory (read).
    ///
    /// The device will write data into the buffer for the CPU to read.
    /// The cache for the corresponding memory region is invalidated before CPU access.
    FromDevice = Self::const_cast(bindings::dma_data_direction_DMA_FROM_DEVICE),

    /// The DMA mapping is not for data transfer.
    ///
    /// This is primarily for debugging purposes. With this direction, the DMA mapping API
    /// will not perform any cache coherency operations.
    None = Self::const_cast(bindings::dma_data_direction_DMA_NONE),
}

impl DataDirection {
    /// Casts the bindgen-generated enum type to a `u32` at compile time.
    ///
    /// This function will cause a compile-time error if the underlying value of the
    /// C enum is out of bounds for `u32`.
    const fn const_cast(val: bindings::dma_data_direction) -> u32 {
        // CAST: The C standard allows compilers to choose different integer types for enums.
        // To safely check the value, we cast it to a wide signed integer type (`i128`)
        // which can hold any standard C integer enum type without truncation.
        let wide_val = val as i128;

        // Check if the value is outside the valid range for the target type `u32`.
        // CAST: `u32::MAX` is cast to `i128` to match the type of `wide_val` for the comparison.
        if wide_val < 0 || wide_val > u32::MAX as i128 {
            // Trigger a compile-time error in a const context.
            build_error!("C enum value is out of bounds for the target type `u32`.");
        }

        // CAST: This cast is valid because the check above guarantees that `wide_val`
        // is within the representable range of `u32`.
        wide_val as u32
    }
}

impl From<DataDirection> for bindings::dma_data_direction {
    /// Returns the raw representation of [`enum dma_data_direction`].
    fn from(direction: DataDirection) -> Self {
        // CAST: `direction as u32` gets the underlying representation of our `#[repr(u32)]` enum.
        // The subsequent cast to `Self` (the bindgen type) assumes the C enum is compatible
        // with the enum variants of `DataDirection`, which is a valid assumption given our
        // compile-time checks.
        direction as u32 as Self
    }
}

/// CPU-owned DMA allocation that can be converted into a device-shared [`Coherent`] object.
///
/// Unlike [`Coherent`], a [`CoherentBox`] is guaranteed to be fully owned by the CPU -- its DMA
/// address is not exposed and it cannot be accessed by a device. This means it can safely be used
/// like a normal boxed allocation (e.g. direct reads, writes, and mutable slices are all safe).
///
/// A typical use is to allocate a [`CoherentBox`], populate it with normal CPU access, and then
/// convert it into a [`Coherent`] object to share it with the device.
///
/// # Examples
///
/// `CoherentBox<T>`:
///
/// ```
/// # use kernel::device::{
/// #     Bound,
/// #     Device,
/// # };
/// use kernel::dma::{attrs::*,
///     Coherent,
///     CoherentBox,
/// };
///
/// # fn test(dev: &Device<Bound>) -> Result {
/// let mut dmem: CoherentBox<u64> = CoherentBox::zeroed(dev, GFP_KERNEL)?;
/// *dmem = 42;
/// let dmem: Coherent<u64> = dmem.into();
/// # Ok::<(), Error>(()) }
/// ```
///
/// `CoherentBox<[T]>`:
///
///
/// ```
/// # use kernel::device::{
/// #     Bound,
/// #     Device,
/// # };
/// use kernel::dma::{attrs::*,
///     Coherent,
///     CoherentBox,
/// };
///
/// # fn test(dev: &Device<Bound>) -> Result {
/// let mut dmem: CoherentBox<[u64]> = CoherentBox::zeroed_slice(dev, 4, GFP_KERNEL)?;
/// dmem.fill(42);
/// let dmem: Coherent<[u64]> = dmem.into();
/// # Ok::<(), Error>(()) }
/// ```
pub struct CoherentBox<T: KnownSize + ?Sized>(Coherent<T>);

impl<T: AsBytes + FromBytes> CoherentBox<[T]> {
    /// [`CoherentBox`] variant of [`Coherent::zeroed_slice_with_attrs`].
    #[inline]
    pub fn zeroed_slice_with_attrs(
        dev: &device::Device<Bound>,
        count: usize,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Self> {
        Coherent::zeroed_slice_with_attrs(dev, count, gfp_flags, dma_attrs).map(Self)
    }

    /// Same as [CoherentBox::zeroed_slice_with_attrs], but with `dma::Attrs(0)`.
    #[inline]
    pub fn zeroed_slice(
        dev: &device::Device<Bound>,
        count: usize,
        gfp_flags: kernel::alloc::Flags,
    ) -> Result<Self> {
        Self::zeroed_slice_with_attrs(dev, count, gfp_flags, Attrs(0))
    }

    /// Initializes the element at `i` using the given initializer.
    ///
    /// Returns `EINVAL` if `i` is out of bounds.
    pub fn init_at<E>(&mut self, i: usize, init: impl Init<T, E>) -> Result
    where
        Error: From<E>,
    {
        if i >= self.0.len() {
            return Err(EINVAL);
        }

        let ptr = &raw mut self[i];

        // SAFETY:
        // - `ptr` is valid, properly aligned, and within this allocation.
        // - `T: AsBytes + FromBytes` guarantees all bit patterns are valid, so partial writes on
        //   error cannot leave the element in an invalid state.
        // - The DMA address has not been exposed yet, so there is no concurrent device access.
        unsafe { init.__init(ptr)? };

        Ok(())
    }

    /// Allocates a region of coherent memory of the same size as `data` and initializes it with a
    /// copy of its contents.
    ///
    /// This is the [`CoherentBox`] variant of [`Coherent::from_slice_with_attrs`].
    ///
    /// # Examples
    ///
    /// ```
    /// use core::ops::Deref;
    ///
    /// # use kernel::device::{Bound, Device};
    /// use kernel::dma::{
    ///     attrs::*,
    ///     CoherentBox
    /// };
    ///
    /// # fn test(dev: &Device<Bound>) -> Result {
    /// let data = [0u8, 1u8, 2u8, 3u8];
    /// let c: CoherentBox<[u8]> =
    ///     CoherentBox::from_slice_with_attrs(dev, &data, GFP_KERNEL, DMA_ATTR_NO_WARN)?;
    ///
    /// assert_eq!(c.deref(), &data);
    /// # Ok::<(), Error>(()) }
    /// ```
    pub fn from_slice_with_attrs(
        dev: &device::Device<Bound>,
        data: &[T],
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Self>
    where
        T: Copy,
    {
        let mut slice = Self(Coherent::<T>::alloc_slice_with_attrs(
            dev,
            data.len(),
            gfp_flags,
            dma_attrs,
        )?);

        // PANIC: `slice` was created with length `data.len()`.
        slice.copy_from_slice(data);

        Ok(slice)
    }

    /// Performs the same functionality as [`CoherentBox::from_slice_with_attrs`], except the
    /// `dma_attrs` is 0 by default.
    #[inline]
    pub fn from_slice(
        dev: &device::Device<Bound>,
        data: &[T],
        gfp_flags: kernel::alloc::Flags,
    ) -> Result<Self>
    where
        T: Copy,
    {
        Self::from_slice_with_attrs(dev, data, gfp_flags, Attrs(0))
    }
}

impl<T: AsBytes + FromBytes> CoherentBox<T> {
    /// Same as [`CoherentBox::zeroed_slice_with_attrs`], but for a single element.
    #[inline]
    pub fn zeroed_with_attrs(
        dev: &device::Device<Bound>,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Self> {
        Coherent::zeroed_with_attrs(dev, gfp_flags, dma_attrs).map(Self)
    }

    /// Same as [`CoherentBox::zeroed_slice`], but for a single element.
    #[inline]
    pub fn zeroed(dev: &device::Device<Bound>, gfp_flags: kernel::alloc::Flags) -> Result<Self> {
        Self::zeroed_with_attrs(dev, gfp_flags, Attrs(0))
    }
}

impl<T: KnownSize + ?Sized> Deref for CoherentBox<T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &Self::Target {
        // SAFETY:
        // - We have not exposed the DMA address yet, so there can't be any concurrent access by a
        //   device.
        // - We have exclusive access to `self.0`.
        unsafe { self.0.as_ref() }
    }
}

impl<T: AsBytes + FromBytes + KnownSize + ?Sized> DerefMut for CoherentBox<T> {
    #[inline]
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY:
        // - We have not exposed the DMA address yet, so there can't be any concurrent access by a
        //   device.
        // - We have exclusive access to `self.0`.
        unsafe { self.0.as_mut() }
    }
}

impl<T: AsBytes + FromBytes + KnownSize + ?Sized> From<CoherentBox<T>> for Coherent<T> {
    #[inline]
    fn from(value: CoherentBox<T>) -> Self {
        value.0
    }
}

/// An abstraction of the `dma_alloc_coherent` API.
///
/// This is an abstraction around the `dma_alloc_coherent` API which is used to allocate and map
/// large coherent DMA regions.
///
/// A [`Coherent`] instance contains a pointer to the allocated region (in the
/// processor's virtual address space) and the device address which can be given to the device
/// as the DMA address base of the region. The region is released once [`Coherent`]
/// is dropped.
///
/// # Invariants
///
/// - For the lifetime of an instance of [`Coherent`], the `cpu_addr` is a valid pointer
///   to an allocated region of coherent memory and `dma_handle` is the DMA address base of the
///   region.
/// - The size in bytes of the allocation is equal to size information via pointer.
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
// Hence, find a way to revoke the device resources of a `Coherent`, but not the
// entire `Coherent` including the allocated memory itself.
pub struct Coherent<T: KnownSize + ?Sized> {
    dev: ARef<device::Device>,
    dma_handle: DmaAddress,
    cpu_addr: NonNull<T>,
    dma_attrs: Attrs,
}

impl<T: KnownSize + ?Sized> Coherent<T> {
    /// Returns the size in bytes of this allocation.
    #[inline]
    pub fn size(&self) -> usize {
        T::size(self.cpu_addr.as_ptr())
    }

    /// Returns the raw pointer to the allocated region in the CPU's virtual address space.
    #[inline]
    pub fn as_ptr(&self) -> *const T {
        self.cpu_addr.as_ptr()
    }

    /// Returns the raw pointer to the allocated region in the CPU's virtual address space as
    /// a mutable pointer.
    #[inline]
    pub fn as_mut_ptr(&self) -> *mut T {
        self.cpu_addr.as_ptr()
    }

    /// Returns a DMA handle which may be given to the device as the DMA address base of
    /// the region.
    #[inline]
    pub fn dma_handle(&self) -> DmaAddress {
        self.dma_handle
    }

    /// Returns a reference to the data in the region.
    ///
    /// # Safety
    ///
    /// * Callers must ensure that the device does not read/write to/from memory while the returned
    ///   slice is live.
    /// * Callers must ensure that this call does not race with a write to the same region while
    ///   the returned slice is live.
    #[inline]
    pub unsafe fn as_ref(&self) -> &T {
        // SAFETY: per safety requirement.
        unsafe { &*self.as_ptr() }
    }

    /// Returns a mutable reference to the data in the region.
    ///
    /// # Safety
    ///
    /// * Callers must ensure that the device does not read/write to/from memory while the returned
    ///   slice is live.
    /// * Callers must ensure that this call does not race with a read or write to the same region
    ///   while the returned slice is live.
    #[expect(clippy::mut_from_ref, reason = "unsafe to use API")]
    #[inline]
    pub unsafe fn as_mut(&self) -> &mut T {
        // SAFETY: per safety requirement.
        unsafe { &mut *self.as_mut_ptr() }
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

impl<T: AsBytes + FromBytes> Coherent<T> {
    /// Allocates a region of `T` of coherent memory.
    fn alloc_with_attrs(
        dev: &device::Device<Bound>,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Self> {
        const {
            assert!(
                core::mem::size_of::<T>() > 0,
                "It doesn't make sense for the allocated type to be a ZST"
            );
        }

        let mut dma_handle = 0;
        // SAFETY: Device pointer is guaranteed as valid by the type invariant on `Device`.
        let addr = unsafe {
            bindings::dma_alloc_attrs(
                dev.as_raw(),
                core::mem::size_of::<T>(),
                &mut dma_handle,
                gfp_flags.as_raw(),
                dma_attrs.as_raw(),
            )
        };
        let cpu_addr = NonNull::new(addr.cast()).ok_or(ENOMEM)?;
        // INVARIANT:
        // - We just successfully allocated a coherent region which is adequately sized for `T`,
        //   hence the cpu address is valid.
        // - We also hold a refcounted reference to the device.
        Ok(Self {
            dev: dev.into(),
            dma_handle,
            cpu_addr,
            dma_attrs,
        })
    }

    /// Allocates a region of type `T` of coherent memory.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::device::{
    /// #     Bound,
    /// #     Device,
    /// # };
    /// use kernel::dma::{
    ///     attrs::*,
    ///     Coherent,
    /// };
    ///
    /// # fn test(dev: &Device<Bound>) -> Result {
    /// let c: Coherent<[u64; 4]> =
    ///     Coherent::zeroed_with_attrs(dev, GFP_KERNEL, DMA_ATTR_NO_WARN)?;
    /// # Ok::<(), Error>(()) }
    /// ```
    #[inline]
    pub fn zeroed_with_attrs(
        dev: &device::Device<Bound>,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Self> {
        Self::alloc_with_attrs(dev, gfp_flags | __GFP_ZERO, dma_attrs)
    }

    /// Performs the same functionality as [`Coherent::zeroed_with_attrs`], except the
    /// `dma_attrs` is 0 by default.
    #[inline]
    pub fn zeroed(dev: &device::Device<Bound>, gfp_flags: kernel::alloc::Flags) -> Result<Self> {
        Self::zeroed_with_attrs(dev, gfp_flags, Attrs(0))
    }

    /// Same as [`Coherent::zeroed_with_attrs`], but instead of a zero-initialization the memory is
    /// initialized with `init`.
    pub fn init_with_attrs<E>(
        dev: &device::Device<Bound>,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
        init: impl Init<T, E>,
    ) -> Result<Self>
    where
        Error: From<E>,
    {
        let dmem = Self::alloc_with_attrs(dev, gfp_flags, dma_attrs)?;
        let ptr = dmem.as_mut_ptr();

        // SAFETY:
        // - `ptr` is valid, properly aligned, and points to exclusively owned memory.
        // - If `__init` fails, `self` is dropped, which safely frees the underlying `Coherent`'s
        //   DMA memory. `T: AsBytes + FromBytes` ensures there are no complex `Drop` requirements
        //   we are bypassing.
        unsafe { init.__init(ptr)? };

        Ok(dmem)
    }

    /// Same as [`Coherent::zeroed`], but instead of a zero-initialization the memory is initialized
    /// with `init`.
    #[inline]
    pub fn init<E>(
        dev: &device::Device<Bound>,
        gfp_flags: kernel::alloc::Flags,
        init: impl Init<T, E>,
    ) -> Result<Self>
    where
        Error: From<E>,
    {
        Self::init_with_attrs(dev, gfp_flags, Attrs(0), init)
    }

    /// Allocates a region of `[T; len]` of coherent memory.
    fn alloc_slice_with_attrs(
        dev: &device::Device<Bound>,
        len: usize,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Coherent<[T]>> {
        const {
            assert!(
                core::mem::size_of::<T>() > 0,
                "It doesn't make sense for the allocated type to be a ZST"
            );
        }

        // `dma_alloc_attrs` cannot handle zero-length allocation, bail early.
        if len == 0 {
            Err(EINVAL)?;
        }

        let size = core::mem::size_of::<T>().checked_mul(len).ok_or(ENOMEM)?;
        let mut dma_handle = 0;
        // SAFETY: Device pointer is guaranteed as valid by the type invariant on `Device`.
        let addr = unsafe {
            bindings::dma_alloc_attrs(
                dev.as_raw(),
                size,
                &mut dma_handle,
                gfp_flags.as_raw(),
                dma_attrs.as_raw(),
            )
        };
        let cpu_addr = NonNull::slice_from_raw_parts(NonNull::new(addr.cast()).ok_or(ENOMEM)?, len);
        // INVARIANT:
        // - We just successfully allocated a coherent region which is adequately sized for
        //   `[T; len]`, hence the cpu address is valid.
        // - We also hold a refcounted reference to the device.
        Ok(Coherent {
            dev: dev.into(),
            dma_handle,
            cpu_addr,
            dma_attrs,
        })
    }

    /// Allocates a zeroed region of type `T` of coherent memory.
    ///
    /// Unlike `Coherent::<[T; N]>::zeroed_with_attrs`, `Coherent::<T>::zeroed_slices` support
    /// a runtime length.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::device::{
    /// #     Bound,
    /// #     Device,
    /// # };
    /// use kernel::dma::{
    ///     attrs::*,
    ///     Coherent,
    /// };
    ///
    /// # fn test(dev: &Device<Bound>) -> Result {
    /// let c: Coherent<[u64]> =
    ///     Coherent::zeroed_slice_with_attrs(dev, 4, GFP_KERNEL, DMA_ATTR_NO_WARN)?;
    /// # Ok::<(), Error>(()) }
    /// ```
    #[inline]
    pub fn zeroed_slice_with_attrs(
        dev: &device::Device<Bound>,
        len: usize,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Coherent<[T]>> {
        Coherent::alloc_slice_with_attrs(dev, len, gfp_flags | __GFP_ZERO, dma_attrs)
    }

    /// Performs the same functionality as [`Coherent::zeroed_slice_with_attrs`], except the
    /// `dma_attrs` is 0 by default.
    #[inline]
    pub fn zeroed_slice(
        dev: &device::Device<Bound>,
        len: usize,
        gfp_flags: kernel::alloc::Flags,
    ) -> Result<Coherent<[T]>> {
        Self::zeroed_slice_with_attrs(dev, len, gfp_flags, Attrs(0))
    }

    /// Allocates a region of coherent memory of the same size as `data` and initializes it with a
    /// copy of its contents.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::device::{Bound, Device};
    /// use kernel::dma::{
    ///     attrs::*,
    ///     Coherent
    /// };
    ///
    /// # fn test(dev: &Device<Bound>) -> Result {
    /// let data = [0u8, 1u8, 2u8, 3u8];
    /// // `c` has the same content as `data`.
    /// let c: Coherent<[u8]> =
    ///     Coherent::from_slice_with_attrs(dev, &data, GFP_KERNEL, DMA_ATTR_NO_WARN)?;
    ///
    /// # Ok::<(), Error>(()) }
    /// ```
    #[inline]
    pub fn from_slice_with_attrs(
        dev: &device::Device<Bound>,
        data: &[T],
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Coherent<[T]>>
    where
        T: Copy,
    {
        CoherentBox::from_slice_with_attrs(dev, data, gfp_flags, dma_attrs).map(Into::into)
    }

    /// Performs the same functionality as [`Coherent::from_slice_with_attrs`], except the
    /// `dma_attrs` is 0 by default.
    #[inline]
    pub fn from_slice(
        dev: &device::Device<Bound>,
        data: &[T],
        gfp_flags: kernel::alloc::Flags,
    ) -> Result<Coherent<[T]>>
    where
        T: Copy,
    {
        Self::from_slice_with_attrs(dev, data, gfp_flags, Attrs(0))
    }
}

impl<T> Coherent<[T]> {
    /// Returns the number of elements `T` in this allocation.
    ///
    /// Note that this is not the size of the allocation in bytes, which is provided by
    /// [`Self::size`].
    #[inline]
    #[expect(clippy::len_without_is_empty, reason = "Coherent slice is never empty")]
    pub fn len(&self) -> usize {
        self.cpu_addr.len()
    }
}

/// Note that the device configured to do DMA must be halted before this object is dropped.
impl<T: KnownSize + ?Sized> Drop for Coherent<T> {
    fn drop(&mut self) {
        let size = T::size(self.cpu_addr.as_ptr());
        // SAFETY: Device pointer is guaranteed as valid by the type invariant on `Device`.
        // The cpu address, and the dma handle are valid due to the type invariants on
        // `Coherent`.
        unsafe {
            bindings::dma_free_attrs(
                self.dev.as_raw(),
                size,
                self.cpu_addr.as_ptr().cast(),
                self.dma_handle,
                self.dma_attrs.as_raw(),
            )
        }
    }
}

// SAFETY: It is safe to send a `Coherent` to another thread if `T`
// can be sent to another thread.
unsafe impl<T: KnownSize + Send + ?Sized> Send for Coherent<T> {}

// SAFETY: Sharing `&Coherent` across threads is safe if `T` is `Sync`, because all
// methods that access the buffer contents (`field_read`, `field_write`, `as_slice`,
// `as_slice_mut`) are `unsafe`, and callers are responsible for ensuring no data races occur.
// The safe methods only return metadata or raw pointers whose use requires `unsafe`.
unsafe impl<T: KnownSize + ?Sized + AsBytes + FromBytes + Sync> Sync for Coherent<T> {}

impl<T: KnownSize + AsBytes + ?Sized> debugfs::BinaryWriter for Coherent<T> {
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        if offset.is_negative() {
            return Err(EINVAL);
        }

        // If the offset is too large for a usize (e.g. on 32-bit platforms),
        // then consider that as past EOF and just return 0 bytes.
        let Ok(offset_val) = usize::try_from(*offset) else {
            return Ok(0);
        };

        let count = self.size().saturating_sub(offset_val).min(writer.len());

        writer.write_dma(self, offset_val, count)?;

        *offset += count as i64;
        Ok(count)
    }
}

/// An opaque DMA allocation without a kernel virtual mapping.
///
/// Unlike [`Coherent`], a `CoherentHandle` does not provide CPU access to the allocated memory.
/// The allocation is always performed with `DMA_ATTR_NO_KERNEL_MAPPING`, meaning no kernel
/// virtual mapping is created for the buffer. The value returned by the C API as the CPU
/// address is an opaque handle used only to free the allocation.
///
/// This is useful for buffers that are only ever accessed by hardware.
///
/// # Invariants
///
/// - `cpu_handle` holds the opaque handle returned by `dma_alloc_attrs` with
///   `DMA_ATTR_NO_KERNEL_MAPPING` set, and is only valid for passing back to `dma_free_attrs`.
/// - `dma_handle` is the corresponding bus address for device DMA.
/// - `size` is the allocation size in bytes as passed to `dma_alloc_attrs`.
/// - `dma_attrs` contains the attributes used for the allocation, always including
///   `DMA_ATTR_NO_KERNEL_MAPPING`.
pub struct CoherentHandle {
    dev: ARef<device::Device>,
    dma_handle: DmaAddress,
    cpu_handle: NonNull<c_void>,
    size: usize,
    dma_attrs: Attrs,
}

impl CoherentHandle {
    /// Allocates `size` bytes of coherent DMA memory without creating a kernel virtual mapping.
    ///
    /// Additional DMA attributes may be passed via `dma_attrs`; `DMA_ATTR_NO_KERNEL_MAPPING` is
    /// always set implicitly.
    ///
    /// Returns `EINVAL` if `size` is zero, `ENOMEM` if the allocation fails.
    pub fn alloc_with_attrs(
        dev: &device::Device<Bound>,
        size: usize,
        gfp_flags: kernel::alloc::Flags,
        dma_attrs: Attrs,
    ) -> Result<Self> {
        if size == 0 {
            return Err(EINVAL);
        }

        let dma_attrs = dma_attrs | Attrs(bindings::DMA_ATTR_NO_KERNEL_MAPPING);
        let mut dma_handle = 0;
        // SAFETY: `dev.as_raw()` is valid by the type invariant on `device::Device`.
        let cpu_handle = unsafe {
            bindings::dma_alloc_attrs(
                dev.as_raw(),
                size,
                &mut dma_handle,
                gfp_flags.as_raw(),
                dma_attrs.as_raw(),
            )
        };

        let cpu_handle = NonNull::new(cpu_handle).ok_or(ENOMEM)?;

        // INVARIANT: `cpu_handle` is the opaque handle from a successful `dma_alloc_attrs` call
        // with `DMA_ATTR_NO_KERNEL_MAPPING`, `dma_handle` is the corresponding DMA address,
        // and we hold a refcounted reference to the device.
        Ok(Self {
            dev: dev.into(),
            dma_handle,
            cpu_handle,
            size,
            dma_attrs,
        })
    }

    /// Allocates `size` bytes of coherent DMA memory without creating a kernel virtual mapping.
    #[inline]
    pub fn alloc(
        dev: &device::Device<Bound>,
        size: usize,
        gfp_flags: kernel::alloc::Flags,
    ) -> Result<Self> {
        Self::alloc_with_attrs(dev, size, gfp_flags, Attrs(0))
    }

    /// Returns the DMA handle for this allocation.
    ///
    /// This address can be programmed into device hardware for DMA access.
    #[inline]
    pub fn dma_handle(&self) -> DmaAddress {
        self.dma_handle
    }

    /// Returns the size in bytes of this allocation.
    #[inline]
    pub fn size(&self) -> usize {
        self.size
    }
}

impl Drop for CoherentHandle {
    fn drop(&mut self) {
        // SAFETY: All values are valid by the type invariants on `CoherentHandle`.
        // `cpu_handle` is the opaque handle from `dma_alloc_attrs` and is passed back unchanged.
        unsafe {
            bindings::dma_free_attrs(
                self.dev.as_raw(),
                self.size,
                self.cpu_handle.as_ptr(),
                self.dma_handle,
                self.dma_attrs.as_raw(),
            )
        }
    }
}

// SAFETY: `CoherentHandle` only holds a device reference, a DMA handle, an opaque CPU handle,
// and a size. None of these are tied to a specific thread.
unsafe impl Send for CoherentHandle {}

// SAFETY: `CoherentHandle` provides no CPU access to the underlying allocation. The only
// operations on `&CoherentHandle` are reading the DMA handle and size, both of which are
// plain `Copy` values.
unsafe impl Sync for CoherentHandle {}

/// Reads a field of an item from an allocated region of structs.
///
/// The syntax is of the form `kernel::dma_read!(dma, proj)` where `dma` is an expression evaluating
/// to a [`Coherent`] and `proj` is a [projection specification](kernel::ptr::project!).
///
/// # Examples
///
/// ```
/// use kernel::device::Device;
/// use kernel::dma::{attrs::*, Coherent};
///
/// struct MyStruct { field: u32, }
///
/// // SAFETY: All bit patterns are acceptable values for `MyStruct`.
/// unsafe impl kernel::transmute::FromBytes for MyStruct{};
/// // SAFETY: Instances of `MyStruct` have no uninitialized portions.
/// unsafe impl kernel::transmute::AsBytes for MyStruct{};
///
/// # fn test(alloc: &kernel::dma::Coherent<[MyStruct]>) -> Result {
/// let whole = kernel::dma_read!(alloc, [2]?);
/// let field = kernel::dma_read!(alloc, [1]?.field);
/// # Ok::<(), Error>(()) }
/// ```
#[macro_export]
macro_rules! dma_read {
    ($dma:expr, $($proj:tt)*) => {{
        let dma = &$dma;
        let ptr = $crate::ptr::project!(
            $crate::dma::Coherent::as_ptr(dma), $($proj)*
        );
        // SAFETY: The pointer created by the projection is within the DMA region.
        unsafe { $crate::dma::Coherent::field_read(dma, ptr) }
    }};
}

/// Writes to a field of an item from an allocated region of structs.
///
/// The syntax is of the form `kernel::dma_write!(dma, proj, val)` where `dma` is an expression
/// evaluating to a [`Coherent`], `proj` is a
/// [projection specification](kernel::ptr::project!), and `val` is the value to be written to the
/// projected location.
///
/// # Examples
///
/// ```
/// use kernel::device::Device;
/// use kernel::dma::{attrs::*, Coherent};
///
/// struct MyStruct { member: u32, }
///
/// // SAFETY: All bit patterns are acceptable values for `MyStruct`.
/// unsafe impl kernel::transmute::FromBytes for MyStruct{};
/// // SAFETY: Instances of `MyStruct` have no uninitialized portions.
/// unsafe impl kernel::transmute::AsBytes for MyStruct{};
///
/// # fn test(alloc: &kernel::dma::Coherent<[MyStruct]>) -> Result {
/// kernel::dma_write!(alloc, [2]?.member, 0xf);
/// kernel::dma_write!(alloc, [1]?, MyStruct { member: 0xf });
/// # Ok::<(), Error>(()) }
/// ```
#[macro_export]
macro_rules! dma_write {
    (@parse [$dma:expr] [$($proj:tt)*] [, $val:expr]) => {{
        let dma = &$dma;
        let ptr = $crate::ptr::project!(
            mut $crate::dma::Coherent::as_mut_ptr(dma), $($proj)*
        );
        let val = $val;
        // SAFETY: The pointer created by the projection is within the DMA region.
        unsafe { $crate::dma::Coherent::field_write(dma, ptr, val) }
    }};
    (@parse [$dma:expr] [$($proj:tt)*] [.$field:tt $($rest:tt)*]) => {
        $crate::dma_write!(@parse [$dma] [$($proj)* .$field] [$($rest)*])
    };
    (@parse [$dma:expr] [$($proj:tt)*] [[$index:expr]? $($rest:tt)*]) => {
        $crate::dma_write!(@parse [$dma] [$($proj)* [$index]?] [$($rest)*])
    };
    (@parse [$dma:expr] [$($proj:tt)*] [[$index:expr] $($rest:tt)*]) => {
        $crate::dma_write!(@parse [$dma] [$($proj)* [$index]] [$($rest)*])
    };
    ($dma:expr, $($rest:tt)*) => {
        $crate::dma_write!(@parse [$dma] [] [$($rest)*])
    };
}
