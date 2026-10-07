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

#[doc(no_inline)]
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

#[doc(no_inline)]
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

#[doc(no_inline)]
pub use zerocopy::{
    FromBytes,
    IntoBytes, //
};

#[doc(no_inline)]
pub use zerocopy_derive::{
    FromBytes,
    IntoBytes, //
};

#[doc(no_inline)]
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
    build_assert::{
        build_assert,
        build_error,
        const_assert,
        static_assert, //
    },
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
    pr_alert_ratelimited,
    pr_crit,
    pr_crit_ratelimited,
    pr_debug,
    pr_debug_ratelimited,
    pr_emerg,
    pr_emerg_ratelimited,
    pr_err,
    pr_err_ratelimited,
    pr_info,
    pr_info_ratelimited,
    pr_notice,
    pr_notice_ratelimited,
    pr_warn,
    pr_warn_ratelimited,
    str::CStrExt as _,
    try_init,
    try_pin_init,
    uaccess::UserPtr,
    ThisModule, //
};

// `super::std_vendor` is hidden, which makes the macro inline for some reason.
#[doc(no_inline)]
pub use super::dbg;
