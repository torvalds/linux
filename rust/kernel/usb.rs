// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (C) 2025 Collabora Ltd.

//! Abstractions for the USB bus.
//!
//! C header: [`include/linux/usb.h`](srctree/include/linux/usb.h)

use crate::{
    bindings, device,
    device_id::{RawDeviceId, RawDeviceIdIndex},
    driver,
    error::{from_result, to_result, Result},
    prelude::*,
    str::CStr,
    types::{AlwaysRefCounted, Opaque},
    ThisModule,
};
use core::{marker::PhantomData, mem::MaybeUninit, ptr::NonNull};

/// An adapter for the registration of USB drivers.
pub struct Adapter<T: Driver>(T);

// SAFETY: A call to `unregister` for a given instance of `RegType` is guaranteed to be valid if
// a preceding call to `register` has been successful.
unsafe impl<T: Driver + 'static> driver::RegistrationOps for Adapter<T> {
    type RegType = bindings::usb_driver;

    unsafe fn register(
        udrv: &Opaque<Self::RegType>,
        name: &'static CStr,
        module: &'static ThisModule,
    ) -> Result {
        // SAFETY: It's safe to set the fields of `struct usb_driver` on initialization.
        unsafe {
            (*udrv.get()).name = name.as_char_ptr();
            (*udrv.get()).probe = Some(Self::probe_callback);
            (*udrv.get()).disconnect = Some(Self::disconnect_callback);
            (*udrv.get()).id_table = T::ID_TABLE.as_ptr();
        }

        // SAFETY: `udrv` is guaranteed to be a valid `RegType`.
        to_result(unsafe {
            bindings::usb_register_driver(udrv.get(), module.0, name.as_char_ptr())
        })
    }

    unsafe fn unregister(udrv: &Opaque<Self::RegType>) {
        // SAFETY: `udrv` is guaranteed to be a valid `RegType`.
        unsafe { bindings::usb_deregister(udrv.get()) };
    }
}

impl<T: Driver + 'static> Adapter<T> {
    extern "C" fn probe_callback(
        intf: *mut bindings::usb_interface,
        id: *const bindings::usb_device_id,
    ) -> kernel::ffi::c_int {
        // SAFETY: The USB core only ever calls the probe callback with a valid pointer to a
        // `struct usb_interface` and `struct usb_device_id`.
        //
        // INVARIANT: `intf` is valid for the duration of `probe_callback()`.
        let intf = unsafe { &*intf.cast::<Interface<device::CoreInternal>>() };

        from_result(|| {
            // SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `struct usb_device_id` and
            // does not add additional invariants, so it's safe to transmute.
            let id = unsafe { &*id.cast::<DeviceId>() };

            let info = T::ID_TABLE.info(id.index());
            let data = T::probe(intf, id, info)?;

            let dev: &device::Device<device::CoreInternal> = intf.as_ref();
            dev.set_drvdata(data);
            Ok(0)
        })
    }

    extern "C" fn disconnect_callback(intf: *mut bindings::usb_interface) {
        // SAFETY: The USB core only ever calls the disconnect callback with a valid pointer to a
        // `struct usb_interface`.
        //
        // INVARIANT: `intf` is valid for the duration of `disconnect_callback()`.
        let intf = unsafe { &*intf.cast::<Interface<device::CoreInternal>>() };

        let dev: &device::Device<device::CoreInternal> = intf.as_ref();

        // SAFETY: `disconnect_callback` is only ever called after a successful call to
        // `probe_callback`, hence it's guaranteed that `Device::set_drvdata()` has been called
        // and stored a `Pin<KBox<T>>`.
        let data = unsafe { dev.drvdata_obtain::<Pin<KBox<T>>>() };

        T::disconnect(intf, data.as_ref());
    }
}

/// Abstraction for the USB device ID structure, i.e. [`struct usb_device_id`].
///
/// [`struct usb_device_id`]: https://docs.kernel.org/driver-api/basics.html#c.usb_device_id
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct DeviceId(bindings::usb_device_id);

impl DeviceId {
    /// Equivalent to C's `USB_DEVICE` macro.
    pub const fn from_id(vendor: u16, product: u16) -> Self {
        Self(bindings::usb_device_id {
            match_flags: bindings::USB_DEVICE_ID_MATCH_DEVICE as u16,
            idVendor: vendor,
            idProduct: product,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }

    /// Equivalent to C's `USB_DEVICE_VER` macro.
    pub const fn from_device_ver(vendor: u16, product: u16, bcd_lo: u16, bcd_hi: u16) -> Self {
        Self(bindings::usb_device_id {
            match_flags: bindings::USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION as u16,
            idVendor: vendor,
            idProduct: product,
            bcdDevice_lo: bcd_lo,
            bcdDevice_hi: bcd_hi,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }

    /// Equivalent to C's `USB_DEVICE_INFO` macro.
    pub const fn from_device_info(class: u8, subclass: u8, protocol: u8) -> Self {
        Self(bindings::usb_device_id {
            match_flags: bindings::USB_DEVICE_ID_MATCH_DEV_INFO as u16,
            bDeviceClass: class,
            bDeviceSubClass: subclass,
            bDeviceProtocol: protocol,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }

    /// Equivalent to C's `USB_INTERFACE_INFO` macro.
    pub const fn from_interface_info(class: u8, subclass: u8, protocol: u8) -> Self {
        Self(bindings::usb_device_id {
            match_flags: bindings::USB_DEVICE_ID_MATCH_INT_INFO as u16,
            bInterfaceClass: class,
            bInterfaceSubClass: subclass,
            bInterfaceProtocol: protocol,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }

    /// Equivalent to C's `USB_DEVICE_INTERFACE_CLASS` macro.
    pub const fn from_device_interface_class(vendor: u16, product: u16, class: u8) -> Self {
        Self(bindings::usb_device_id {
            match_flags: (bindings::USB_DEVICE_ID_MATCH_DEVICE
                | bindings::USB_DEVICE_ID_MATCH_INT_CLASS) as u16,
            idVendor: vendor,
            idProduct: product,
            bInterfaceClass: class,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }

    /// Equivalent to C's `USB_DEVICE_INTERFACE_PROTOCOL` macro.
    pub const fn from_device_interface_protocol(vendor: u16, product: u16, protocol: u8) -> Self {
        Self(bindings::usb_device_id {
            match_flags: (bindings::USB_DEVICE_ID_MATCH_DEVICE
                | bindings::USB_DEVICE_ID_MATCH_INT_PROTOCOL) as u16,
            idVendor: vendor,
            idProduct: product,
            bInterfaceProtocol: protocol,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }

    /// Equivalent to C's `USB_DEVICE_INTERFACE_NUMBER` macro.
    pub const fn from_device_interface_number(vendor: u16, product: u16, number: u8) -> Self {
        Self(bindings::usb_device_id {
            match_flags: (bindings::USB_DEVICE_ID_MATCH_DEVICE
                | bindings::USB_DEVICE_ID_MATCH_INT_NUMBER) as u16,
            idVendor: vendor,
            idProduct: product,
            bInterfaceNumber: number,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }

    /// Equivalent to C's `USB_DEVICE_AND_INTERFACE_INFO` macro.
    pub const fn from_device_and_interface_info(
        vendor: u16,
        product: u16,
        class: u8,
        subclass: u8,
        protocol: u8,
    ) -> Self {
        Self(bindings::usb_device_id {
            match_flags: (bindings::USB_DEVICE_ID_MATCH_INT_INFO
                | bindings::USB_DEVICE_ID_MATCH_DEVICE) as u16,
            idVendor: vendor,
            idProduct: product,
            bInterfaceClass: class,
            bInterfaceSubClass: subclass,
            bInterfaceProtocol: protocol,
            // SAFETY: It is safe to use all zeroes for the other fields of `usb_device_id`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        })
    }
}

// SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `usb_device_id` and does not add
// additional invariants, so it's safe to transmute to `RawType`.
unsafe impl RawDeviceId for DeviceId {
    type RawType = bindings::usb_device_id;
}

// SAFETY: `DRIVER_DATA_OFFSET` is the offset to the `driver_info` field.
unsafe impl RawDeviceIdIndex for DeviceId {
    const DRIVER_DATA_OFFSET: usize = core::mem::offset_of!(bindings::usb_device_id, driver_info);

    fn index(&self) -> usize {
        self.0.driver_info
    }
}

/// [`IdTable`](kernel::device_id::IdTable) type for USB.
pub type IdTable<T> = &'static dyn kernel::device_id::IdTable<DeviceId, T>;

/// Create a USB `IdTable` with its alias for modpost.
#[macro_export]
macro_rules! usb_device_table {
    ($table_name:ident, $module_table_name:ident, $id_info_type: ty, $table_data: expr) => {
        const $table_name: $crate::device_id::IdArray<
            $crate::usb::DeviceId,
            $id_info_type,
            { $table_data.len() },
        > = $crate::device_id::IdArray::new($table_data);

        $crate::module_device_table!("usb", $module_table_name, $table_name);
    };
}

/// The USB driver trait.
///
/// # Examples
///
///```
/// # use kernel::{bindings, device::Core, usb};
/// use kernel::prelude::*;
///
/// struct MyDriver;
///
/// kernel::usb_device_table!(
///     USB_TABLE,
///     MODULE_USB_TABLE,
///     <MyDriver as usb::Driver>::IdInfo,
///     [
///         (usb::DeviceId::from_id(0x1234, 0x5678), ()),
///         (usb::DeviceId::from_id(0xabcd, 0xef01), ()),
///     ]
/// );
///
/// impl usb::Driver for MyDriver {
///     type IdInfo = ();
///     const ID_TABLE: usb::IdTable<Self::IdInfo> = &USB_TABLE;
///
///     fn probe(
///         _interface: &usb::Interface<Core>,
///         _id: &usb::DeviceId,
///         _info: &Self::IdInfo,
///     ) -> Result<Pin<KBox<Self>>> {
///         Err(ENODEV)
///     }
///
///     fn disconnect(_interface: &usb::Interface<Core>, _data: Pin<&Self>) {}
/// }
///```
pub trait Driver {
    /// The type holding information about each one of the device ids supported by the driver.
    type IdInfo: 'static;

    /// The table of device ids supported by the driver.
    const ID_TABLE: IdTable<Self::IdInfo>;

    /// USB driver probe.
    ///
    /// Called when a new USB interface is bound to this driver.
    /// Implementers should attempt to initialize the interface here.
    fn probe(
        interface: &Interface<device::Core>,
        id: &DeviceId,
        id_info: &Self::IdInfo,
    ) -> Result<Pin<KBox<Self>>>;

    /// USB driver disconnect.
    ///
    /// Called when the USB interface is about to be unbound from this driver.
    fn disconnect(interface: &Interface<device::Core>, data: Pin<&Self>);
}

/// A USB interface.
///
/// This structure represents the Rust abstraction for a C [`struct usb_interface`].
/// The implementation abstracts the usage of a C [`struct usb_interface`] passed
/// in from the C side.
///
/// # Invariants
///
/// An [`Interface`] instance represents a valid [`struct usb_interface`] created
/// by the C portion of the kernel.
///
/// [`struct usb_interface`]: https://www.kernel.org/doc/html/latest/driver-api/usb/usb.html#c.usb_interface
#[repr(transparent)]
pub struct Interface<Ctx: device::DeviceContext = device::Normal>(
    Opaque<bindings::usb_interface>,
    PhantomData<Ctx>,
);

impl<Ctx: device::DeviceContext> Interface<Ctx> {
    fn as_raw(&self) -> *mut bindings::usb_interface {
        self.0.get()
    }
}

// SAFETY: `Interface` is a transparent wrapper of a type that doesn't depend on
// `Interface`'s generic argument.
kernel::impl_device_context_deref!(unsafe { Interface });
kernel::impl_device_context_into_aref!(Interface);

impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Interface<Ctx> {
    fn as_ref(&self) -> &device::Device<Ctx> {
        // SAFETY: By the type invariant of `Self`, `self.as_raw()` is a pointer to a valid
        // `struct usb_interface`.
        let dev = unsafe { &raw mut ((*self.as_raw()).dev) };

        // SAFETY: `dev` points to a valid `struct device`.
        unsafe { device::Device::from_raw(dev) }
    }
}

impl<Ctx: device::DeviceContext> AsRef<Device> for Interface<Ctx> {
    fn as_ref(&self) -> &Device {
        // SAFETY: `self.as_raw()` is valid by the type invariants.
        let usb_dev = unsafe { bindings::interface_to_usbdev(self.as_raw()) };

        // SAFETY: For a valid `struct usb_interface` pointer, the above call to
        // `interface_to_usbdev()` guarantees to return a valid pointer to a `struct usb_device`.
        unsafe { &*(usb_dev.cast()) }
    }
}

// SAFETY: Instances of `Interface` are always reference-counted.
unsafe impl AlwaysRefCounted for Interface {
    fn inc_ref(&self) {
        // SAFETY: The invariants of `Interface` guarantee that `self.as_raw()`
        // returns a valid `struct usb_interface` pointer, for which we will
        // acquire a new refcount.
        unsafe { bindings::usb_get_intf(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::usb_put_intf(obj.cast().as_ptr()) }
    }
}

// SAFETY: A `Interface` is always reference-counted and can be released from any thread.
unsafe impl Send for Interface {}

// SAFETY: It is safe to send a &Interface to another thread because we do not
// allow any mutation through a shared reference.
unsafe impl Sync for Interface {}

/// A USB device.
///
/// This structure represents the Rust abstraction for a C [`struct usb_device`].
/// The implementation abstracts the usage of a C [`struct usb_device`] passed in
/// from the C side.
///
/// # Invariants
///
/// A [`Device`] instance represents a valid [`struct usb_device`] created by the C portion of the
/// kernel.
///
/// [`struct usb_device`]: https://www.kernel.org/doc/html/latest/driver-api/usb/usb.html#c.usb_device
#[repr(transparent)]
struct Device<Ctx: device::DeviceContext = device::Normal>(
    Opaque<bindings::usb_device>,
    PhantomData<Ctx>,
);

impl<Ctx: device::DeviceContext> Device<Ctx> {
    fn as_raw(&self) -> *mut bindings::usb_device {
        self.0.get()
    }
}

// SAFETY: `Device` is a transparent wrapper of a type that doesn't depend on `Device`'s generic
// argument.
kernel::impl_device_context_deref!(unsafe { Device });
kernel::impl_device_context_into_aref!(Device);

// SAFETY: Instances of `Device` are always reference-counted.
unsafe impl AlwaysRefCounted for Device {
    fn inc_ref(&self) {
        // SAFETY: The invariants of `Device` guarantee that `self.as_raw()`
        // returns a valid `struct usb_device` pointer, for which we will
        // acquire a new refcount.
        unsafe { bindings::usb_get_dev(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::usb_put_dev(obj.cast().as_ptr()) }
    }
}

impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Device<Ctx> {
    fn as_ref(&self) -> &device::Device<Ctx> {
        // SAFETY: By the type invariant of `Self`, `self.as_raw()` is a pointer to a valid
        // `struct usb_device`.
        let dev = unsafe { &raw mut ((*self.as_raw()).dev) };

        // SAFETY: `dev` points to a valid `struct device`.
        unsafe { device::Device::from_raw(dev) }
    }
}

// SAFETY: A `Device` is always reference-counted and can be released from any thread.
unsafe impl Send for Device {}

// SAFETY: It is safe to send a &Device to another thread because we do not
// allow any mutation through a shared reference.
unsafe impl Sync for Device {}

/// Declares a kernel module that exposes a single USB driver.
///
/// # Examples
///
/// ```ignore
/// module_usb_driver! {
///     type: MyDriver,
///     name: "Module name",
///     author: ["Author name"],
///     description: "Description",
///     license: "GPL v2",
/// }
/// ```
#[macro_export]
macro_rules! module_usb_driver {
    ($($f:tt)*) => {
        $crate::module_driver!(<T>, $crate::usb::Adapter<T>, { $($f)* });
    }
}
