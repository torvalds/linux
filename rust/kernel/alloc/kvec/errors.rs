// SPDX-License-Identifier: GPL-2.0

//! Errors for the [`Vec`] type.

use core::fmt::{self, Debug, Formatter};
use kernel::prelude::*;

/// Error type for [`Vec::push_within_capacity`].
pub struct PushError<T>(pub T);

impl<T> Debug for PushError<T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "Not enough capacity")
    }
}

impl<T> From<PushError<T>> for Error {
    fn from(_: PushError<T>) -> Error {
        // Returning ENOMEM isn't appropriate because the system is not out of memory. The vector
        // is just full and we are refusing to resize it.
        EINVAL
    }
}
