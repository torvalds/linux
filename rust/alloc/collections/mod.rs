// SPDX-License-Identifier: Apache-2.0 OR MIT

//! Collection types.

#![stable(feature = "rust1", since = "1.0.0")]

#[cfg(not(no_global_oom_handling))]
pub mod binary_heap;
#[cfg(not(no_global_oom_handling))]
mod btree;
#[cfg(not(no_global_oom_handling))]
pub mod linked_list;
#[cfg(not(no_global_oom_handling))]
pub mod vec_deque;

#[cfg(not(no_global_oom_handling))]
#[stable(feature = "rust1", since = "1.0.0")]
pub mod btree_map {
    //! An ordered map based on a B-Tree.
    #[stable(feature = "rust1", since = "1.0.0")]
    pub use super::btree::map::*;
}

#[cfg(not(no_global_oom_handling))]
#[stable(feature = "rust1", since = "1.0.0")]
pub mod btree_set {
    //! An ordered set based on a B-Tree.
    #[stable(feature = "rust1", since = "1.0.0")]
    pub use super::btree::set::*;
}

#[cfg(not(no_global_oom_handling))]
#[stable(feature = "rust1", since = "1.0.0")]
#[doc(no_inline)]
pub use binary_heap::BinaryHeap;

#[cfg(not(no_global_oom_handling))]
#[stable(feature = "rust1", since = "1.0.0")]
#[doc(no_inline)]
pub use btree_map::BTreeMap;

#[cfg(not(no_global_oom_handling))]
#[stable(feature = "rust1", since = "1.0.0")]
#[doc(no_inline)]
pub use btree_set::BTreeSet;

#[cfg(not(no_global_oom_handling))]
#[stable(feature = "rust1", since = "1.0.0")]
#[doc(no_inline)]
pub use linked_list::LinkedList;

#[cfg(not(no_global_oom_handling))]
#[stable(feature = "rust1", since = "1.0.0")]
#[doc(no_inline)]
pub use vec_deque::VecDeque;

use crate::alloc::{Layout, LayoutError};
use core::fmt::Display;

/// The error type for `try_reserve` methods.
#[derive(Clone, PartialEq, Eq, Debug)]
#[stable(feature = "try_reserve", since = "1.57.0")]
pub struct TryReserveError {
    kind: TryReserveErrorKind,
}

impl TryReserveError {
    /// Details about the allocation that caused the error
    #[inline]
    #[must_use]
    #[unstable(
        feature = "try_reserve_kind",
        reason = "Uncertain how much info should be exposed",
        issue = "48043"
    )]
    pub fn kind(&self) -> TryReserveErrorKind {
        self.kind.clone()
    }
}

/// Details of the allocation that caused a `TryReserveError`
#[derive(Clone, PartialEq, Eq, Debug)]
#[unstable(
    feature = "try_reserve_kind",
    reason = "Uncertain how much info should be exposed",
    issue = "48043"
)]
pub enum TryReserveErrorKind {
    /// Error due to the computed capacity exceeding the collection's maximum
    /// (usually `isize::MAX` bytes).
    CapacityOverflow,

    /// The memory allocator returned an error
    AllocError {
        /// The layout of allocation request that failed
        layout: Layout,

        #[doc(hidden)]
        #[unstable(
            feature = "container_error_extra",
            issue = "none",
            reason = "\
            Enable exposing the allocator’s custom error value \
            if an associated type is added in the future: \
            https://github.com/rust-lang/wg-allocators/issues/23"
        )]
        non_exhaustive: (),
    },
}

#[unstable(
    feature = "try_reserve_kind",
    reason = "Uncertain how much info should be exposed",
    issue = "48043"
)]
impl From<TryReserveErrorKind> for TryReserveError {
    #[inline]
    fn from(kind: TryReserveErrorKind) -> Self {
        Self { kind }
    }
}

#[unstable(feature = "try_reserve_kind", reason = "new API", issue = "48043")]
impl From<LayoutError> for TryReserveErrorKind {
    /// Always evaluates to [`TryReserveErrorKind::CapacityOverflow`].
    #[inline]
    fn from(_: LayoutError) -> Self {
        TryReserveErrorKind::CapacityOverflow
    }
}

#[stable(feature = "try_reserve", since = "1.57.0")]
impl Display for TryReserveError {
    fn fmt(
        &self,
        fmt: &mut core::fmt::Formatter<'_>,
    ) -> core::result::Result<(), core::fmt::Error> {
        fmt.write_str("memory allocation failed")?;
        let reason = match self.kind {
            TryReserveErrorKind::CapacityOverflow => {
                " because the computed capacity exceeded the collection's maximum"
            }
            TryReserveErrorKind::AllocError { .. } => {
                " because the memory allocator returned an error"
            }
        };
        fmt.write_str(reason)
    }
}

/// An intermediate trait for specialization of `Extend`.
#[doc(hidden)]
trait SpecExtend<I: IntoIterator> {
    /// Extends `self` with the contents of the given iterator.
    fn spec_extend(&mut self, iter: I);
}

#[stable(feature = "try_reserve", since = "1.57.0")]
impl core::error::Error for TryReserveError {}
