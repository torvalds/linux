// SPDX-License-Identifier: Apache-2.0 OR MIT

//! This module contains API-internal items for pin-init.
//!
//! These items must not be used outside of
//! - `kernel/init.rs`
//! - `macros/pin_data.rs`
//! - `macros/pinned_drop.rs`

use super::*;

/// See the [nomicon] for what subtyping is. See also [this table].
///
/// [nomicon]: https://doc.rust-lang.org/nomicon/subtyping.html
/// [this table]: https://doc.rust-lang.org/nomicon/phantom-data.html#table-of-phantomdata-patterns
type Invariant<T> = PhantomData<fn(*mut T) -> *mut T>;

/// This is the module-internal type implementing `PinInit` and `Init`. It is unsafe to create this
/// type, since the closure needs to fulfill the same safety requirement as the
/// `__pinned_init`/`__init` functions.
pub(crate) struct InitClosure<F, T: ?Sized, E>(pub(crate) F, pub(crate) Invariant<(E, T)>);

// SAFETY: While constructing the `InitClosure`, the user promised that it upholds the
// `__init` invariants.
unsafe impl<T: ?Sized, F, E> Init<T, E> for InitClosure<F, T, E>
where
    F: FnOnce(*mut T) -> Result<(), E>,
{
    #[inline]
    unsafe fn __init(self, slot: *mut T) -> Result<(), E> {
        (self.0)(slot)
    }
}
