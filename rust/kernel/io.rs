// SPDX-License-Identifier: GPL-2.0

//! Memory-mapped IO.
//!
//! C header: [`include/asm-generic/io.h`](srctree/include/asm-generic/io.h)

use crate::{
    bindings,
    prelude::*, //
};

pub mod mem;
pub mod poll;
pub mod resource;

pub use resource::Resource;

/// Physical address type.
///
/// This is a type alias to either `u32` or `u64` depending on the config option
/// `CONFIG_PHYS_ADDR_T_64BIT`, and it can be a u64 even on 32-bit architectures.
pub type PhysAddr = bindings::phys_addr_t;

/// Resource Size type.
///
/// This is a type alias to either `u32` or `u64` depending on the config option
/// `CONFIG_PHYS_ADDR_T_64BIT`, and it can be a u64 even on 32-bit architectures.
pub type ResourceSize = bindings::resource_size_t;

/// Raw representation of an MMIO region.
///
/// By itself, the existence of an instance of this structure does not provide any guarantees that
/// the represented MMIO region does exist or is properly mapped.
///
/// Instead, the bus specific MMIO implementation must convert this raw representation into an
/// `Mmio` instance providing the actual memory accessors. Only by the conversion into an `Mmio`
/// structure any guarantees are given.
pub struct MmioRaw<const SIZE: usize = 0> {
    addr: usize,
    maxsize: usize,
}

impl<const SIZE: usize> MmioRaw<SIZE> {
    /// Returns a new `MmioRaw` instance on success, an error otherwise.
    pub fn new(addr: usize, maxsize: usize) -> Result<Self> {
        if maxsize < SIZE {
            return Err(EINVAL);
        }

        Ok(Self { addr, maxsize })
    }

    /// Returns the base address of the MMIO region.
    #[inline]
    pub fn addr(&self) -> usize {
        self.addr
    }

    /// Returns the maximum size of the MMIO region.
    #[inline]
    pub fn maxsize(&self) -> usize {
        self.maxsize
    }
}

/// IO-mapped memory region.
///
/// The creator (usually a subsystem / bus such as PCI) is responsible for creating the
/// mapping, performing an additional region request etc.
///
/// # Invariant
///
/// `addr` is the start and `maxsize` the length of valid I/O mapped memory region of size
/// `maxsize`.
///
/// # Examples
///
/// ```no_run
/// use kernel::{
///     bindings,
///     ffi::c_void,
///     io::{
///         Io,
///         IoKnownSize,
///         Mmio,
///         MmioRaw,
///         PhysAddr,
///     },
/// };
/// use core::ops::Deref;
///
/// // See also `pci::Bar` for a real example.
/// struct IoMem<const SIZE: usize>(MmioRaw<SIZE>);
///
/// impl<const SIZE: usize> IoMem<SIZE> {
///     /// # Safety
///     ///
///     /// [`paddr`, `paddr` + `SIZE`) must be a valid MMIO region that is mappable into the CPUs
///     /// virtual address space.
///     unsafe fn new(paddr: usize) -> Result<Self>{
///         // SAFETY: By the safety requirements of this function [`paddr`, `paddr` + `SIZE`) is
///         // valid for `ioremap`.
///         let addr = unsafe { bindings::ioremap(paddr as PhysAddr, SIZE) };
///         if addr.is_null() {
///             return Err(ENOMEM);
///         }
///
///         Ok(IoMem(MmioRaw::new(addr as usize, SIZE)?))
///     }
/// }
///
/// impl<const SIZE: usize> Drop for IoMem<SIZE> {
///     fn drop(&mut self) {
///         // SAFETY: `self.0.addr()` is guaranteed to be properly mapped by `Self::new`.
///         unsafe { bindings::iounmap(self.0.addr() as *mut c_void); };
///     }
/// }
///
/// impl<const SIZE: usize> Deref for IoMem<SIZE> {
///    type Target = Mmio<SIZE>;
///
///    fn deref(&self) -> &Self::Target {
///         // SAFETY: The memory range stored in `self` has been properly mapped in `Self::new`.
///         unsafe { Mmio::from_raw(&self.0) }
///    }
/// }
///
///# fn no_run() -> Result<(), Error> {
/// // SAFETY: Invalid usage for example purposes.
/// let iomem = unsafe { IoMem::<{ core::mem::size_of::<u32>() }>::new(0xBAAAAAAD)? };
/// iomem.write32(0x42, 0x0);
/// assert!(iomem.try_write32(0x42, 0x0).is_ok());
/// assert!(iomem.try_write32(0x42, 0x4).is_err());
/// # Ok(())
/// # }
/// ```
#[repr(transparent)]
pub struct Mmio<const SIZE: usize = 0>(MmioRaw<SIZE>);

/// Checks whether an access of type `U` at the given `offset`
/// is valid within this region.
#[inline]
const fn offset_valid<U>(offset: usize, size: usize) -> bool {
    let type_size = core::mem::size_of::<U>();
    if let Some(end) = offset.checked_add(type_size) {
        end <= size && offset % type_size == 0
    } else {
        false
    }
}

/// Trait indicating that an I/O backend supports operations of a certain type and providing an
/// implementation for these operations.
///
/// Different I/O backends can implement this trait to expose only the operations they support.
///
/// For example, a PCI configuration space may implement `IoCapable<u8>`, `IoCapable<u16>`,
/// and `IoCapable<u32>`, but not `IoCapable<u64>`, while an MMIO region on a 64-bit
/// system might implement all four.
pub trait IoCapable<T> {
    /// Performs an I/O read of type `T` at `address` and returns the result.
    ///
    /// # Safety
    ///
    /// The range `[address..address + size_of::<T>()]` must be within the bounds of `Self`.
    unsafe fn io_read(&self, address: usize) -> T;

    /// Performs an I/O write of `value` at `address`.
    ///
    /// # Safety
    ///
    /// The range `[address..address + size_of::<T>()]` must be within the bounds of `Self`.
    unsafe fn io_write(&self, value: T, address: usize);
}

/// Types implementing this trait (e.g. MMIO BARs or PCI config regions)
/// can perform I/O operations on regions of memory.
///
/// This is an abstract representation to be implemented by arbitrary I/O
/// backends (e.g. MMIO, PCI config space, etc.).
///
/// The [`Io`] trait provides:
/// - Base address and size information
/// - Helper methods for offset validation and address calculation
/// - Fallible (runtime checked) accessors for different data widths
///
/// Which I/O methods are available depends on which [`IoCapable<T>`] traits
/// are implemented for the type.
///
/// # Examples
///
/// For MMIO regions, all widths (u8, u16, u32, and u64 on 64-bit systems) are typically
/// supported. For PCI configuration space, u8, u16, and u32 are supported but u64 is not.
pub trait Io {
    /// Returns the base address of this mapping.
    fn addr(&self) -> usize;

    /// Returns the maximum size of this mapping.
    fn maxsize(&self) -> usize;

    /// Returns the absolute I/O address for a given `offset`,
    /// performing runtime bound checks.
    #[inline]
    fn io_addr<U>(&self, offset: usize) -> Result<usize> {
        if !offset_valid::<U>(offset, self.maxsize()) {
            return Err(EINVAL);
        }

        // Probably no need to check, since the safety requirements of `Self::new` guarantee that
        // this can't overflow.
        self.addr().checked_add(offset).ok_or(EINVAL)
    }

    /// Fallible 8-bit read with runtime bounds check.
    #[inline(always)]
    fn try_read8(&self, offset: usize) -> Result<u8>
    where
        Self: IoCapable<u8>,
    {
        let address = self.io_addr::<u8>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        Ok(unsafe { self.io_read(address) })
    }

    /// Fallible 16-bit read with runtime bounds check.
    #[inline(always)]
    fn try_read16(&self, offset: usize) -> Result<u16>
    where
        Self: IoCapable<u16>,
    {
        let address = self.io_addr::<u16>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        Ok(unsafe { self.io_read(address) })
    }

    /// Fallible 32-bit read with runtime bounds check.
    #[inline(always)]
    fn try_read32(&self, offset: usize) -> Result<u32>
    where
        Self: IoCapable<u32>,
    {
        let address = self.io_addr::<u32>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        Ok(unsafe { self.io_read(address) })
    }

    /// Fallible 64-bit read with runtime bounds check.
    #[inline(always)]
    fn try_read64(&self, offset: usize) -> Result<u64>
    where
        Self: IoCapable<u64>,
    {
        let address = self.io_addr::<u64>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        Ok(unsafe { self.io_read(address) })
    }

    /// Fallible 8-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write8(&self, value: u8, offset: usize) -> Result
    where
        Self: IoCapable<u8>,
    {
        let address = self.io_addr::<u8>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        unsafe { self.io_write(value, address) };
        Ok(())
    }

    /// Fallible 16-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write16(&self, value: u16, offset: usize) -> Result
    where
        Self: IoCapable<u16>,
    {
        let address = self.io_addr::<u16>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        unsafe { self.io_write(value, address) };
        Ok(())
    }

    /// Fallible 32-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write32(&self, value: u32, offset: usize) -> Result
    where
        Self: IoCapable<u32>,
    {
        let address = self.io_addr::<u32>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        unsafe { self.io_write(value, address) };
        Ok(())
    }

    /// Fallible 64-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write64(&self, value: u64, offset: usize) -> Result
    where
        Self: IoCapable<u64>,
    {
        let address = self.io_addr::<u64>(offset)?;

        // SAFETY: `address` has been validated by `io_addr`.
        unsafe { self.io_write(value, address) };
        Ok(())
    }

    /// Infallible 8-bit read with compile-time bounds check.
    #[inline(always)]
    fn read8(&self, offset: usize) -> u8
    where
        Self: IoKnownSize + IoCapable<u8>,
    {
        let address = self.io_addr_assert::<u8>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_read(address) }
    }

    /// Infallible 16-bit read with compile-time bounds check.
    #[inline(always)]
    fn read16(&self, offset: usize) -> u16
    where
        Self: IoKnownSize + IoCapable<u16>,
    {
        let address = self.io_addr_assert::<u16>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_read(address) }
    }

    /// Infallible 32-bit read with compile-time bounds check.
    #[inline(always)]
    fn read32(&self, offset: usize) -> u32
    where
        Self: IoKnownSize + IoCapable<u32>,
    {
        let address = self.io_addr_assert::<u32>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_read(address) }
    }

    /// Infallible 64-bit read with compile-time bounds check.
    #[inline(always)]
    fn read64(&self, offset: usize) -> u64
    where
        Self: IoKnownSize + IoCapable<u64>,
    {
        let address = self.io_addr_assert::<u64>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_read(address) }
    }

    /// Infallible 8-bit write with compile-time bounds check.
    #[inline(always)]
    fn write8(&self, value: u8, offset: usize)
    where
        Self: IoKnownSize + IoCapable<u8>,
    {
        let address = self.io_addr_assert::<u8>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_write(value, address) }
    }

    /// Infallible 16-bit write with compile-time bounds check.
    #[inline(always)]
    fn write16(&self, value: u16, offset: usize)
    where
        Self: IoKnownSize + IoCapable<u16>,
    {
        let address = self.io_addr_assert::<u16>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_write(value, address) }
    }

    /// Infallible 32-bit write with compile-time bounds check.
    #[inline(always)]
    fn write32(&self, value: u32, offset: usize)
    where
        Self: IoKnownSize + IoCapable<u32>,
    {
        let address = self.io_addr_assert::<u32>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_write(value, address) }
    }

    /// Infallible 64-bit write with compile-time bounds check.
    #[inline(always)]
    fn write64(&self, value: u64, offset: usize)
    where
        Self: IoKnownSize + IoCapable<u64>,
    {
        let address = self.io_addr_assert::<u64>(offset);

        // SAFETY: `address` has been validated by `io_addr_assert`.
        unsafe { self.io_write(value, address) }
    }
}

/// Trait for types with a known size at compile time.
///
/// This trait is implemented by I/O backends that have a compile-time known size,
/// enabling the use of infallible I/O accessors with compile-time bounds checking.
///
/// Types implementing this trait can use the infallible methods in [`Io`] trait
/// (e.g., `read8`, `write32`), which require `Self: IoKnownSize` bound.
pub trait IoKnownSize: Io {
    /// Minimum usable size of this region.
    const MIN_SIZE: usize;

    /// Returns the absolute I/O address for a given `offset`,
    /// performing compile-time bound checks.
    // Always inline to optimize out error path of `build_assert`.
    #[inline(always)]
    fn io_addr_assert<U>(&self, offset: usize) -> usize {
        build_assert!(offset_valid::<U>(offset, Self::MIN_SIZE));

        self.addr() + offset
    }
}

/// Implements [`IoCapable`] on `$mmio` for `$ty` using `$read_fn` and `$write_fn`.
macro_rules! impl_mmio_io_capable {
    ($mmio:ident, $(#[$attr:meta])* $ty:ty, $read_fn:ident, $write_fn:ident) => {
        $(#[$attr])*
        impl<const SIZE: usize> IoCapable<$ty> for $mmio<SIZE> {
            unsafe fn io_read(&self, address: usize) -> $ty {
                // SAFETY: By the trait invariant `address` is a valid address for MMIO operations.
                unsafe { bindings::$read_fn(address as *const c_void) }
            }

            unsafe fn io_write(&self, value: $ty, address: usize) {
                // SAFETY: By the trait invariant `address` is a valid address for MMIO operations.
                unsafe { bindings::$write_fn(value, address as *mut c_void) }
            }
        }
    };
}

// MMIO regions support 8, 16, and 32-bit accesses.
impl_mmio_io_capable!(Mmio, u8, readb, writeb);
impl_mmio_io_capable!(Mmio, u16, readw, writew);
impl_mmio_io_capable!(Mmio, u32, readl, writel);
// MMIO regions on 64-bit systems also support 64-bit accesses.
impl_mmio_io_capable!(
    Mmio,
    #[cfg(CONFIG_64BIT)]
    u64,
    readq,
    writeq
);

impl<const SIZE: usize> Io for Mmio<SIZE> {
    /// Returns the base address of this mapping.
    #[inline]
    fn addr(&self) -> usize {
        self.0.addr()
    }

    /// Returns the maximum size of this mapping.
    #[inline]
    fn maxsize(&self) -> usize {
        self.0.maxsize()
    }
}

impl<const SIZE: usize> IoKnownSize for Mmio<SIZE> {
    const MIN_SIZE: usize = SIZE;
}

impl<const SIZE: usize> Mmio<SIZE> {
    /// Converts an `MmioRaw` into an `Mmio` instance, providing the accessors to the MMIO mapping.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `addr` is the start of a valid I/O mapped memory region of size
    /// `maxsize`.
    pub unsafe fn from_raw(raw: &MmioRaw<SIZE>) -> &Self {
        // SAFETY: `Mmio` is a transparent wrapper around `MmioRaw`.
        unsafe { &*core::ptr::from_ref(raw).cast() }
    }
}

/// [`Mmio`] wrapper using relaxed accessors.
///
/// This type provides an implementation of [`Io`] that uses relaxed I/O MMIO operands instead of
/// the regular ones.
///
/// See [`Mmio::relaxed`] for a usage example.
#[repr(transparent)]
pub struct RelaxedMmio<const SIZE: usize = 0>(Mmio<SIZE>);

impl<const SIZE: usize> Io for RelaxedMmio<SIZE> {
    #[inline]
    fn addr(&self) -> usize {
        self.0.addr()
    }

    #[inline]
    fn maxsize(&self) -> usize {
        self.0.maxsize()
    }
}

impl<const SIZE: usize> IoKnownSize for RelaxedMmio<SIZE> {
    const MIN_SIZE: usize = SIZE;
}

impl<const SIZE: usize> Mmio<SIZE> {
    /// Returns a [`RelaxedMmio`] reference that performs relaxed I/O operations.
    ///
    /// Relaxed accessors do not provide ordering guarantees with respect to DMA or memory accesses
    /// and can be used when such ordering is not required.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// use kernel::io::{
    ///     Io,
    ///     Mmio,
    ///     RelaxedMmio,
    /// };
    ///
    /// fn do_io(io: &Mmio<0x100>) {
    ///     // The access is performed using `readl_relaxed` instead of `readl`.
    ///     let v = io.relaxed().read32(0x10);
    /// }
    ///
    /// ```
    pub fn relaxed(&self) -> &RelaxedMmio<SIZE> {
        // SAFETY: `RelaxedMmio` is `#[repr(transparent)]` over `Mmio`, so `Mmio<SIZE>` and
        // `RelaxedMmio<SIZE>` have identical layout.
        unsafe { core::mem::transmute(self) }
    }
}

// MMIO regions support 8, 16, and 32-bit accesses.
impl_mmio_io_capable!(RelaxedMmio, u8, readb_relaxed, writeb_relaxed);
impl_mmio_io_capable!(RelaxedMmio, u16, readw_relaxed, writew_relaxed);
impl_mmio_io_capable!(RelaxedMmio, u32, readl_relaxed, writel_relaxed);
// MMIO regions on 64-bit systems also support 64-bit accesses.
impl_mmio_io_capable!(
    RelaxedMmio,
    #[cfg(CONFIG_64BIT)]
    u64,
    readq_relaxed,
    writeq_relaxed
);
