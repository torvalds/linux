// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Logic for static keys.
//!
//! C header: [`include/linux/jump_label.h`](srctree/include/linux/jump_label.h).

/// Branch based on a static key.
///
/// Takes three arguments:
///
/// * `key` - the path to the static variable containing the `static_key`.
/// * `keytyp` - the type of `key`.
/// * `field` - the name of the field of `key` that contains the `static_key`.
///
/// # Safety
///
/// The macro must be used with a real static key defined by C.
#[macro_export]
macro_rules! static_branch_unlikely {
    ($key:path, $keytyp:ty, $field:ident) => {{
        let _key: *const $keytyp = ::core::ptr::addr_of!($key);
        let _key: *const $crate::bindings::static_key_false = ::core::ptr::addr_of!((*_key).$field);
        let _key: *const $crate::bindings::static_key = _key.cast();

        $crate::bindings::static_key_count(_key.cast_mut()) > 0
    }};
}
pub use static_branch_unlikely;
