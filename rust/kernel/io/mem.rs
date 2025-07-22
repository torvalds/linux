// SPDX-License-Identifier: GPL-2.0

//! Generic memory-mapped IO.

use core::ops::Deref;

use crate::c_str;
use crate::device::Bound;
use crate::device::Device;
use crate::devres::Devres;
use crate::io;
use crate::io::resource::Region;
use crate::io::resource::Resource;
use crate::io::Io;
use crate::io::IoRaw;
use crate::prelude::*;

/// An IO request for a specific device and resource.
pub struct IoRequest<'a> {
    device: &'a Device<Bound>,
    resource: &'a Resource,
}

impl<'a> IoRequest<'a> {
    /// Creates a new [`IoRequest`] instance.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `resource` is valid for `device` during the
    /// lifetime `'a`.
    pub(crate) unsafe fn new(device: &'a Device<Bound>, resource: &'a Resource) -> Self {
        IoRequest { device, resource }
    }

    /// Maps an [`IoRequest`] where the size is known at compile time.
    ///
    /// This uses the [`ioremap()`] C API.
    ///
    /// [`ioremap()`]: https://docs.kernel.org/driver-api/device-io.html#getting-access-to-the-device
    ///
    /// # Examples
    ///
    /// The following example uses a [`kernel::platform::Device`] for
    /// illustration purposes.
    ///
    /// ```no_run
    /// use kernel::{bindings, c_str, platform, of, device::Core};
    /// struct SampleDriver;
    ///
    /// impl platform::Driver for SampleDriver {
    ///    # type IdInfo = ();
    ///
    ///    fn probe(
    ///       pdev: &platform::Device<Core>,
    ///       info: Option<&Self::IdInfo>,
    ///    ) -> Result<Pin<KBox<Self>>> {
    ///       let offset = 0; // Some offset.
    ///
    ///       // If the size is known at compile time, use [`Self::iomap_sized`].
    ///       //
    ///       // No runtime checks will apply when reading and writing.
    ///       let request = pdev.io_request_by_index(0).ok_or(ENODEV)?;
    ///       let iomem = request.iomap_sized::<42>();
    ///       let iomem = KBox::pin_init(iomem, GFP_KERNEL)?;
    ///
    ///       let io = iomem.access(pdev.as_ref())?;
    ///
    ///       // Read and write a 32-bit value at `offset`.
    ///       let data = io.read32_relaxed(offset);
    ///
    ///       io.write32_relaxed(data, offset);
    ///
    ///       # Ok(KBox::new(SampleDriver, GFP_KERNEL)?.into())
    ///     }
    /// }
    /// ```
    pub fn iomap_sized<const SIZE: usize>(self) -> impl PinInit<Devres<IoMem<SIZE>>, Error> + 'a {
        IoMem::new(self)
    }

    /// Same as [`Self::iomap_sized`] but with exclusive access to the
    /// underlying region.
    ///
    /// This uses the [`ioremap()`] C API.
    ///
    /// [`ioremap()`]: https://docs.kernel.org/driver-api/device-io.html#getting-access-to-the-device
    pub fn iomap_exclusive_sized<const SIZE: usize>(
        self,
    ) -> impl PinInit<Devres<ExclusiveIoMem<SIZE>>, Error> + 'a {
        ExclusiveIoMem::new(self)
    }

    /// Maps an [`IoRequest`] where the size is not known at compile time,
    ///
    /// This uses the [`ioremap()`] C API.
    ///
    /// [`ioremap()`]: https://docs.kernel.org/driver-api/device-io.html#getting-access-to-the-device
    ///
    /// # Examples
    ///
    /// The following example uses a [`kernel::platform::Device`] for
    /// illustration purposes.
    ///
    /// ```no_run
    /// use kernel::{bindings, c_str, platform, of, device::Core};
    /// struct SampleDriver;
    ///
    /// impl platform::Driver for SampleDriver {
    ///    # type IdInfo = ();
    ///
    ///    fn probe(
    ///       pdev: &platform::Device<Core>,
    ///       info: Option<&Self::IdInfo>,
    ///    ) -> Result<Pin<KBox<Self>>> {
    ///       let offset = 0; // Some offset.
    ///
    ///       // Unlike [`Self::iomap_sized`], here the size of the memory region
    ///       // is not known at compile time, so only the `try_read*` and `try_write*`
    ///       // family of functions should be used, leading to runtime checks on every
    ///       // access.
    ///       let request = pdev.io_request_by_index(0).ok_or(ENODEV)?;
    ///       let iomem = request.iomap();
    ///       let iomem = KBox::pin_init(iomem, GFP_KERNEL)?;
    ///
    ///       let io = iomem.access(pdev.as_ref())?;
    ///
    ///       let data = io.try_read32_relaxed(offset)?;
    ///
    ///       io.try_write32_relaxed(data, offset)?;
    ///
    ///       # Ok(KBox::new(SampleDriver, GFP_KERNEL)?.into())
    ///     }
    /// }
    /// ```
    pub fn iomap(self) -> impl PinInit<Devres<IoMem<0>>, Error> + 'a {
        Self::iomap_sized::<0>(self)
    }

    /// Same as [`Self::iomap`] but with exclusive access to the underlying
    /// region.
    pub fn iomap_exclusive(self) -> impl PinInit<Devres<ExclusiveIoMem<0>>, Error> + 'a {
        Self::iomap_exclusive_sized::<0>(self)
    }
}

/// An exclusive memory-mapped IO region.
///
/// # Invariants
///
/// - [`ExclusiveIoMem`] has exclusive access to the underlying [`IoMem`].
pub struct ExclusiveIoMem<const SIZE: usize> {
    /// The underlying `IoMem` instance.
    iomem: IoMem<SIZE>,

    /// The region abstraction. This represents exclusive access to the
    /// range represented by the underlying `iomem`.
    ///
    /// This field is needed for ownership of the region.
    _region: Region,
}

impl<const SIZE: usize> ExclusiveIoMem<SIZE> {
    /// Creates a new `ExclusiveIoMem` instance.
    fn ioremap(resource: &Resource) -> Result<Self> {
        let start = resource.start();
        let size = resource.size();
        let name = resource.name().unwrap_or(c_str!(""));

        let region = resource
            .request_region(
                start,
                size,
                name.to_cstring()?,
                io::resource::Flags::IORESOURCE_MEM,
            )
            .ok_or(EBUSY)?;

        let iomem = IoMem::ioremap(resource)?;

        let iomem = ExclusiveIoMem {
            iomem,
            _region: region,
        };

        Ok(iomem)
    }

    /// Creates a new `ExclusiveIoMem` instance from a previously acquired [`IoRequest`].
    pub fn new<'a>(io_request: IoRequest<'a>) -> impl PinInit<Devres<Self>, Error> + 'a {
        let dev = io_request.device;
        let res = io_request.resource;

        Devres::new(dev, Self::ioremap(res))
    }
}

impl<const SIZE: usize> Deref for ExclusiveIoMem<SIZE> {
    type Target = Io<SIZE>;

    fn deref(&self) -> &Self::Target {
        &self.iomem
    }
}

/// A generic memory-mapped IO region.
///
/// Accesses to the underlying region is checked either at compile time, if the
/// region's size is known at that point, or at runtime otherwise.
///
/// # Invariants
///
/// [`IoMem`] always holds an [`IoRaw`] instance that holds a valid pointer to the
/// start of the I/O memory mapped region.
pub struct IoMem<const SIZE: usize = 0> {
    io: IoRaw<SIZE>,
}

impl<const SIZE: usize> IoMem<SIZE> {
    fn ioremap(resource: &Resource) -> Result<Self> {
        // Note: Some ioremap() implementations use types that depend on the CPU
        // word width rather than the bus address width.
        //
        // TODO: Properly address this in the C code to avoid this `try_into`.
        let size = resource.size().try_into()?;
        if size == 0 {
            return Err(EINVAL);
        }

        let res_start = resource.start();

        let addr = if resource
            .flags()
            .contains(io::resource::Flags::IORESOURCE_MEM_NONPOSTED)
        {
            // SAFETY:
            // - `res_start` and `size` are read from a presumably valid `struct resource`.
            // - `size` is known not to be zero at this point.
            unsafe { bindings::ioremap_np(res_start, size) }
        } else {
            // SAFETY:
            // - `res_start` and `size` are read from a presumably valid `struct resource`.
            // - `size` is known not to be zero at this point.
            unsafe { bindings::ioremap(res_start, size) }
        };

        if addr.is_null() {
            return Err(ENOMEM);
        }

        let io = IoRaw::new(addr as usize, size)?;
        let io = IoMem { io };

        Ok(io)
    }

    /// Creates a new `IoMem` instance from a previously acquired [`IoRequest`].
    pub fn new<'a>(io_request: IoRequest<'a>) -> impl PinInit<Devres<Self>, Error> + 'a {
        let dev = io_request.device;
        let res = io_request.resource;

        Devres::new(dev, Self::ioremap(res))
    }
}

impl<const SIZE: usize> Drop for IoMem<SIZE> {
    fn drop(&mut self) {
        // SAFETY: Safe as by the invariant of `Io`.
        unsafe { bindings::iounmap(self.io.addr() as *mut c_void) }
    }
}

impl<const SIZE: usize> Deref for IoMem<SIZE> {
    type Target = Io<SIZE>;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Safe as by the invariant of `IoMem`.
        unsafe { Io::from_raw(&self.io) }
    }
}
