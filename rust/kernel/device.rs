// SPDX-License-Identifier: GPL-2.0

//! Generic devices that are part of the kernel's driver model.
//!
//! C header: [`include/linux/device.h`](srctree/include/linux/device.h)

use crate::{
    bindings, fmt,
    sync::aref::ARef,
    types::{ForeignOwnable, Opaque},
};
use core::{marker::PhantomData, ptr};

#[cfg(CONFIG_PRINTK)]
use crate::c_str;

pub mod property;

/// The core representation of a device in the kernel's driver model.
///
/// This structure represents the Rust abstraction for a C `struct device`. A [`Device`] can either
/// exist as temporary reference (see also [`Device::from_raw`]), which is only valid within a
/// certain scope or as [`ARef<Device>`], owning a dedicated reference count.
///
/// # Device Types
///
/// A [`Device`] can represent either a bus device or a class device.
///
/// ## Bus Devices
///
/// A bus device is a [`Device`] that is associated with a physical or virtual bus. Examples of
/// buses include PCI, USB, I2C, and SPI. Devices attached to a bus are registered with a specific
/// bus type, which facilitates matching devices with appropriate drivers based on IDs or other
/// identifying information. Bus devices are visible in sysfs under `/sys/bus/<bus-name>/devices/`.
///
/// ## Class Devices
///
/// A class device is a [`Device`] that is associated with a logical category of functionality
/// rather than a physical bus. Examples of classes include block devices, network interfaces, sound
/// cards, and input devices. Class devices are grouped under a common class and exposed to
/// userspace via entries in `/sys/class/<class-name>/`.
///
/// # Device Context
///
/// [`Device`] references are generic over a [`DeviceContext`], which represents the type state of
/// a [`Device`].
///
/// As the name indicates, this type state represents the context of the scope the [`Device`]
/// reference is valid in. For instance, the [`Bound`] context guarantees that the [`Device`] is
/// bound to a driver for the entire duration of the existence of a [`Device<Bound>`] reference.
///
/// Other [`DeviceContext`] types besides [`Bound`] are [`Normal`], [`Core`] and [`CoreInternal`].
///
/// Unless selected otherwise [`Device`] defaults to the [`Normal`] [`DeviceContext`], which by
/// itself has no additional requirements.
///
/// It is always up to the caller of [`Device::from_raw`] to select the correct [`DeviceContext`]
/// type for the corresponding scope the [`Device`] reference is created in.
///
/// All [`DeviceContext`] types other than [`Normal`] are intended to be used with
/// [bus devices](#bus-devices) only.
///
/// # Implementing Bus Devices
///
/// This section provides a guideline to implement bus specific devices, such as [`pci::Device`] or
/// [`platform::Device`].
///
/// A bus specific device should be defined as follows.
///
/// ```ignore
/// #[repr(transparent)]
/// pub struct Device<Ctx: device::DeviceContext = device::Normal>(
///     Opaque<bindings::bus_device_type>,
///     PhantomData<Ctx>,
/// );
/// ```
///
/// Since devices are reference counted, [`AlwaysRefCounted`] should be implemented for `Device`
/// (i.e. `Device<Normal>`). Note that [`AlwaysRefCounted`] must not be implemented for any other
/// [`DeviceContext`], since all other device context types are only valid within a certain scope.
///
/// In order to be able to implement the [`DeviceContext`] dereference hierarchy, bus device
/// implementations should call the [`impl_device_context_deref`] macro as shown below.
///
/// ```ignore
/// // SAFETY: `Device` is a transparent wrapper of a type that doesn't depend on `Device`'s
/// // generic argument.
/// kernel::impl_device_context_deref!(unsafe { Device });
/// ```
///
/// In order to convert from a any [`Device<Ctx>`] to [`ARef<Device>`], bus devices can implement
/// the following macro call.
///
/// ```ignore
/// kernel::impl_device_context_into_aref!(Device);
/// ```
///
/// Bus devices should also implement the following [`AsRef`] implementation, such that users can
/// easily derive a generic [`Device`] reference.
///
/// ```ignore
/// impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Device<Ctx> {
///     fn as_ref(&self) -> &device::Device<Ctx> {
///         ...
///     }
/// }
/// ```
///
/// # Implementing Class Devices
///
/// Class device implementations require less infrastructure and depend slightly more on the
/// specific subsystem.
///
/// An example implementation for a class device could look like this.
///
/// ```ignore
/// #[repr(C)]
/// pub struct Device<T: class::Driver> {
///     dev: Opaque<bindings::class_device_type>,
///     data: T::Data,
/// }
/// ```
///
/// This class device uses the sub-classing pattern to embed the driver's private data within the
/// allocation of the class device. For this to be possible the class device is generic over the
/// class specific `Driver` trait implementation.
///
/// Just like any device, class devices are reference counted and should hence implement
/// [`AlwaysRefCounted`] for `Device`.
///
/// Class devices should also implement the following [`AsRef`] implementation, such that users can
/// easily derive a generic [`Device`] reference.
///
/// ```ignore
/// impl<T: class::Driver> AsRef<device::Device> for Device<T> {
///     fn as_ref(&self) -> &device::Device {
///         ...
///     }
/// }
/// ```
///
/// An example for a class device implementation is
#[cfg_attr(CONFIG_DRM = "y", doc = "[`drm::Device`](kernel::drm::Device).")]
#[cfg_attr(not(CONFIG_DRM = "y"), doc = "`drm::Device`.")]
///
/// # Invariants
///
/// A `Device` instance represents a valid `struct device` created by the C portion of the kernel.
///
/// Instances of this type are always reference-counted, that is, a call to `get_device` ensures
/// that the allocation remains valid at least until the matching call to `put_device`.
///
/// `bindings::device::release` is valid to be called from any thread, hence `ARef<Device>` can be
/// dropped from any thread.
///
/// [`AlwaysRefCounted`]: kernel::types::AlwaysRefCounted
/// [`impl_device_context_deref`]: kernel::impl_device_context_deref
/// [`pci::Device`]: kernel::pci::Device
/// [`platform::Device`]: kernel::platform::Device
#[repr(transparent)]
pub struct Device<Ctx: DeviceContext = Normal>(Opaque<bindings::device>, PhantomData<Ctx>);

impl Device {
    /// Creates a new reference-counted abstraction instance of an existing `struct device` pointer.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `ptr` is valid, non-null, and has a non-zero reference count,
    /// i.e. it must be ensured that the reference count of the C `struct device` `ptr` points to
    /// can't drop to zero, for the duration of this function call.
    ///
    /// It must also be ensured that `bindings::device::release` can be called from any thread.
    /// While not officially documented, this should be the case for any `struct device`.
    pub unsafe fn get_device(ptr: *mut bindings::device) -> ARef<Self> {
        // SAFETY: By the safety requirements ptr is valid
        unsafe { Self::from_raw(ptr) }.into()
    }

    /// Convert a [`&Device`](Device) into a [`&Device<Bound>`](Device<Bound>).
    ///
    /// # Safety
    ///
    /// The caller is responsible to ensure that the returned [`&Device<Bound>`](Device<Bound>)
    /// only lives as long as it can be guaranteed that the [`Device`] is actually bound.
    pub unsafe fn as_bound(&self) -> &Device<Bound> {
        let ptr = core::ptr::from_ref(self);

        // CAST: By the safety requirements the caller is responsible to guarantee that the
        // returned reference only lives as long as the device is actually bound.
        let ptr = ptr.cast();

        // SAFETY:
        // - `ptr` comes from `from_ref(self)` above, hence it's guaranteed to be valid.
        // - Any valid `Device` pointer is also a valid pointer for `Device<Bound>`.
        unsafe { &*ptr }
    }
}

impl Device<CoreInternal> {
    /// Store a pointer to the bound driver's private data.
    pub fn set_drvdata(&self, data: impl ForeignOwnable) {
        // SAFETY: By the type invariants, `self.as_raw()` is a valid pointer to a `struct device`.
        unsafe { bindings::dev_set_drvdata(self.as_raw(), data.into_foreign().cast()) }
    }

    /// Take ownership of the private data stored in this [`Device`].
    ///
    /// # Safety
    ///
    /// - Must only be called once after a preceding call to [`Device::set_drvdata`].
    /// - The type `T` must match the type of the `ForeignOwnable` previously stored by
    ///   [`Device::set_drvdata`].
    pub unsafe fn drvdata_obtain<T: ForeignOwnable>(&self) -> T {
        // SAFETY: By the type invariants, `self.as_raw()` is a valid pointer to a `struct device`.
        let ptr = unsafe { bindings::dev_get_drvdata(self.as_raw()) };

        // SAFETY:
        // - By the safety requirements of this function, `ptr` comes from a previous call to
        //   `into_foreign()`.
        // - `dev_get_drvdata()` guarantees to return the same pointer given to `dev_set_drvdata()`
        //   in `into_foreign()`.
        unsafe { T::from_foreign(ptr.cast()) }
    }

    /// Borrow the driver's private data bound to this [`Device`].
    ///
    /// # Safety
    ///
    /// - Must only be called after a preceding call to [`Device::set_drvdata`] and before
    ///   [`Device::drvdata_obtain`].
    /// - The type `T` must match the type of the `ForeignOwnable` previously stored by
    ///   [`Device::set_drvdata`].
    pub unsafe fn drvdata_borrow<T: ForeignOwnable>(&self) -> T::Borrowed<'_> {
        // SAFETY: By the type invariants, `self.as_raw()` is a valid pointer to a `struct device`.
        let ptr = unsafe { bindings::dev_get_drvdata(self.as_raw()) };

        // SAFETY:
        // - By the safety requirements of this function, `ptr` comes from a previous call to
        //   `into_foreign()`.
        // - `dev_get_drvdata()` guarantees to return the same pointer given to `dev_set_drvdata()`
        //   in `into_foreign()`.
        unsafe { T::borrow(ptr.cast()) }
    }
}

impl<Ctx: DeviceContext> Device<Ctx> {
    /// Obtain the raw `struct device *`.
    pub(crate) fn as_raw(&self) -> *mut bindings::device {
        self.0.get()
    }

    /// Returns a reference to the parent device, if any.
    #[cfg_attr(not(CONFIG_AUXILIARY_BUS), expect(dead_code))]
    pub(crate) fn parent(&self) -> Option<&Device> {
        // SAFETY:
        // - By the type invariant `self.as_raw()` is always valid.
        // - The parent device is only ever set at device creation.
        let parent = unsafe { (*self.as_raw()).parent };

        if parent.is_null() {
            None
        } else {
            // SAFETY:
            // - Since `parent` is not NULL, it must be a valid pointer to a `struct device`.
            // - `parent` is valid for the lifetime of `self`, since a `struct device` holds a
            //   reference count of its parent.
            Some(unsafe { Device::from_raw(parent) })
        }
    }

    /// Convert a raw C `struct device` pointer to a `&'a Device`.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `ptr` is valid, non-null, and has a non-zero reference count,
    /// i.e. it must be ensured that the reference count of the C `struct device` `ptr` points to
    /// can't drop to zero, for the duration of this function call and the entire duration when the
    /// returned reference exists.
    pub unsafe fn from_raw<'a>(ptr: *mut bindings::device) -> &'a Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        unsafe { &*ptr.cast() }
    }

    /// Prints an emergency-level message (level 0) prefixed with device information.
    ///
    /// More details are available from [`dev_emerg`].
    ///
    /// [`dev_emerg`]: crate::dev_emerg
    pub fn pr_emerg(&self, args: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
        unsafe { self.printk(bindings::KERN_EMERG, args) };
    }

    /// Prints an alert-level message (level 1) prefixed with device information.
    ///
    /// More details are available from [`dev_alert`].
    ///
    /// [`dev_alert`]: crate::dev_alert
    pub fn pr_alert(&self, args: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
        unsafe { self.printk(bindings::KERN_ALERT, args) };
    }

    /// Prints a critical-level message (level 2) prefixed with device information.
    ///
    /// More details are available from [`dev_crit`].
    ///
    /// [`dev_crit`]: crate::dev_crit
    pub fn pr_crit(&self, args: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
        unsafe { self.printk(bindings::KERN_CRIT, args) };
    }

    /// Prints an error-level message (level 3) prefixed with device information.
    ///
    /// More details are available from [`dev_err`].
    ///
    /// [`dev_err`]: crate::dev_err
    pub fn pr_err(&self, args: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
        unsafe { self.printk(bindings::KERN_ERR, args) };
    }

    /// Prints a warning-level message (level 4) prefixed with device information.
    ///
    /// More details are available from [`dev_warn`].
    ///
    /// [`dev_warn`]: crate::dev_warn
    pub fn pr_warn(&self, args: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
        unsafe { self.printk(bindings::KERN_WARNING, args) };
    }

    /// Prints a notice-level message (level 5) prefixed with device information.
    ///
    /// More details are available from [`dev_notice`].
    ///
    /// [`dev_notice`]: crate::dev_notice
    pub fn pr_notice(&self, args: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
        unsafe { self.printk(bindings::KERN_NOTICE, args) };
    }

    /// Prints an info-level message (level 6) prefixed with device information.
    ///
    /// More details are available from [`dev_info`].
    ///
    /// [`dev_info`]: crate::dev_info
    pub fn pr_info(&self, args: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
        unsafe { self.printk(bindings::KERN_INFO, args) };
    }

    /// Prints a debug-level message (level 7) prefixed with device information.
    ///
    /// More details are available from [`dev_dbg`].
    ///
    /// [`dev_dbg`]: crate::dev_dbg
    pub fn pr_dbg(&self, args: fmt::Arguments<'_>) {
        if cfg!(debug_assertions) {
            // SAFETY: `klevel` is null-terminated, uses one of the kernel constants.
            unsafe { self.printk(bindings::KERN_DEBUG, args) };
        }
    }

    /// Prints the provided message to the console.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `klevel` is null-terminated; in particular, one of the
    /// `KERN_*`constants, for example, `KERN_CRIT`, `KERN_ALERT`, etc.
    #[cfg_attr(not(CONFIG_PRINTK), allow(unused_variables))]
    unsafe fn printk(&self, klevel: &[u8], msg: fmt::Arguments<'_>) {
        // SAFETY: `klevel` is null-terminated and one of the kernel constants. `self.as_raw`
        // is valid because `self` is valid. The "%pA" format string expects a pointer to
        // `fmt::Arguments`, which is what we're passing as the last argument.
        #[cfg(CONFIG_PRINTK)]
        unsafe {
            bindings::_dev_printk(
                klevel.as_ptr().cast::<crate::ffi::c_char>(),
                self.as_raw(),
                c_str!("%pA").as_char_ptr(),
                core::ptr::from_ref(&msg).cast::<crate::ffi::c_void>(),
            )
        };
    }

    /// Obtain the [`FwNode`](property::FwNode) corresponding to this [`Device`].
    pub fn fwnode(&self) -> Option<&property::FwNode> {
        // SAFETY: `self` is valid.
        let fwnode_handle = unsafe { bindings::__dev_fwnode(self.as_raw()) };
        if fwnode_handle.is_null() {
            return None;
        }
        // SAFETY: `fwnode_handle` is valid. Its lifetime is tied to `&self`. We
        // return a reference instead of an `ARef<FwNode>` because `dev_fwnode()`
        // doesn't increment the refcount. It is safe to cast from a
        // `struct fwnode_handle*` to a `*const FwNode` because `FwNode` is
        // defined as a `#[repr(transparent)]` wrapper around `fwnode_handle`.
        Some(unsafe { &*fwnode_handle.cast() })
    }
}

// SAFETY: `Device` is a transparent wrapper of a type that doesn't depend on `Device`'s generic
// argument.
kernel::impl_device_context_deref!(unsafe { Device });
kernel::impl_device_context_into_aref!(Device);

// SAFETY: Instances of `Device` are always reference-counted.
unsafe impl crate::sync::aref::AlwaysRefCounted for Device {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the refcount is non-zero.
        unsafe { bindings::get_device(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::put_device(obj.cast().as_ptr()) }
    }
}

// SAFETY: As by the type invariant `Device` can be sent to any thread.
unsafe impl Send for Device {}

// SAFETY: `Device` can be shared among threads because all immutable methods are protected by the
// synchronization in `struct device`.
unsafe impl Sync for Device {}

/// Marker trait for the context or scope of a bus specific device.
///
/// [`DeviceContext`] is a marker trait for types representing the context of a bus specific
/// [`Device`].
///
/// The specific device context types are: [`CoreInternal`], [`Core`], [`Bound`] and [`Normal`].
///
/// [`DeviceContext`] types are hierarchical, which means that there is a strict hierarchy that
/// defines which [`DeviceContext`] type can be derived from another. For instance, any
/// [`Device<Core>`] can dereference to a [`Device<Bound>`].
///
/// The following enumeration illustrates the dereference hierarchy of [`DeviceContext`] types.
///
/// - [`CoreInternal`] => [`Core`] => [`Bound`] => [`Normal`]
///
/// Bus devices can automatically implement the dereference hierarchy by using
/// [`impl_device_context_deref`].
///
/// Note that the guarantee for a [`Device`] reference to have a certain [`DeviceContext`] comes
/// from the specific scope the [`Device`] reference is valid in.
///
/// [`impl_device_context_deref`]: kernel::impl_device_context_deref
pub trait DeviceContext: private::Sealed {}

/// The [`Normal`] context is the default [`DeviceContext`] of any [`Device`].
///
/// The normal context does not indicate any specific context. Any `Device<Ctx>` is also a valid
/// [`Device<Normal>`]. It is the only [`DeviceContext`] for which it is valid to implement
/// [`AlwaysRefCounted`] for.
///
/// [`AlwaysRefCounted`]: kernel::types::AlwaysRefCounted
pub struct Normal;

/// The [`Core`] context is the context of a bus specific device when it appears as argument of
/// any bus specific callback, such as `probe()`.
///
/// The core context indicates that the [`Device<Core>`] reference's scope is limited to the bus
/// callback it appears in. It is intended to be used for synchronization purposes. Bus device
/// implementations can implement methods for [`Device<Core>`], such that they can only be called
/// from bus callbacks.
pub struct Core;

/// Semantically the same as [`Core`], but reserved for internal usage of the corresponding bus
/// abstraction.
///
/// The internal core context is intended to be used in exactly the same way as the [`Core`]
/// context, with the difference that this [`DeviceContext`] is internal to the corresponding bus
/// abstraction.
///
/// This context mainly exists to share generic [`Device`] infrastructure that should only be called
/// from bus callbacks with bus abstractions, but without making them accessible for drivers.
pub struct CoreInternal;

/// The [`Bound`] context is the [`DeviceContext`] of a bus specific device when it is guaranteed to
/// be bound to a driver.
///
/// The bound context indicates that for the entire duration of the lifetime of a [`Device<Bound>`]
/// reference, the [`Device`] is guaranteed to be bound to a driver.
///
/// Some APIs, such as [`dma::CoherentAllocation`] or [`Devres`] rely on the [`Device`] to be bound,
/// which can be proven with the [`Bound`] device context.
///
/// Any abstraction that can guarantee a scope where the corresponding bus device is bound, should
/// provide a [`Device<Bound>`] reference to its users for this scope. This allows users to benefit
/// from optimizations for accessing device resources, see also [`Devres::access`].
///
/// [`Devres`]: kernel::devres::Devres
/// [`Devres::access`]: kernel::devres::Devres::access
/// [`dma::CoherentAllocation`]: kernel::dma::CoherentAllocation
pub struct Bound;

mod private {
    pub trait Sealed {}

    impl Sealed for super::Bound {}
    impl Sealed for super::Core {}
    impl Sealed for super::CoreInternal {}
    impl Sealed for super::Normal {}
}

impl DeviceContext for Bound {}
impl DeviceContext for Core {}
impl DeviceContext for CoreInternal {}
impl DeviceContext for Normal {}

/// # Safety
///
/// The type given as `$device` must be a transparent wrapper of a type that doesn't depend on the
/// generic argument of `$device`.
#[doc(hidden)]
#[macro_export]
macro_rules! __impl_device_context_deref {
    (unsafe { $device:ident, $src:ty => $dst:ty }) => {
        impl ::core::ops::Deref for $device<$src> {
            type Target = $device<$dst>;

            fn deref(&self) -> &Self::Target {
                let ptr: *const Self = self;

                // CAST: `$device<$src>` and `$device<$dst>` transparently wrap the same type by the
                // safety requirement of the macro.
                let ptr = ptr.cast::<Self::Target>();

                // SAFETY: `ptr` was derived from `&self`.
                unsafe { &*ptr }
            }
        }
    };
}

/// Implement [`core::ops::Deref`] traits for allowed [`DeviceContext`] conversions of a (bus
/// specific) device.
///
/// # Safety
///
/// The type given as `$device` must be a transparent wrapper of a type that doesn't depend on the
/// generic argument of `$device`.
#[macro_export]
macro_rules! impl_device_context_deref {
    (unsafe { $device:ident }) => {
        // SAFETY: This macro has the exact same safety requirement as
        // `__impl_device_context_deref!`.
        ::kernel::__impl_device_context_deref!(unsafe {
            $device,
            $crate::device::CoreInternal => $crate::device::Core
        });

        // SAFETY: This macro has the exact same safety requirement as
        // `__impl_device_context_deref!`.
        ::kernel::__impl_device_context_deref!(unsafe {
            $device,
            $crate::device::Core => $crate::device::Bound
        });

        // SAFETY: This macro has the exact same safety requirement as
        // `__impl_device_context_deref!`.
        ::kernel::__impl_device_context_deref!(unsafe {
            $device,
            $crate::device::Bound => $crate::device::Normal
        });
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! __impl_device_context_into_aref {
    ($src:ty, $device:tt) => {
        impl ::core::convert::From<&$device<$src>> for $crate::sync::aref::ARef<$device> {
            fn from(dev: &$device<$src>) -> Self {
                (&**dev).into()
            }
        }
    };
}

/// Implement [`core::convert::From`], such that all `&Device<Ctx>` can be converted to an
/// `ARef<Device>`.
#[macro_export]
macro_rules! impl_device_context_into_aref {
    ($device:tt) => {
        ::kernel::__impl_device_context_into_aref!($crate::device::CoreInternal, $device);
        ::kernel::__impl_device_context_into_aref!($crate::device::Core, $device);
        ::kernel::__impl_device_context_into_aref!($crate::device::Bound, $device);
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! dev_printk {
    ($method:ident, $dev:expr, $($f:tt)*) => {
        {
            ($dev).$method($crate::prelude::fmt!($($f)*));
        }
    }
}

/// Prints an emergency-level message (level 0) prefixed with device information.
///
/// This level should be used if the system is unusable.
///
/// Equivalent to the kernel's `dev_emerg` macro.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_emerg!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_emerg {
    ($($f:tt)*) => { $crate::dev_printk!(pr_emerg, $($f)*); }
}

/// Prints an alert-level message (level 1) prefixed with device information.
///
/// This level should be used if action must be taken immediately.
///
/// Equivalent to the kernel's `dev_alert` macro.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_alert!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_alert {
    ($($f:tt)*) => { $crate::dev_printk!(pr_alert, $($f)*); }
}

/// Prints a critical-level message (level 2) prefixed with device information.
///
/// This level should be used in critical conditions.
///
/// Equivalent to the kernel's `dev_crit` macro.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_crit!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_crit {
    ($($f:tt)*) => { $crate::dev_printk!(pr_crit, $($f)*); }
}

/// Prints an error-level message (level 3) prefixed with device information.
///
/// This level should be used in error conditions.
///
/// Equivalent to the kernel's `dev_err` macro.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_err!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_err {
    ($($f:tt)*) => { $crate::dev_printk!(pr_err, $($f)*); }
}

/// Prints a warning-level message (level 4) prefixed with device information.
///
/// This level should be used in warning conditions.
///
/// Equivalent to the kernel's `dev_warn` macro.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_warn!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_warn {
    ($($f:tt)*) => { $crate::dev_printk!(pr_warn, $($f)*); }
}

/// Prints a notice-level message (level 5) prefixed with device information.
///
/// This level should be used in normal but significant conditions.
///
/// Equivalent to the kernel's `dev_notice` macro.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_notice!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_notice {
    ($($f:tt)*) => { $crate::dev_printk!(pr_notice, $($f)*); }
}

/// Prints an info-level message (level 6) prefixed with device information.
///
/// This level should be used for informational messages.
///
/// Equivalent to the kernel's `dev_info` macro.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_info!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_info {
    ($($f:tt)*) => { $crate::dev_printk!(pr_info, $($f)*); }
}

/// Prints a debug-level message (level 7) prefixed with device information.
///
/// This level should be used for debug messages.
///
/// Equivalent to the kernel's `dev_dbg` macro, except that it doesn't support dynamic debug yet.
///
/// Mimics the interface of [`std::print!`]. More information about the syntax is available from
/// [`core::fmt`] and [`std::format!`].
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
/// [`std::format!`]: https://doc.rust-lang.org/std/macro.format.html
///
/// # Examples
///
/// ```
/// # use kernel::device::Device;
///
/// fn example(dev: &Device) {
///     dev_dbg!(dev, "hello {}\n", "there");
/// }
/// ```
#[macro_export]
macro_rules! dev_dbg {
    ($($f:tt)*) => { $crate::dev_printk!(pr_dbg, $($f)*); }
}
