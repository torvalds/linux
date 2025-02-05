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

        #[cfg(not(CONFIG_JUMP_LABEL))]
        {
            $crate::bindings::static_key_count(_key.cast_mut()) > 0
        }

        #[cfg(CONFIG_JUMP_LABEL)]
        $crate::jump_label::arch_static_branch! { $key, $keytyp, $field, false }
    }};
}
pub use static_branch_unlikely;

/// Assert that the assembly block evaluates to a string literal.
#[cfg(CONFIG_JUMP_LABEL)]
const _: &str = include!(concat!(
    env!("OBJTREE"),
    "/rust/kernel/generated_arch_static_branch_asm.rs"
));

#[macro_export]
#[doc(hidden)]
#[cfg(CONFIG_JUMP_LABEL)]
macro_rules! arch_static_branch {
    ($key:path, $keytyp:ty, $field:ident, $branch:expr) => {'my_label: {
        $crate::asm!(
            include!(concat!(env!("OBJTREE"), "/rust/kernel/generated_arch_static_branch_asm.rs"));
            l_yes = label {
                break 'my_label true;
            },
            symb = sym $key,
            off = const ::core::mem::offset_of!($keytyp, $field),
            branch = const $crate::jump_label::bool_to_int($branch),
        );

        break 'my_label false;
    }};
}

#[cfg(CONFIG_JUMP_LABEL)]
pub use arch_static_branch;

/// A helper used by inline assembly to pass a boolean to as a `const` parameter.
///
/// Using this function instead of a cast lets you assert that the input is a boolean, and not some
/// other type that can also be cast to an integer.
#[doc(hidden)]
pub const fn bool_to_int(b: bool) -> i32 {
    b as i32
}
