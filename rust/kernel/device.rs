// SPDX-License-Identifier: GPL-2.0

//! Generic devices that are part of the kernel's driver model.
//!
//! C header: [`include/linux/device.h`](srctree/include/linux/device.h)

use crate::{
    bindings,
    str::CStr,
    types::{ARef, Opaque},
};
use core::{fmt, marker::PhantomData, ptr};

#[cfg(CONFIG_PRINTK)]
use crate::c_str;

/// A reference-counted device.
///
/// This structure represents the Rust abstraction for a C `struct device`. This implementation
/// abstracts the usage of an already existing C `struct device` within Rust code that we get
/// passed from the C side.
///
/// An instance of this abstraction can be obtained temporarily or permanent.
///
/// A temporary one is bound to the lifetime of the C `struct device` pointer used for creation.
/// A permanent instance is always reference-counted and hence not restricted by any lifetime
/// boundaries.
///
/// For subsystems it is recommended to create a permanent instance to wrap into a subsystem
/// specific device structure (e.g. `pci::Device`). This is useful for passing it to drivers in
/// `T::probe()`, such that a driver can store the `ARef<Device>` (equivalent to storing a
/// `struct device` pointer in a C driver) for arbitrary purposes, e.g. allocating DMA coherent
/// memory.
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
        unsafe { Self::as_ref(ptr) }.into()
    }
}

impl<Ctx: DeviceContext> Device<Ctx> {
    /// Obtain the raw `struct device *`.
    pub(crate) fn as_raw(&self) -> *mut bindings::device {
        self.0.get()
    }

    /// Returns a reference to the parent device, if any.
    #[cfg_attr(not(CONFIG_AUXILIARY_BUS), expect(dead_code))]
    pub(crate) fn parent(&self) -> Option<&Self> {
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
            Some(unsafe { Self::as_ref(parent) })
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
    pub unsafe fn as_ref<'a>(ptr: *mut bindings::device) -> &'a Self {
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
                klevel as *const _ as *const crate::ffi::c_char,
                self.as_raw(),
                c_str!("%pA").as_char_ptr(),
                &msg as *const _ as *const crate::ffi::c_void,
            )
        };
    }

    /// Checks if property is present or not.
    pub fn property_present(&self, name: &CStr) -> bool {
        // SAFETY: By the invariant of `CStr`, `name` is null-terminated.
        unsafe { bindings::device_property_present(self.as_raw().cast_const(), name.as_char_ptr()) }
    }
}

// SAFETY: `Device` is a transparent wrapper of a type that doesn't depend on `Device`'s generic
// argument.
kernel::impl_device_context_deref!(unsafe { Device });
kernel::impl_device_context_into_aref!(Device);

// SAFETY: Instances of `Device` are always reference-counted.
unsafe impl crate::types::AlwaysRefCounted for Device {
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

/// Marker trait for the context of a bus specific device.
///
/// Some functions of a bus specific device should only be called from a certain context, i.e. bus
/// callbacks, such as `probe()`.
///
/// This is the marker trait for structures representing the context of a bus specific device.
pub trait DeviceContext: private::Sealed {}

/// The [`Normal`] context is the context of a bus specific device when it is not an argument of
/// any bus callback.
pub struct Normal;

/// The [`Core`] context is the context of a bus specific device when it is supplied as argument of
/// any of the bus callbacks, such as `probe()`.
pub struct Core;

/// The [`Bound`] context is the context of a bus specific device reference when it is guaranteed to
/// be bound for the duration of its lifetime.
pub struct Bound;

mod private {
    pub trait Sealed {}

    impl Sealed for super::Bound {}
    impl Sealed for super::Core {}
    impl Sealed for super::Normal {}
}

impl DeviceContext for Bound {}
impl DeviceContext for Core {}
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
        impl ::core::convert::From<&$device<$src>> for $crate::types::ARef<$device> {
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
        ::kernel::__impl_device_context_into_aref!($crate::device::Core, $device);
        ::kernel::__impl_device_context_into_aref!($crate::device::Bound, $device);
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! dev_printk {
    ($method:ident, $dev:expr, $($f:tt)*) => {
        {
            ($dev).$method(core::format_args!($($f)*));
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
/// [`core::fmt`] and `alloc::format!`.
///
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
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
