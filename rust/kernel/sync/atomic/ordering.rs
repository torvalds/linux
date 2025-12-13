// SPDX-License-Identifier: GPL-2.0

//! Memory orderings.
//!
//! The semantics of these orderings follows the [`LKMM`] definitions and rules.
//!
//! - [`Acquire`] provides ordering between the load part of the annotated operation and all the
//!   following memory accesses, and if there is a store part, the store part has the [`Relaxed`]
//!   ordering.
//! - [`Release`] provides ordering between all the preceding memory accesses and the store part of
//!   the annotated operation, and if there is a load part, the load part has the [`Relaxed`]
//!   ordering.
//! - [`Full`] means "fully-ordered", that is:
//!   - It provides ordering between all the preceding memory accesses and the annotated operation.
//!   - It provides ordering between the annotated operation and all the following memory accesses.
//!   - It provides ordering between all the preceding memory accesses and all the following memory
//!     accesses.
//!   - All the orderings are the same strength as a full memory barrier (i.e. `smp_mb()`).
//! - [`Relaxed`] provides no ordering except the dependency orderings. Dependency orderings are
//!   described in "DEPENDENCY RELATIONS" in [`LKMM`]'s [`explanation`].
//!
//! [`LKMM`]: srctree/tools/memory-model/
//! [`explanation`]: srctree/tools/memory-model/Documentation/explanation.txt

/// The annotation type for relaxed memory ordering, for the description of relaxed memory
/// ordering, see [module-level documentation].
///
/// [module-level documentation]: crate::sync::atomic::ordering
pub struct Relaxed;

/// The annotation type for acquire memory ordering, for the description of acquire memory
/// ordering, see [module-level documentation].
///
/// [module-level documentation]: crate::sync::atomic::ordering
pub struct Acquire;

/// The annotation type for release memory ordering, for the description of release memory
/// ordering, see [module-level documentation].
///
/// [module-level documentation]: crate::sync::atomic::ordering
pub struct Release;

/// The annotation type for fully-ordered memory ordering, for the description fully-ordered memory
/// ordering, see [module-level documentation].
///
/// [module-level documentation]: crate::sync::atomic::ordering
pub struct Full;

/// Describes the exact memory ordering.
#[doc(hidden)]
pub enum OrderingType {
    /// Relaxed ordering.
    Relaxed,
    /// Acquire ordering.
    Acquire,
    /// Release ordering.
    Release,
    /// Fully-ordered.
    Full,
}

mod internal {
    /// Sealed trait, can be only implemented inside atomic mod.
    pub trait Sealed {}

    impl Sealed for super::Relaxed {}
    impl Sealed for super::Acquire {}
    impl Sealed for super::Release {}
    impl Sealed for super::Full {}
}

/// The trait bound for annotating operations that support any ordering.
pub trait Ordering: internal::Sealed {
    /// Describes the exact memory ordering.
    const TYPE: OrderingType;
}

impl Ordering for Relaxed {
    const TYPE: OrderingType = OrderingType::Relaxed;
}

impl Ordering for Acquire {
    const TYPE: OrderingType = OrderingType::Acquire;
}

impl Ordering for Release {
    const TYPE: OrderingType = OrderingType::Release;
}

impl Ordering for Full {
    const TYPE: OrderingType = OrderingType::Full;
}

/// The trait bound for operations that only support acquire or relaxed ordering.
pub trait AcquireOrRelaxed: Ordering {}

impl AcquireOrRelaxed for Acquire {}
impl AcquireOrRelaxed for Relaxed {}

/// The trait bound for operations that only support release or relaxed ordering.
pub trait ReleaseOrRelaxed: Ordering {}

impl ReleaseOrRelaxed for Release {}
impl ReleaseOrRelaxed for Relaxed {}
