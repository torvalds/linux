// SPDX-License-Identifier: GPL-2.0

//! Firmware abstraction
//!
//! C header: [`include/linux/firmware.h`](srctree/include/linux/firmware.h)

use crate::{bindings, device::Device, error::Error, error::Result, str::CStr};
use core::ptr::NonNull;

/// # Invariants
///
/// One of the following: `bindings::request_firmware`, `bindings::firmware_request_nowarn`,
/// `bindings::firmware_request_platform`, `bindings::request_firmware_direct`.
struct FwFunc(
    unsafe extern "C" fn(*mut *const bindings::firmware, *const i8, *mut bindings::device) -> i32,
);

impl FwFunc {
    fn request() -> Self {
        Self(bindings::request_firmware)
    }

    fn request_nowarn() -> Self {
        Self(bindings::firmware_request_nowarn)
    }
}

/// Abstraction around a C `struct firmware`.
///
/// This is a simple abstraction around the C firmware API. Just like with the C API, firmware can
/// be requested. Once requested the abstraction provides direct access to the firmware buffer as
/// `&[u8]`. The firmware is released once [`Firmware`] is dropped.
///
/// # Invariants
///
/// The pointer is valid, and has ownership over the instance of `struct firmware`.
///
/// The `Firmware`'s backing buffer is not modified.
///
/// # Examples
///
/// ```no_run
/// # use kernel::{c_str, device::Device, firmware::Firmware};
///
/// # fn no_run() -> Result<(), Error> {
/// # // SAFETY: *NOT* safe, just for the example to get an `ARef<Device>` instance
/// # let dev = unsafe { Device::from_raw(core::ptr::null_mut()) };
///
/// let fw = Firmware::request(c_str!("path/to/firmware.bin"), &dev)?;
/// let blob = fw.data();
///
/// # Ok(())
/// # }
/// ```
pub struct Firmware(NonNull<bindings::firmware>);

impl Firmware {
    fn request_internal(name: &CStr, dev: &Device, func: FwFunc) -> Result<Self> {
        let mut fw: *mut bindings::firmware = core::ptr::null_mut();
        let pfw: *mut *mut bindings::firmware = &mut fw;

        // SAFETY: `pfw` is a valid pointer to a NULL initialized `bindings::firmware` pointer.
        // `name` and `dev` are valid as by their type invariants.
        let ret = unsafe { func.0(pfw as _, name.as_char_ptr(), dev.as_raw()) };
        if ret != 0 {
            return Err(Error::from_errno(ret));
        }

        // SAFETY: `func` not bailing out with a non-zero error code, guarantees that `fw` is a
        // valid pointer to `bindings::firmware`.
        Ok(Firmware(unsafe { NonNull::new_unchecked(fw) }))
    }

    /// Send a firmware request and wait for it. See also `bindings::request_firmware`.
    pub fn request(name: &CStr, dev: &Device) -> Result<Self> {
        Self::request_internal(name, dev, FwFunc::request())
    }

    /// Send a request for an optional firmware module. See also
    /// `bindings::firmware_request_nowarn`.
    pub fn request_nowarn(name: &CStr, dev: &Device) -> Result<Self> {
        Self::request_internal(name, dev, FwFunc::request_nowarn())
    }

    fn as_raw(&self) -> *mut bindings::firmware {
        self.0.as_ptr()
    }

    /// Returns the size of the requested firmware in bytes.
    pub fn size(&self) -> usize {
        // SAFETY: `self.as_raw()` is valid by the type invariant.
        unsafe { (*self.as_raw()).size }
    }

    /// Returns the requested firmware as `&[u8]`.
    pub fn data(&self) -> &[u8] {
        // SAFETY: `self.as_raw()` is valid by the type invariant. Additionally,
        // `bindings::firmware` guarantees, if successfully requested, that
        // `bindings::firmware::data` has a size of `bindings::firmware::size` bytes.
        unsafe { core::slice::from_raw_parts((*self.as_raw()).data, self.size()) }
    }
}

impl Drop for Firmware {
    fn drop(&mut self) {
        // SAFETY: `self.as_raw()` is valid by the type invariant.
        unsafe { bindings::release_firmware(self.as_raw()) };
    }
}

// SAFETY: `Firmware` only holds a pointer to a C `struct firmware`, which is safe to be used from
// any thread.
unsafe impl Send for Firmware {}

// SAFETY: `Firmware` only holds a pointer to a C `struct firmware`, references to which are safe to
// be used from any thread.
unsafe impl Sync for Firmware {}
