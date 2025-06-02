// SPDX-License-Identifier: GPL-2.0

//! Memory-mapped IO.
//!
//! C header: [`include/asm-generic/io.h`](srctree/include/asm-generic/io.h)

use crate::error::{code::EINVAL, Result};
use crate::{bindings, build_assert};

/// Raw representation of an MMIO region.
///
/// By itself, the existence of an instance of this structure does not provide any guarantees that
/// the represented MMIO region does exist or is properly mapped.
///
/// Instead, the bus specific MMIO implementation must convert this raw representation into an `Io`
/// instance providing the actual memory accessors. Only by the conversion into an `Io` structure
/// any guarantees are given.
pub struct IoRaw<const SIZE: usize = 0> {
    addr: usize,
    maxsize: usize,
}

impl<const SIZE: usize> IoRaw<SIZE> {
    /// Returns a new `IoRaw` instance on success, an error otherwise.
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

/// IO-mapped memory, starting at the base address @addr and spanning @maxlen bytes.
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
/// # use kernel::{bindings, io::{Io, IoRaw}};
/// # use core::ops::Deref;
///
/// // See also [`pci::Bar`] for a real example.
/// struct IoMem<const SIZE: usize>(IoRaw<SIZE>);
///
/// impl<const SIZE: usize> IoMem<SIZE> {
///     /// # Safety
///     ///
///     /// [`paddr`, `paddr` + `SIZE`) must be a valid MMIO region that is mappable into the CPUs
///     /// virtual address space.
///     unsafe fn new(paddr: usize) -> Result<Self>{
///         // SAFETY: By the safety requirements of this function [`paddr`, `paddr` + `SIZE`) is
///         // valid for `ioremap`.
///         let addr = unsafe { bindings::ioremap(paddr as _, SIZE as _) };
///         if addr.is_null() {
///             return Err(ENOMEM);
///         }
///
///         Ok(IoMem(IoRaw::new(addr as _, SIZE)?))
///     }
/// }
///
/// impl<const SIZE: usize> Drop for IoMem<SIZE> {
///     fn drop(&mut self) {
///         // SAFETY: `self.0.addr()` is guaranteed to be properly mapped by `Self::new`.
///         unsafe { bindings::iounmap(self.0.addr() as _); };
///     }
/// }
///
/// impl<const SIZE: usize> Deref for IoMem<SIZE> {
///    type Target = Io<SIZE>;
///
///    fn deref(&self) -> &Self::Target {
///         // SAFETY: The memory range stored in `self` has been properly mapped in `Self::new`.
///         unsafe { Io::from_raw(&self.0) }
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
pub struct Io<const SIZE: usize = 0>(IoRaw<SIZE>);

macro_rules! define_read {
    ($(#[$attr:meta])* $name:ident, $try_name:ident, $c_fn:ident -> $type_name:ty) => {
        /// Read IO data from a given offset known at compile time.
        ///
        /// Bound checks are performed on compile time, hence if the offset is not known at compile
        /// time, the build will fail.
        $(#[$attr])*
        #[inline]
        pub fn $name(&self, offset: usize) -> $type_name {
            let addr = self.io_addr_assert::<$type_name>(offset);

            // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
            unsafe { bindings::$c_fn(addr as _) }
        }

        /// Read IO data from a given offset.
        ///
        /// Bound checks are performed on runtime, it fails if the offset (plus the type size) is
        /// out of bounds.
        $(#[$attr])*
        pub fn $try_name(&self, offset: usize) -> Result<$type_name> {
            let addr = self.io_addr::<$type_name>(offset)?;

            // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
            Ok(unsafe { bindings::$c_fn(addr as _) })
        }
    };
}

macro_rules! define_write {
    ($(#[$attr:meta])* $name:ident, $try_name:ident, $c_fn:ident <- $type_name:ty) => {
        /// Write IO data from a given offset known at compile time.
        ///
        /// Bound checks are performed on compile time, hence if the offset is not known at compile
        /// time, the build will fail.
        $(#[$attr])*
        #[inline]
        pub fn $name(&self, value: $type_name, offset: usize) {
            let addr = self.io_addr_assert::<$type_name>(offset);

            // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
            unsafe { bindings::$c_fn(value, addr as _, ) }
        }

        /// Write IO data from a given offset.
        ///
        /// Bound checks are performed on runtime, it fails if the offset (plus the type size) is
        /// out of bounds.
        $(#[$attr])*
        pub fn $try_name(&self, value: $type_name, offset: usize) -> Result {
            let addr = self.io_addr::<$type_name>(offset)?;

            // SAFETY: By the type invariant `addr` is a valid address for MMIO operations.
            unsafe { bindings::$c_fn(value, addr as _) }
            Ok(())
        }
    };
}

impl<const SIZE: usize> Io<SIZE> {
    /// Converts an `IoRaw` into an `Io` instance, providing the accessors to the MMIO mapping.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `addr` is the start of a valid I/O mapped memory region of size
    /// `maxsize`.
    pub unsafe fn from_raw(raw: &IoRaw<SIZE>) -> &Self {
        // SAFETY: `Io` is a transparent wrapper around `IoRaw`.
        unsafe { &*core::ptr::from_ref(raw).cast() }
    }

    /// Returns the base address of this mapping.
    #[inline]
    pub fn addr(&self) -> usize {
        self.0.addr()
    }

    /// Returns the maximum size of this mapping.
    #[inline]
    pub fn maxsize(&self) -> usize {
        self.0.maxsize()
    }

    #[inline]
    const fn offset_valid<U>(offset: usize, size: usize) -> bool {
        let type_size = core::mem::size_of::<U>();
        if let Some(end) = offset.checked_add(type_size) {
            end <= size && offset % type_size == 0
        } else {
            false
        }
    }

    #[inline]
    fn io_addr<U>(&self, offset: usize) -> Result<usize> {
        if !Self::offset_valid::<U>(offset, self.maxsize()) {
            return Err(EINVAL);
        }

        // Probably no need to check, since the safety requirements of `Self::new` guarantee that
        // this can't overflow.
        self.addr().checked_add(offset).ok_or(EINVAL)
    }

    #[inline]
    fn io_addr_assert<U>(&self, offset: usize) -> usize {
        build_assert!(Self::offset_valid::<U>(offset, SIZE));

        self.addr() + offset
    }

    define_read!(read8, try_read8, readb -> u8);
    define_read!(read16, try_read16, readw -> u16);
    define_read!(read32, try_read32, readl -> u32);
    define_read!(
        #[cfg(CONFIG_64BIT)]
        read64,
        try_read64,
        readq -> u64
    );

    define_read!(read8_relaxed, try_read8_relaxed, readb_relaxed -> u8);
    define_read!(read16_relaxed, try_read16_relaxed, readw_relaxed -> u16);
    define_read!(read32_relaxed, try_read32_relaxed, readl_relaxed -> u32);
    define_read!(
        #[cfg(CONFIG_64BIT)]
        read64_relaxed,
        try_read64_relaxed,
        readq_relaxed -> u64
    );

    define_write!(write8, try_write8, writeb <- u8);
    define_write!(write16, try_write16, writew <- u16);
    define_write!(write32, try_write32, writel <- u32);
    define_write!(
        #[cfg(CONFIG_64BIT)]
        write64,
        try_write64,
        writeq <- u64
    );

    define_write!(write8_relaxed, try_write8_relaxed, writeb_relaxed <- u8);
    define_write!(write16_relaxed, try_write16_relaxed, writew_relaxed <- u16);
    define_write!(write32_relaxed, try_write32_relaxed, writel_relaxed <- u32);
    define_write!(
        #[cfg(CONFIG_64BIT)]
        write64_relaxed,
        try_write64_relaxed,
        writeq_relaxed <- u64
    );
}
