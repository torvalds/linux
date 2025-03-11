// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Miscdevice support.
//!
//! C headers: [`include/linux/miscdevice.h`](srctree/include/linux/miscdevice.h).
//!
//! Reference: <https://www.kernel.org/doc/html/latest/driver-api/misc_devices.html>

use crate::{
    bindings,
    error::{to_result, Error, Result, VTABLE_DEFAULT_ERROR},
    prelude::*,
    str::CStr,
    types::{ForeignOwnable, Opaque},
};
use core::{
    ffi::{c_int, c_long, c_uint, c_ulong},
    marker::PhantomData,
    mem::MaybeUninit,
    pin::Pin,
};

/// Options for creating a misc device.
#[derive(Copy, Clone)]
pub struct MiscDeviceOptions {
    /// The name of the miscdevice.
    pub name: &'static CStr,
}

impl MiscDeviceOptions {
    /// Create a raw `struct miscdev` ready for registration.
    pub const fn into_raw<T: MiscDevice>(self) -> bindings::miscdevice {
        // SAFETY: All zeros is valid for this C type.
        let mut result: bindings::miscdevice = unsafe { MaybeUninit::zeroed().assume_init() };
        result.minor = bindings::MISC_DYNAMIC_MINOR as _;
        result.name = self.name.as_char_ptr();
        result.fops = create_vtable::<T>();
        result
    }
}

/// A registration of a miscdevice.
///
/// # Invariants
///
/// `inner` is a registered misc device.
#[repr(transparent)]
#[pin_data(PinnedDrop)]
pub struct MiscDeviceRegistration<T> {
    #[pin]
    inner: Opaque<bindings::miscdevice>,
    _t: PhantomData<T>,
}

// SAFETY: It is allowed to call `misc_deregister` on a different thread from where you called
// `misc_register`.
unsafe impl<T> Send for MiscDeviceRegistration<T> {}
// SAFETY: All `&self` methods on this type are written to ensure that it is safe to call them in
// parallel.
unsafe impl<T> Sync for MiscDeviceRegistration<T> {}

impl<T: MiscDevice> MiscDeviceRegistration<T> {
    /// Register a misc device.
    pub fn register(opts: MiscDeviceOptions) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            inner <- Opaque::try_ffi_init(move |slot: *mut bindings::miscdevice| {
                // SAFETY: The initializer can write to the provided `slot`.
                unsafe { slot.write(opts.into_raw::<T>()) };

                // SAFETY: We just wrote the misc device options to the slot. The miscdevice will
                // get unregistered before `slot` is deallocated because the memory is pinned and
                // the destructor of this type deallocates the memory.
                // INVARIANT: If this returns `Ok(())`, then the `slot` will contain a registered
                // misc device.
                to_result(unsafe { bindings::misc_register(slot) })
            }),
            _t: PhantomData,
        })
    }

    /// Returns a raw pointer to the misc device.
    pub fn as_raw(&self) -> *mut bindings::miscdevice {
        self.inner.get()
    }
}

#[pinned_drop]
impl<T> PinnedDrop for MiscDeviceRegistration<T> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: We know that the device is registered by the type invariants.
        unsafe { bindings::misc_deregister(self.inner.get()) };
    }
}

/// Trait implemented by the private data of an open misc device.
#[vtable]
pub trait MiscDevice {
    /// What kind of pointer should `Self` be wrapped in.
    type Ptr: ForeignOwnable + Send + Sync;

    /// Called when the misc device is opened.
    ///
    /// The returned pointer will be stored as the private data for the file.
    fn open() -> Result<Self::Ptr>;

    /// Called when the misc device is released.
    fn release(device: Self::Ptr) {
        drop(device);
    }

    /// Handler for ioctls.
    ///
    /// The `cmd` argument is usually manipulated using the utilties in [`kernel::ioctl`].
    ///
    /// [`kernel::ioctl`]: mod@crate::ioctl
    fn ioctl(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _cmd: u32,
        _arg: usize,
    ) -> Result<isize> {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Handler for ioctls.
    ///
    /// Used for 32-bit userspace on 64-bit platforms.
    ///
    /// This method is optional and only needs to be provided if the ioctl relies on structures
    /// that have different layout on 32-bit and 64-bit userspace. If no implementation is
    /// provided, then `compat_ptr_ioctl` will be used instead.
    #[cfg(CONFIG_COMPAT)]
    fn compat_ioctl(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _cmd: u32,
        _arg: usize,
    ) -> Result<isize> {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }
}

const fn create_vtable<T: MiscDevice>() -> &'static bindings::file_operations {
    const fn maybe_fn<T: Copy>(check: bool, func: T) -> Option<T> {
        if check {
            Some(func)
        } else {
            None
        }
    }

    struct VtableHelper<T: MiscDevice> {
        _t: PhantomData<T>,
    }
    impl<T: MiscDevice> VtableHelper<T> {
        const VTABLE: bindings::file_operations = bindings::file_operations {
            open: Some(fops_open::<T>),
            release: Some(fops_release::<T>),
            unlocked_ioctl: maybe_fn(T::HAS_IOCTL, fops_ioctl::<T>),
            #[cfg(CONFIG_COMPAT)]
            compat_ioctl: if T::HAS_COMPAT_IOCTL {
                Some(fops_compat_ioctl::<T>)
            } else if T::HAS_IOCTL {
                Some(bindings::compat_ptr_ioctl)
            } else {
                None
            },
            // SAFETY: All zeros is a valid value for `bindings::file_operations`.
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        };
    }

    &VtableHelper::<T>::VTABLE
}

/// # Safety
///
/// `file` and `inode` must be the file and inode for a file that is undergoing initialization.
/// The file must be associated with a `MiscDeviceRegistration<T>`.
unsafe extern "C" fn fops_open<T: MiscDevice>(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: The pointers are valid and for a file being opened.
    let ret = unsafe { bindings::generic_file_open(inode, file) };
    if ret != 0 {
        return ret;
    }

    let ptr = match T::open() {
        Ok(ptr) => ptr,
        Err(err) => return err.to_errno(),
    };

    // SAFETY: The open call of a file owns the private data.
    unsafe { (*file).private_data = ptr.into_foreign().cast_mut() };

    0
}

/// # Safety
///
/// `file` and `inode` must be the file and inode for a file that is being released. The file must
/// be associated with a `MiscDeviceRegistration<T>`.
unsafe extern "C" fn fops_release<T: MiscDevice>(
    _inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: The release call of a file owns the private data.
    let private = unsafe { (*file).private_data };
    // SAFETY: The release call of a file owns the private data.
    let ptr = unsafe { <T::Ptr as ForeignOwnable>::from_foreign(private) };

    T::release(ptr);

    0
}

/// # Safety
///
/// `file` must be a valid file that is associated with a `MiscDeviceRegistration<T>`.
unsafe extern "C" fn fops_ioctl<T: MiscDevice>(
    file: *mut bindings::file,
    cmd: c_uint,
    arg: c_ulong,
) -> c_long {
    // SAFETY: The ioctl call of a file can access the private data.
    let private = unsafe { (*file).private_data };
    // SAFETY: Ioctl calls can borrow the private data of the file.
    let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private) };

    match T::ioctl(device, cmd, arg as usize) {
        Ok(ret) => ret as c_long,
        Err(err) => err.to_errno() as c_long,
    }
}

/// # Safety
///
/// `file` must be a valid file that is associated with a `MiscDeviceRegistration<T>`.
#[cfg(CONFIG_COMPAT)]
unsafe extern "C" fn fops_compat_ioctl<T: MiscDevice>(
    file: *mut bindings::file,
    cmd: c_uint,
    arg: c_ulong,
) -> c_long {
    // SAFETY: The compat ioctl call of a file can access the private data.
    let private = unsafe { (*file).private_data };
    // SAFETY: Ioctl calls can borrow the private data of the file.
    let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private) };

    match T::compat_ioctl(device, cmd, arg as usize) {
        Ok(ret) => ret as c_long,
        Err(err) => err.to_errno() as c_long,
    }
}
