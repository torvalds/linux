// SPDX-License-Identifier: GPL-2.0

//! PCI memory-mapped I/O infrastructure.

use super::Device;
use crate::{
    bindings,
    device,
    devres::Devres,
    io::{
        Io,
        IoRaw, //
    },
    prelude::*,
    sync::aref::ARef, //
};
use core::ops::Deref;

/// A PCI BAR to perform I/O-Operations on.
///
/// # Invariants
///
/// `Bar` always holds an `IoRaw` inststance that holds a valid pointer to the start of the I/O
/// memory mapped PCI BAR and its size.
pub struct Bar<const SIZE: usize = 0> {
    pdev: ARef<Device>,
    io: IoRaw<SIZE>,
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
            // `pdev` valid by the invariants of `Device`.
            // `num` is checked for validity by a previous call to `Device::resource_len`.
            unsafe { bindings::pci_release_region(pdev.as_raw(), num) };
            return Err(ENOMEM);
        }

        let io = match IoRaw::new(ioptr, len as usize) {
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
    type Target = Io<SIZE>;

    fn deref(&self) -> &Self::Target {
        // SAFETY: By the type invariant of `Self`, the MMIO range in `self.io` is properly mapped.
        unsafe { Io::from_raw(&self.io) }
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
}
