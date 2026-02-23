// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! SoC Driver Abstraction.
//!
//! C header: [`include/linux/sys_soc.h`](srctree/include/linux/sys_soc.h)

use crate::{
    bindings,
    error,
    prelude::*,
    str::CString,
    types::Opaque, //
};
use core::ptr::NonNull;

/// Attributes for a SoC device.
///
/// These are both exported to userspace under /sys/devices/socX and provided to other drivers to
/// match against via `soc_device_match` (not yet available in Rust) to enable quirks or
/// device-specific support where necessary.
///
/// All fields are freeform - they have no specific formatting, just defined meanings.
/// For example, the [`machine`](`Attributes::machine`) field could be "DB8500" or
/// "Qualcomm Technologies, Inc. SM8560 HDK", but regardless it should identify a board or product.
pub struct Attributes {
    /// Should generally be a board ID or product ID. Examples
    /// include DB8500 (ST-Ericsson) or "Qualcomm Technologies, inc. SM8560 HDK".
    ///
    /// If this field is not populated, the SoC infrastructure will try to populate it from
    /// `/model` in the device tree.
    pub machine: Option<CString>,
    /// The broader class this SoC belongs to. Examples include ux500
    /// (for DB8500) or Snapdragon (for SM8650).
    ///
    /// On chips with ARM firmware supporting SMCCC v1.2+, this may be a JEDEC JEP106 manufacturer
    /// identification.
    pub family: Option<CString>,
    /// The manufacturing revision of the part. Frequently this is MAJOR.MINOR, but not always.
    pub revision: Option<CString>,
    /// Serial Number - uniquely identifies a specific SoC. If present, should be unique (buying a
    /// replacement part should change it if present). This field cannot be matched on and is
    /// solely present to export through /sys.
    pub serial_number: Option<CString>,
    /// SoC ID - identifies a specific SoC kind in question, sometimes more specifically than
    /// `machine` if the same SoC is used in multiple products. Some devices use this to specify a
    /// SoC name, e.g. "I.MX??", and others just print an ID number (e.g. Tegra and Qualcomm).
    ///
    /// On chips with ARM firmware supporting SMCCC v1.2+, this may be a JEDEC JEP106 manufacturer
    /// identification (the family value) followed by a colon and then a 4-digit ID value.
    pub soc_id: Option<CString>,
}

struct BuiltAttributes {
    // While `inner` has pointers to `_backing`, it is to the interior of the `CStrings`, not
    // `backing` itself, so it does not need to be pinned.
    _backing: Attributes,
    // `Opaque` makes us `!Unpin`, as the registration holds a pointer to `inner` when used.
    inner: Opaque<bindings::soc_device_attribute>,
}

fn cstring_to_c(mcs: &Option<CString>) -> *const kernel::ffi::c_char {
    mcs.as_ref()
        .map(|cs| cs.as_char_ptr())
        .unwrap_or(core::ptr::null())
}

impl BuiltAttributes {
    fn as_mut_ptr(&self) -> *mut bindings::soc_device_attribute {
        self.inner.get()
    }
}

impl Attributes {
    fn build(self) -> BuiltAttributes {
        BuiltAttributes {
            inner: Opaque::new(bindings::soc_device_attribute {
                machine: cstring_to_c(&self.machine),
                family: cstring_to_c(&self.family),
                revision: cstring_to_c(&self.revision),
                serial_number: cstring_to_c(&self.serial_number),
                soc_id: cstring_to_c(&self.soc_id),
                data: core::ptr::null(),
                custom_attr_group: core::ptr::null(),
            }),
            _backing: self,
        }
    }
}

#[pin_data(PinnedDrop)]
/// Registration handle for your soc_dev. If you let it go out of scope, your soc_dev will be
/// unregistered.
pub struct Registration {
    #[pin]
    attr: BuiltAttributes,
    soc_dev: NonNull<bindings::soc_device>,
}

// SAFETY: We provide no operations through `&Registration`.
unsafe impl Sync for Registration {}

// SAFETY: All pointers are normal allocations, not thread-specific.
unsafe impl Send for Registration {}

#[pinned_drop]
impl PinnedDrop for Registration {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: Device always contains a live pointer to a soc_device that can be unregistered
        unsafe { bindings::soc_device_unregister(self.soc_dev.as_ptr()) }
    }
}

impl Registration {
    /// Register a new SoC device
    pub fn new(attr: Attributes) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            attr: attr.build(),
            soc_dev: {
                // SAFETY:
                // * The struct provided through attr is backed by pinned data next to it,
                //   so as long as attr lives, the strings pointed to by the struct will too.
                // * `attr` is pinned, so the pinned data won't move.
                // * If it returns a device, and so others may try to read this data, by
                //   caller invariant, `attr` won't be released until the device is.
                let raw_soc = error::from_err_ptr(unsafe {
                    bindings::soc_device_register(attr.as_mut_ptr())
                })?;

                NonNull::new(raw_soc).ok_or(EINVAL)?
            },
        }? Error)
    }
}
