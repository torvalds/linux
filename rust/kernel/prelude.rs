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
pub use core::pin::Pin;

pub use crate::alloc::{box_ext::BoxExt, flags::*, vec_ext::VecExt};

#[doc(no_inline)]
pub use alloc::{boxed::Box, vec::Vec};

#[doc(no_inline)]
pub use macros::{module, pin_data, pinned_drop, vtable, Zeroable};

pub use super::build_assert;

// `super::std_vendor` is hidden, which makes the macro inline for some reason.
#[doc(no_inline)]
pub use super::dbg;
pub use super::{pr_alert, pr_crit, pr_debug, pr_emerg, pr_err, pr_info, pr_notice, pr_warn};

pub use super::{init, pin_init, try_init, try_pin_init};

pub use super::static_assert;

pub use super::error::{code::*, Error, Result};

pub use super::{str::CStr, ThisModule};

pub use super::init::{InPlaceInit, Init, PinInit};

pub use super::current;
