// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

//! Traits for rendering or updating values exported to DebugFS.

use crate::sync::Mutex;
use core::fmt::{self, Debug, Formatter};

/// A trait for types that can be written into a string.
///
/// This works very similarly to `Debug`, and is automatically implemented if `Debug` is
/// implemented for a type. It is also implemented for any writable type inside a `Mutex`.
///
/// The derived implementation of `Debug` [may
/// change](https://doc.rust-lang.org/std/fmt/trait.Debug.html#stability)
/// between Rust versions, so if stability is key for your use case, please implement `Writer`
/// explicitly instead.
pub trait Writer {
    /// Formats the value using the given formatter.
    fn write(&self, f: &mut Formatter<'_>) -> fmt::Result;
}

impl<T: Writer> Writer for Mutex<T> {
    fn write(&self, f: &mut Formatter<'_>) -> fmt::Result {
        self.lock().write(f)
    }
}

impl<T: Debug> Writer for T {
    fn write(&self, f: &mut Formatter<'_>) -> fmt::Result {
        writeln!(f, "{self:?}")
    }
}
