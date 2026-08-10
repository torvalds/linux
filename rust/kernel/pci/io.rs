// SPDX-License-Identifier: GPL-2.0

//! PCI memory-mapped I/O infrastructure.

use super::Device;
use crate::{
    bindings,
    device,
    devres::Devres,
    io::{
        IoBackend,
        IoBase,
        IoCapable,
        Mmio,
        MmioBackend,
        MmioRaw,
        Region, //
    },
    prelude::*,
    ptr::KnownSize, //
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

/// Alias for normal (256-byte) PCI configuration space.
pub type Normal = Region<256>;

/// Alias for extended (4096-byte) PCIe configuration space.
pub type Extended = Region<4096>;

/// A view of PCI configuration space of a device.
///
/// Provides typed read and write accessors for configuration registers
/// using the standard `pci_read_config_*` and `pci_write_config_*` helpers.
///
/// The generic parameter `T` is the type of the view. The full configuration space is also a
/// special type of view; in such cases, `T` can be [`Normal`] for 256-byte legacy configuration
/// space or [`Extended`] for 4096-byte PCIe extended configuration space (default).
///
/// # Invariants
///
/// `ptr` is aligned and range `ptr..ptr + KnownSize::size(ptr)` is within
/// `0..pdev.cfg_size().into_raw()`.
pub struct ConfigSpace<'a, T: ?Sized = Extended> {
    pub(crate) pdev: &'a Device<device::Bound>,
    ptr: *mut T,
}

impl<T: ?Sized> Copy for ConfigSpace<'_, T> {}
impl<T: ?Sized> Clone for ConfigSpace<'_, T> {
    #[inline]
    fn clone(&self) -> Self {
        *self
    }
}

// SAFETY: `ConfigSpace<'_, T>` is conceptually `&T` but in I/O memory.
unsafe impl<T: ?Sized + Sync> Send for ConfigSpace<'_, T> {}

// SAFETY: `ConfigSpace<'_, T>` is conceptually `&T` but in I/O memory.
unsafe impl<T: ?Sized + Sync> Sync for ConfigSpace<'_, T> {}

/// I/O Backend for PCI configuration space.
pub struct ConfigSpaceBackend;

impl IoBackend for ConfigSpaceBackend {
    type View<'a, T: ?Sized + KnownSize> = ConfigSpace<'a, T>;

    #[inline]
    fn as_ptr<'a, T: ?Sized + KnownSize>(view: ConfigSpace<'a, T>) -> *mut T {
        view.ptr
    }

    #[inline]
    unsafe fn project_view<'a, T: ?Sized + KnownSize, U: ?Sized + KnownSize>(
        view: Self::View<'a, T>,
        ptr: *mut U,
    ) -> Self::View<'a, U> {
        // INVARIANT: Per safety requirement.
        ConfigSpace {
            pdev: view.pdev,
            ptr,
        }
    }
}

/// Implements [`IoCapable`] on [`ConfigSpace`] for `$ty` using `$read_fn` and `$write_fn`.
macro_rules! impl_config_space_io_capable {
    ($ty:ty, $read_fn:ident, $write_fn:ident) => {
        impl IoCapable<$ty> for ConfigSpaceBackend {
            fn io_read(view: ConfigSpace<'_, $ty>) -> $ty {
                // CAST: The offset is cast to `i32` because the C functions expect a 32-bit
                // signed offset parameter. PCI configuration space size is at most 4096 bytes,
                // so the value always fits within `i32` without truncation or sign change.
                let addr = view.ptr.addr() as i32;

                let mut val: $ty = 0;

                // Return value from C function is ignored in infallible accessors.
                // SAFETY: By the type invariant `pdev` is a valid address.
                let _ = unsafe { bindings::$read_fn(view.pdev.as_raw(), addr, &mut val) };
                val
            }

            fn io_write(view: ConfigSpace<'_, $ty>, value: $ty) {
                // CAST: The offset is cast to `i32` because the C functions expect a 32-bit
                // signed offset parameter. PCI configuration space size is at most 4096 bytes,
                // so the value always fits within `i32` without truncation or sign change.
                let addr = view.ptr.addr() as i32;

                // Return value from C function is ignored in infallible accessors.
                // SAFETY: By the type invariant `pdev` is a valid address.
                let _ = unsafe { bindings::$write_fn(view.pdev.as_raw(), addr, value) };
            }
        }
    };
}

// PCI configuration space supports 8, 16, and 32-bit accesses.
impl_config_space_io_capable!(u8, pci_read_config_byte, pci_write_config_byte);
impl_config_space_io_capable!(u16, pci_read_config_word, pci_write_config_word);
impl_config_space_io_capable!(u32, pci_read_config_dword, pci_write_config_dword);

impl<'a, T: ?Sized + KnownSize> IoBase<'a> for ConfigSpace<'a, T> {
    type Backend = ConfigSpaceBackend;
    type Target = T;

    #[inline]
    fn as_view(self) -> ConfigSpace<'a, T> {
        self
    }
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
pub struct Bar<'a, const SIZE: usize = 0> {
    pdev: &'a Device<device::Bound>,
    io: MmioRaw<crate::io::Region<SIZE>>,
    num: i32,
}

impl<'a, const SIZE: usize> Bar<'a, SIZE> {
    pub(super) fn new(
        pdev: &'a Device<device::Bound>,
        num: u32,
        name: &'static CStr,
    ) -> Result<Self> {
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

        let io = match MmioRaw::new_region(ioptr, len as usize) {
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

        Ok(Bar { pdev, io, num })
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
        unsafe { Self::do_release(self.pdev, self.io.addr(), self.num) };
    }

    /// Consume the `Bar` and register it as a device-managed resource.
    ///
    /// The returned `Devres<Bar<'static, SIZE>>` can outlive the original lifetime `'a`. Access
    /// to the BAR is revoked when the device is unbound.
    pub fn into_devres(self) -> Result<Devres<Bar<'static, SIZE>>> {
        // SAFETY: Casting to `'static` is sound because `Devres` guarantees the `Bar` does not
        // actually outlive the device -- access is revoked and the resource is released when the
        // device is unbound.
        let bar: Bar<'static, SIZE> = unsafe { core::mem::transmute(self) };
        let pdev = bar.pdev;
        Devres::new(pdev.as_ref(), bar)
    }
}

impl Bar<'_> {
    #[inline]
    pub(super) fn index_is_valid(index: u32) -> bool {
        // A `struct pci_dev` owns an array of resources with at most `PCI_NUM_RESOURCES` entries.
        index < bindings::PCI_NUM_RESOURCES
    }
}

impl<const SIZE: usize> Drop for Bar<'_, SIZE> {
    fn drop(&mut self) {
        self.release();
    }
}

impl<'a, const SIZE: usize> IoBase<'a> for &'a Bar<'_, SIZE> {
    type Backend = MmioBackend;
    type Target = crate::io::Region<SIZE>;

    #[inline]
    fn as_view(self) -> Mmio<'a, Self::Target> {
        // SAFETY: By the type invariant of `Self`, the MMIO range in `self.io` is properly mapped.
        unsafe { Mmio::from_raw(self.io) }
    }
}

impl Device<device::Bound> {
    /// Maps an entire PCI BAR after performing a region-request on it. I/O operation bound checks
    /// can be performed on compile time for offsets (plus the requested type size) < SIZE.
    pub fn iomap_region_sized<'a, const SIZE: usize>(
        &'a self,
        bar: u32,
        name: &'static CStr,
    ) -> Result<Bar<'a, SIZE>> {
        Bar::new(self, bar, name)
    }

    /// Maps an entire PCI BAR after performing a region-request on it.
    pub fn iomap_region<'a>(&'a self, bar: u32, name: &'static CStr) -> Result<Bar<'a>> {
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

    /// Return a view of the normal (256-byte) config space.
    pub fn config_space<'a>(&'a self) -> ConfigSpace<'a, Normal> {
        // INVARIANT: null is aligned and the range is within config space.
        ConfigSpace {
            pdev: self,
            ptr: Normal::ptr_from_raw_parts_mut(core::ptr::null_mut(), self.cfg_size().into_raw()),
        }
    }

    /// Return a view of the extended (4096-byte) config space.
    pub fn config_space_extended<'a>(&'a self) -> Result<ConfigSpace<'a, Extended>> {
        if self.cfg_size() != ConfigSpaceSize::Extended {
            return Err(EINVAL);
        }

        // INVARIANT: null is aligned and we just checked the `cfg_size`.
        Ok(ConfigSpace {
            pdev: self,
            ptr: Extended::ptr_from_raw_parts_mut(core::ptr::null_mut(), 4096),
        })
    }
}
