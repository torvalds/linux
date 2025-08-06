// SPDX-License-Identifier: GPL-2.0

//! Foreign function interface (FFI) types.
//!
//! This crate provides mapping from C primitive types to Rust ones.
//!
//! The Rust [`core`] crate provides [`core::ffi`], which maps integer types to the platform default
//! C ABI. The kernel does not use [`core::ffi`], so it can customise the mapping that deviates from
//! the platform default.

#![no_std]

macro_rules! alias {
    ($($name:ident = $ty:ty;)*) => {$(
        #[allow(non_camel_case_types, missing_docs)]
        pub type $name = $ty;

        // Check size compatibility with `core`.
        const _: () = assert!(
            ::core::mem::size_of::<$name>() == ::core::mem::size_of::<::core::ffi::$name>()
        );
    )*}
}

alias! {
    // `core::ffi::c_char` is either `i8` or `u8` depending on architecture. In the kernel, we use
    // `-funsigned-char` so it's always mapped to `u8`.
    c_char = u8;

    c_schar = i8;
    c_uchar = u8;

    c_short = i16;
    c_ushort = u16;

    c_int = i32;
    c_uint = u32;

    // In the kernel, `intptr_t` is defined to be `long` in all platforms, so we can map the type to
    // `isize`.
    c_long = isize;
    c_ulong = usize;

    c_longlong = i64;
    c_ulonglong = u64;
}

pub use core::ffi::c_void;
