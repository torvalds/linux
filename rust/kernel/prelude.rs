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
    mem::{align_of, align_of_val, size_of, size_of_val},
    pin::Pin,
};

pub use ::ffi::{
    c_char, c_int, c_long, c_longlong, c_schar, c_short, c_uchar, c_uint, c_ulong, c_ulonglong,
    c_ushort, c_void,
};

pub use crate::alloc::{flags::*, Box, KBox, KVBox, KVVec, KVec, VBox, VVec, Vec};

#[doc(no_inline)]
pub use macros::{export, kunit_tests, module, vtable};

pub use pin_init::{init, pin_data, pin_init, pinned_drop, InPlaceWrite, Init, PinInit, Zeroable};

pub use super::{build_assert, build_error};

// `super::std_vendor` is hidden, which makes the macro inline for some reason.
#[doc(no_inline)]
pub use super::dbg;
pub use super::{dev_alert, dev_crit, dev_dbg, dev_emerg, dev_err, dev_info, dev_notice, dev_warn};
pub use super::{pr_alert, pr_crit, pr_debug, pr_emerg, pr_err, pr_info, pr_notice, pr_warn};
pub use core::format_args as fmt;

pub use super::{try_init, try_pin_init};

pub use super::static_assert;

pub use super::error::{code::*, Error, Result};

pub use super::{str::CStr, ThisModule};

pub use super::init::InPlaceInit;

pub use super::current;

pub use super::uaccess::UserPtr;
