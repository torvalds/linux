// SPDX-License-Identifier: GPL-2.0

//! Infrastructure for interfacing Rust code with C kernel subsystems.
//!
//! This module is intended for low-level, unsafe Rust infrastructure code
//! that interoperates between Rust and C. It is *not* for use directly in
//! Rust drivers.

pub mod list;
