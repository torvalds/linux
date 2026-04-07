// SPDX-License-Identifier: GPL-2.0

//! The `kernel` prelude.
//!
//! These are the most common items used by Rust code in the kernel,
//! intended to be imported by all Rust code, for convenience.
//!
//! # Examples
//!
//! ```
//! use kernel::prelude::*;
//! ```

#[doc(no_inline)]
pub use core::{
    mem::{
        align_of,
        align_of_val,
        size_of,
        size_of_val, //
    },
    pin::Pin, //
};

pub use ::ffi::{
    c_char,
    c_int,
    c_long,
    c_longlong,
    c_schar,
    c_short,
    c_uchar,
    c_uint,
    c_ulong,
    c_ulonglong,
    c_ushort,
    c_void,
    CStr, //
};

#[doc(no_inline)]
pub use macros::{
    export,
    fmt,
    kunit_tests,
    module,
    vtable, //
};

pub use pin_init::{
    init,
    pin_data,
    pin_init,
    pinned_drop,
    InPlaceWrite,
    Init,
    PinInit,
    Zeroable, //
};

pub use super::{
    alloc::{
        flags::*,
        Box,
        KBox,
        KVBox,
        KVVec,
        KVec,
        VBox,
        VVec,
        Vec, //
    },
    build_assert,
    build_error,
    const_assert,
    current,
    dev_alert,
    dev_crit,
    dev_dbg,
    dev_emerg,
    dev_err,
    dev_info,
    dev_notice,
    dev_warn,
    error::{
        code::*,
        Error,
        Result, //
    },
    init::InPlaceInit,
    pr_alert,
    pr_crit,
    pr_debug,
    pr_emerg,
    pr_err,
    pr_info,
    pr_notice,
    pr_warn,
    static_assert,
    str::CStrExt as _,
    try_init,
    try_pin_init,
    uaccess::UserPtr,
    ThisModule, //
};

// `super::std_vendor` is hidden, which makes the macro inline for some reason.
#[doc(no_inline)]
pub use super::dbg;
