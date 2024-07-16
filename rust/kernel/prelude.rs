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

pub use super::{
    error::{Error, Result},
    pr_emerg, pr_info, ThisModule,
};
pub use alloc::{boxed::Box, vec::Vec};
pub use core::pin::Pin;
pub use macros::module;
