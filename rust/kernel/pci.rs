// SPDX-License-Identifier: GPL-2.0

//! Abstractions for the PCI bus.
//!
//! C header: [`include/linux/pci.h`](srctree/include/linux/pci.h)

use crate::{
    bindings, container_of, device,
    device_id::{RawDeviceId, RawDeviceIdIndex},
    devres::Devres,
    driver,
    error::{from_result, to_result, Result},
    io::{Io, IoRaw},
    irq::{self, IrqRequest},
    str::CStr,
    sync::aref::ARef,
    types::Opaque,
    ThisModule,
};
use core::{
    marker::PhantomData,
    ops::Deref,
    ptr::{addr_of_mut, NonNull},
};
use kernel::prelude::*;

mod id;

pub use self::id::{Class, ClassMask, Vendor};

/// An adapter for the registration of PCI drivers.
pub struct Adapter<T: Driver>(T);

// SAFETY: A call to `unregister` for a given instance of `RegType` is guaranteed to be valid if
// a preceding call to `register` has been successful.
unsafe impl<T: Driver + 'static> driver::RegistrationOps for Adapter<T> {
    type RegType = bindings::pci_driver;

    unsafe fn register(
        pdrv: &Opaque<Self::RegType>,
        name: &'static CStr,
        module: &'static ThisModule,
    ) -> Result {
        // SAFETY: It's safe to set the fields of `struct pci_driver` on initialization.
        unsafe {
            (*pdrv.get()).name = name.as_char_ptr();
            (*pdrv.get()).probe = Some(Self::probe_callback);
            (*pdrv.get()).remove = Some(Self::remove_callback);
            (*pdrv.get()).id_table = T::ID_TABLE.as_ptr();
        }

        // SAFETY: `pdrv` is guaranteed to be a valid `RegType`.
        to_result(unsafe {
            bindings::__pci_register_driver(pdrv.get(), module.0, name.as_char_ptr())
        })
    }

    unsafe fn unregister(pdrv: &Opaque<Self::RegType>) {
        // SAFETY: `pdrv` is guaranteed to be a valid `RegType`.
        unsafe { bindings::pci_unregister_driver(pdrv.get()) }
    }
}

impl<T: Driver + 'static> Adapter<T> {
    extern "C" fn probe_callback(
        pdev: *mut bindings::pci_dev,
        id: *const bindings::pci_device_id,
    ) -> c_int {
        // SAFETY: The PCI bus only ever calls the probe callback with a valid pointer to a
        // `struct pci_dev`.
        //
        // INVARIANT: `pdev` is valid for the duration of `probe_callback()`.
        let pdev = unsafe { &*pdev.cast::<Device<device::CoreInternal>>() };

        // SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `struct pci_device_id` and
        // does not add additional invariants, so it's safe to transmute.
        let id = unsafe { &*id.cast::<DeviceId>() };
        let info = T::ID_TABLE.info(id.index());

        from_result(|| {
            let data = T::probe(pdev, info)?;

            pdev.as_ref().set_drvdata(data);
            Ok(0)
        })
    }

    extern "C" fn remove_callback(pdev: *mut bindings::pci_dev) {
        // SAFETY: The PCI bus only ever calls the remove callback with a valid pointer to a
        // `struct pci_dev`.
        //
        // INVARIANT: `pdev` is valid for the duration of `remove_callback()`.
        let pdev = unsafe { &*pdev.cast::<Device<device::CoreInternal>>() };

        // SAFETY: `remove_callback` is only ever called after a successful call to
        // `probe_callback`, hence it's guaranteed that `Device::set_drvdata()` has been called
        // and stored a `Pin<KBox<T>>`.
        let data = unsafe { pdev.as_ref().drvdata_obtain::<Pin<KBox<T>>>() };

        T::unbind(pdev, data.as_ref());
    }
}

/// Declares a kernel module that exposes a single PCI driver.
///
/// # Examples
///
///```ignore
/// kernel::module_pci_driver! {
///     type: MyDriver,
///     name: "Module name",
///     authors: ["Author name"],
///     description: "Description",
///     license: "GPL v2",
/// }
///```
#[macro_export]
macro_rules! module_pci_driver {
($($f:tt)*) => {
    $crate::module_driver!(<T>, $crate::pci::Adapter<T>, { $($f)* });
};
}

/// Abstraction for the PCI device ID structure ([`struct pci_device_id`]).
///
/// [`struct pci_device_id`]: https://docs.kernel.org/PCI/pci.html#c.pci_device_id
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct DeviceId(bindings::pci_device_id);

impl DeviceId {
    const PCI_ANY_ID: u32 = !0;

    /// Equivalent to C's `PCI_DEVICE` macro.
    ///
    /// Create a new `pci::DeviceId` from a vendor and device ID.
    #[inline]
    pub const fn from_id(vendor: Vendor, device: u32) -> Self {
        Self(bindings::pci_device_id {
            vendor: vendor.as_raw() as u32,
            device,
            subvendor: DeviceId::PCI_ANY_ID,
            subdevice: DeviceId::PCI_ANY_ID,
            class: 0,
            class_mask: 0,
            driver_data: 0,
            override_only: 0,
        })
    }

    /// Equivalent to C's `PCI_DEVICE_CLASS` macro.
    ///
    /// Create a new `pci::DeviceId` from a class number and mask.
    #[inline]
    pub const fn from_class(class: u32, class_mask: u32) -> Self {
        Self(bindings::pci_device_id {
            vendor: DeviceId::PCI_ANY_ID,
            device: DeviceId::PCI_ANY_ID,
            subvendor: DeviceId::PCI_ANY_ID,
            subdevice: DeviceId::PCI_ANY_ID,
            class,
            class_mask,
            driver_data: 0,
            override_only: 0,
        })
    }

    /// Create a new [`DeviceId`] from a class number, mask, and specific vendor.
    ///
    /// This is more targeted than [`DeviceId::from_class`]: in addition to matching by [`Vendor`],
    /// it also matches the PCI [`Class`] (up to the entire 24 bits, depending on the
    /// [`ClassMask`]).
    #[inline]
    pub const fn from_class_and_vendor(
        class: Class,
        class_mask: ClassMask,
        vendor: Vendor,
    ) -> Self {
        Self(bindings::pci_device_id {
            vendor: vendor.as_raw() as u32,
            device: DeviceId::PCI_ANY_ID,
            subvendor: DeviceId::PCI_ANY_ID,
            subdevice: DeviceId::PCI_ANY_ID,
            class: class.as_raw(),
            class_mask: class_mask.as_raw(),
            driver_data: 0,
            override_only: 0,
        })
    }
}

// SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `pci_device_id` and does not add
// additional invariants, so it's safe to transmute to `RawType`.
unsafe impl RawDeviceId for DeviceId {
    type RawType = bindings::pci_device_id;
}

// SAFETY: `DRIVER_DATA_OFFSET` is the offset to the `driver_data` field.
unsafe impl RawDeviceIdIndex for DeviceId {
    const DRIVER_DATA_OFFSET: usize = core::mem::offset_of!(bindings::pci_device_id, driver_data);

    fn index(&self) -> usize {
        self.0.driver_data
    }
}

/// `IdTable` type for PCI.
pub type IdTable<T> = &'static dyn kernel::device_id::IdTable<DeviceId, T>;

/// Create a PCI `IdTable` with its alias for modpost.
#[macro_export]
macro_rules! pci_device_table {
    ($table_name:ident, $module_table_name:ident, $id_info_type: ty, $table_data: expr) => {
        const $table_name: $crate::device_id::IdArray<
            $crate::pci::DeviceId,
            $id_info_type,
            { $table_data.len() },
        > = $crate::device_id::IdArray::new($table_data);

        $crate::module_device_table!("pci", $module_table_name, $table_name);
    };
}

/// The PCI driver trait.
///
/// # Examples
///
///```
/// # use kernel::{bindings, device::Core, pci};
///
/// struct MyDriver;
///
/// kernel::pci_device_table!(
///     PCI_TABLE,
///     MODULE_PCI_TABLE,
///     <MyDriver as pci::Driver>::IdInfo,
///     [
///         (
///             pci::DeviceId::from_id(pci::Vendor::REDHAT, bindings::PCI_ANY_ID as u32),
///             (),
///         )
///     ]
/// );
///
/// impl pci::Driver for MyDriver {
///     type IdInfo = ();
///     const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;
///
///     fn probe(
///         _pdev: &pci::Device<Core>,
///         _id_info: &Self::IdInfo,
///     ) -> Result<Pin<KBox<Self>>> {
///         Err(ENODEV)
///     }
/// }
///```
/// Drivers must implement this trait in order to get a PCI driver registered. Please refer to the
/// `Adapter` documentation for an example.
pub trait Driver: Send {
    /// The type holding information about each device id supported by the driver.
    // TODO: Use `associated_type_defaults` once stabilized:
    //
    // ```
    // type IdInfo: 'static = ();
    // ```
    type IdInfo: 'static;

    /// The table of device ids supported by the driver.
    const ID_TABLE: IdTable<Self::IdInfo>;

    /// PCI driver probe.
    ///
    /// Called when a new pci device is added or discovered. Implementers should
    /// attempt to initialize the device here.
    fn probe(dev: &Device<device::Core>, id_info: &Self::IdInfo) -> Result<Pin<KBox<Self>>>;

    /// PCI driver unbind.
    ///
    /// Called when a [`Device`] is unbound from its bound [`Driver`]. Implementing this callback
    /// is optional.
    ///
    /// This callback serves as a place for drivers to perform teardown operations that require a
    /// `&Device<Core>` or `&Device<Bound>` reference. For instance, drivers may try to perform I/O
    /// operations to gracefully tear down the device.
    ///
    /// Otherwise, release operations for driver resources should be performed in `Self::drop`.
    fn unbind(dev: &Device<device::Core>, this: Pin<&Self>) {
        let _ = (dev, this);
    }
}

/// The PCI device representation.
///
/// This structure represents the Rust abstraction for a C `struct pci_dev`. The implementation
/// abstracts the usage of an already existing C `struct pci_dev` within Rust code that we get
/// passed from the C side.
///
/// # Invariants
///
/// A [`Device`] instance represents a valid `struct pci_dev` created by the C portion of the
/// kernel.
#[repr(transparent)]
pub struct Device<Ctx: device::DeviceContext = device::Normal>(
    Opaque<bindings::pci_dev>,
    PhantomData<Ctx>,
);

/// A PCI BAR to perform I/O-Operations on.
///
/// # Invariants
///
/// `Bar` always holds an `IoRaw` inststance that holds a valid pointer to the start of the I/O
/// memory mapped PCI bar and its size.
pub struct Bar<const SIZE: usize = 0> {
    pdev: ARef<Device>,
    io: IoRaw<SIZE>,
    num: i32,
}

impl<const SIZE: usize> Bar<SIZE> {
    fn new(pdev: &Device, num: u32, name: &CStr) -> Result<Self> {
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
    /// `ioptr` must be a valid pointer to the memory mapped PCI bar number `num`.
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
    fn index_is_valid(index: u32) -> bool {
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

impl<Ctx: device::DeviceContext> Device<Ctx> {
    #[inline]
    fn as_raw(&self) -> *mut bindings::pci_dev {
        self.0.get()
    }
}

impl Device {
    /// Returns the PCI vendor ID as [`Vendor`].
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::{device::Core, pci::{self, Vendor}, prelude::*};
    /// fn log_device_info(pdev: &pci::Device<Core>) -> Result {
    ///     // Get an instance of `Vendor`.
    ///     let vendor = pdev.vendor_id();
    ///     dev_info!(
    ///         pdev.as_ref(),
    ///         "Device: Vendor={}, Device=0x{:x}\n",
    ///         vendor,
    ///         pdev.device_id()
    ///     );
    ///     Ok(())
    /// }
    /// ```
    #[inline]
    pub fn vendor_id(&self) -> Vendor {
        // SAFETY: `self.as_raw` is a valid pointer to a `struct pci_dev`.
        let vendor_id = unsafe { (*self.as_raw()).vendor };
        Vendor::from_raw(vendor_id)
    }

    /// Returns the PCI device ID.
    #[inline]
    pub fn device_id(&self) -> u16 {
        // SAFETY: By its type invariant `self.as_raw` is always a valid pointer to a
        // `struct pci_dev`.
        unsafe { (*self.as_raw()).device }
    }

    /// Returns the PCI revision ID.
    #[inline]
    pub fn revision_id(&self) -> u8 {
        // SAFETY: By its type invariant `self.as_raw` is always a valid pointer to a
        // `struct pci_dev`.
        unsafe { (*self.as_raw()).revision }
    }

    /// Returns the PCI bus device/function.
    #[inline]
    pub fn dev_id(&self) -> u16 {
        // SAFETY: By its type invariant `self.as_raw` is always a valid pointer to a
        // `struct pci_dev`.
        unsafe { bindings::pci_dev_id(self.as_raw()) }
    }

    /// Returns the PCI subsystem vendor ID.
    #[inline]
    pub fn subsystem_vendor_id(&self) -> u16 {
        // SAFETY: By its type invariant `self.as_raw` is always a valid pointer to a
        // `struct pci_dev`.
        unsafe { (*self.as_raw()).subsystem_vendor }
    }

    /// Returns the PCI subsystem device ID.
    #[inline]
    pub fn subsystem_device_id(&self) -> u16 {
        // SAFETY: By its type invariant `self.as_raw` is always a valid pointer to a
        // `struct pci_dev`.
        unsafe { (*self.as_raw()).subsystem_device }
    }

    /// Returns the start of the given PCI bar resource.
    pub fn resource_start(&self, bar: u32) -> Result<bindings::resource_size_t> {
        if !Bar::index_is_valid(bar) {
            return Err(EINVAL);
        }

        // SAFETY:
        // - `bar` is a valid bar number, as guaranteed by the above call to `Bar::index_is_valid`,
        // - by its type invariant `self.as_raw` is always a valid pointer to a `struct pci_dev`.
        Ok(unsafe { bindings::pci_resource_start(self.as_raw(), bar.try_into()?) })
    }

    /// Returns the size of the given PCI bar resource.
    pub fn resource_len(&self, bar: u32) -> Result<bindings::resource_size_t> {
        if !Bar::index_is_valid(bar) {
            return Err(EINVAL);
        }

        // SAFETY:
        // - `bar` is a valid bar number, as guaranteed by the above call to `Bar::index_is_valid`,
        // - by its type invariant `self.as_raw` is always a valid pointer to a `struct pci_dev`.
        Ok(unsafe { bindings::pci_resource_len(self.as_raw(), bar.try_into()?) })
    }

    /// Returns the PCI class as a `Class` struct.
    #[inline]
    pub fn pci_class(&self) -> Class {
        // SAFETY: `self.as_raw` is a valid pointer to a `struct pci_dev`.
        Class::from_raw(unsafe { (*self.as_raw()).class })
    }
}

impl Device<device::Bound> {
    /// Mapps an entire PCI-BAR after performing a region-request on it. I/O operation bound checks
    /// can be performed on compile time for offsets (plus the requested type size) < SIZE.
    pub fn iomap_region_sized<'a, const SIZE: usize>(
        &'a self,
        bar: u32,
        name: &'a CStr,
    ) -> impl PinInit<Devres<Bar<SIZE>>, Error> + 'a {
        Devres::new(self.as_ref(), Bar::<SIZE>::new(self, bar, name))
    }

    /// Mapps an entire PCI-BAR after performing a region-request on it.
    pub fn iomap_region<'a>(
        &'a self,
        bar: u32,
        name: &'a CStr,
    ) -> impl PinInit<Devres<Bar>, Error> + 'a {
        self.iomap_region_sized::<0>(bar, name)
    }

    /// Returns an [`IrqRequest`] for the IRQ vector at the given index, if any.
    pub fn irq_vector(&self, index: u32) -> Result<IrqRequest<'_>> {
        // SAFETY: `self.as_raw` returns a valid pointer to a `struct pci_dev`.
        let irq = unsafe { crate::bindings::pci_irq_vector(self.as_raw(), index) };
        if irq < 0 {
            return Err(crate::error::Error::from_errno(irq));
        }
        // SAFETY: `irq` is guaranteed to be a valid IRQ number for `&self`.
        Ok(unsafe { IrqRequest::new(self.as_ref(), irq as u32) })
    }

    /// Returns a [`kernel::irq::Registration`] for the IRQ vector at the given
    /// index.
    pub fn request_irq<'a, T: crate::irq::Handler + 'static>(
        &'a self,
        index: u32,
        flags: irq::Flags,
        name: &'static CStr,
        handler: impl PinInit<T, Error> + 'a,
    ) -> Result<impl PinInit<irq::Registration<T>, Error> + 'a> {
        let request = self.irq_vector(index)?;

        Ok(irq::Registration::<T>::new(request, flags, name, handler))
    }

    /// Returns a [`kernel::irq::ThreadedRegistration`] for the IRQ vector at
    /// the given index.
    pub fn request_threaded_irq<'a, T: crate::irq::ThreadedHandler + 'static>(
        &'a self,
        index: u32,
        flags: irq::Flags,
        name: &'static CStr,
        handler: impl PinInit<T, Error> + 'a,
    ) -> Result<impl PinInit<irq::ThreadedRegistration<T>, Error> + 'a> {
        let request = self.irq_vector(index)?;

        Ok(irq::ThreadedRegistration::<T>::new(
            request, flags, name, handler,
        ))
    }
}

impl Device<device::Core> {
    /// Enable memory resources for this device.
    pub fn enable_device_mem(&self) -> Result {
        // SAFETY: `self.as_raw` is guaranteed to be a pointer to a valid `struct pci_dev`.
        to_result(unsafe { bindings::pci_enable_device_mem(self.as_raw()) })
    }

    /// Enable bus-mastering for this device.
    #[inline]
    pub fn set_master(&self) {
        // SAFETY: `self.as_raw` is guaranteed to be a pointer to a valid `struct pci_dev`.
        unsafe { bindings::pci_set_master(self.as_raw()) };
    }
}

// SAFETY: `Device` is a transparent wrapper of a type that doesn't depend on `Device`'s generic
// argument.
kernel::impl_device_context_deref!(unsafe { Device });
kernel::impl_device_context_into_aref!(Device);

impl crate::dma::Device for Device<device::Core> {}

// SAFETY: Instances of `Device` are always reference-counted.
unsafe impl crate::sync::aref::AlwaysRefCounted for Device {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the refcount is non-zero.
        unsafe { bindings::pci_dev_get(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::pci_dev_put(obj.cast().as_ptr()) }
    }
}

impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Device<Ctx> {
    fn as_ref(&self) -> &device::Device<Ctx> {
        // SAFETY: By the type invariant of `Self`, `self.as_raw()` is a pointer to a valid
        // `struct pci_dev`.
        let dev = unsafe { addr_of_mut!((*self.as_raw()).dev) };

        // SAFETY: `dev` points to a valid `struct device`.
        unsafe { device::Device::from_raw(dev) }
    }
}

impl<Ctx: device::DeviceContext> TryFrom<&device::Device<Ctx>> for &Device<Ctx> {
    type Error = kernel::error::Error;

    fn try_from(dev: &device::Device<Ctx>) -> Result<Self, Self::Error> {
        // SAFETY: By the type invariant of `Device`, `dev.as_raw()` is a valid pointer to a
        // `struct device`.
        if !unsafe { bindings::dev_is_pci(dev.as_raw()) } {
            return Err(EINVAL);
        }

        // SAFETY: We've just verified that the bus type of `dev` equals `bindings::pci_bus_type`,
        // hence `dev` must be embedded in a valid `struct pci_dev` as guaranteed by the
        // corresponding C code.
        let pdev = unsafe { container_of!(dev.as_raw(), bindings::pci_dev, dev) };

        // SAFETY: `pdev` is a valid pointer to a `struct pci_dev`.
        Ok(unsafe { &*pdev.cast() })
    }
}

// SAFETY: A `Device` is always reference-counted and can be released from any thread.
unsafe impl Send for Device {}

// SAFETY: `Device` can be shared among threads because all methods of `Device`
// (i.e. `Device<Normal>) are thread safe.
unsafe impl Sync for Device {}
