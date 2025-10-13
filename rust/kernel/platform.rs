// SPDX-License-Identifier: GPL-2.0

//! Abstractions for the platform bus.
//!
//! C header: [`include/linux/platform_device.h`](srctree/include/linux/platform_device.h)

use crate::{
    acpi, bindings, container_of,
    device::{self, Bound},
    driver,
    error::{from_result, to_result, Result},
    io::{mem::IoRequest, Resource},
    irq::{self, IrqRequest},
    of,
    prelude::*,
    types::Opaque,
    ThisModule,
};

use core::{
    marker::PhantomData,
    ptr::{addr_of_mut, NonNull},
};

/// An adapter for the registration of platform drivers.
pub struct Adapter<T: Driver>(T);

// SAFETY: A call to `unregister` for a given instance of `RegType` is guaranteed to be valid if
// a preceding call to `register` has been successful.
unsafe impl<T: Driver + 'static> driver::RegistrationOps for Adapter<T> {
    type RegType = bindings::platform_driver;

    unsafe fn register(
        pdrv: &Opaque<Self::RegType>,
        name: &'static CStr,
        module: &'static ThisModule,
    ) -> Result {
        let of_table = match T::OF_ID_TABLE {
            Some(table) => table.as_ptr(),
            None => core::ptr::null(),
        };

        let acpi_table = match T::ACPI_ID_TABLE {
            Some(table) => table.as_ptr(),
            None => core::ptr::null(),
        };

        // SAFETY: It's safe to set the fields of `struct platform_driver` on initialization.
        unsafe {
            (*pdrv.get()).driver.name = name.as_char_ptr();
            (*pdrv.get()).probe = Some(Self::probe_callback);
            (*pdrv.get()).remove = Some(Self::remove_callback);
            (*pdrv.get()).driver.of_match_table = of_table;
            (*pdrv.get()).driver.acpi_match_table = acpi_table;
        }

        // SAFETY: `pdrv` is guaranteed to be a valid `RegType`.
        to_result(unsafe { bindings::__platform_driver_register(pdrv.get(), module.0) })
    }

    unsafe fn unregister(pdrv: &Opaque<Self::RegType>) {
        // SAFETY: `pdrv` is guaranteed to be a valid `RegType`.
        unsafe { bindings::platform_driver_unregister(pdrv.get()) };
    }
}

impl<T: Driver + 'static> Adapter<T> {
    extern "C" fn probe_callback(pdev: *mut bindings::platform_device) -> kernel::ffi::c_int {
        // SAFETY: The platform bus only ever calls the probe callback with a valid pointer to a
        // `struct platform_device`.
        //
        // INVARIANT: `pdev` is valid for the duration of `probe_callback()`.
        let pdev = unsafe { &*pdev.cast::<Device<device::CoreInternal>>() };
        let info = <Self as driver::Adapter>::id_info(pdev.as_ref());

        from_result(|| {
            let data = T::probe(pdev, info)?;

            pdev.as_ref().set_drvdata(data);
            Ok(0)
        })
    }

    extern "C" fn remove_callback(pdev: *mut bindings::platform_device) {
        // SAFETY: The platform bus only ever calls the remove callback with a valid pointer to a
        // `struct platform_device`.
        //
        // INVARIANT: `pdev` is valid for the duration of `probe_callback()`.
        let pdev = unsafe { &*pdev.cast::<Device<device::CoreInternal>>() };

        // SAFETY: `remove_callback` is only ever called after a successful call to
        // `probe_callback`, hence it's guaranteed that `Device::set_drvdata()` has been called
        // and stored a `Pin<KBox<T>>`.
        let data = unsafe { pdev.as_ref().drvdata_obtain::<Pin<KBox<T>>>() };

        T::unbind(pdev, data.as_ref());
    }
}

impl<T: Driver + 'static> driver::Adapter for Adapter<T> {
    type IdInfo = T::IdInfo;

    fn of_id_table() -> Option<of::IdTable<Self::IdInfo>> {
        T::OF_ID_TABLE
    }

    fn acpi_id_table() -> Option<acpi::IdTable<Self::IdInfo>> {
        T::ACPI_ID_TABLE
    }
}

/// Declares a kernel module that exposes a single platform driver.
///
/// # Examples
///
/// ```ignore
/// kernel::module_platform_driver! {
///     type: MyDriver,
///     name: "Module name",
///     authors: ["Author name"],
///     description: "Description",
///     license: "GPL v2",
/// }
/// ```
#[macro_export]
macro_rules! module_platform_driver {
    ($($f:tt)*) => {
        $crate::module_driver!(<T>, $crate::platform::Adapter<T>, { $($f)* });
    };
}

/// The platform driver trait.
///
/// Drivers must implement this trait in order to get a platform driver registered.
///
/// # Examples
///
///```
/// # use kernel::{acpi, bindings, c_str, device::Core, of, platform};
///
/// struct MyDriver;
///
/// kernel::of_device_table!(
///     OF_TABLE,
///     MODULE_OF_TABLE,
///     <MyDriver as platform::Driver>::IdInfo,
///     [
///         (of::DeviceId::new(c_str!("test,device")), ())
///     ]
/// );
///
/// kernel::acpi_device_table!(
///     ACPI_TABLE,
///     MODULE_ACPI_TABLE,
///     <MyDriver as platform::Driver>::IdInfo,
///     [
///         (acpi::DeviceId::new(c_str!("LNUXBEEF")), ())
///     ]
/// );
///
/// impl platform::Driver for MyDriver {
///     type IdInfo = ();
///     const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);
///     const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);
///
///     fn probe(
///         _pdev: &platform::Device<Core>,
///         _id_info: Option<&Self::IdInfo>,
///     ) -> Result<Pin<KBox<Self>>> {
///         Err(ENODEV)
///     }
/// }
///```
pub trait Driver: Send {
    /// The type holding driver private data about each device id supported by the driver.
    // TODO: Use associated_type_defaults once stabilized:
    //
    // ```
    // type IdInfo: 'static = ();
    // ```
    type IdInfo: 'static;

    /// The table of OF device ids supported by the driver.
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = None;

    /// The table of ACPI device ids supported by the driver.
    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = None;

    /// Platform driver probe.
    ///
    /// Called when a new platform device is added or discovered.
    /// Implementers should attempt to initialize the device here.
    fn probe(dev: &Device<device::Core>, id_info: Option<&Self::IdInfo>)
        -> Result<Pin<KBox<Self>>>;

    /// Platform driver unbind.
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

/// The platform device representation.
///
/// This structure represents the Rust abstraction for a C `struct platform_device`. The
/// implementation abstracts the usage of an already existing C `struct platform_device` within Rust
/// code that we get passed from the C side.
///
/// # Invariants
///
/// A [`Device`] instance represents a valid `struct platform_device` created by the C portion of
/// the kernel.
#[repr(transparent)]
pub struct Device<Ctx: device::DeviceContext = device::Normal>(
    Opaque<bindings::platform_device>,
    PhantomData<Ctx>,
);

impl<Ctx: device::DeviceContext> Device<Ctx> {
    fn as_raw(&self) -> *mut bindings::platform_device {
        self.0.get()
    }

    /// Returns the resource at `index`, if any.
    pub fn resource_by_index(&self, index: u32) -> Option<&Resource> {
        // SAFETY: `self.as_raw()` returns a valid pointer to a `struct platform_device`.
        let resource = unsafe {
            bindings::platform_get_resource(self.as_raw(), bindings::IORESOURCE_MEM, index)
        };

        if resource.is_null() {
            return None;
        }

        // SAFETY: `resource` is a valid pointer to a `struct resource` as
        // returned by `platform_get_resource`.
        Some(unsafe { Resource::from_raw(resource) })
    }

    /// Returns the resource with a given `name`, if any.
    pub fn resource_by_name(&self, name: &CStr) -> Option<&Resource> {
        // SAFETY: `self.as_raw()` returns a valid pointer to a `struct
        // platform_device` and `name` points to a valid C string.
        let resource = unsafe {
            bindings::platform_get_resource_byname(
                self.as_raw(),
                bindings::IORESOURCE_MEM,
                name.as_char_ptr(),
            )
        };

        if resource.is_null() {
            return None;
        }

        // SAFETY: `resource` is a valid pointer to a `struct resource` as
        // returned by `platform_get_resource`.
        Some(unsafe { Resource::from_raw(resource) })
    }
}

impl Device<Bound> {
    /// Returns an `IoRequest` for the resource at `index`, if any.
    pub fn io_request_by_index(&self, index: u32) -> Option<IoRequest<'_>> {
        self.resource_by_index(index)
            // SAFETY: `resource` is a valid resource for `&self` during the
            // lifetime of the `IoRequest`.
            .map(|resource| unsafe { IoRequest::new(self.as_ref(), resource) })
    }

    /// Returns an `IoRequest` for the resource with a given `name`, if any.
    pub fn io_request_by_name(&self, name: &CStr) -> Option<IoRequest<'_>> {
        self.resource_by_name(name)
            // SAFETY: `resource` is a valid resource for `&self` during the
            // lifetime of the `IoRequest`.
            .map(|resource| unsafe { IoRequest::new(self.as_ref(), resource) })
    }
}

macro_rules! define_irq_accessor_by_index {
    (
        $(#[$meta:meta])* $fn_name:ident,
        $request_fn:ident,
        $reg_type:ident,
        $handler_trait:ident
    ) => {
        $(#[$meta])*
        pub fn $fn_name<'a, T: irq::$handler_trait + 'static>(
            &'a self,
            flags: irq::Flags,
            index: u32,
            name: &'static CStr,
            handler: impl PinInit<T, Error> + 'a,
        ) -> Result<impl PinInit<irq::$reg_type<T>, Error> + 'a> {
            let request = self.$request_fn(index)?;

            Ok(irq::$reg_type::<T>::new(
                request,
                flags,
                name,
                handler,
            ))
        }
    };
}

macro_rules! define_irq_accessor_by_name {
    (
        $(#[$meta:meta])* $fn_name:ident,
        $request_fn:ident,
        $reg_type:ident,
        $handler_trait:ident
    ) => {
        $(#[$meta])*
        pub fn $fn_name<'a, T: irq::$handler_trait + 'static>(
            &'a self,
            flags: irq::Flags,
            irq_name: &CStr,
            name: &'static CStr,
            handler: impl PinInit<T, Error> + 'a,
        ) -> Result<impl PinInit<irq::$reg_type<T>, Error> + 'a> {
            let request = self.$request_fn(irq_name)?;

            Ok(irq::$reg_type::<T>::new(
                request,
                flags,
                name,
                handler,
            ))
        }
    };
}

impl Device<Bound> {
    /// Returns an [`IrqRequest`] for the IRQ at the given index, if any.
    pub fn irq_by_index(&self, index: u32) -> Result<IrqRequest<'_>> {
        // SAFETY: `self.as_raw` returns a valid pointer to a `struct platform_device`.
        let irq = unsafe { bindings::platform_get_irq(self.as_raw(), index) };

        if irq < 0 {
            return Err(Error::from_errno(irq));
        }

        // SAFETY: `irq` is guaranteed to be a valid IRQ number for `&self`.
        Ok(unsafe { IrqRequest::new(self.as_ref(), irq as u32) })
    }

    /// Returns an [`IrqRequest`] for the IRQ at the given index, but does not
    /// print an error if the IRQ cannot be obtained.
    pub fn optional_irq_by_index(&self, index: u32) -> Result<IrqRequest<'_>> {
        // SAFETY: `self.as_raw` returns a valid pointer to a `struct platform_device`.
        let irq = unsafe { bindings::platform_get_irq_optional(self.as_raw(), index) };

        if irq < 0 {
            return Err(Error::from_errno(irq));
        }

        // SAFETY: `irq` is guaranteed to be a valid IRQ number for `&self`.
        Ok(unsafe { IrqRequest::new(self.as_ref(), irq as u32) })
    }

    /// Returns an [`IrqRequest`] for the IRQ with the given name, if any.
    pub fn irq_by_name(&self, name: &CStr) -> Result<IrqRequest<'_>> {
        // SAFETY: `self.as_raw` returns a valid pointer to a `struct platform_device`.
        let irq = unsafe { bindings::platform_get_irq_byname(self.as_raw(), name.as_char_ptr()) };

        if irq < 0 {
            return Err(Error::from_errno(irq));
        }

        // SAFETY: `irq` is guaranteed to be a valid IRQ number for `&self`.
        Ok(unsafe { IrqRequest::new(self.as_ref(), irq as u32) })
    }

    /// Returns an [`IrqRequest`] for the IRQ with the given name, but does not
    /// print an error if the IRQ cannot be obtained.
    pub fn optional_irq_by_name(&self, name: &CStr) -> Result<IrqRequest<'_>> {
        // SAFETY: `self.as_raw` returns a valid pointer to a `struct platform_device`.
        let irq = unsafe {
            bindings::platform_get_irq_byname_optional(self.as_raw(), name.as_char_ptr())
        };

        if irq < 0 {
            return Err(Error::from_errno(irq));
        }

        // SAFETY: `irq` is guaranteed to be a valid IRQ number for `&self`.
        Ok(unsafe { IrqRequest::new(self.as_ref(), irq as u32) })
    }

    define_irq_accessor_by_index!(
        /// Returns a [`irq::Registration`] for the IRQ at the given index.
        request_irq_by_index,
        irq_by_index,
        Registration,
        Handler
    );
    define_irq_accessor_by_name!(
        /// Returns a [`irq::Registration`] for the IRQ with the given name.
        request_irq_by_name,
        irq_by_name,
        Registration,
        Handler
    );
    define_irq_accessor_by_index!(
        /// Does the same as [`Self::request_irq_by_index`], except that it does
        /// not print an error message if the IRQ cannot be obtained.
        request_optional_irq_by_index,
        optional_irq_by_index,
        Registration,
        Handler
    );
    define_irq_accessor_by_name!(
        /// Does the same as [`Self::request_irq_by_name`], except that it does
        /// not print an error message if the IRQ cannot be obtained.
        request_optional_irq_by_name,
        optional_irq_by_name,
        Registration,
        Handler
    );

    define_irq_accessor_by_index!(
        /// Returns a [`irq::ThreadedRegistration`] for the IRQ at the given index.
        request_threaded_irq_by_index,
        irq_by_index,
        ThreadedRegistration,
        ThreadedHandler
    );
    define_irq_accessor_by_name!(
        /// Returns a [`irq::ThreadedRegistration`] for the IRQ with the given name.
        request_threaded_irq_by_name,
        irq_by_name,
        ThreadedRegistration,
        ThreadedHandler
    );
    define_irq_accessor_by_index!(
        /// Does the same as [`Self::request_threaded_irq_by_index`], except
        /// that it does not print an error message if the IRQ cannot be
        /// obtained.
        request_optional_threaded_irq_by_index,
        optional_irq_by_index,
        ThreadedRegistration,
        ThreadedHandler
    );
    define_irq_accessor_by_name!(
        /// Does the same as [`Self::request_threaded_irq_by_name`], except that
        /// it does not print an error message if the IRQ cannot be obtained.
        request_optional_threaded_irq_by_name,
        optional_irq_by_name,
        ThreadedRegistration,
        ThreadedHandler
    );
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
        unsafe { bindings::get_device(self.as_ref().as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::platform_device_put(obj.cast().as_ptr()) }
    }
}

impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Device<Ctx> {
    fn as_ref(&self) -> &device::Device<Ctx> {
        // SAFETY: By the type invariant of `Self`, `self.as_raw()` is a pointer to a valid
        // `struct platform_device`.
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
        if !unsafe { bindings::dev_is_platform(dev.as_raw()) } {
            return Err(EINVAL);
        }

        // SAFETY: We've just verified that the bus type of `dev` equals
        // `bindings::platform_bus_type`, hence `dev` must be embedded in a valid
        // `struct platform_device` as guaranteed by the corresponding C code.
        let pdev = unsafe { container_of!(dev.as_raw(), bindings::platform_device, dev) };

        // SAFETY: `pdev` is a valid pointer to a `struct platform_device`.
        Ok(unsafe { &*pdev.cast() })
    }
}

// SAFETY: A `Device` is always reference-counted and can be released from any thread.
unsafe impl Send for Device {}

// SAFETY: `Device` can be shared among threads because all methods of `Device`
// (i.e. `Device<Normal>) are thread safe.
unsafe impl Sync for Device {}
