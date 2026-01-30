// SPDX-License-Identifier: GPL-2.0

//! PCI memory-mapped I/O infrastructure.

use super::Device;
use crate::{
    bindings,
    device,
    devres::Devres,
    io::{
        define_read,
        define_write,
        Io,
        IoCapable,
        IoKnownSize,
        Mmio,
        MmioRaw, //
    },
    prelude::*,
    sync::aref::ARef, //
};
use core::{
    marker::PhantomData,
    ops::Deref, //
};

/// Represents the size of a PCI configuration space.
///
/// PCI devices can have either a *normal* (legacy) configuration space of 256 bytes,
/// or an *extended* configuration space of 4096 bytes as defined in the PCI Express
/// specification.
#[repr(usize)]
#[derive(Eq, PartialEq)]
pub enum ConfigSpaceSize {
    /// 256-byte legacy PCI configuration space.
    Normal = 256,

    /// 4096-byte PCIe extended configuration space.
    Extended = 4096,
}

impl ConfigSpaceSize {
    /// Get the raw value of this enum.
    #[inline(always)]
    pub const fn into_raw(self) -> usize {
        // CAST: PCI configuration space size is at most 4096 bytes, so the value always fits
        // within `usize` without truncation or sign change.
        self as usize
    }
}

/// Marker type for normal (256-byte) PCI configuration space.
pub struct Normal;

/// Marker type for extended (4096-byte) PCIe configuration space.
pub struct Extended;

/// Trait for PCI configuration space size markers.
///
/// This trait is implemented by [`Normal`] and [`Extended`] to provide
/// compile-time knowledge of the configuration space size.
pub trait ConfigSpaceKind {
    /// The size of this configuration space in bytes.
    const SIZE: usize;
}

impl ConfigSpaceKind for Normal {
    const SIZE: usize = 256;
}

impl ConfigSpaceKind for Extended {
    const SIZE: usize = 4096;
}

/// The PCI configuration space of a device.
///
/// Provides typed read and write accessors for configuration registers
/// using the standard `pci_read_config_*` and `pci_write_config_*` helpers.
///
/// The generic parameter `S` indicates the maximum size of the configuration space.
/// Use [`Normal`] for 256-byte legacy configuration space or [`Extended`] for
/// 4096-byte PCIe extended configuration space (default).
pub struct ConfigSpace<'a, S: ConfigSpaceKind = Extended> {
    pub(crate) pdev: &'a Device<device::Bound>,
    _marker: PhantomData<S>,
}

/// Internal helper macros used to invoke C PCI configuration space read functions.
///
/// This macro is intended to be used by higher-level PCI configuration space access macros
/// (define_read) and provides a unified expansion for infallible vs. fallible read semantics. It
/// emits a direct call into the corresponding C helper and performs the required cast to the Rust
/// return type.
///
/// # Parameters
///
/// * `$c_fn` – The C function performing the PCI configuration space write.
/// * `$self` – The I/O backend object.
/// * `$ty` – The type of the value to read.
/// * `$addr` – The PCI configuration space offset to read.
///
/// This macro does not perform any validation; all invariants must be upheld by the higher-level
/// abstraction invoking it.
macro_rules! call_config_read {
    (infallible, $c_fn:ident, $self:ident, $ty:ty, $addr:expr) => {{
        let mut val: $ty = 0;
        // SAFETY: By the type invariant `$self.pdev` is a valid address.
        // CAST: The offset is cast to `i32` because the C functions expect a 32-bit signed offset
        // parameter. PCI configuration space size is at most 4096 bytes, so the value always fits
        // within `i32` without truncation or sign change.
        // Return value from C function is ignored in infallible accessors.
        let _ret = unsafe { bindings::$c_fn($self.pdev.as_raw(), $addr as i32, &mut val) };
        val
    }};
}

/// Internal helper macros used to invoke C PCI configuration space write functions.
///
/// This macro is intended to be used by higher-level PCI configuration space access macros
/// (define_write) and provides a unified expansion for infallible vs. fallible read semantics. It
/// emits a direct call into the corresponding C helper and performs the required cast to the Rust
/// return type.
///
/// # Parameters
///
/// * `$c_fn` – The C function performing the PCI configuration space write.
/// * `$self` – The I/O backend object.
/// * `$ty` – The type of the written value.
/// * `$addr` – The configuration space offset to write.
/// * `$value` – The value to write.
///
/// This macro does not perform any validation; all invariants must be upheld by the higher-level
/// abstraction invoking it.
macro_rules! call_config_write {
    (infallible, $c_fn:ident, $self:ident, $ty:ty, $addr:expr, $value:expr) => {
        // SAFETY: By the type invariant `$self.pdev` is a valid address.
        // CAST: The offset is cast to `i32` because the C functions expect a 32-bit signed offset
        // parameter. PCI configuration space size is at most 4096 bytes, so the value always fits
        // within `i32` without truncation or sign change.
        // Return value from C function is ignored in infallible accessors.
        let _ret = unsafe { bindings::$c_fn($self.pdev.as_raw(), $addr as i32, $value) };
    };
}

// PCI configuration space supports 8, 16, and 32-bit accesses.
impl<'a, S: ConfigSpaceKind> IoCapable<u8> for ConfigSpace<'a, S> {}
impl<'a, S: ConfigSpaceKind> IoCapable<u16> for ConfigSpace<'a, S> {}
impl<'a, S: ConfigSpaceKind> IoCapable<u32> for ConfigSpace<'a, S> {}

impl<'a, S: ConfigSpaceKind> Io for ConfigSpace<'a, S> {
    /// Returns the base address of the I/O region. It is always 0 for configuration space.
    #[inline]
    fn addr(&self) -> usize {
        0
    }

    /// Returns the maximum size of the configuration space.
    #[inline]
    fn maxsize(&self) -> usize {
        self.pdev.cfg_size().into_raw()
    }

    // PCI configuration space does not support fallible operations.
    // The default implementations from the Io trait are not used.

    define_read!(infallible, read8, call_config_read(pci_read_config_byte) -> u8);
    define_read!(infallible, read16, call_config_read(pci_read_config_word) -> u16);
    define_read!(infallible, read32, call_config_read(pci_read_config_dword) -> u32);

    define_write!(infallible, write8, call_config_write(pci_write_config_byte) <- u8);
    define_write!(infallible, write16, call_config_write(pci_write_config_word) <- u16);
    define_write!(infallible, write32, call_config_write(pci_write_config_dword) <- u32);
}

impl<'a, S: ConfigSpaceKind> IoKnownSize for ConfigSpace<'a, S> {
    const MIN_SIZE: usize = S::SIZE;
}

/// A PCI BAR to perform I/O-Operations on.
///
/// I/O backend assumes that the device is little-endian and will automatically
/// convert from little-endian to CPU endianness.
///
/// # Invariants
///
/// `Bar` always holds an `IoRaw` instance that holds a valid pointer to the start of the I/O
/// memory mapped PCI BAR and its size.
pub struct Bar<const SIZE: usize = 0> {
    pdev: ARef<Device>,
    io: MmioRaw<SIZE>,
    num: i32,
}

impl<const SIZE: usize> Bar<SIZE> {
    pub(super) fn new(pdev: &Device, num: u32, name: &CStr) -> Result<Self> {
        let len = pdev.resource_len(num)?;
        if len == 0 {
            return Err(ENOMEM);
        }

        // Convert to `i32`, since that's what all the C bindings use.
        let num = i32::try_from(num)?;

        // SAFETY:
        // `pdev` is valid by the invariants of `Device`.
        // `num` is checked for validity by a previous call to `Device::resource_len`.
        // `name` is always valid.
        let ret = unsafe { bindings::pci_request_region(pdev.as_raw(), num, name.as_char_ptr()) };
        if ret != 0 {
            return Err(EBUSY);
        }

        // SAFETY:
        // `pdev` is valid by the invariants of `Device`.
        // `num` is checked for validity by a previous call to `Device::resource_len`.
        // `name` is always valid.
        let ioptr: usize = unsafe { bindings::pci_iomap(pdev.as_raw(), num, 0) } as usize;
        if ioptr == 0 {
            // SAFETY:
            // `pdev` is valid by the invariants of `Device`.
            // `num` is checked for validity by a previous call to `Device::resource_len`.
            unsafe { bindings::pci_release_region(pdev.as_raw(), num) };
            return Err(ENOMEM);
        }

        let io = match MmioRaw::new(ioptr, len as usize) {
            Ok(io) => io,
            Err(err) => {
                // SAFETY:
                // `pdev` is valid by the invariants of `Device`.
                // `ioptr` is guaranteed to be the start of a valid I/O mapped memory region.
                // `num` is checked for validity by a previous call to `Device::resource_len`.
                unsafe { Self::do_release(pdev, ioptr, num) };
                return Err(err);
            }
        };

        Ok(Bar {
            pdev: pdev.into(),
            io,
            num,
        })
    }

    /// # Safety
    ///
    /// `ioptr` must be a valid pointer to the memory mapped PCI BAR number `num`.
    unsafe fn do_release(pdev: &Device, ioptr: usize, num: i32) {
        // SAFETY:
        // `pdev` is valid by the invariants of `Device`.
        // `ioptr` is valid by the safety requirements.
        // `num` is valid by the safety requirements.
        unsafe {
            bindings::pci_iounmap(pdev.as_raw(), ioptr as *mut c_void);
            bindings::pci_release_region(pdev.as_raw(), num);
        }
    }

    fn release(&self) {
        // SAFETY: The safety requirements are guaranteed by the type invariant of `self.pdev`.
        unsafe { Self::do_release(&self.pdev, self.io.addr(), self.num) };
    }
}

impl Bar {
    #[inline]
    pub(super) fn index_is_valid(index: u32) -> bool {
        // A `struct pci_dev` owns an array of resources with at most `PCI_NUM_RESOURCES` entries.
        index < bindings::PCI_NUM_RESOURCES
    }
}

impl<const SIZE: usize> Drop for Bar<SIZE> {
    fn drop(&mut self) {
        self.release();
    }
}

impl<const SIZE: usize> Deref for Bar<SIZE> {
    type Target = Mmio<SIZE>;

    fn deref(&self) -> &Self::Target {
        // SAFETY: By the type invariant of `Self`, the MMIO range in `self.io` is properly mapped.
        unsafe { Mmio::from_raw(&self.io) }
    }
}

impl Device<device::Bound> {
    /// Maps an entire PCI BAR after performing a region-request on it. I/O operation bound checks
    /// can be performed on compile time for offsets (plus the requested type size) < SIZE.
    pub fn iomap_region_sized<'a, const SIZE: usize>(
        &'a self,
        bar: u32,
        name: &'a CStr,
    ) -> impl PinInit<Devres<Bar<SIZE>>, Error> + 'a {
        Devres::new(self.as_ref(), Bar::<SIZE>::new(self, bar, name))
    }

    /// Maps an entire PCI BAR after performing a region-request on it.
    pub fn iomap_region<'a>(
        &'a self,
        bar: u32,
        name: &'a CStr,
    ) -> impl PinInit<Devres<Bar>, Error> + 'a {
        self.iomap_region_sized::<0>(bar, name)
    }

    /// Returns the size of configuration space.
    pub fn cfg_size(&self) -> ConfigSpaceSize {
        // SAFETY: `self.as_raw` is a valid pointer to a `struct pci_dev`.
        let size = unsafe { (*self.as_raw()).cfg_size };
        match size {
            256 => ConfigSpaceSize::Normal,
            4096 => ConfigSpaceSize::Extended,
            _ => {
                // PANIC: The PCI subsystem only ever reports the configuration space size as either
                // `ConfigSpaceSize::Normal` or `ConfigSpaceSize::Extended`.
                unreachable!();
            }
        }
    }

    /// Return an initialized normal (256-byte) config space object.
    pub fn config_space<'a>(&'a self) -> ConfigSpace<'a, Normal> {
        ConfigSpace {
            pdev: self,
            _marker: PhantomData,
        }
    }

    /// Return an initialized extended (4096-byte) config space object.
    pub fn config_space_extended<'a>(&'a self) -> Result<ConfigSpace<'a, Extended>> {
        if self.cfg_size() != ConfigSpaceSize::Extended {
            return Err(EINVAL);
        }

        Ok(ConfigSpace {
            pdev: self,
            _marker: PhantomData,
        })
    }
}
