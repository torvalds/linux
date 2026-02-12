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

/// Internal helper macros used to invoke C MMIO read functions.
///
/// This macro is intended to be used by higher-level MMIO access macros (define_read) and provides
/// a unified expansion for infallible vs. fallible read semantics. It emits a direct call into the
/// corresponding C helper and performs the required cast to the Rust return type.
///
/// # Parameters
///
/// * `$c_fn` – The C function performing the MMIO read.
/// * `$self` – The I/O backend object.
/// * `$ty` – The type of the value to be read.
/// * `$addr` – The MMIO address to read.
///
/// This macro does not perform any validation; all invariants must be upheld by the higher-level
/// abstraction invoking it.
macro_rules! call_mmio_read {
    (infallible, $c_fn:ident, $self:ident, $type:ty, $addr:expr) => {
        // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
        unsafe { bindings::$c_fn($addr as *const c_void) as $type }
    };

    (fallible, $c_fn:ident, $self:ident, $type:ty, $addr:expr) => {{
        // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
        Ok(unsafe { bindings::$c_fn($addr as *const c_void) as $type })
    }};
}

/// Internal helper macros used to invoke C MMIO write functions.
///
/// This macro is intended to be used by higher-level MMIO access macros (define_write) and provides
/// a unified expansion for infallible vs. fallible write semantics. It emits a direct call into the
/// corresponding C helper and performs the required cast to the Rust return type.
///
/// # Parameters
///
/// * `$c_fn` – The C function performing the MMIO write.
/// * `$self` – The I/O backend object.
/// * `$ty` – The type of the written value.
/// * `$addr` – The MMIO address to write.
/// * `$value` – The value to write.
///
/// This macro does not perform any validation; all invariants must be upheld by the higher-level
/// abstraction invoking it.
macro_rules! call_mmio_write {
    (infallible, $c_fn:ident, $self:ident, $ty:ty, $addr:expr, $value:expr) => {
        // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
        unsafe { bindings::$c_fn($value, $addr as *mut c_void) }
    };

    (fallible, $c_fn:ident, $self:ident, $ty:ty, $addr:expr, $value:expr) => {{
        // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
        unsafe { bindings::$c_fn($value, $addr as *mut c_void) };
        Ok(())
    }};
}

macro_rules! define_read {
    (infallible, $(#[$attr:meta])* $vis:vis $name:ident, $call_macro:ident($c_fn:ident) ->
     $type_name:ty) => {
        /// Read IO data from a given offset known at compile time.
        ///
        /// Bound checks are performed on compile time, hence if the offset is not known at compile
        /// time, the build will fail.
        $(#[$attr])*
        // Always inline to optimize out error path of `io_addr_assert`.
        #[inline(always)]
        $vis fn $name(&self, offset: usize) -> $type_name {
            let addr = self.io_addr_assert::<$type_name>(offset);

            // SAFETY: By the type invariant `addr` is a valid address for IO operations.
            $call_macro!(infallible, $c_fn, self, $type_name, addr)
        }
    };

    (fallible, $(#[$attr:meta])* $vis:vis $try_name:ident, $call_macro:ident($c_fn:ident) ->
     $type_name:ty) => {
        /// Read IO data from a given offset.
        ///
        /// Bound checks are performed on runtime, it fails if the offset (plus the type size) is
        /// out of bounds.
        $(#[$attr])*
        $vis fn $try_name(&self, offset: usize) -> Result<$type_name> {
            let addr = self.io_addr::<$type_name>(offset)?;

            // SAFETY: By the type invariant `addr` is a valid address for IO operations.
            $call_macro!(fallible, $c_fn, self, $type_name, addr)
        }
    };
}
pub(crate) use define_read;

macro_rules! define_write {
    (infallible, $(#[$attr:meta])* $vis:vis $name:ident, $call_macro:ident($c_fn:ident) <-
     $type_name:ty) => {
        /// Write IO data from a given offset known at compile time.
        ///
        /// Bound checks are performed on compile time, hence if the offset is not known at compile
        /// time, the build will fail.
        $(#[$attr])*
        // Always inline to optimize out error path of `io_addr_assert`.
        #[inline(always)]
        $vis fn $name(&self, value: $type_name, offset: usize) {
            let addr = self.io_addr_assert::<$type_name>(offset);

            $call_macro!(infallible, $c_fn, self, $type_name, addr, value);
        }
    };

    (fallible, $(#[$attr:meta])* $vis:vis $try_name:ident, $call_macro:ident($c_fn:ident) <-
     $type_name:ty) => {
        /// Write IO data from a given offset.
        ///
        /// Bound checks are performed on runtime, it fails if the offset (plus the type size) is
        /// out of bounds.
        $(#[$attr])*
        $vis fn $try_name(&self, value: $type_name, offset: usize) -> Result {
            let addr = self.io_addr::<$type_name>(offset)?;

            $call_macro!(fallible, $c_fn, self, $type_name, addr, value)
        }
    };
}
pub(crate) use define_write;

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

/// Marker trait indicating that an I/O backend supports operations of a certain type.
///
/// Different I/O backends can implement this trait to expose only the operations they support.
///
/// For example, a PCI configuration space may implement `IoCapable<u8>`, `IoCapable<u16>`,
/// and `IoCapable<u32>`, but not `IoCapable<u64>`, while an MMIO region on a 64-bit
/// system might implement all four.
pub trait IoCapable<T> {}

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
    fn try_read8(&self, _offset: usize) -> Result<u8>
    where
        Self: IoCapable<u8>,
    {
        build_error!("Backend does not support fallible 8-bit read")
    }

    /// Fallible 16-bit read with runtime bounds check.
    #[inline(always)]
    fn try_read16(&self, _offset: usize) -> Result<u16>
    where
        Self: IoCapable<u16>,
    {
        build_error!("Backend does not support fallible 16-bit read")
    }

    /// Fallible 32-bit read with runtime bounds check.
    #[inline(always)]
    fn try_read32(&self, _offset: usize) -> Result<u32>
    where
        Self: IoCapable<u32>,
    {
        build_error!("Backend does not support fallible 32-bit read")
    }

    /// Fallible 64-bit read with runtime bounds check.
    #[inline(always)]
    fn try_read64(&self, _offset: usize) -> Result<u64>
    where
        Self: IoCapable<u64>,
    {
        build_error!("Backend does not support fallible 64-bit read")
    }

    /// Fallible 8-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write8(&self, _value: u8, _offset: usize) -> Result
    where
        Self: IoCapable<u8>,
    {
        build_error!("Backend does not support fallible 8-bit write")
    }

    /// Fallible 16-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write16(&self, _value: u16, _offset: usize) -> Result
    where
        Self: IoCapable<u16>,
    {
        build_error!("Backend does not support fallible 16-bit write")
    }

    /// Fallible 32-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write32(&self, _value: u32, _offset: usize) -> Result
    where
        Self: IoCapable<u32>,
    {
        build_error!("Backend does not support fallible 32-bit write")
    }

    /// Fallible 64-bit write with runtime bounds check.
    #[inline(always)]
    fn try_write64(&self, _value: u64, _offset: usize) -> Result
    where
        Self: IoCapable<u64>,
    {
        build_error!("Backend does not support fallible 64-bit write")
    }

    /// Infallible 8-bit read with compile-time bounds check.
    #[inline(always)]
    fn read8(&self, _offset: usize) -> u8
    where
        Self: IoKnownSize + IoCapable<u8>,
    {
        build_error!("Backend does not support infallible 8-bit read")
    }

    /// Infallible 16-bit read with compile-time bounds check.
    #[inline(always)]
    fn read16(&self, _offset: usize) -> u16
    where
        Self: IoKnownSize + IoCapable<u16>,
    {
        build_error!("Backend does not support infallible 16-bit read")
    }

    /// Infallible 32-bit read with compile-time bounds check.
    #[inline(always)]
    fn read32(&self, _offset: usize) -> u32
    where
        Self: IoKnownSize + IoCapable<u32>,
    {
        build_error!("Backend does not support infallible 32-bit read")
    }

    /// Infallible 64-bit read with compile-time bounds check.
    #[inline(always)]
    fn read64(&self, _offset: usize) -> u64
    where
        Self: IoKnownSize + IoCapable<u64>,
    {
        build_error!("Backend does not support infallible 64-bit read")
    }

    /// Infallible 8-bit write with compile-time bounds check.
    #[inline(always)]
    fn write8(&self, _value: u8, _offset: usize)
    where
        Self: IoKnownSize + IoCapable<u8>,
    {
        build_error!("Backend does not support infallible 8-bit write")
    }

    /// Infallible 16-bit write with compile-time bounds check.
    #[inline(always)]
    fn write16(&self, _value: u16, _offset: usize)
    where
        Self: IoKnownSize + IoCapable<u16>,
    {
        build_error!("Backend does not support infallible 16-bit write")
    }

    /// Infallible 32-bit write with compile-time bounds check.
    #[inline(always)]
    fn write32(&self, _value: u32, _offset: usize)
    where
        Self: IoKnownSize + IoCapable<u32>,
    {
        build_error!("Backend does not support infallible 32-bit write")
    }

    /// Infallible 64-bit write with compile-time bounds check.
    #[inline(always)]
    fn write64(&self, _value: u64, _offset: usize)
    where
        Self: IoKnownSize + IoCapable<u64>,
    {
        build_error!("Backend does not support infallible 64-bit write")
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

// MMIO regions support 8, 16, and 32-bit accesses.
impl<const SIZE: usize> IoCapable<u8> for Mmio<SIZE> {}
impl<const SIZE: usize> IoCapable<u16> for Mmio<SIZE> {}
impl<const SIZE: usize> IoCapable<u32> for Mmio<SIZE> {}

// MMIO regions on 64-bit systems also support 64-bit accesses.
#[cfg(CONFIG_64BIT)]
impl<const SIZE: usize> IoCapable<u64> for Mmio<SIZE> {}

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

    define_read!(fallible, try_read8, call_mmio_read(readb) -> u8);
    define_read!(fallible, try_read16, call_mmio_read(readw) -> u16);
    define_read!(fallible, try_read32, call_mmio_read(readl) -> u32);
    define_read!(
        fallible,
        #[cfg(CONFIG_64BIT)]
        try_read64,
        call_mmio_read(readq) -> u64
    );

    define_write!(fallible, try_write8, call_mmio_write(writeb) <- u8);
    define_write!(fallible, try_write16, call_mmio_write(writew) <- u16);
    define_write!(fallible, try_write32, call_mmio_write(writel) <- u32);
    define_write!(
        fallible,
        #[cfg(CONFIG_64BIT)]
        try_write64,
        call_mmio_write(writeq) <- u64
    );

    define_read!(infallible, read8, call_mmio_read(readb) -> u8);
    define_read!(infallible, read16, call_mmio_read(readw) -> u16);
    define_read!(infallible, read32, call_mmio_read(readl) -> u32);
    define_read!(
        infallible,
        #[cfg(CONFIG_64BIT)]
        read64,
        call_mmio_read(readq) -> u64
    );

    define_write!(infallible, write8, call_mmio_write(writeb) <- u8);
    define_write!(infallible, write16, call_mmio_write(writew) <- u16);
    define_write!(infallible, write32, call_mmio_write(writel) <- u32);
    define_write!(
        infallible,
        #[cfg(CONFIG_64BIT)]
        write64,
        call_mmio_write(writeq) <- u64
    );
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

    define_read!(infallible, pub read8_relaxed, call_mmio_read(readb_relaxed) -> u8);
    define_read!(infallible, pub read16_relaxed, call_mmio_read(readw_relaxed) -> u16);
    define_read!(infallible, pub read32_relaxed, call_mmio_read(readl_relaxed) -> u32);
    define_read!(
        infallible,
        #[cfg(CONFIG_64BIT)]
        pub read64_relaxed,
        call_mmio_read(readq_relaxed) -> u64
    );

    define_read!(fallible, pub try_read8_relaxed, call_mmio_read(readb_relaxed) -> u8);
    define_read!(fallible, pub try_read16_relaxed, call_mmio_read(readw_relaxed) -> u16);
    define_read!(fallible, pub try_read32_relaxed, call_mmio_read(readl_relaxed) -> u32);
    define_read!(
        fallible,
        #[cfg(CONFIG_64BIT)]
        pub try_read64_relaxed,
        call_mmio_read(readq_relaxed) -> u64
    );

    define_write!(infallible, pub write8_relaxed, call_mmio_write(writeb_relaxed) <- u8);
    define_write!(infallible, pub write16_relaxed, call_mmio_write(writew_relaxed) <- u16);
    define_write!(infallible, pub write32_relaxed, call_mmio_write(writel_relaxed) <- u32);
    define_write!(
        infallible,
        #[cfg(CONFIG_64BIT)]
        pub write64_relaxed,
        call_mmio_write(writeq_relaxed) <- u64
    );

    define_write!(fallible, pub try_write8_relaxed, call_mmio_write(writeb_relaxed) <- u8);
    define_write!(fallible, pub try_write16_relaxed, call_mmio_write(writew_relaxed) <- u16);
    define_write!(fallible, pub try_write32_relaxed, call_mmio_write(writel_relaxed) <- u32);
    define_write!(
        fallible,
        #[cfg(CONFIG_64BIT)]
        pub try_write64_relaxed,
        call_mmio_write(writeq_relaxed) <- u64
    );
}
