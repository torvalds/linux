// SPDX-License-Identifier: GPL-2.0

//! Generic memory-mapped IO.

use crate::{
    device::{
        Bound,
        Device, //
    },
    devres::DevresLt,
    io::{
        self,
        resource::{
            Region,
            Resource, //
        },
        IoBase,
        Mmio,
        MmioBackend,
        MmioRaw, //
    },
    prelude::*,
    types::{
        CovariantForLt,
        ForLt, //
    },
};

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
    /// use kernel::{
    ///     bindings,
    ///     device::Core,
    ///     io::Io,
    ///     of,
    ///     platform,
    /// };
    /// struct SampleDriver;
    ///
    /// impl platform::Driver for SampleDriver {
    ///    # type IdInfo = ();
    ///    # type Data<'bound> = Self;
    ///
    ///    fn probe<'bound>(
    ///       pdev: &'bound platform::Device<Core<'_>>,
    ///       info: Option<&'bound Self::IdInfo>,
    ///    ) -> impl PinInit<Self, Error> + 'bound {
    ///       let offset = 0; // Some offset.
    ///
    ///       // If the size is known at compile time, use [`Self::iomap_sized`].
    ///       //
    ///       // No runtime checks will apply when reading and writing.
    ///       let request = pdev.io_request_by_index(0).ok_or(ENODEV)?;
    ///       let iomem = request.iomap_sized::<42>()?;
    ///
    ///       // Read and write a 32-bit value at `offset`.
    ///       let data = iomem.read32(offset);
    ///
    ///       iomem.write32(data, offset);
    ///
    ///       # Ok(SampleDriver)
    ///     }
    /// }
    /// ```
    pub fn iomap_sized<const SIZE: usize>(self) -> Result<IoMem<'a, SIZE>> {
        IoMem::ioremap(self.device, self.resource)
    }

    /// Same as [`Self::iomap_sized`] but with exclusive access to the
    /// underlying region.
    ///
    /// This uses the [`ioremap()`] C API.
    ///
    /// [`ioremap()`]: https://docs.kernel.org/driver-api/device-io.html#getting-access-to-the-device
    pub fn iomap_exclusive_sized<const SIZE: usize>(self) -> Result<ExclusiveIoMem<'a, SIZE>> {
        ExclusiveIoMem::ioremap(self.device, self.resource)
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
    /// use kernel::{
    ///     bindings,
    ///     device::Core,
    ///     io::Io,
    ///     of,
    ///     platform,
    /// };
    /// struct SampleDriver;
    ///
    /// impl platform::Driver for SampleDriver {
    ///    # type IdInfo = ();
    ///    # type Data<'bound> = Self;
    ///
    ///    fn probe<'bound>(
    ///       pdev: &'bound platform::Device<Core<'_>>,
    ///       info: Option<&'bound Self::IdInfo>,
    ///    ) -> impl PinInit<Self, Error> + 'bound {
    ///       let offset = 0; // Some offset.
    ///
    ///       // Unlike [`Self::iomap_sized`], here the size of the memory region
    ///       // is not known at compile time, so only the `try_read*` and `try_write*`
    ///       // family of functions should be used, leading to runtime checks on every
    ///       // access.
    ///       let request = pdev.io_request_by_index(0).ok_or(ENODEV)?;
    ///       let iomem = request.iomap()?;
    ///
    ///       let data = iomem.try_read32(offset)?;
    ///
    ///       iomem.try_write32(data, offset)?;
    ///
    ///       # Ok(SampleDriver)
    ///     }
    /// }
    /// ```
    pub fn iomap(self) -> Result<IoMem<'a>> {
        self.iomap_sized::<0>()
    }

    /// Same as [`Self::iomap`] but with exclusive access to the underlying
    /// region.
    pub fn iomap_exclusive(self) -> Result<ExclusiveIoMem<'a, 0>> {
        self.iomap_exclusive_sized::<0>()
    }
}

/// An exclusive memory-mapped IO region.
///
/// # Invariants
///
/// - [`ExclusiveIoMem`] has exclusive access to the underlying [`IoMem`].
pub struct ExclusiveIoMem<'a, const SIZE: usize> {
    /// The underlying `IoMem` instance.
    iomem: IoMem<'a, SIZE>,

    /// The region abstraction. This represents exclusive access to the
    /// range represented by the underlying `iomem`.
    ///
    /// This field is needed for ownership of the region.
    _region: Region,
}

impl<const SIZE: usize> ForLt for ExclusiveIoMem<'static, SIZE> {
    type Of<'a> = ExclusiveIoMem<'a, SIZE>;
}

// SAFETY: `ExclusiveIoMem<'a, SIZE>` is covariant over `'a`; it holds an `IoMem<'a, SIZE>`,
// which holds `&'a Device<Bound>`, which is covariant.
unsafe impl<const SIZE: usize> CovariantForLt for ExclusiveIoMem<'static, SIZE> {}

/// A device-managed exclusive I/O memory region.
///
/// See [`ExclusiveIoMem::into_devres`].
pub type DevresExclusiveIoMem<const SIZE: usize> = DevresLt<ExclusiveIoMem<'static, SIZE>>;

impl<'a, const SIZE: usize> ExclusiveIoMem<'a, SIZE> {
    /// Creates a new `ExclusiveIoMem` instance.
    fn ioremap(dev: &'a Device<Bound>, resource: &Resource) -> Result<Self> {
        let start = resource.start();
        let size = resource.size();
        let name = resource.name().unwrap_or_default();

        let region = resource
            .request_region(
                start,
                size,
                name.to_cstring()?,
                io::resource::Flags::IORESOURCE_MEM,
            )
            .ok_or(EBUSY)?;

        let iomem = IoMem::ioremap(dev, resource)?;

        Ok(ExclusiveIoMem {
            iomem,
            _region: region,
        })
    }

    /// Consume the `ExclusiveIoMem` and register it as a device-managed resource.
    ///
    /// The returned [`DevresExclusiveIoMem`] can outlive the original borrow and be stored in
    /// driver data. Access to the I/O memory is revoked automatically when the device is unbound.
    pub fn into_devres(self) -> Result<DevresExclusiveIoMem<SIZE>> {
        let dev = self.iomem.dev;
        // SAFETY: `ExclusiveIoMem` only holds a device reference and an I/O mapping, both of
        // which remain valid for the device's full bound scope, not just for `'a`.
        unsafe { DevresLt::new(dev, self) }
    }
}

impl<'a, const SIZE: usize> IoBase<'a> for &'a ExclusiveIoMem<'_, SIZE> {
    type Backend = MmioBackend;
    type Target = super::Region<SIZE>;

    #[inline]
    fn as_view(self) -> Mmio<'a, Self::Target> {
        self.iomem.as_view()
    }
}

/// A generic memory-mapped IO region.
///
/// Accesses to the underlying region is checked either at compile time, if the
/// region's size is known at that point, or at runtime otherwise.
///
/// # Invariants
///
/// [`IoMem`] always holds an [`MmioRaw`] instance that holds a valid pointer to the
/// start of the I/O memory mapped region.
pub struct IoMem<'a, const SIZE: usize = 0> {
    dev: &'a Device<Bound>,
    io: MmioRaw<super::Region<SIZE>>,
}

impl<const SIZE: usize> ForLt for IoMem<'static, SIZE> {
    type Of<'a> = IoMem<'a, SIZE>;
}

// SAFETY: `IoMem<'a, SIZE>` is covariant over `'a`; it holds `&'a Device<Bound>`,
// which is covariant.
unsafe impl<const SIZE: usize> CovariantForLt for IoMem<'static, SIZE> {}

/// A device-managed I/O memory region.
///
/// See [`IoMem::into_devres`].
pub type DevresIoMem<const SIZE: usize = 0> = DevresLt<IoMem<'static, SIZE>>;

impl<'a, const SIZE: usize> IoMem<'a, SIZE> {
    fn ioremap(dev: &'a Device<Bound>, resource: &Resource) -> Result<Self> {
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

        let io = MmioRaw::new_region(addr as usize, size)?;
        Ok(IoMem { dev, io })
    }

    /// Consume the `IoMem` and register it as a device-managed resource.
    ///
    /// The returned [`DevresIoMem`] can outlive the original borrow and be stored in driver data.
    /// Access to the I/O memory is revoked automatically when the device is unbound.
    pub fn into_devres(self) -> Result<DevresIoMem<SIZE>> {
        let dev = self.dev;
        // SAFETY: `IoMem` only holds a device reference and an I/O mapping, both of which
        // remain valid for the device's full bound scope, not just for `'a`.
        unsafe { DevresLt::new(dev, self) }
    }
}

impl<const SIZE: usize> Drop for IoMem<'_, SIZE> {
    fn drop(&mut self) {
        // SAFETY: Safe as by the invariant of `Io`.
        unsafe { bindings::iounmap(self.io.addr() as *mut c_void) }
    }
}

impl<'a, const SIZE: usize> IoBase<'a> for &'a IoMem<'_, SIZE> {
    type Backend = MmioBackend;
    type Target = super::Region<SIZE>;

    #[inline]
    fn as_view(self) -> Mmio<'a, Self::Target> {
        // SAFETY: Safe as by the invariant of `IoMem`.
        unsafe { Mmio::from_raw(self.io) }
    }
}
