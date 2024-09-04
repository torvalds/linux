// SPDX-License-Identifier: Apache-2.0 OR MIT

//! Rust standard library vendored code.
//!
//! The contents of this file come from the Rust standard library, hosted in
//! the <https://github.com/rust-lang/rust> repository, licensed under
//! "Apache-2.0 OR MIT" and adapted for kernel use. For copyright details,
//! see <https://github.com/rust-lang/rust/blob/master/COPYRIGHT>.

use crate::sync::{arc::ArcInner, Arc};
use core::any::Any;

impl Arc<dyn Any + Send + Sync> {
    /// Attempt to downcast the `Arc<dyn Any + Send + Sync>` to a concrete type.
    pub fn downcast<T>(self) -> core::result::Result<Arc<T>, Self>
    where
        T: Any + Send + Sync,
    {
        if (*self).is::<T>() {
            // SAFETY: We have just checked that the type is correct, so we can cast the pointer.
            unsafe {
                let ptr = self.ptr.cast::<ArcInner<T>>();
                core::mem::forget(self);
                Ok(Arc::from_inner(ptr))
            }
        } else {
            Err(self)
        }
    }
}
