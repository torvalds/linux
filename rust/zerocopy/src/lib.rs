// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2018 The Fuchsia Authors
//
// Licensed under the 2-Clause BSD License <LICENSE-BSD or
// https://opensource.org/license/bsd-2-clause>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

// After updating the following doc comment, make sure to run the following
// command to update `README.md` based on its contents:
//
//   cargo -q run --manifest-path tools/Cargo.toml -p generate-readme > README.md

//! ***<span style="font-size: 140%">Fast, safe, <span
//! style="color:red;">compile error</span>. Pick two.</span>***
//!
//! Zerocopy makes zero-cost memory manipulation effortless. We write `unsafe`
//! so you don't have to.
//!
//! *For an overview of what's changed from zerocopy 0.7, check out our [release
//! notes][release-notes], which include a step-by-step upgrading guide.*
//!
//! *Have questions? Need more out of zerocopy? Submit a [customer request
//! issue][customer-request-issue] or ask the maintainers on
//! [GitHub][github-q-a] or [Discord][discord]!*
//!
//! [customer-request-issue]: https://github.com/google/zerocopy/issues/new/choose
//! [release-notes]: https://github.com/google/zerocopy/discussions/1680
//! [github-q-a]: https://github.com/google/zerocopy/discussions/categories/q-a
//! [discord]: https://discord.gg/MAvWH2R6zk
//!
//! # Overview
//!
//! ##### Conversion Traits
//!
//! Zerocopy provides four derivable traits for zero-cost conversions:
//! - [`TryFromBytes`] indicates that a type may safely be converted from
//!   certain byte sequences (conditional on runtime checks)
//! - [`FromZeros`] indicates that a sequence of zero bytes represents a valid
//!   instance of a type
//! - [`FromBytes`] indicates that a type may safely be converted from an
//!   arbitrary byte sequence
//! - [`IntoBytes`] indicates that a type may safely be converted *to* a byte
//!   sequence
//!
//! These traits support sized types, slices, and [slice DSTs][slice-dsts].
//!
//! [slice-dsts]: KnownLayout#dynamically-sized-types
//!
//! ##### Marker Traits
//!
//! Zerocopy provides three derivable marker traits that do not provide any
//! functionality themselves, but are required to call certain methods provided
//! by the conversion traits:
//! - [`KnownLayout`] indicates that zerocopy can reason about certain layout
//!   qualities of a type
//! - [`Immutable`] indicates that a type is free from interior mutability,
//!   except by ownership or an exclusive (`&mut`) borrow
//! - [`Unaligned`] indicates that a type's alignment requirement is 1
//!
//! You should generally derive these marker traits whenever possible.
//!
//! ##### Conversion Macros
//!
//! Zerocopy provides six macros for safe casting between types:
//!
//! - ([`try_`][try_transmute])[`transmute`] (conditionally) converts a value of
//!   one type to a value of another type of the same size
//! - ([`try_`][try_transmute_mut])[`transmute_mut`] (conditionally) converts a
//!   mutable reference of one type to a mutable reference of another type of
//!   the same size
//! - ([`try_`][try_transmute_ref])[`transmute_ref`] (conditionally) converts a
//!   mutable or immutable reference of one type to an immutable reference of
//!   another type of the same size
//!
//! These macros perform *compile-time* size and alignment checks, meaning that
//! unconditional casts have zero cost at runtime. Conditional casts do not need
//! to validate size or alignment runtime, but do need to validate contents.
//!
//! These macros cannot be used in generic contexts. For generic conversions,
//! use the methods defined by the [conversion traits](#conversion-traits).
//!
//! ##### Byteorder-Aware Numerics
//!
//! Zerocopy provides byte-order aware integer types that support these
//! conversions; see the [`byteorder`] module. These types are especially useful
//! for network parsing.
//!
//! # Cargo Features
//!
//! - **`alloc`**
//!   By default, `zerocopy` is `no_std`. When the `alloc` feature is enabled,
//!   the `alloc` crate is added as a dependency, and some allocation-related
//!   functionality is added.
//!
//! - **`std`**
//!   By default, `zerocopy` is `no_std`. When the `std` feature is enabled, the
//!   `std` crate is added as a dependency (ie, `no_std` is disabled), and
//!   support for some `std` types is added. `std` implies `alloc`.
//!
//! - **`derive`**
//!   Provides derives for the core marker traits via the `zerocopy-derive`
//!   crate. These derives are re-exported from `zerocopy`, so it is not
//!   necessary to depend on `zerocopy-derive` directly.
//!
//!   However, you may experience better compile times if you instead directly
//!   depend on both `zerocopy` and `zerocopy-derive` in your `Cargo.toml`,
//!   since doing so will allow Rust to compile these crates in parallel. To do
//!   so, do *not* enable the `derive` feature, and list both dependencies in
//!   your `Cargo.toml` with the same leading non-zero version number; e.g:
//!
//!   ```toml
//!   [dependencies]
//!   zerocopy = "0.X"
//!   zerocopy-derive = "0.X"
//!   ```
//!
//!   To avoid the risk of [duplicate import errors][duplicate-import-errors] if
//!   one of your dependencies enables zerocopy's `derive` feature, import
//!   derives as `use zerocopy_derive::*` rather than by name (e.g., `use
//!   zerocopy_derive::FromBytes`).
//!
//! - **`simd`**
//!   When the `simd` feature is enabled, `FromZeros`, `FromBytes`, and
//!   `IntoBytes` impls are emitted for all stable SIMD types which exist on the
//!   target platform. Note that the layout of SIMD types is not yet stabilized,
//!   so these impls may be removed in the future if layout changes make them
//!   invalid. For more information, see the Unsafe Code Guidelines Reference
//!   page on the [layout of packed SIMD vectors][simd-layout].
//!
//! - **`simd-nightly`**
//!   Enables the `simd` feature and adds support for SIMD types which are only
//!   available on nightly. Since these types are unstable, support for any type
//!   may be removed at any point in the future.
//!
//! - **`float-nightly`**
//!   Adds support for the unstable `f16` and `f128` types. These types are
//!   not yet fully implemented and may not be supported on all platforms.
//!
//! [duplicate-import-errors]: https://github.com/google/zerocopy/issues/1587
//! [simd-layout]: https://rust-lang.github.io/unsafe-code-guidelines/layout/packed-simd-vectors.html
//!
//! # Build Tuning
//!
//! ## `--cfg zerocopy_inline_always`
//!
//! Upgrades `#[inline]` to `#[inline(always)]` on many of zerocopy's public
//! functions and methods. This provides a narrowly-scoped alternative that
//! *may* improve the optimization of hot paths using zerocopy without the broad
//! compile-time penalties of configuring `codegen-units=1`.
//!
//! # Security Ethos
//!
//! Zerocopy is expressly designed for use in security-critical contexts. We
//! strive to ensure that that zerocopy code is sound under Rust's current
//! memory model, and *any future memory model*. We ensure this by:
//! - **...not 'guessing' about Rust's semantics.**
//!   We annotate `unsafe` code with a precise rationale for its soundness that
//!   cites a relevant section of Rust's official documentation. When Rust's
//!   documented semantics are unclear, we work with the Rust Operational
//!   Semantics Team to clarify Rust's documentation.
//! - **...rigorously testing our implementation.**
//!   We run tests using [Miri], ensuring that zerocopy is sound across a wide
//!   array of supported target platforms of varying endianness and pointer
//!   width, and across both current and experimental memory models of Rust.
//! - **...formally proving the correctness of our implementation.**
//!   We apply formal verification tools like [Kani][kani] to prove zerocopy's
//!   correctness.
//!
//! For more information, see our full [soundness policy].
//!
//! [Miri]: https://github.com/rust-lang/miri
//! [Kani]: https://github.com/model-checking/kani
//! [soundness policy]: https://github.com/google/zerocopy/blob/main/POLICIES.md#soundness
//!
//! # Relationship to Project Safe Transmute
//!
//! [Project Safe Transmute] is an official initiative of the Rust Project to
//! develop language-level support for safer transmutation. The Project consults
//! with crates like zerocopy to identify aspects of safer transmutation that
//! would benefit from compiler support, and has developed an [experimental,
//! compiler-supported analysis][mcp-transmutability] which determines whether,
//! for a given type, any value of that type may be soundly transmuted into
//! another type. Once this functionality is sufficiently mature, zerocopy
//! intends to replace its internal transmutability analysis (implemented by our
//! custom derives) with the compiler-supported one. This change will likely be
//! an implementation detail that is invisible to zerocopy's users.
//!
//! Project Safe Transmute will not replace the need for most of zerocopy's
//! higher-level abstractions. The experimental compiler analysis is a tool for
//! checking the soundness of `unsafe` code, not a tool to avoid writing
//! `unsafe` code altogether. For the foreseeable future, crates like zerocopy
//! will still be required in order to provide higher-level abstractions on top
//! of the building block provided by Project Safe Transmute.
//!
//! [Project Safe Transmute]: https://rust-lang.github.io/rfcs/2835-project-safe-transmute.html
//! [mcp-transmutability]: https://github.com/rust-lang/compiler-team/issues/411
//!
//! # MSRV
//!
//! See our [MSRV policy].
//!
//! [MSRV policy]: https://github.com/google/zerocopy/blob/main/POLICIES.md#msrv
//!
//! # Changelog
//!
//! Zerocopy uses [GitHub Releases].
//!
//! [GitHub Releases]: https://github.com/google/zerocopy/releases
//!
//! # Thanks
//!
//! Zerocopy is maintained by engineers at Google with help from [many wonderful
//! contributors][contributors]. Thank you to everyone who has lent a hand in
//! making Rust a little more secure!
//!
//! [contributors]: https://github.com/google/zerocopy/graphs/contributors

// Sometimes we want to use lints which were added after our MSRV.
// `unknown_lints` is `warn` by default and we deny warnings in CI, so without
// this attribute, any unknown lint would cause a CI failure when testing with
// our MSRV.
#![allow(unknown_lints, non_local_definitions, unreachable_patterns)]
#![deny(renamed_and_removed_lints)]
#![deny(
    anonymous_parameters,
    deprecated_in_future,
    late_bound_lifetime_arguments,
    missing_copy_implementations,
    missing_debug_implementations,
    missing_docs,
    path_statements,
    patterns_in_fns_without_body,
    rust_2018_idioms,
    trivial_numeric_casts,
    unreachable_pub,
    unsafe_op_in_unsafe_fn,
    unused_extern_crates,
    // We intentionally choose not to deny `unused_qualifications`. When items
    // are added to the prelude (e.g., `core::mem::size_of`), this has the
    // consequence of making some uses trigger this lint on the latest toolchain
    // (e.g., `mem::size_of`), but fixing it (e.g. by replacing with `size_of`)
    // does not work on older toolchains.
    //
    // We tested a more complicated fix in #1413, but ultimately decided that,
    // since this lint is just a minor style lint, the complexity isn't worth it
    // - it's fine to occasionally have unused qualifications slip through,
    // especially since these do not affect our user-facing API in any way.
    variant_size_differences
)]
#![cfg_attr(
    __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS,
    deny(fuzzy_provenance_casts, lossy_provenance_casts)
)]
#![deny(
    clippy::all,
    clippy::alloc_instead_of_core,
    clippy::arithmetic_side_effects,
    clippy::as_underscore,
    clippy::assertions_on_result_states,
    clippy::as_conversions,
    clippy::correctness,
    clippy::dbg_macro,
    clippy::decimal_literal_representation,
    clippy::double_must_use,
    clippy::get_unwrap,
    clippy::indexing_slicing,
    clippy::missing_inline_in_public_items,
    clippy::missing_safety_doc,
    clippy::multiple_unsafe_ops_per_block,
    clippy::must_use_candidate,
    clippy::must_use_unit,
    clippy::obfuscated_if_else,
    clippy::perf,
    clippy::print_stdout,
    clippy::return_self_not_must_use,
    clippy::std_instead_of_core,
    clippy::style,
    clippy::suspicious,
    clippy::todo,
    clippy::undocumented_unsafe_blocks,
    clippy::unimplemented,
    clippy::unnested_or_patterns,
    clippy::unwrap_used,
    clippy::use_debug
)]
// `clippy::incompatible_msrv` (implied by `clippy::suspicious`): This sometimes
// has false positives, and we test on our MSRV in CI, so it doesn't help us
// anyway.
#![allow(clippy::needless_lifetimes, clippy::type_complexity, clippy::incompatible_msrv)]
#![deny(
    rustdoc::bare_urls,
    rustdoc::broken_intra_doc_links,
    rustdoc::invalid_codeblock_attributes,
    rustdoc::invalid_html_tags,
    rustdoc::invalid_rust_codeblocks,
    rustdoc::missing_crate_level_docs,
    rustdoc::private_intra_doc_links
)]
// In test code, it makes sense to weight more heavily towards concise, readable
// code over correct or debuggable code.
#![cfg_attr(any(test, kani), allow(
    // In tests, you get line numbers and have access to source code, so panic
    // messages are less important. You also often unwrap a lot, which would
    // make expect'ing instead very verbose.
    clippy::unwrap_used,
    // In tests, there's no harm to "panic risks" - the worst that can happen is
    // that your test will fail, and you'll fix it. By contrast, panic risks in
    // production code introduce the possibly of code panicking unexpectedly "in
    // the field".
    clippy::arithmetic_side_effects,
    clippy::indexing_slicing,
))]
#![cfg_attr(not(any(test, kani, feature = "std")), no_std)]
#![cfg_attr(
    all(feature = "simd-nightly", target_arch = "arm"),
    feature(stdarch_arm_neon_intrinsics)
)]
#![cfg_attr(
    all(feature = "simd-nightly", any(target_arch = "powerpc", target_arch = "powerpc64")),
    feature(stdarch_powerpc)
)]
#![cfg_attr(feature = "float-nightly", feature(f16, f128))]
#![cfg_attr(doc_cfg, feature(doc_cfg))]
#![cfg_attr(__ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS, feature(coverage_attribute))]
#![cfg_attr(
    any(__ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS, miri),
    feature(layout_for_ptr)
)]
#![cfg_attr(all(test, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), feature(test))]

// This is a hack to allow zerocopy-derive derives to work in this crate. They
// assume that zerocopy is linked as an extern crate, so they access items from
// it as `zerocopy::Xxx`. This makes that still work.
#[cfg(any(feature = "derive", test))]
extern crate self as zerocopy;

#[cfg(all(test, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS))]
extern crate test;

#[doc(hidden)]
#[macro_use]
pub mod util;

pub mod byte_slice;
pub mod byteorder;
mod deprecated;

#[cfg(__ZEROCOPY_INTERNAL_USE_ONLY_DEV_MODE)]
pub mod doctests;

// This module is `pub` so that zerocopy's error types and error handling
// documentation is grouped together in a cohesive module. In practice, we
// expect most users to use the re-export of `error`'s items to avoid identifier
// stuttering.
pub mod error;
mod impls;
#[doc(hidden)]
pub mod layout;
mod macros;
#[cfg_attr(not(zerocopy_unstable_ptr), doc(hidden))]
#[cfg_attr(doc_cfg, doc(cfg(zerocopy_unstable_ptr)))]
pub mod pointer;
mod r#ref;
mod split_at;
// FIXME(#252): If we make this pub, come up with a better name.
mod wrappers;

use core::{
    cell::{Cell, UnsafeCell},
    cmp::Ordering,
    fmt::{self, Debug, Display, Formatter},
    hash::Hasher,
    marker::PhantomData,
    mem::{self, ManuallyDrop, MaybeUninit as CoreMaybeUninit},
    num::{
        NonZeroI128, NonZeroI16, NonZeroI32, NonZeroI64, NonZeroI8, NonZeroIsize, NonZeroU128,
        NonZeroU16, NonZeroU32, NonZeroU64, NonZeroU8, NonZeroUsize, Wrapping,
    },
    ops::{Deref, DerefMut},
    ptr::{self, NonNull},
    slice,
};
#[cfg(feature = "std")]
use std::io;

#[doc(hidden)]
pub use crate::pointer::{
    invariant::{self, BecauseExclusive},
    PtrInner,
};
pub use crate::{
    byte_slice::*,
    byteorder::*,
    error::*,
    r#ref::*,
    split_at::{Split, SplitAt},
    wrappers::*,
};

#[cfg(any(feature = "alloc", test, kani))]
extern crate alloc;
#[cfg(any(feature = "alloc", test))]
use alloc::{boxed::Box, vec::Vec};
#[cfg(any(feature = "alloc", test))]
use core::alloc::Layout;

// Used by `KnownLayout`.
#[doc(hidden)]
pub use crate::layout::*;
// Used by `TryFromBytes::is_bit_valid`.
#[doc(hidden)]
pub use crate::pointer::{invariant::BecauseImmutable, Maybe, Ptr};
// For each trait polyfill, as soon as the corresponding feature is stable, the
// polyfill import will be unused because method/function resolution will prefer
// the inherent method/function over a trait method/function. Thus, we suppress
// the `unused_imports` warning.
//
// See the documentation on `util::polyfills` for more information.
#[allow(unused_imports)]
use crate::util::polyfills::{self, NonNullExt as _, NumExt as _};
#[cfg_attr(not(zerocopy_unstable_ptr), doc(hidden))]
#[cfg_attr(doc_cfg, doc(cfg(zerocopy_unstable_ptr)))]
pub use crate::util::MetadataOf;

#[cfg(all(test, not(__ZEROCOPY_INTERNAL_USE_ONLY_DEV_MODE)))]
const _: () = {
    #[deprecated = "Development of zerocopy using cargo is not supported. Please use `cargo.sh` or `win-cargo.bat` instead."]
    #[allow(unused)]
    const WARNING: () = ();
    #[warn(deprecated)]
    WARNING
};

/// Implements [`KnownLayout`].
///
/// This derive analyzes various aspects of a type's layout that are needed for
/// some of zerocopy's APIs. It can be applied to structs, enums, and unions;
/// e.g.:
///
/// ```
/// # use zerocopy_derive::KnownLayout;
/// #[derive(KnownLayout)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(KnownLayout)]
/// enum MyEnum {
/// #   V00,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(KnownLayout)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// # Limitations
///
/// This derive cannot currently be applied to unsized structs without an
/// explicit `repr` attribute.
///
/// Some invocations of this derive run afoul of a [known bug] in Rust's type
/// privacy checker. For example, this code:
///
/// ```compile_fail,E0446
/// use zerocopy::*;
/// # use zerocopy_derive::*;
///
/// #[derive(KnownLayout)]
/// #[repr(C)]
/// pub struct PublicType {
///     leading: Foo,
///     trailing: Bar,
/// }
///
/// #[derive(KnownLayout)]
/// struct Foo;
///
/// #[derive(KnownLayout)]
/// struct Bar;
/// ```
///
/// ...results in a compilation error:
///
/// ```text
/// error[E0446]: private type `Bar` in public interface
///  --> examples/bug.rs:3:10
///    |
/// 3  | #[derive(KnownLayout)]
///    |          ^^^^^^^^^^^ can't leak private type
/// ...
/// 14 | struct Bar;
///    | ---------- `Bar` declared as private
///    |
///    = note: this error originates in the derive macro `KnownLayout` (in Nightly builds, run with -Z macro-backtrace for more info)
/// ```
///
/// This issue arises when `#[derive(KnownLayout)]` is applied to `repr(C)`
/// structs whose trailing field type is less public than the enclosing struct.
///
/// To work around this, mark the trailing field type `pub` and annotate it with
/// `#[doc(hidden)]`; e.g.:
///
/// ```no_run
/// use zerocopy::*;
/// # use zerocopy_derive::*;
///
/// #[derive(KnownLayout)]
/// #[repr(C)]
/// pub struct PublicType {
///     leading: Foo,
///     trailing: Bar,
/// }
///
/// #[derive(KnownLayout)]
/// struct Foo;
///
/// #[doc(hidden)]
/// #[derive(KnownLayout)]
/// pub struct Bar; // <- `Bar` is now also `pub`
/// ```
///
/// [known bug]: https://github.com/rust-lang/rust/issues/45713
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::KnownLayout;
// These exist so that code which was written against the old names will get
// less confusing error messages when they upgrade to a more recent version of
// zerocopy. On our MSRV toolchain, the error messages read, for example:
//
//   error[E0603]: trait `FromZeroes` is private
//       --> examples/deprecated.rs:1:15
//        |
//   1    | use zerocopy::FromZeroes;
//        |               ^^^^^^^^^^ private trait
//        |
//   note: the trait `FromZeroes` is defined here
//       --> /Users/josh/workspace/zerocopy/src/lib.rs:1845:5
//        |
//   1845 | use FromZeros as FromZeroes;
//        |     ^^^^^^^^^^^^^^^^^^^^^^^
//
// The "note" provides enough context to make it easy to figure out how to fix
// the error.
#[allow(unused)]
use {FromZeros as FromZeroes, IntoBytes as AsBytes, Ref as LayoutVerified};

/// Indicates that zerocopy can reason about certain aspects of a type's layout.
///
/// This trait is required by many of zerocopy's APIs. It supports sized types,
/// slices, and [slice DSTs](#dynamically-sized-types).
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(KnownLayout)]`][derive]; e.g.:
///
/// ```
/// # use zerocopy_derive::KnownLayout;
/// #[derive(KnownLayout)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(KnownLayout)]
/// enum MyEnum {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(KnownLayout)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// This derive performs a sophisticated analysis to deduce the layout
/// characteristics of types. You **must** implement this trait via the derive.
///
/// # Dynamically-sized types
///
/// `KnownLayout` supports slice-based dynamically sized types ("slice DSTs").
///
/// A slice DST is a type whose trailing field is either a slice or another
/// slice DST, rather than a type with fixed size. For example:
///
/// ```
/// #[repr(C)]
/// struct PacketHeader {
/// # /*
///     ...
/// # */
/// }
///
/// #[repr(C)]
/// struct Packet {
///     header: PacketHeader,
///     body: [u8],
/// }
/// ```
///
/// It can be useful to think of slice DSTs as a generalization of slices - in
/// other words, a normal slice is just the special case of a slice DST with
/// zero leading fields. In particular:
/// - Like slices, slice DSTs can have different lengths at runtime
/// - Like slices, slice DSTs cannot be passed by-value, but only by reference
///   or via other indirection such as `Box`
/// - Like slices, a reference (or `Box`, or other pointer type) to a slice DST
///   encodes the number of elements in the trailing slice field
///
/// ## Slice DST layout
///
/// Just like other composite Rust types, the layout of a slice DST is not
/// well-defined unless it is specified using an explicit `#[repr(...)]`
/// attribute such as `#[repr(C)]`. [Other representations are
/// supported][reprs], but in this section, we'll use `#[repr(C)]` as our
/// example.
///
/// A `#[repr(C)]` slice DST is laid out [just like sized `#[repr(C)]`
/// types][repr-c-structs], but the presence of a variable-length field
/// introduces the possibility of *dynamic padding*. In particular, it may be
/// necessary to add trailing padding *after* the trailing slice field in order
/// to satisfy the outer type's alignment, and the amount of padding required
/// may be a function of the length of the trailing slice field. This is just a
/// natural consequence of the normal `#[repr(C)]` rules applied to slice DSTs,
/// but it can result in surprising behavior. For example, consider the
/// following type:
///
/// ```
/// #[repr(C)]
/// struct Foo {
///     a: u32,
///     b: u8,
///     z: [u16],
/// }
/// ```
///
/// Assuming that `u32` has alignment 4 (this is not true on all platforms),
/// then `Foo` has alignment 4 as well. Here is the smallest possible value for
/// `Foo`:
///
/// ```text
/// byte offset | 01234567
///       field | aaaab---
///                    ><
/// ```
///
/// In this value, `z` has length 0. Abiding by `#[repr(C)]`, the lowest offset
/// that we can place `z` at is 5, but since `z` has alignment 2, we need to
/// round up to offset 6. This means that there is one byte of padding between
/// `b` and `z`, then 0 bytes of `z` itself (denoted `><` in this diagram), and
/// then two bytes of padding after `z` in order to satisfy the overall
/// alignment of `Foo`. The size of this instance is 8 bytes.
///
/// What about if `z` has length 1?
///
/// ```text
/// byte offset | 01234567
///       field | aaaab-zz
/// ```
///
/// In this instance, `z` has length 1, and thus takes up 2 bytes. That means
/// that we no longer need padding after `z` in order to satisfy `Foo`'s
/// alignment. We've now seen two different values of `Foo` with two different
/// lengths of `z`, but they both have the same size - 8 bytes.
///
/// What about if `z` has length 2?
///
/// ```text
/// byte offset | 012345678901
///       field | aaaab-zzzz--
/// ```
///
/// Now `z` has length 2, and thus takes up 4 bytes. This brings our un-padded
/// size to 10, and so we now need another 2 bytes of padding after `z` to
/// satisfy `Foo`'s alignment.
///
/// Again, all of this is just a logical consequence of the `#[repr(C)]` rules
/// applied to slice DSTs, but it can be surprising that the amount of trailing
/// padding becomes a function of the trailing slice field's length, and thus
/// can only be computed at runtime.
///
/// [reprs]: https://doc.rust-lang.org/reference/type-layout.html#representations
/// [repr-c-structs]: https://doc.rust-lang.org/reference/type-layout.html#reprc-structs
///
/// ## What is a valid size?
///
/// There are two places in zerocopy's API that we refer to "a valid size" of a
/// type. In normal casts or conversions, where the source is a byte slice, we
/// need to know whether the source byte slice is a valid size of the
/// destination type. In prefix or suffix casts, we need to know whether *there
/// exists* a valid size of the destination type which fits in the source byte
/// slice and, if so, what the largest such size is.
///
/// As outlined above, a slice DST's size is defined by the number of elements
/// in its trailing slice field. However, there is not necessarily a 1-to-1
/// mapping between trailing slice field length and overall size. As we saw in
/// the previous section with the type `Foo`, instances with both 0 and 1
/// elements in the trailing `z` field result in a `Foo` whose size is 8 bytes.
///
/// When we say "x is a valid size of `T`", we mean one of two things:
/// - If `T: Sized`, then we mean that `x == size_of::<T>()`
/// - If `T` is a slice DST, then we mean that there exists a `len` such that the instance of
///   `T` with `len` trailing slice elements has size `x`
///
/// When we say "largest possible size of `T` that fits in a byte slice", we
/// mean one of two things:
/// - If `T: Sized`, then we mean `size_of::<T>()` if the byte slice is at least
///   `size_of::<T>()` bytes long
/// - If `T` is a slice DST, then we mean to consider all values, `len`, such
///   that the instance of `T` with `len` trailing slice elements fits in the
///   byte slice, and to choose the largest such `len`, if any
///
///
/// # Safety
///
/// This trait does not convey any safety guarantees to code outside this crate.
///
/// You must not rely on the `#[doc(hidden)]` internals of `KnownLayout`. Future
/// releases of zerocopy may make backwards-breaking changes to these items,
/// including changes that only affect soundness, which may cause code which
/// uses those items to silently become unsound.
///
#[cfg_attr(feature = "derive", doc = "[derive]: zerocopy_derive::KnownLayout")]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.KnownLayout.html"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(KnownLayout)]` to `{Self}`")
)]
pub unsafe trait KnownLayout {
    // The `Self: Sized` bound makes it so that `KnownLayout` can still be
    // object safe. It's not currently object safe thanks to `const LAYOUT`, and
    // it likely won't be in the future, but there's no reason not to be
    // forwards-compatible with object safety.
    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// The type of metadata stored in a pointer to `Self`.
    ///
    /// This is `()` for sized types and [`usize`] for slice DSTs.
    type PointerMetadata: PointerMetadata;

    /// A maybe-uninitialized analog of `Self`
    ///
    /// # Safety
    ///
    /// `Self::LAYOUT` and `Self::MaybeUninit::LAYOUT` are identical.
    /// `Self::MaybeUninit` admits uninitialized bytes in all positions.
    #[doc(hidden)]
    type MaybeUninit: ?Sized + KnownLayout<PointerMetadata = Self::PointerMetadata>;

    /// The layout of `Self`.
    ///
    /// # Safety
    ///
    /// Callers may assume that `LAYOUT` accurately reflects the layout of
    /// `Self`. In particular:
    /// - `LAYOUT.align` is equal to `Self`'s alignment
    /// - If `Self: Sized`, then `LAYOUT.size_info == SizeInfo::Sized { size }`
    ///   where `size == size_of::<Self>()`
    /// - If `Self` is a slice DST, then `LAYOUT.size_info ==
    ///   SizeInfo::SliceDst(slice_layout)` where:
    ///   - The size, `size`, of an instance of `Self` with `elems` trailing
    ///     slice elements is equal to `slice_layout.offset +
    ///     slice_layout.elem_size * elems` rounded up to the nearest multiple
    ///     of `LAYOUT.align`
    ///   - For such an instance, any bytes in the range `[slice_layout.offset +
    ///     slice_layout.elem_size * elems, size)` are padding and must not be
    ///     assumed to be initialized
    #[doc(hidden)]
    const LAYOUT: DstLayout;

    /// SAFETY: The returned pointer has the same address and provenance as
    /// `bytes`. If `Self` is a DST, the returned pointer's referent has `elems`
    /// elements in its trailing slice.
    #[doc(hidden)]
    fn raw_from_ptr_len(bytes: NonNull<u8>, meta: Self::PointerMetadata) -> NonNull<Self>;

    /// Extracts the metadata from a pointer to `Self`.
    ///
    /// # Safety
    ///
    /// `pointer_to_metadata` always returns the correct metadata stored in
    /// `ptr`.
    #[doc(hidden)]
    fn pointer_to_metadata(ptr: *mut Self) -> Self::PointerMetadata;

    /// Computes the length of the byte range addressed by `ptr`.
    ///
    /// Returns `None` if the resulting length would not fit in an `usize`.
    ///
    /// # Safety
    ///
    /// Callers may assume that `size_of_val_raw` always returns the correct
    /// size.
    ///
    /// Callers may assume that, if `ptr` addresses a byte range whose length
    /// fits in an `usize`, this will return `Some`.
    #[doc(hidden)]
    #[must_use]
    #[inline(always)]
    fn size_of_val_raw(ptr: NonNull<Self>) -> Option<usize> {
        let meta = Self::pointer_to_metadata(ptr.as_ptr());
        // SAFETY: `size_for_metadata` promises to only return `None` if the
        // resulting size would not fit in a `usize`.
        Self::size_for_metadata(meta)
    }

    #[doc(hidden)]
    #[must_use]
    #[inline(always)]
    fn raw_dangling() -> NonNull<Self> {
        let meta = Self::PointerMetadata::from_elem_count(0);
        Self::raw_from_ptr_len(NonNull::dangling(), meta)
    }

    /// Computes the size of an object of type `Self` with the given pointer
    /// metadata.
    ///
    /// # Safety
    ///
    /// `size_for_metadata` promises to return `None` if and only if the
    /// resulting size would not fit in a [`usize`]. Note that the returned size
    /// could exceed the actual maximum valid size of an allocated object,
    /// [`isize::MAX`].
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::KnownLayout;
    ///
    /// assert_eq!(u8::size_for_metadata(()), Some(1));
    /// assert_eq!(u16::size_for_metadata(()), Some(2));
    /// assert_eq!(<[u8]>::size_for_metadata(42), Some(42));
    /// assert_eq!(<[u16]>::size_for_metadata(42), Some(84));
    ///
    /// // This size exceeds the maximum valid object size (`isize::MAX`):
    /// assert_eq!(<[u8]>::size_for_metadata(usize::MAX), Some(usize::MAX));
    ///
    /// // This size, if computed, would exceed `usize::MAX`:
    /// assert_eq!(<[u16]>::size_for_metadata(usize::MAX), None);
    /// ```
    #[inline(always)]
    fn size_for_metadata(meta: Self::PointerMetadata) -> Option<usize> {
        meta.size_for_metadata(Self::LAYOUT)
    }

    /// Computes whether `meta` can describe a valid allocation of `Self`.
    ///
    /// # Safety
    ///
    /// `is_valid_metadata` promises to return `true` if and only if the size of
    /// an allocation of `Self` with `meta` would not overflow an
    /// [`isize::MAX`].
    #[doc(hidden)]
    #[inline(always)]
    fn is_valid_metadata(meta: Self::PointerMetadata) -> bool {
        meta.to_elem_count() <= maximum_trailing_slice_len::<Self>().to_elem_count()
    }
}

/// Efficiently produces the [`TrailingSliceLayout`] of `T`.
#[inline(always)]
pub(crate) fn trailing_slice_layout<T>() -> TrailingSliceLayout
where
    T: ?Sized + KnownLayout<PointerMetadata = usize>,
{
    trait LayoutFacts {
        const SIZE_INFO: TrailingSliceLayout;
    }

    impl<T: ?Sized> LayoutFacts for T
    where
        T: KnownLayout<PointerMetadata = usize>,
    {
        const SIZE_INFO: TrailingSliceLayout = match T::LAYOUT.size_info {
            crate::SizeInfo::Sized { .. } => const_panic!("unreachable"),
            crate::SizeInfo::SliceDst(info) => info,
        };
    }

    T::SIZE_INFO
}

/// Efficiently produces the maximum trailing slice length `T`.
#[inline(always)]
pub(crate) fn maximum_trailing_slice_len<T>() -> usize
where
    T: ?Sized + KnownLayout,
{
    trait LayoutFacts {
        const MAX_LEN: usize;
    }

    impl<T: ?Sized> LayoutFacts for T
    where
        T: KnownLayout,
    {
        const MAX_LEN: usize = match T::LAYOUT.size_info {
            SizeInfo::SliceDst(TrailingSliceLayout { elem_size: 0, .. }) => usize::MAX,
            _ => match T::LAYOUT.validate_cast_and_convert_metadata(
                T::LAYOUT.align.get(),
                DstLayout::MAX_SIZE,
                CastType::Prefix,
            ) {
                Ok((elems, _)) => elems,
                Err(_) => const_panic!("unreachable"),
            },
        };
    }

    T::MAX_LEN
}

/// The metadata associated with a [`KnownLayout`] type.
#[doc(hidden)]
pub trait PointerMetadata: Copy + Eq + Debug + Ord {
    /// Constructs a `Self` from an element count.
    ///
    /// If `Self = ()`, this returns `()`. If `Self = usize`, this returns
    /// `elems`. No other types are currently supported.
    fn from_elem_count(elems: usize) -> Self;

    /// Converts `self` to an element count.
    ///
    /// If `Self = ()`, this returns `0`. If `Self = usize`, this returns
    /// `self`. No other types are currently supported.
    fn to_elem_count(self) -> usize;

    /// Computes the size of the object with the given layout and pointer
    /// metadata.
    ///
    /// # Panics
    ///
    /// If `Self = ()`, `layout` must describe a sized type. If `Self = usize`,
    /// `layout` must describe a slice DST. Otherwise, `size_for_metadata` may
    /// panic.
    ///
    /// # Safety
    ///
    /// `size_for_metadata` promises to only return `None` if the resulting size
    /// would not fit in a `usize`.
    fn size_for_metadata(self, layout: DstLayout) -> Option<usize>;
}

impl PointerMetadata for () {
    #[inline]
    #[allow(clippy::unused_unit)]
    fn from_elem_count(_elems: usize) -> () {}

    #[inline]
    fn to_elem_count(self) -> usize {
        0
    }

    #[inline]
    fn size_for_metadata(self, layout: DstLayout) -> Option<usize> {
        match layout.size_info {
            SizeInfo::Sized { size } => Some(size),
            // NOTE: This branch is unreachable, but we return `None` rather
            // than `unreachable!()` to avoid generating panic paths.
            SizeInfo::SliceDst(_) => None,
        }
    }
}

impl PointerMetadata for usize {
    #[inline]
    fn from_elem_count(elems: usize) -> usize {
        elems
    }

    #[inline]
    fn to_elem_count(self) -> usize {
        self
    }

    #[inline]
    fn size_for_metadata(self, layout: DstLayout) -> Option<usize> {
        match layout.size_info {
            SizeInfo::SliceDst(TrailingSliceLayout { offset, elem_size }) => {
                let slice_len = elem_size.checked_mul(self)?;
                let without_padding = offset.checked_add(slice_len)?;
                without_padding.checked_add(util::padding_needed_for(without_padding, layout.align))
            }
            // NOTE: This branch is unreachable, but we return `None` rather
            // than `unreachable!()` to avoid generating panic paths.
            SizeInfo::Sized { .. } => None,
        }
    }
}

// SAFETY: Delegates safety to `DstLayout::for_slice`.
unsafe impl<T> KnownLayout for [T] {
    #[allow(clippy::missing_inline_in_public_items, dead_code)]
    #[cfg_attr(
        all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS),
        coverage(off)
    )]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized,
    {
    }

    type PointerMetadata = usize;

    // SAFETY: `CoreMaybeUninit<T>::LAYOUT` and `T::LAYOUT` are identical
    // because `CoreMaybeUninit<T>` has the same size and alignment as `T` [1].
    // Consequently, `[CoreMaybeUninit<T>]::LAYOUT` and `[T]::LAYOUT` are
    // identical, because they both lack a fixed-sized prefix and because they
    // inherit the alignments of their inner element type (which are identical)
    // [2][3].
    //
    // `[CoreMaybeUninit<T>]` admits uninitialized bytes at all positions
    // because `CoreMaybeUninit<T>` admits uninitialized bytes at all positions
    // and because the inner elements of `[CoreMaybeUninit<T>]` are laid out
    // back-to-back [2][3].
    //
    // [1] Per https://doc.rust-lang.org/1.81.0/std/mem/union.MaybeUninit.html#layout-1:
    //
    //   `MaybeUninit<T>` is guaranteed to have the same size, alignment, and ABI as
    //   `T`
    //
    // [2] Per https://doc.rust-lang.org/1.82.0/reference/type-layout.html#slice-layout:
    //
    //   Slices have the same layout as the section of the array they slice.
    //
    // [3] Per https://doc.rust-lang.org/1.82.0/reference/type-layout.html#array-layout:
    //
    //   An array of `[T; N]` has a size of `size_of::<T>() * N` and the same
    //   alignment of `T`. Arrays are laid out so that the zero-based `nth`
    //   element of the array is offset from the start of the array by `n *
    //   size_of::<T>()` bytes.
    type MaybeUninit = [CoreMaybeUninit<T>];

    const LAYOUT: DstLayout = DstLayout::for_slice::<T>();

    // SAFETY: `.cast` preserves address and provenance. The returned pointer
    // refers to an object with `elems` elements by construction.
    #[inline(always)]
    fn raw_from_ptr_len(data: NonNull<u8>, elems: usize) -> NonNull<Self> {
        // FIXME(#67): Remove this allow. See NonNullExt for more details.
        #[allow(unstable_name_collisions)]
        NonNull::slice_from_raw_parts(data.cast::<T>(), elems)
    }

    #[inline(always)]
    fn pointer_to_metadata(ptr: *mut [T]) -> usize {
        #[allow(clippy::as_conversions)]
        let slc = ptr as *const [()];

        // SAFETY:
        // - `()` has alignment 1, so `slc` is trivially aligned.
        // - `slc` was derived from a non-null pointer.
        // - The size is 0 regardless of the length, so it is sound to
        //   materialize a reference regardless of location.
        // - By invariant, `self.ptr` has valid provenance.
        let slc = unsafe { &*slc };

        // This is correct because the preceding `as` cast preserves the number
        // of slice elements. [1]
        //
        // [1] Per https://doc.rust-lang.org/reference/expressions/operator-expr.html#pointer-to-pointer-cast:
        //
        //   For slice types like `[T]` and `[U]`, the raw pointer types `*const
        //   [T]`, `*mut [T]`, `*const [U]`, and `*mut [U]` encode the number of
        //   elements in this slice. Casts between these raw pointer types
        //   preserve the number of elements. ... The same holds for `str` and
        //   any compound type whose unsized tail is a slice type, such as
        //   struct `Foo(i32, [u8])` or `(u64, Foo)`.
        slc.len()
    }
}

#[rustfmt::skip]
impl_known_layout!(
    (),
    u8, i8, u16, i16, u32, i32, u64, i64, u128, i128, usize, isize, f32, f64,
    bool, char,
    NonZeroU8, NonZeroI8, NonZeroU16, NonZeroI16, NonZeroU32, NonZeroI32,
    NonZeroU64, NonZeroI64, NonZeroU128, NonZeroI128, NonZeroUsize, NonZeroIsize
);
#[rustfmt::skip]
#[cfg(feature = "float-nightly")]
impl_known_layout!(
    #[cfg_attr(doc_cfg, doc(cfg(feature = "float-nightly")))]
    f16,
    #[cfg_attr(doc_cfg, doc(cfg(feature = "float-nightly")))]
    f128
);
#[rustfmt::skip]
impl_known_layout!(
    T         => Option<T>,
    T: ?Sized => PhantomData<T>,
    T         => Wrapping<T>,
    T         => CoreMaybeUninit<T>,
    T: ?Sized => *const T,
    T: ?Sized => *mut T,
    T: ?Sized => &'_ T,
    T: ?Sized => &'_ mut T,
);
impl_known_layout!(const N: usize, T => [T; N]);

// SAFETY: `str` has the same representation as `[u8]`. `ManuallyDrop<T>` [1],
// `UnsafeCell<T>` [2], and `Cell<T>` [3] have the same representation as `T`.
//
// [1] Per https://doc.rust-lang.org/1.85.0/std/mem/struct.ManuallyDrop.html:
//
//   `ManuallyDrop<T>` is guaranteed to have the same layout and bit validity as
//   `T`
//
// [2] Per https://doc.rust-lang.org/1.85.0/core/cell/struct.UnsafeCell.html#memory-layout:
//
//   `UnsafeCell<T>` has the same in-memory representation as its inner type
//   `T`.
//
// [3] Per https://doc.rust-lang.org/1.85.0/core/cell/struct.Cell.html#memory-layout:
//
//   `Cell<T>` has the same in-memory representation as `T`.
#[allow(clippy::multiple_unsafe_ops_per_block)]
const _: () = unsafe {
    unsafe_impl_known_layout!(
        #[repr([u8])]
        str
    );
    unsafe_impl_known_layout!(T: ?Sized + KnownLayout => #[repr(T)] ManuallyDrop<T>);
    unsafe_impl_known_layout!(T: ?Sized + KnownLayout => #[repr(T)] UnsafeCell<T>);
    unsafe_impl_known_layout!(T: ?Sized + KnownLayout => #[repr(T)] Cell<T>);
};

// SAFETY:
// - By consequence of the invariant on `T::MaybeUninit` that `T::LAYOUT` and
//   `T::MaybeUninit::LAYOUT` are equal, `T` and `T::MaybeUninit` have the same:
//   - Fixed prefix size
//   - Alignment
//   - (For DSTs) trailing slice element size
// - By consequence of the above, referents `T::MaybeUninit` and `T` have the
//   require the same kind of pointer metadata, and thus it is valid to perform
//   an `as` cast from `*mut T` and `*mut T::MaybeUninit`, and this operation
//   preserves referent size (ie, `size_of_val_raw`).
const _: () = unsafe {
    unsafe_impl_known_layout!(T: ?Sized + KnownLayout => #[repr(T::MaybeUninit)] MaybeUninit<T>)
};

// FIXME(#196, #2856): Eventually, we'll want to support enums variants and
// union fields being treated uniformly since they behave similarly to each
// other in terms of projecting validity – specifically, for a type `T` with
// validity `V`, if `T` is a struct type, then its fields straightforwardly also
// have validity `V`. By contrast, if `T` is an enum or union type, then
// validity is not straightforwardly recursive in this way.
#[doc(hidden)]
pub const STRUCT_VARIANT_ID: i128 = -1;
#[doc(hidden)]
pub const UNION_VARIANT_ID: i128 = -2;
#[doc(hidden)]
pub const REPR_C_UNION_VARIANT_ID: i128 = -3;

/// # Safety
///
/// `Self::ProjectToTag` must satisfy its safety invariant.
#[doc(hidden)]
pub unsafe trait HasTag {
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// The type's enum tag, or `()` for non-enum types.
    type Tag: Immutable;

    /// A pointer projection from `Self` to its tag.
    ///
    /// # Safety
    ///
    /// It must be the case that, for all `slf: Ptr<'_, Self, I>`, it is sound
    /// to project from `slf` to `Ptr<'_, Self::Tag, I>` using this projection.
    type ProjectToTag: pointer::cast::Project<Self, Self::Tag>;
}

/// Projects a given field from `Self`.
///
/// All implementations of `HasField` for a particular field `f` in `Self`
/// should use the same `Field` type; this ensures that `Field` is inferable
/// given an explicit `VARIANT_ID` and `FIELD_ID`.
///
/// # Safety
///
/// A field `f` is `HasField` for `Self` if and only if:
///
/// - If `Self` has the layout of a struct or union type, then `VARIANT_ID` is
///   `STRUCT_VARIANT_ID` or `UNION_VARIANT_ID` respectively; otherwise, if
///   `Self` has the layout of an enum type, `VARIANT_ID` is the numerical index
///   of the enum variant in which `f` appears. Note that `Self` does not need
///   to actually *be* such a type – it just needs to have the same layout as
///   such a type. For example, a `#[repr(transparent)]` wrapper around an enum
///   has the same layout as that enum.
/// - If `f` has name `n`, `FIELD_ID` is `zerocopy::ident_id!(n)`; otherwise,
///   if `f` is at index `i`, `FIELD_ID` is `zerocopy::ident_id!(i)`.
/// - `Field` is a type with the same visibility as `f`.
/// - `Type` has the same type as `f`.
///
/// The caller must **not** assume that a pointer's referent being aligned
/// implies that calling `project` on that pointer will result in a pointer to
/// an aligned referent. For example, `HasField` may be implemented for
/// `#[repr(packed)]` structs.
///
/// The implementation of `project` must satisfy its safety post-condition.
#[doc(hidden)]
pub unsafe trait HasField<Field, const VARIANT_ID: i128, const FIELD_ID: i128>:
    HasTag
{
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// The type of the field.
    type Type: ?Sized;

    /// Projects from `slf` to the field.
    ///
    /// Users should generally not call `project` directly, and instead should
    /// use high-level APIs like [`PtrInner::project`] or [`Ptr::project`].
    ///
    /// # Safety
    ///
    /// The returned pointer refers to a non-strict subset of the bytes of
    /// `slf`'s referent, and has the same provenance as `slf`.
    #[must_use]
    fn project(slf: PtrInner<'_, Self>) -> *mut Self::Type;
}

/// Projects a given field from `Self`.
///
/// Implementations of this trait encode the conditions under which a field can
/// be projected from a `Ptr<'_, Self, I>`, and how the invariants of that
/// [`Ptr`] (`I`) determine the invariants of pointers projected from it. In
/// other words, it is a type-level function over invariants; `I` goes in,
/// `Self::Invariants` comes out.
///
/// # Safety
///
/// `T: ProjectField<Field, I, VARIANT_ID, FIELD_ID>` if, for a
/// `ptr: Ptr<'_, T, I>` such that `T::is_projectable(ptr).is_ok()`,
/// `<T as HasField<Field, VARIANT_ID, FIELD_ID>>::project(ptr.as_inner())`
/// conforms to `T::Invariants`.
#[doc(hidden)]
pub unsafe trait ProjectField<Field, I, const VARIANT_ID: i128, const FIELD_ID: i128>:
    HasField<Field, VARIANT_ID, FIELD_ID>
where
    I: invariant::Invariants,
{
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// The invariants of the projected field pointer, with respect to the
    /// invariants, `I`, of the containing pointer. The aliasing dimension of
    /// the invariants is guaranteed to remain unchanged.
    type Invariants: invariant::Invariants<Aliasing = I::Aliasing>;

    /// The failure mode of projection. `()` if the projection is fallible,
    /// otherwise [`core::convert::Infallible`].
    type Error;

    /// Is the given field projectable from `ptr`?
    ///
    /// If a field with [`Self::Invariants`] is projectable from the referent,
    /// this function produces an `Ok(ptr)` from which the projection can be
    /// made; otherwise `Err`.
    ///
    /// This method must be overriden if the field's projectability depends on
    /// the value of the bytes in `ptr`.
    #[inline(always)]
    fn is_projectable<'a>(_ptr: Ptr<'a, Self::Tag, I>) -> Result<(), Self::Error> {
        trait IsInfallible {
            const IS_INFALLIBLE: bool;
        }

        struct Projection<T, Field, I, const VARIANT_ID: i128, const FIELD_ID: i128>(
            PhantomData<(Field, I, T)>,
        )
        where
            T: ?Sized + HasField<Field, VARIANT_ID, FIELD_ID>,
            I: invariant::Invariants;

        impl<T, Field, I, const VARIANT_ID: i128, const FIELD_ID: i128> IsInfallible
            for Projection<T, Field, I, VARIANT_ID, FIELD_ID>
        where
            T: ?Sized + HasField<Field, VARIANT_ID, FIELD_ID>,
            I: invariant::Invariants,
        {
            const IS_INFALLIBLE: bool = {
                let is_infallible = match VARIANT_ID {
                    // For nondestructive projections of struct and union
                    // fields, the projected field's satisfaction of
                    // `Invariants` does not depend on the value of the
                    // referent. This default implementation of `is_projectable`
                    // is non-destructive, as it does not overwrite any part of
                    // the referent.
                    crate::STRUCT_VARIANT_ID | crate::UNION_VARIANT_ID => true,
                    _enum_variant => {
                        use crate::invariant::{Validity, ValidityKind};
                        match I::Validity::KIND {
                            // The `Uninit` and `Initialized` validity
                            // invariants do not depend on the enum's tag. In
                            // particular, we don't actually care about what
                            // variant is present – we can treat *any* range of
                            // uninitialized or initialized memory as containing
                            // an uninitialized or initialized instance of *any*
                            // type – the type itself is irrelevant.
                            ValidityKind::Uninit | ValidityKind::Initialized => true,
                            // The projectability of an enum field from an
                            // `AsInitialized` or `Valid` state is a dynamic
                            // property of its tag.
                            ValidityKind::AsInitialized | ValidityKind::Valid => false,
                        }
                    }
                };
                const_assert!(is_infallible);
                is_infallible
            };
        }

        const_assert!(
            <Projection<Self, Field, I, VARIANT_ID, FIELD_ID> as IsInfallible>::IS_INFALLIBLE
        );

        Ok(())
    }
}

/// Analyzes whether a type is [`FromZeros`].
///
/// This derive analyzes, at compile time, whether the annotated type satisfies
/// the [safety conditions] of `FromZeros` and implements `FromZeros` and its
/// supertraits if it is sound to do so. This derive can be applied to structs,
/// enums, and unions; e.g.:
///
/// ```
/// # use zerocopy_derive::{FromZeros, Immutable};
/// #[derive(FromZeros)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromZeros)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant0,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromZeros, Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// [safety conditions]: trait@FromZeros#safety
///
/// # Analysis
///
/// *This section describes, roughly, the analysis performed by this derive to
/// determine whether it is sound to implement `FromZeros` for a given type.
/// Unless you are modifying the implementation of this derive, or attempting to
/// manually implement `FromZeros` for a type yourself, you don't need to read
/// this section.*
///
/// If a type has the following properties, then this derive can implement
/// `FromZeros` for that type:
///
/// - If the type is a struct, all of its fields must be `FromZeros`.
/// - If the type is an enum:
///   - It must have a defined representation (`repr`s `C`, `u8`, `u16`, `u32`,
///     `u64`, `usize`, `i8`, `i16`, `i32`, `i64`, or `isize`).
///   - It must have a variant with a discriminant/tag of `0`, and its fields
///     must be `FromZeros`. See [the reference] for a description of
///     discriminant values are specified.
///   - The fields of that variant must be `FromZeros`.
///
/// This analysis is subject to change. Unsafe code may *only* rely on the
/// documented [safety conditions] of `FromZeros`, and must *not* rely on the
/// implementation details of this derive.
///
/// [the reference]: https://doc.rust-lang.org/reference/items/enumerations.html#custom-discriminant-values-for-fieldless-enumerations
///
/// ## Why isn't an explicit representation required for structs?
///
/// Neither this derive, nor the [safety conditions] of `FromZeros`, requires
/// that structs are marked with `#[repr(C)]`.
///
/// Per the [Rust reference](reference),
///
/// > The representation of a type can change the padding between fields, but
/// > does not change the layout of the fields themselves.
///
/// [reference]: https://doc.rust-lang.org/reference/type-layout.html#representations
///
/// Since the layout of structs only consists of padding bytes and field bytes,
/// a struct is soundly `FromZeros` if:
/// 1. its padding is soundly `FromZeros`, and
/// 2. its fields are soundly `FromZeros`.
///
/// The answer to the first question is always yes: padding bytes do not have
/// any validity constraints. A [discussion] of this question in the Unsafe Code
/// Guidelines Working Group concluded that it would be virtually unimaginable
/// for future versions of rustc to add validity constraints to padding bytes.
///
/// [discussion]: https://github.com/rust-lang/unsafe-code-guidelines/issues/174
///
/// Whether a struct is soundly `FromZeros` therefore solely depends on whether
/// its fields are `FromZeros`.
// FIXME(#146): Document why we don't require an enum to have an explicit `repr`
// attribute.
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::FromZeros;
/// Analyzes whether a type is [`Immutable`].
///
/// This derive analyzes, at compile time, whether the annotated type satisfies
/// the [safety conditions] of `Immutable` and implements `Immutable` if it is
/// sound to do so. This derive can be applied to structs, enums, and unions;
/// e.g.:
///
/// ```
/// # use zerocopy_derive::Immutable;
/// #[derive(Immutable)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Immutable)]
/// enum MyEnum {
/// #   Variant0,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// # Analysis
///
/// *This section describes, roughly, the analysis performed by this derive to
/// determine whether it is sound to implement `Immutable` for a given type.
/// Unless you are modifying the implementation of this derive, you don't need
/// to read this section.*
///
/// If a type has the following properties, then this derive can implement
/// `Immutable` for that type:
///
/// - All fields must be `Immutable`.
///
/// This analysis is subject to change. Unsafe code may *only* rely on the
/// documented [safety conditions] of `Immutable`, and must *not* rely on the
/// implementation details of this derive.
///
/// [safety conditions]: trait@Immutable#safety
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::Immutable;

/// Types which are free from interior mutability.
///
/// `T: Immutable` indicates that `T` does not permit interior mutation, except
/// by ownership or an exclusive (`&mut`) borrow.
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(Immutable)]`][derive] (requires the `derive` Cargo feature);
/// e.g.:
///
/// ```
/// # use zerocopy_derive::Immutable;
/// #[derive(Immutable)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Immutable)]
/// enum MyEnum {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// This derive performs a sophisticated, compile-time safety analysis to
/// determine whether a type is `Immutable`.
///
/// # Safety
///
/// Unsafe code outside of this crate must not make any assumptions about `T`
/// based on `T: Immutable`. We reserve the right to relax the requirements for
/// `Immutable` in the future, and if unsafe code outside of this crate makes
/// assumptions based on `T: Immutable`, future relaxations may cause that code
/// to become unsound.
///
// # Safety (Internal)
//
// If `T: Immutable`, unsafe code *inside of this crate* may assume that, given
// `t: &T`, `t` does not permit interior mutation of its referent. Because
// [`UnsafeCell`] is the only type which permits interior mutation, it is
// sufficient (though not necessary) to guarantee that `T` contains no
// `UnsafeCell`s.
//
// [`UnsafeCell`]: core::cell::UnsafeCell
#[cfg_attr(
    feature = "derive",
    doc = "[derive]: zerocopy_derive::Immutable",
    doc = "[derive-analysis]: zerocopy_derive::Immutable#analysis"
)]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.Immutable.html"),
    doc = concat!("[derive-analysis]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.Immutable.html#analysis"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(Immutable)]` to `{Self}`")
)]
pub unsafe trait Immutable {
    // The `Self: Sized` bound makes it so that `Immutable` is still object
    // safe.
    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;
}

/// Implements [`TryFromBytes`].
///
/// This derive synthesizes the runtime checks required to check whether a
/// sequence of initialized bytes corresponds to a valid instance of a type.
/// This derive can be applied to structs, enums, and unions; e.g.:
///
/// ```
/// # use zerocopy_derive::{TryFromBytes, Immutable};
/// #[derive(TryFromBytes)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(TryFromBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   V00,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(TryFromBytes, Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// # Portability
///
/// To ensure consistent endianness for enums with multi-byte representations,
/// explicitly specify and convert each discriminant using `.to_le()` or
/// `.to_be()`; e.g.:
///
/// ```
/// # use zerocopy_derive::TryFromBytes;
/// // `DataStoreVersion` is encoded in little-endian.
/// #[derive(TryFromBytes)]
/// #[repr(u32)]
/// pub enum DataStoreVersion {
///     /// Version 1 of the data store.
///     V1 = 9u32.to_le(),
///
///     /// Version 2 of the data store.
///     V2 = 10u32.to_le(),
/// }
/// ```
///
/// [safety conditions]: trait@TryFromBytes#safety
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::TryFromBytes;

/// Types for which some bit patterns are valid.
///
/// A memory region of the appropriate length which contains initialized bytes
/// can be viewed as a `TryFromBytes` type so long as the runtime value of those
/// bytes corresponds to a [*valid instance*] of that type. For example,
/// [`bool`] is `TryFromBytes`, so zerocopy can transmute a [`u8`] into a
/// [`bool`] so long as it first checks that the value of the [`u8`] is `0` or
/// `1`.
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(TryFromBytes)]`][derive]; e.g.:
///
/// ```
/// # use zerocopy_derive::{TryFromBytes, Immutable};
/// #[derive(TryFromBytes)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(TryFromBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   V00,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(TryFromBytes, Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// This derive ensures that the runtime check of whether bytes correspond to a
/// valid instance is sound. You **must** implement this trait via the derive.
///
/// # What is a "valid instance"?
///
/// In Rust, each type has *bit validity*, which refers to the set of bit
/// patterns which may appear in an instance of that type. It is impossible for
/// safe Rust code to produce values which violate bit validity (ie, values
/// outside of the "valid" set of bit patterns). If `unsafe` code produces an
/// invalid value, this is considered [undefined behavior].
///
/// Rust's bit validity rules are currently being decided, which means that some
/// types have three classes of bit patterns: those which are definitely valid,
/// and whose validity is documented in the language; those which may or may not
/// be considered valid at some point in the future; and those which are
/// definitely invalid.
///
/// Zerocopy takes a conservative approach, and only considers a bit pattern to
/// be valid if its validity is a documented guarantee provided by the
/// language.
///
/// For most use cases, Rust's current guarantees align with programmers'
/// intuitions about what ought to be valid. As a result, zerocopy's
/// conservatism should not affect most users.
///
/// If you are negatively affected by lack of support for a particular type,
/// we encourage you to let us know by [filing an issue][github-repo].
///
/// # `TryFromBytes` is not symmetrical with [`IntoBytes`]
///
/// There are some types which implement both `TryFromBytes` and [`IntoBytes`],
/// but for which `TryFromBytes` is not guaranteed to accept all byte sequences
/// produced by `IntoBytes`. In other words, for some `T: TryFromBytes +
/// IntoBytes`, there exist values of `t: T` such that
/// `TryFromBytes::try_ref_from_bytes(t.as_bytes()) == None`. Code should not
/// generally assume that values produced by `IntoBytes` will necessarily be
/// accepted as valid by `TryFromBytes`.
///
/// # Safety
///
/// On its own, `T: TryFromBytes` does not make any guarantees about the layout
/// or representation of `T`. It merely provides the ability to perform a
/// validity check at runtime via methods like [`try_ref_from_bytes`].
///
/// You must not rely on the `#[doc(hidden)]` internals of `TryFromBytes`.
/// Future releases of zerocopy may make backwards-breaking changes to these
/// items, including changes that only affect soundness, which may cause code
/// which uses those items to silently become unsound.
///
/// [undefined behavior]: https://raphlinus.github.io/programming/rust/2018/08/17/undefined-behavior.html
/// [github-repo]: https://github.com/google/zerocopy
/// [`try_ref_from_bytes`]: TryFromBytes::try_ref_from_bytes
/// [*valid instance*]: #what-is-a-valid-instance
#[cfg_attr(feature = "derive", doc = "[derive]: zerocopy_derive::TryFromBytes")]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.TryFromBytes.html"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(TryFromBytes)]` to `{Self}`")
)]
pub unsafe trait TryFromBytes {
    // The `Self: Sized` bound makes it so that `TryFromBytes` is still object
    // safe.
    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// Does a given memory range contain a valid instance of `Self`?
    ///
    /// # Safety
    ///
    /// Unsafe code may assume that, if `is_bit_valid(candidate)` returns true,
    /// `*candidate` contains a valid `Self`.
    ///
    /// # Panics
    ///
    /// `is_bit_valid` may panic. Callers are responsible for ensuring that any
    /// `unsafe` code remains sound even in the face of `is_bit_valid`
    /// panicking. (We support user-defined validation routines; so long as
    /// these routines are not required to be `unsafe`, there is no way to
    /// ensure that these do not generate panics.)
    ///
    /// Besides user-defined validation routines panicking, `is_bit_valid` will
    /// either panic or fail to compile if called on a pointer with [`Shared`]
    /// aliasing when `Self: !Immutable`.
    ///
    /// [`UnsafeCell`]: core::cell::UnsafeCell
    /// [`Shared`]: invariant::Shared
    #[doc(hidden)]
    fn is_bit_valid<A>(candidate: Maybe<'_, Self, A>) -> bool
    where
        A: invariant::Alignment;

    /// Attempts to interpret the given `source` as a `&Self`.
    ///
    /// If the bytes of `source` are a valid instance of `Self`, this method
    /// returns a reference to those bytes interpreted as a `Self`. If the
    /// length of `source` is not a [valid size of `Self`][valid-size], or if
    /// `source` is not appropriately aligned, or if `source` is not a valid
    /// instance of `Self`, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][ConvertError::from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = ZSTy::try_ref_from_bytes(0u16.as_bytes()); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the byte sequence `0xC0C0`.
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &[0xC0, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5][..];
    ///
    /// let packet = Packet::try_ref_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[0, 1], [2, 3], [4, 5]]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &[0x10, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5][..];
    /// assert!(Packet::try_ref_from_bytes(bytes).is_err());
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_ref_from_bytes",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_ref_from_bytes(source: &[u8]) -> Result<&Self, TryCastError<&[u8], Self>>
    where
        Self: KnownLayout + Immutable,
    {
        static_assert_dst_is_not_zst!(Self);
        match Ptr::from_ref(source).try_cast_into_no_leftover::<Self, BecauseImmutable>(None) {
            Ok(source) => {
                // This call may panic. If that happens, it doesn't cause any soundness
                // issues, as we have not generated any invalid state which we need to
                // fix before returning.
                match source.try_into_valid() {
                    Ok(valid) => Ok(valid.as_ref()),
                    Err(e) => {
                        Err(e.map_src(|src| src.as_bytes::<BecauseImmutable>().as_ref()).into())
                    }
                }
            }
            Err(e) => Err(e.map_src(Ptr::as_ref).into()),
        }
    }

    /// Attempts to interpret the prefix of the given `source` as a `&Self`.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the leading bytes of `source`. If that prefix is a valid
    /// instance of `Self`, this method returns a reference to those bytes
    /// interpreted as `Self`, and a reference to the remaining bytes. If there
    /// are insufficient bytes, or if `source` is not appropriately aligned, or
    /// if those bytes are not a valid instance of `Self`, this returns `Err`.
    /// If [`Self: Unaligned`][self-unaligned], you can [infallibly discard the
    /// alignment error][ConvertError::from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = ZSTy::try_ref_from_prefix(0u16.as_bytes()); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `Packet`.
    /// let bytes = &[0xC0, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5, 6][..];
    ///
    /// let (packet, suffix) = Packet::try_ref_from_prefix(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[0, 1], [2, 3], [4, 5]]);
    /// assert_eq!(suffix, &[6u8][..]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &[0x10, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5, 6][..];
    /// assert!(Packet::try_ref_from_prefix(bytes).is_err());
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_ref_from_prefix",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_ref_from_prefix(source: &[u8]) -> Result<(&Self, &[u8]), TryCastError<&[u8], Self>>
    where
        Self: KnownLayout + Immutable,
    {
        static_assert_dst_is_not_zst!(Self);
        try_ref_from_prefix_suffix(source, CastType::Prefix, None)
    }

    /// Attempts to interpret the suffix of the given `source` as a `&Self`.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the trailing bytes of `source`. If that suffix is a
    /// valid instance of `Self`, this method returns a reference to those bytes
    /// interpreted as `Self`, and a reference to the preceding bytes. If there
    /// are insufficient bytes, or if the suffix of `source` would not be
    /// appropriately aligned, or if the suffix is not a valid instance of
    /// `Self`, this returns `Err`. If [`Self: Unaligned`][self-unaligned], you
    /// can [infallibly discard the alignment error][ConvertError::from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = ZSTy::try_ref_from_suffix(0u16.as_bytes()); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `Packet`.
    /// let bytes = &[0, 0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let (prefix, packet) = Packet::try_ref_from_suffix(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    /// assert_eq!(prefix, &[0u8][..]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 77, 240, 0xC0, 0x10][..];
    /// assert!(Packet::try_ref_from_suffix(bytes).is_err());
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_ref_from_suffix",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_ref_from_suffix(source: &[u8]) -> Result<(&[u8], &Self), TryCastError<&[u8], Self>>
    where
        Self: KnownLayout + Immutable,
    {
        static_assert_dst_is_not_zst!(Self);
        try_ref_from_prefix_suffix(source, CastType::Suffix, None).map(swap)
    }

    /// Attempts to interpret the given `source` as a `&mut Self` without
    /// copying.
    ///
    /// If the bytes of `source` are a valid instance of `Self`, this method
    /// returns a reference to those bytes interpreted as a `Self`. If the
    /// length of `source` is not a [valid size of `Self`][valid-size], or if
    /// `source` is not appropriately aligned, or if `source` is not a valid
    /// instance of `Self`, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][ConvertError::from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut source = [85, 85];
    /// let _ = ZSTy::try_mut_from_bytes(&mut source[..]); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &mut [0xC0, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5][..];
    ///
    /// let packet = Packet::try_mut_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[0, 1], [2, 3], [4, 5]]);
    ///
    /// packet.temperature = 111;
    ///
    /// assert_eq!(bytes, [0xC0, 0xC0, 240, 111, 0, 1, 2, 3, 4, 5]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0x10, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5, 6][..];
    /// assert!(Packet::try_mut_from_bytes(bytes).is_err());
    /// ```
    ///
    #[doc = codegen_header!("h5", "try_mut_from_bytes")]
    ///
    /// See [`TryFromBytes::try_ref_from_bytes`](#method.try_ref_from_bytes.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_mut_from_bytes(bytes: &mut [u8]) -> Result<&mut Self, TryCastError<&mut [u8], Self>>
    where
        Self: KnownLayout + IntoBytes,
    {
        static_assert_dst_is_not_zst!(Self);
        match Ptr::from_mut(bytes).try_cast_into_no_leftover::<Self, BecauseExclusive>(None) {
            Ok(source) => {
                // This call may panic. If that happens, it doesn't cause any soundness
                // issues, as we have not generated any invalid state which we need to
                // fix before returning.
                match source.try_into_valid() {
                    Ok(source) => Ok(source.as_mut()),
                    Err(e) => Err(e.map_src(|src| src.as_bytes().as_mut()).into()),
                }
            }
            Err(e) => Err(e.map_src(Ptr::as_mut).into()),
        }
    }

    /// Attempts to interpret the prefix of the given `source` as a `&mut
    /// Self`.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the leading bytes of `source`. If that prefix is a valid
    /// instance of `Self`, this method returns a reference to those bytes
    /// interpreted as `Self`, and a reference to the remaining bytes. If there
    /// are insufficient bytes, or if `source` is not appropriately aligned, or
    /// if the bytes are not a valid instance of `Self`, this returns `Err`. If
    /// [`Self: Unaligned`][self-unaligned], you can [infallibly discard the
    /// alignment error][ConvertError::from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut source = [85, 85];
    /// let _ = ZSTy::try_mut_from_prefix(&mut source[..]); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `Packet`.
    /// let bytes = &mut [0xC0, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5, 6][..];
    ///
    /// let (packet, suffix) = Packet::try_mut_from_prefix(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[0, 1], [2, 3], [4, 5]]);
    /// assert_eq!(suffix, &[6u8][..]);
    ///
    /// packet.temperature = 111;
    /// suffix[0] = 222;
    ///
    /// assert_eq!(bytes, [0xC0, 0xC0, 240, 111, 0, 1, 2, 3, 4, 5, 222]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0x10, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5, 6][..];
    /// assert!(Packet::try_mut_from_prefix(bytes).is_err());
    /// ```
    ///
    #[doc = codegen_header!("h5", "try_mut_from_prefix")]
    ///
    /// See [`TryFromBytes::try_ref_from_prefix`](#method.try_ref_from_prefix.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_mut_from_prefix(
        source: &mut [u8],
    ) -> Result<(&mut Self, &mut [u8]), TryCastError<&mut [u8], Self>>
    where
        Self: KnownLayout + IntoBytes,
    {
        static_assert_dst_is_not_zst!(Self);
        try_mut_from_prefix_suffix(source, CastType::Prefix, None)
    }

    /// Attempts to interpret the suffix of the given `source` as a `&mut
    /// Self`.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the trailing bytes of `source`. If that suffix is a
    /// valid instance of `Self`, this method returns a reference to those bytes
    /// interpreted as `Self`, and a reference to the preceding bytes. If there
    /// are insufficient bytes, or if the suffix of `source` would not be
    /// appropriately aligned, or if the suffix is not a valid instance of
    /// `Self`, this returns `Err`. If [`Self: Unaligned`][self-unaligned], you
    /// can [infallibly discard the alignment error][ConvertError::from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut source = [85, 85];
    /// let _ = ZSTy::try_mut_from_suffix(&mut source[..]); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `Packet`.
    /// let bytes = &mut [0, 0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let (prefix, packet) = Packet::try_mut_from_suffix(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    /// assert_eq!(prefix, &[0u8][..]);
    ///
    /// prefix[0] = 111;
    /// packet.temperature = 222;
    ///
    /// assert_eq!(bytes, [111, 0xC0, 0xC0, 240, 222, 2, 3, 4, 5, 6, 7]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 77, 240, 0xC0, 0x10][..];
    /// assert!(Packet::try_mut_from_suffix(bytes).is_err());
    /// ```
    ///
    #[doc = codegen_header!("h5", "try_mut_from_suffix")]
    ///
    /// See [`TryFromBytes::try_ref_from_suffix`](#method.try_ref_from_suffix.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_mut_from_suffix(
        source: &mut [u8],
    ) -> Result<(&mut [u8], &mut Self), TryCastError<&mut [u8], Self>>
    where
        Self: KnownLayout + IntoBytes,
    {
        static_assert_dst_is_not_zst!(Self);
        try_mut_from_prefix_suffix(source, CastType::Suffix, None).map(swap)
    }

    /// Attempts to interpret the given `source` as a `&Self` with a DST length
    /// equal to `count`.
    ///
    /// This method attempts to return a reference to `source` interpreted as a
    /// `Self` with `count` trailing elements. If the length of `source` is not
    /// equal to the size of `Self` with `count` elements, if `source` is not
    /// appropriately aligned, or if `source` does not contain a valid instance
    /// of `Self`, this returns `Err`. If [`Self: Unaligned`][self-unaligned],
    /// you can [infallibly discard the alignment error][ConvertError::from].
    ///
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Examples
    ///
    /// ```
    /// # #![allow(non_camel_case_types)] // For C0::xC0
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &[0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let packet = Packet::try_ref_from_bytes_with_elems(bytes, 3).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 77, 240, 0xC0, 0xC0][..];
    /// assert!(Packet::try_ref_from_bytes_with_elems(bytes, 3).is_err());
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`try_ref_from_bytes`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use core::num::NonZeroU16;
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: NonZeroU16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = 0xCAFEu16.as_bytes();
    /// let zsty = ZSTy::try_ref_from_bytes_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`try_ref_from_bytes`]: TryFromBytes::try_ref_from_bytes
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_ref_from_bytes_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_ref_from_bytes_with_elems(
        source: &[u8],
        count: usize,
    ) -> Result<&Self, TryCastError<&[u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + Immutable,
    {
        match Ptr::from_ref(source).try_cast_into_no_leftover::<Self, BecauseImmutable>(Some(count))
        {
            Ok(source) => {
                // This call may panic. If that happens, it doesn't cause any soundness
                // issues, as we have not generated any invalid state which we need to
                // fix before returning.
                match source.try_into_valid() {
                    Ok(source) => Ok(source.as_ref()),
                    Err(e) => {
                        Err(e.map_src(|src| src.as_bytes::<BecauseImmutable>().as_ref()).into())
                    }
                }
            }
            Err(e) => Err(e.map_src(Ptr::as_ref).into()),
        }
    }

    /// Attempts to interpret the prefix of the given `source` as a `&Self` with
    /// a DST length equal to `count`.
    ///
    /// This method attempts to return a reference to the prefix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the remaining bytes. If the length of `source` is less than the size
    /// of `Self` with `count` elements, if `source` is not appropriately
    /// aligned, or if the prefix of `source` does not contain a valid instance
    /// of `Self`, this returns `Err`. If [`Self: Unaligned`][self-unaligned],
    /// you can [infallibly discard the alignment error][ConvertError::from].
    ///
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Examples
    ///
    /// ```
    /// # #![allow(non_camel_case_types)] // For C0::xC0
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &[0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7, 8][..];
    ///
    /// let (packet, suffix) = Packet::try_ref_from_prefix_with_elems(bytes, 3).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    /// assert_eq!(suffix, &[8u8][..]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 77, 240, 0xC0, 0xC0][..];
    /// assert!(Packet::try_ref_from_prefix_with_elems(bytes, 3).is_err());
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`try_ref_from_prefix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use core::num::NonZeroU16;
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: NonZeroU16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = 0xCAFEu16.as_bytes();
    /// let (zsty, _) = ZSTy::try_ref_from_prefix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`try_ref_from_prefix`]: TryFromBytes::try_ref_from_prefix
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_ref_from_prefix_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_ref_from_prefix_with_elems(
        source: &[u8],
        count: usize,
    ) -> Result<(&Self, &[u8]), TryCastError<&[u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + Immutable,
    {
        try_ref_from_prefix_suffix(source, CastType::Prefix, Some(count))
    }

    /// Attempts to interpret the suffix of the given `source` as a `&Self` with
    /// a DST length equal to `count`.
    ///
    /// This method attempts to return a reference to the suffix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the preceding bytes. If the length of `source` is less than the size
    /// of `Self` with `count` elements, if the suffix of `source` is not
    /// appropriately aligned, or if the suffix of `source` does not contain a
    /// valid instance of `Self`, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][ConvertError::from].
    ///
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Examples
    ///
    /// ```
    /// # #![allow(non_camel_case_types)] // For C0::xC0
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &[123, 0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let (prefix, packet) = Packet::try_ref_from_suffix_with_elems(bytes, 3).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    /// assert_eq!(prefix, &[123u8][..]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 77, 240, 0xC0, 0xC0][..];
    /// assert!(Packet::try_ref_from_suffix_with_elems(bytes, 3).is_err());
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`try_ref_from_prefix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use core::num::NonZeroU16;
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: NonZeroU16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = 0xCAFEu16.as_bytes();
    /// let (_, zsty) = ZSTy::try_ref_from_suffix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`try_ref_from_prefix`]: TryFromBytes::try_ref_from_prefix
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_ref_from_suffix_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_ref_from_suffix_with_elems(
        source: &[u8],
        count: usize,
    ) -> Result<(&[u8], &Self), TryCastError<&[u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + Immutable,
    {
        try_ref_from_prefix_suffix(source, CastType::Suffix, Some(count)).map(swap)
    }

    /// Attempts to interpret the given `source` as a `&mut Self` with a DST
    /// length equal to `count`.
    ///
    /// This method attempts to return a reference to `source` interpreted as a
    /// `Self` with `count` trailing elements. If the length of `source` is not
    /// equal to the size of `Self` with `count` elements, if `source` is not
    /// appropriately aligned, or if `source` does not contain a valid instance
    /// of `Self`, this returns `Err`. If [`Self: Unaligned`][self-unaligned],
    /// you can [infallibly discard the alignment error][ConvertError::from].
    ///
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Examples
    ///
    /// ```
    /// # #![allow(non_camel_case_types)] // For C0::xC0
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &mut [0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let packet = Packet::try_mut_from_bytes_with_elems(bytes, 3).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    ///
    /// packet.temperature = 111;
    ///
    /// assert_eq!(bytes, [0xC0, 0xC0, 240, 111, 2, 3, 4, 5, 6, 7]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 77, 240, 0xC0, 0xC0][..];
    /// assert!(Packet::try_mut_from_bytes_with_elems(bytes, 3).is_err());
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`try_mut_from_bytes`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use core::num::NonZeroU16;
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: NonZeroU16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut src = 0xCAFEu16;
    /// let src = src.as_mut_bytes();
    /// let zsty = ZSTy::try_mut_from_bytes_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`try_mut_from_bytes`]: TryFromBytes::try_mut_from_bytes
    ///  
    #[doc = codegen_header!("h5", "try_mut_from_bytes_with_elems")]
    ///
    /// See [`TryFromBytes::try_ref_from_bytes_with_elems`](#method.try_ref_from_bytes_with_elems.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_mut_from_bytes_with_elems(
        source: &mut [u8],
        count: usize,
    ) -> Result<&mut Self, TryCastError<&mut [u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + IntoBytes,
    {
        match Ptr::from_mut(source).try_cast_into_no_leftover::<Self, BecauseExclusive>(Some(count))
        {
            Ok(source) => {
                // This call may panic. If that happens, it doesn't cause any soundness
                // issues, as we have not generated any invalid state which we need to
                // fix before returning.
                match source.try_into_valid() {
                    Ok(source) => Ok(source.as_mut()),
                    Err(e) => Err(e.map_src(|src| src.as_bytes().as_mut()).into()),
                }
            }
            Err(e) => Err(e.map_src(Ptr::as_mut).into()),
        }
    }

    /// Attempts to interpret the prefix of the given `source` as a `&mut Self`
    /// with a DST length equal to `count`.
    ///
    /// This method attempts to return a reference to the prefix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the remaining bytes. If the length of `source` is less than the size
    /// of `Self` with `count` elements, if `source` is not appropriately
    /// aligned, or if the prefix of `source` does not contain a valid instance
    /// of `Self`, this returns `Err`. If [`Self: Unaligned`][self-unaligned],
    /// you can [infallibly discard the alignment error][ConvertError::from].
    ///
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Examples
    ///
    /// ```
    /// # #![allow(non_camel_case_types)] // For C0::xC0
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &mut [0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7, 8][..];
    ///
    /// let (packet, suffix) = Packet::try_mut_from_prefix_with_elems(bytes, 3).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    /// assert_eq!(suffix, &[8u8][..]);
    ///
    /// packet.temperature = 111;
    /// suffix[0] = 222;
    ///
    /// assert_eq!(bytes, [0xC0, 0xC0, 240, 111, 2, 3, 4, 5, 6, 7, 222]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 77, 240, 0xC0, 0xC0][..];
    /// assert!(Packet::try_mut_from_prefix_with_elems(bytes, 3).is_err());
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`try_mut_from_prefix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use core::num::NonZeroU16;
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: NonZeroU16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut src = 0xCAFEu16;
    /// let src = src.as_mut_bytes();
    /// let (zsty, _) = ZSTy::try_mut_from_prefix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`try_mut_from_prefix`]: TryFromBytes::try_mut_from_prefix
    ///
    #[doc = codegen_header!("h5", "try_mut_from_prefix_with_elems")]
    ///
    /// See [`TryFromBytes::try_ref_from_prefix_with_elems`](#method.try_ref_from_prefix_with_elems.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_mut_from_prefix_with_elems(
        source: &mut [u8],
        count: usize,
    ) -> Result<(&mut Self, &mut [u8]), TryCastError<&mut [u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + IntoBytes,
    {
        try_mut_from_prefix_suffix(source, CastType::Prefix, Some(count))
    }

    /// Attempts to interpret the suffix of the given `source` as a `&mut Self`
    /// with a DST length equal to `count`.
    ///
    /// This method attempts to return a reference to the suffix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the preceding bytes. If the length of `source` is less than the size
    /// of `Self` with `count` elements, if the suffix of `source` is not
    /// appropriately aligned, or if the suffix of `source` does not contain a
    /// valid instance of `Self`, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][ConvertError::from].
    ///
    /// [self-unaligned]: Unaligned
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Examples
    ///
    /// ```
    /// # #![allow(non_camel_case_types)] // For C0::xC0
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    ///     marshmallows: [[u8; 2]],
    /// }
    ///
    /// let bytes = &mut [123, 0xC0, 0xC0, 240, 77, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let (prefix, packet) = Packet::try_mut_from_suffix_with_elems(bytes, 3).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(packet.marshmallows, [[2, 3], [4, 5], [6, 7]]);
    /// assert_eq!(prefix, &[123u8][..]);
    ///
    /// prefix[0] = 111;
    /// packet.temperature = 222;
    ///
    /// assert_eq!(bytes, [111, 0xC0, 0xC0, 240, 222, 2, 3, 4, 5, 6, 7]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 77, 240, 0xC0, 0xC0][..];
    /// assert!(Packet::try_mut_from_suffix_with_elems(bytes, 3).is_err());
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`try_mut_from_prefix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use core::num::NonZeroU16;
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: NonZeroU16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut src = 0xCAFEu16;
    /// let src = src.as_mut_bytes();
    /// let (_, zsty) = ZSTy::try_mut_from_suffix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`try_mut_from_prefix`]: TryFromBytes::try_mut_from_prefix
    ///
    #[doc = codegen_header!("h5", "try_mut_from_suffix_with_elems")]
    ///
    /// See [`TryFromBytes::try_ref_from_suffix_with_elems`](#method.try_ref_from_suffix_with_elems.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_mut_from_suffix_with_elems(
        source: &mut [u8],
        count: usize,
    ) -> Result<(&mut [u8], &mut Self), TryCastError<&mut [u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + IntoBytes,
    {
        try_mut_from_prefix_suffix(source, CastType::Suffix, Some(count)).map(swap)
    }

    /// Attempts to read the given `source` as a `Self`.
    ///
    /// If `source.len() != size_of::<Self>()` or the bytes are not a valid
    /// instance of `Self`, this returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    /// }
    ///
    /// let bytes = &[0xC0, 0xC0, 240, 77][..];
    ///
    /// let packet = Packet::try_read_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &mut [0x10, 0xC0, 240, 77][..];
    /// assert!(Packet::try_read_from_bytes(bytes).is_err());
    /// ```
    ///
    /// # Performance Considerations
    ///
    /// In this version of zerocopy, this method reads the `source` into a
    /// well-aligned stack allocation and *then* validates that the allocation
    /// is a valid `Self`. This ensures that validation can be performed using
    /// aligned reads (which carry a performance advantage over unaligned reads
    /// on many platforms) at the cost of an unconditional copy.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_read_from_bytes",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_read_from_bytes(source: &[u8]) -> Result<Self, TryReadError<&[u8], Self>>
    where
        Self: Sized,
    {
        // FIXME(#2981): If `align_of::<Self>() == 1`, validate `source` in-place.

        let candidate = match CoreMaybeUninit::<Self>::read_from_bytes(source) {
            Ok(candidate) => candidate,
            Err(e) => {
                return Err(TryReadError::Size(e.with_dst()));
            }
        };
        // SAFETY: `candidate` was copied from from `source: &[u8]`, so all of
        // its bytes are initialized.
        unsafe { try_read_from(source, candidate) }
    }

    /// Attempts to read a `Self` from the prefix of the given `source`.
    ///
    /// This attempts to read a `Self` from the first `size_of::<Self>()` bytes
    /// of `source`, returning that `Self` and any remaining bytes. If
    /// `source.len() < size_of::<Self>()` or the bytes are not a valid instance
    /// of `Self`, it returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    /// }
    ///
    /// // These are more bytes than are needed to encode a `Packet`.
    /// let bytes = &[0xC0, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5, 6][..];
    ///
    /// let (packet, suffix) = Packet::try_read_from_prefix(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(suffix, &[0u8, 1, 2, 3, 4, 5, 6][..]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &[0x10, 0xC0, 240, 77, 0, 1, 2, 3, 4, 5, 6][..];
    /// assert!(Packet::try_read_from_prefix(bytes).is_err());
    /// ```
    ///
    /// # Performance Considerations
    ///
    /// In this version of zerocopy, this method reads the `source` into a
    /// well-aligned stack allocation and *then* validates that the allocation
    /// is a valid `Self`. This ensures that validation can be performed using
    /// aligned reads (which carry a performance advantage over unaligned reads
    /// on many platforms) at the cost of an unconditional copy.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_read_from_prefix",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_read_from_prefix(source: &[u8]) -> Result<(Self, &[u8]), TryReadError<&[u8], Self>>
    where
        Self: Sized,
    {
        // FIXME(#2981): If `align_of::<Self>() == 1`, validate `source` in-place.

        let (candidate, suffix) = match CoreMaybeUninit::<Self>::read_from_prefix(source) {
            Ok(candidate) => candidate,
            Err(e) => {
                return Err(TryReadError::Size(e.with_dst()));
            }
        };
        // SAFETY: `candidate` was copied from from `source: &[u8]`, so all of
        // its bytes are initialized.
        unsafe { try_read_from(source, candidate).map(|slf| (slf, suffix)) }
    }

    /// Attempts to read a `Self` from the suffix of the given `source`.
    ///
    /// This attempts to read a `Self` from the last `size_of::<Self>()` bytes
    /// of `source`, returning that `Self` and any preceding bytes. If
    /// `source.len() < size_of::<Self>()` or the bytes are not a valid instance
    /// of `Self`, it returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// # #![allow(non_camel_case_types)] // For C0::xC0
    /// use zerocopy::TryFromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// // The only valid value of this type is the byte `0xC0`
    /// #[derive(TryFromBytes)]
    /// #[repr(u8)]
    /// enum C0 { xC0 = 0xC0 }
    ///
    /// // The only valid value of this type is the bytes `0xC0C0`.
    /// #[derive(TryFromBytes)]
    /// #[repr(C)]
    /// struct C0C0(C0, C0);
    ///
    /// #[derive(TryFromBytes)]
    /// #[repr(C)]
    /// struct Packet {
    ///     magic_number: C0C0,
    ///     mug_size: u8,
    ///     temperature: u8,
    /// }
    ///
    /// // These are more bytes than are needed to encode a `Packet`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 0xC0, 0xC0, 240, 77][..];
    ///
    /// let (prefix, packet) = Packet::try_read_from_suffix(bytes).unwrap();
    ///
    /// assert_eq!(packet.mug_size, 240);
    /// assert_eq!(packet.temperature, 77);
    /// assert_eq!(prefix, &[0u8, 1, 2, 3, 4, 5][..]);
    ///
    /// // These bytes are not valid instance of `Packet`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 0x10, 0xC0, 240, 77][..];
    /// assert!(Packet::try_read_from_suffix(bytes).is_err());
    /// ```
    ///
    /// # Performance Considerations
    ///
    /// In this version of zerocopy, this method reads the `source` into a
    /// well-aligned stack allocation and *then* validates that the allocation
    /// is a valid `Self`. This ensures that validation can be performed using
    /// aligned reads (which carry a performance advantage over unaligned reads
    /// on many platforms) at the cost of an unconditional copy.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "try_read_from_suffix",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn try_read_from_suffix(source: &[u8]) -> Result<(&[u8], Self), TryReadError<&[u8], Self>>
    where
        Self: Sized,
    {
        // FIXME(#2981): If `align_of::<Self>() == 1`, validate `source` in-place.

        let (prefix, candidate) = match CoreMaybeUninit::<Self>::read_from_suffix(source) {
            Ok(candidate) => candidate,
            Err(e) => {
                return Err(TryReadError::Size(e.with_dst()));
            }
        };
        // SAFETY: `candidate` was copied from from `source: &[u8]`, so all of
        // its bytes are initialized.
        unsafe { try_read_from(source, candidate).map(|slf| (prefix, slf)) }
    }
}

#[inline(always)]
fn try_ref_from_prefix_suffix<T: TryFromBytes + KnownLayout + Immutable + ?Sized>(
    source: &[u8],
    cast_type: CastType,
    meta: Option<T::PointerMetadata>,
) -> Result<(&T, &[u8]), TryCastError<&[u8], T>> {
    match Ptr::from_ref(source).try_cast_into::<T, BecauseImmutable>(cast_type, meta) {
        Ok((source, prefix_suffix)) => {
            // This call may panic. If that happens, it doesn't cause any soundness
            // issues, as we have not generated any invalid state which we need to
            // fix before returning.
            match source.try_into_valid() {
                Ok(valid) => Ok((valid.as_ref(), prefix_suffix.as_ref())),
                Err(e) => Err(e.map_src(|src| src.as_bytes::<BecauseImmutable>().as_ref()).into()),
            }
        }
        Err(e) => Err(e.map_src(Ptr::as_ref).into()),
    }
}

#[inline(always)]
fn try_mut_from_prefix_suffix<T: IntoBytes + TryFromBytes + KnownLayout + ?Sized>(
    candidate: &mut [u8],
    cast_type: CastType,
    meta: Option<T::PointerMetadata>,
) -> Result<(&mut T, &mut [u8]), TryCastError<&mut [u8], T>> {
    match Ptr::from_mut(candidate).try_cast_into::<T, BecauseExclusive>(cast_type, meta) {
        Ok((candidate, prefix_suffix)) => {
            // This call may panic. If that happens, it doesn't cause any soundness
            // issues, as we have not generated any invalid state which we need to
            // fix before returning.
            match candidate.try_into_valid() {
                Ok(valid) => Ok((valid.as_mut(), prefix_suffix.as_mut())),
                Err(e) => Err(e.map_src(|src| src.as_bytes().as_mut()).into()),
            }
        }
        Err(e) => Err(e.map_src(Ptr::as_mut).into()),
    }
}

#[inline(always)]
fn swap<T, U>((t, u): (T, U)) -> (U, T) {
    (u, t)
}

/// # Safety
///
/// All bytes of `candidate` must be initialized.
#[inline(always)]
unsafe fn try_read_from<S, T: TryFromBytes>(
    source: S,
    mut candidate: CoreMaybeUninit<T>,
) -> Result<T, TryReadError<S, T>> {
    // We use `from_mut` despite not mutating via `c_ptr` so that we don't need
    // to add a `T: Immutable` bound.
    let c_ptr = Ptr::from_mut(&mut candidate);
    // SAFETY: `c_ptr` has no uninitialized sub-ranges because it derived from
    // `candidate`, which the caller promises is entirely initialized. Since
    // `candidate` is a `MaybeUninit`, it has no validity requirements, and so
    // no values written to an `Initialized` `c_ptr` can violate its validity.
    // Since `c_ptr` has `Exclusive` aliasing, no mutations may happen except
    // via `c_ptr` so long as it is live, so we don't need to worry about the
    // fact that `c_ptr` may have more restricted validity than `candidate`.
    let c_ptr = unsafe { c_ptr.assume_validity::<invariant::Initialized>() };
    let mut c_ptr = c_ptr.cast::<_, crate::pointer::cast::CastSized, _>();

    // Since we don't have `T: KnownLayout`, we hack around that by using
    // `Wrapping<T>`, which implements `KnownLayout` even if `T` doesn't.
    //
    // This call may panic. If that happens, it doesn't cause any soundness
    // issues, as we have not generated any invalid state which we need to fix
    // before returning.
    if !Wrapping::<T>::is_bit_valid(c_ptr.reborrow_shared().forget_aligned()) {
        return Err(ValidityError::new(source).into());
    }

    fn _assert_same_size_and_validity<T>()
    where
        Wrapping<T>: pointer::TransmuteFrom<T, invariant::Valid, invariant::Valid>,
        T: pointer::TransmuteFrom<Wrapping<T>, invariant::Valid, invariant::Valid>,
    {
    }

    _assert_same_size_and_validity::<T>();

    // SAFETY: We just validated that `candidate` contains a valid
    // `Wrapping<T>`, which has the same size and bit validity as `T`, as
    // guaranteed by the preceding type assertion.
    Ok(unsafe { candidate.assume_init() })
}

/// Types for which a sequence of `0` bytes is a valid instance.
///
/// Any memory region of the appropriate length which is guaranteed to contain
/// only zero bytes can be viewed as any `FromZeros` type with no runtime
/// overhead. This is useful whenever memory is known to be in a zeroed state,
/// such memory returned from some allocation routines.
///
/// # Warning: Padding bytes
///
/// Note that, when a value is moved or copied, only the non-padding bytes of
/// that value are guaranteed to be preserved. It is unsound to assume that
/// values written to padding bytes are preserved after a move or copy. For more
/// details, see the [`FromBytes` docs][frombytes-warning-padding-bytes].
///
/// [frombytes-warning-padding-bytes]: FromBytes#warning-padding-bytes
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(FromZeros)]`][derive]; e.g.:
///
/// ```
/// # use zerocopy_derive::{FromZeros, Immutable};
/// #[derive(FromZeros)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromZeros)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant0,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromZeros, Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// This derive performs a sophisticated, compile-time safety analysis to
/// determine whether a type is `FromZeros`.
///
/// # Safety
///
/// *This section describes what is required in order for `T: FromZeros`, and
/// what unsafe code may assume of such types. If you don't plan on implementing
/// `FromZeros` manually, and you don't plan on writing unsafe code that
/// operates on `FromZeros` types, then you don't need to read this section.*
///
/// If `T: FromZeros`, then unsafe code may assume that it is sound to produce a
/// `T` whose bytes are all initialized to zero. If a type is marked as
/// `FromZeros` which violates this contract, it may cause undefined behavior.
///
/// `#[derive(FromZeros)]` only permits [types which satisfy these
/// requirements][derive-analysis].
///
#[cfg_attr(
    feature = "derive",
    doc = "[derive]: zerocopy_derive::FromZeros",
    doc = "[derive-analysis]: zerocopy_derive::FromZeros#analysis"
)]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.FromZeros.html"),
    doc = concat!("[derive-analysis]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.FromZeros.html#analysis"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(FromZeros)]` to `{Self}`")
)]
pub unsafe trait FromZeros: TryFromBytes {
    // The `Self: Sized` bound makes it so that `FromZeros` is still object
    // safe.
    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// Overwrites `self` with zeros.
    ///
    /// Sets every byte in `self` to 0. While this is similar to doing `*self =
    /// Self::new_zeroed()`, it differs in that `zero` does not semantically
    /// drop the current value and replace it with a new one — it simply
    /// modifies the bytes of the existing value.
    ///
    /// # Examples
    ///
    /// ```
    /// # use zerocopy::FromZeros;
    /// # use zerocopy_derive::*;
    /// #
    /// #[derive(FromZeros)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// let mut header = PacketHeader {
    ///     src_port: 100u16.to_be_bytes(),
    ///     dst_port: 200u16.to_be_bytes(),
    ///     length: 300u16.to_be_bytes(),
    ///     checksum: 400u16.to_be_bytes(),
    /// };
    ///
    /// header.zero();
    ///
    /// assert_eq!(header.src_port, [0, 0]);
    /// assert_eq!(header.dst_port, [0, 0]);
    /// assert_eq!(header.length, [0, 0]);
    /// assert_eq!(header.checksum, [0, 0]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "zero",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline(always)]
    fn zero(&mut self) {
        let slf: *mut Self = self;
        let len = mem::size_of_val(self);
        // SAFETY:
        // - `self` is guaranteed by the type system to be valid for writes of
        //   size `size_of_val(self)`.
        // - `u8`'s alignment is 1, and thus `self` is guaranteed to be aligned
        //   as required by `u8`.
        // - Since `Self: FromZeros`, the all-zeros instance is a valid instance
        //   of `Self.`
        //
        // FIXME(#429): Add references to docs and quotes.
        unsafe { ptr::write_bytes(slf.cast::<u8>(), 0, len) };
    }

    /// Creates an instance of `Self` from zeroed bytes.
    ///
    /// # Examples
    ///
    /// ```
    /// # use zerocopy::FromZeros;
    /// # use zerocopy_derive::*;
    /// #
    /// #[derive(FromZeros)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// let header: PacketHeader = FromZeros::new_zeroed();
    ///
    /// assert_eq!(header.src_port, [0, 0]);
    /// assert_eq!(header.dst_port, [0, 0]);
    /// assert_eq!(header.length, [0, 0]);
    /// assert_eq!(header.checksum, [0, 0]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "new_zeroed",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn new_zeroed() -> Self
    where
        Self: Sized,
    {
        // SAFETY: `FromZeros` says that the all-zeros bit pattern is legal.
        unsafe { mem::zeroed() }
    }

    /// Creates a `Box<Self>` from zeroed bytes.
    ///
    /// This function is useful for allocating large values on the heap and
    /// zero-initializing them, without ever creating a temporary instance of
    /// `Self` on the stack. For example, `<[u8; 1048576]>::new_box_zeroed()`
    /// will allocate `[u8; 1048576]` directly on the heap; it does not require
    /// storing `[u8; 1048576]` in a temporary variable on the stack.
    ///
    /// On systems that use a heap implementation that supports allocating from
    /// pre-zeroed memory, using `new_box_zeroed` (or related functions) may
    /// have performance benefits.
    ///
    /// # Errors
    ///
    /// Returns an error on allocation failure. Allocation failure is guaranteed
    /// never to cause a panic or an abort.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "new_box_zeroed",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects (other than allocation)"]
    #[cfg(any(feature = "alloc", test))]
    #[cfg_attr(doc_cfg, doc(cfg(feature = "alloc")))]
    #[inline]
    fn new_box_zeroed() -> Result<Box<Self>, AllocError>
    where
        Self: Sized,
    {
        // If `T` is a ZST, then return a proper boxed instance of it. There is
        // no allocation, but `Box` does require a correct dangling pointer.
        let layout = Layout::new::<Self>();
        if layout.size() == 0 {
            // Construct the `Box` from a dangling pointer to avoid calling
            // `Self::new_zeroed`. This ensures that stack space is never
            // allocated for `Self` even on lower opt-levels where this branch
            // might not get optimized out.

            // SAFETY: Per [1], when `T` is a ZST, `Box<T>`'s only validity
            // requirements are that the pointer is non-null and sufficiently
            // aligned. Per [2], `NonNull::dangling` produces a pointer which
            // is sufficiently aligned. Since the produced pointer is a
            // `NonNull`, it is non-null.
            //
            // [1] Per https://doc.rust-lang.org/1.81.0/std/boxed/index.html#memory-layout:
            //
            //   For zero-sized values, the `Box` pointer has to be non-null and sufficiently aligned.
            //
            // [2] Per https://doc.rust-lang.org/std/ptr/struct.NonNull.html#method.dangling:
            //
            //   Creates a new `NonNull` that is dangling, but well-aligned.
            return Ok(unsafe { Box::from_raw(NonNull::dangling().as_ptr()) });
        }

        // FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
        #[allow(clippy::undocumented_unsafe_blocks)]
        let ptr = unsafe { alloc::alloc::alloc_zeroed(layout).cast::<Self>() };
        if ptr.is_null() {
            return Err(AllocError);
        }
        // FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
        #[allow(clippy::undocumented_unsafe_blocks)]
        Ok(unsafe { Box::from_raw(ptr) })
    }

    /// Creates a `Box<[Self]>` (a boxed slice) from zeroed bytes.
    ///
    /// This function is useful for allocating large values of `[Self]` on the
    /// heap and zero-initializing them, without ever creating a temporary
    /// instance of `[Self; _]` on the stack. For example,
    /// `u8::new_box_slice_zeroed(1048576)` will allocate the slice directly on
    /// the heap; it does not require storing the slice on the stack.
    ///
    /// On systems that use a heap implementation that supports allocating from
    /// pre-zeroed memory, using `new_box_slice_zeroed` may have performance
    /// benefits.
    ///
    /// If `Self` is a zero-sized type, then this function will return a
    /// `Box<[Self]>` that has the correct `len`. Such a box cannot contain any
    /// actual information, but its `len()` property will report the correct
    /// value.
    ///
    /// # Errors
    ///
    /// Returns an error on allocation failure. Allocation failure is
    /// guaranteed never to cause a panic or an abort.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "new_box_zeroed_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects (other than allocation)"]
    #[cfg(feature = "alloc")]
    #[cfg_attr(doc_cfg, doc(cfg(feature = "alloc")))]
    #[inline]
    fn new_box_zeroed_with_elems(count: usize) -> Result<Box<Self>, AllocError>
    where
        Self: KnownLayout<PointerMetadata = usize>,
    {
        // SAFETY: `alloc::alloc::alloc_zeroed` is a valid argument of
        // `new_box`. The referent of the pointer returned by `alloc_zeroed`
        // (and, consequently, the `Box` derived from it) is a valid instance of
        // `Self`, because `Self` is `FromZeros`.
        unsafe { crate::util::new_box(count, alloc::alloc::alloc_zeroed) }
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromZeros::new_box_zeroed_with_elems`")]
    #[doc(hidden)]
    #[cfg(feature = "alloc")]
    #[cfg_attr(doc_cfg, doc(cfg(feature = "alloc")))]
    #[must_use = "has no side effects (other than allocation)"]
    #[inline(always)]
    fn new_box_slice_zeroed(len: usize) -> Result<Box<[Self]>, AllocError>
    where
        Self: Sized,
    {
        <[Self]>::new_box_zeroed_with_elems(len)
    }

    /// Creates a `Vec<Self>` from zeroed bytes.
    ///
    /// This function is useful for allocating large values of `Vec`s and
    /// zero-initializing them, without ever creating a temporary instance of
    /// `[Self; _]` (or many temporary instances of `Self`) on the stack. For
    /// example, `u8::new_vec_zeroed(1048576)` will allocate directly on the
    /// heap; it does not require storing intermediate values on the stack.
    ///
    /// On systems that use a heap implementation that supports allocating from
    /// pre-zeroed memory, using `new_vec_zeroed` may have performance benefits.
    ///
    /// If `Self` is a zero-sized type, then this function will return a
    /// `Vec<Self>` that has the correct `len`. Such a `Vec` cannot contain any
    /// actual information, but its `len()` property will report the correct
    /// value.
    ///
    /// # Errors
    ///
    /// Returns an error on allocation failure. Allocation failure is
    /// guaranteed never to cause a panic or an abort.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "new_vec_zeroed",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects (other than allocation)"]
    #[cfg(feature = "alloc")]
    #[cfg_attr(doc_cfg, doc(cfg(feature = "alloc")))]
    #[inline(always)]
    fn new_vec_zeroed(len: usize) -> Result<Vec<Self>, AllocError>
    where
        Self: Sized,
    {
        <[Self]>::new_box_zeroed_with_elems(len).map(Into::into)
    }

    /// Extends a `Vec<Self>` by pushing `additional` new items onto the end of
    /// the vector. The new items are initialized with zeros.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "extend_vec_zeroed",
        format = "coco_static_size",
    )]
    #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
    #[cfg(feature = "alloc")]
    #[cfg_attr(doc_cfg, doc(cfg(all(rust = "1.57.0", feature = "alloc"))))]
    #[inline(always)]
    fn extend_vec_zeroed(v: &mut Vec<Self>, additional: usize) -> Result<(), AllocError>
    where
        Self: Sized,
    {
        // PANICS: We pass `v.len()` for `position`, so the `position > v.len()`
        // panic condition is not satisfied.
        <Self as FromZeros>::insert_vec_zeroed(v, v.len(), additional)
    }

    /// Inserts `additional` new items into `Vec<Self>` at `position`. The new
    /// items are initialized with zeros.
    ///
    /// # Panics
    ///
    /// Panics if `position > v.len()`.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "insert_vec_zeroed",
        format = "coco_static_size",
    )]
    #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
    #[cfg(feature = "alloc")]
    #[cfg_attr(doc_cfg, doc(cfg(all(rust = "1.57.0", feature = "alloc"))))]
    #[inline]
    fn insert_vec_zeroed(
        v: &mut Vec<Self>,
        position: usize,
        additional: usize,
    ) -> Result<(), AllocError>
    where
        Self: Sized,
    {
        assert!(position <= v.len());
        // We only conditionally compile on versions on which `try_reserve` is
        // stable; the Clippy lint is a false positive.
        v.try_reserve(additional).map_err(|_| AllocError)?;
        // SAFETY: The `try_reserve` call guarantees that these cannot overflow:
        // * `ptr.add(position)`
        // * `position + additional`
        // * `v.len() + additional`
        //
        // `v.len() - position` cannot overflow because we asserted that
        // `position <= v.len()`.
        #[allow(clippy::multiple_unsafe_ops_per_block)]
        unsafe {
            // This is a potentially overlapping copy.
            let ptr = v.as_mut_ptr();
            #[allow(clippy::arithmetic_side_effects)]
            ptr.add(position).copy_to(ptr.add(position + additional), v.len() - position);
            ptr.add(position).write_bytes(0, additional);
            #[allow(clippy::arithmetic_side_effects)]
            v.set_len(v.len() + additional);
        }

        Ok(())
    }
}

/// Analyzes whether a type is [`FromBytes`].
///
/// This derive analyzes, at compile time, whether the annotated type satisfies
/// the [safety conditions] of `FromBytes` and implements `FromBytes` and its
/// supertraits if it is sound to do so. This derive can be applied to structs,
/// enums, and unions;
/// e.g.:
///
/// ```
/// # use zerocopy_derive::{FromBytes, FromZeros, Immutable};
/// #[derive(FromBytes)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   V00, V01, V02, V03, V04, V05, V06, V07, V08, V09, V0A, V0B, V0C, V0D, V0E,
/// #   V0F, V10, V11, V12, V13, V14, V15, V16, V17, V18, V19, V1A, V1B, V1C, V1D,
/// #   V1E, V1F, V20, V21, V22, V23, V24, V25, V26, V27, V28, V29, V2A, V2B, V2C,
/// #   V2D, V2E, V2F, V30, V31, V32, V33, V34, V35, V36, V37, V38, V39, V3A, V3B,
/// #   V3C, V3D, V3E, V3F, V40, V41, V42, V43, V44, V45, V46, V47, V48, V49, V4A,
/// #   V4B, V4C, V4D, V4E, V4F, V50, V51, V52, V53, V54, V55, V56, V57, V58, V59,
/// #   V5A, V5B, V5C, V5D, V5E, V5F, V60, V61, V62, V63, V64, V65, V66, V67, V68,
/// #   V69, V6A, V6B, V6C, V6D, V6E, V6F, V70, V71, V72, V73, V74, V75, V76, V77,
/// #   V78, V79, V7A, V7B, V7C, V7D, V7E, V7F, V80, V81, V82, V83, V84, V85, V86,
/// #   V87, V88, V89, V8A, V8B, V8C, V8D, V8E, V8F, V90, V91, V92, V93, V94, V95,
/// #   V96, V97, V98, V99, V9A, V9B, V9C, V9D, V9E, V9F, VA0, VA1, VA2, VA3, VA4,
/// #   VA5, VA6, VA7, VA8, VA9, VAA, VAB, VAC, VAD, VAE, VAF, VB0, VB1, VB2, VB3,
/// #   VB4, VB5, VB6, VB7, VB8, VB9, VBA, VBB, VBC, VBD, VBE, VBF, VC0, VC1, VC2,
/// #   VC3, VC4, VC5, VC6, VC7, VC8, VC9, VCA, VCB, VCC, VCD, VCE, VCF, VD0, VD1,
/// #   VD2, VD3, VD4, VD5, VD6, VD7, VD8, VD9, VDA, VDB, VDC, VDD, VDE, VDF, VE0,
/// #   VE1, VE2, VE3, VE4, VE5, VE6, VE7, VE8, VE9, VEA, VEB, VEC, VED, VEE, VEF,
/// #   VF0, VF1, VF2, VF3, VF4, VF5, VF6, VF7, VF8, VF9, VFA, VFB, VFC, VFD, VFE,
/// #   VFF,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromBytes, Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// [safety conditions]: trait@FromBytes#safety
///
/// # Analysis
///
/// *This section describes, roughly, the analysis performed by this derive to
/// determine whether it is sound to implement `FromBytes` for a given type.
/// Unless you are modifying the implementation of this derive, or attempting to
/// manually implement `FromBytes` for a type yourself, you don't need to read
/// this section.*
///
/// If a type has the following properties, then this derive can implement
/// `FromBytes` for that type:
///
/// - If the type is a struct, all of its fields must be `FromBytes`.
/// - If the type is an enum:
///   - It must have a defined representation which is one of `u8`, `u16`, `i8`,
///     or `i16`.
///   - The maximum number of discriminants must be used (so that every possible
///     bit pattern is a valid one).
///   - Its fields must be `FromBytes`.
///
/// This analysis is subject to change. Unsafe code may *only* rely on the
/// documented [safety conditions] of `FromBytes`, and must *not* rely on the
/// implementation details of this derive.
///
/// ## Why isn't an explicit representation required for structs?
///
/// Neither this derive, nor the [safety conditions] of `FromBytes`, requires
/// that structs are marked with `#[repr(C)]`.
///
/// Per the [Rust reference](reference),
///
/// > The representation of a type can change the padding between fields, but
/// > does not change the layout of the fields themselves.
///
/// [reference]: https://doc.rust-lang.org/reference/type-layout.html#representations
///
/// Since the layout of structs only consists of padding bytes and field bytes,
/// a struct is soundly `FromBytes` if:
/// 1. its padding is soundly `FromBytes`, and
/// 2. its fields are soundly `FromBytes`.
///
/// The answer to the first question is always yes: padding bytes do not have
/// any validity constraints. A [discussion] of this question in the Unsafe Code
/// Guidelines Working Group concluded that it would be virtually unimaginable
/// for future versions of rustc to add validity constraints to padding bytes.
///
/// [discussion]: https://github.com/rust-lang/unsafe-code-guidelines/issues/174
///
/// Whether a struct is soundly `FromBytes` therefore solely depends on whether
/// its fields are `FromBytes`.
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::FromBytes;

/// Types for which any bit pattern is valid.
///
/// Any memory region of the appropriate length which contains initialized bytes
/// can be viewed as any `FromBytes` type with no runtime overhead. This is
/// useful for efficiently parsing bytes as structured data.
///
/// # Warning: Padding bytes
///
/// Note that, when a value is moved or copied, only the non-padding bytes of
/// that value are guaranteed to be preserved. It is unsound to assume that
/// values written to padding bytes are preserved after a move or copy. For
/// example, the following is unsound:
///
/// ```rust,no_run
/// use core::mem::{size_of, transmute};
/// use zerocopy::FromZeros;
/// # use zerocopy_derive::*;
///
/// // Assume `Foo` is a type with padding bytes.
/// #[derive(FromZeros, Default)]
/// struct Foo {
/// # /*
///     ...
/// # */
/// }
///
/// let mut foo: Foo = Foo::default();
/// FromZeros::zero(&mut foo);
/// // UNSOUND: Although `FromZeros::zero` writes zeros to all bytes of `foo`,
/// // those writes are not guaranteed to be preserved in padding bytes when
/// // `foo` is moved, so this may expose padding bytes as `u8`s.
/// let foo_bytes: [u8; size_of::<Foo>()] = unsafe { transmute(foo) };
/// ```
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(FromBytes)]`][derive]; e.g.:
///
/// ```
/// # use zerocopy_derive::{FromBytes, Immutable};
/// #[derive(FromBytes)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   V00, V01, V02, V03, V04, V05, V06, V07, V08, V09, V0A, V0B, V0C, V0D, V0E,
/// #   V0F, V10, V11, V12, V13, V14, V15, V16, V17, V18, V19, V1A, V1B, V1C, V1D,
/// #   V1E, V1F, V20, V21, V22, V23, V24, V25, V26, V27, V28, V29, V2A, V2B, V2C,
/// #   V2D, V2E, V2F, V30, V31, V32, V33, V34, V35, V36, V37, V38, V39, V3A, V3B,
/// #   V3C, V3D, V3E, V3F, V40, V41, V42, V43, V44, V45, V46, V47, V48, V49, V4A,
/// #   V4B, V4C, V4D, V4E, V4F, V50, V51, V52, V53, V54, V55, V56, V57, V58, V59,
/// #   V5A, V5B, V5C, V5D, V5E, V5F, V60, V61, V62, V63, V64, V65, V66, V67, V68,
/// #   V69, V6A, V6B, V6C, V6D, V6E, V6F, V70, V71, V72, V73, V74, V75, V76, V77,
/// #   V78, V79, V7A, V7B, V7C, V7D, V7E, V7F, V80, V81, V82, V83, V84, V85, V86,
/// #   V87, V88, V89, V8A, V8B, V8C, V8D, V8E, V8F, V90, V91, V92, V93, V94, V95,
/// #   V96, V97, V98, V99, V9A, V9B, V9C, V9D, V9E, V9F, VA0, VA1, VA2, VA3, VA4,
/// #   VA5, VA6, VA7, VA8, VA9, VAA, VAB, VAC, VAD, VAE, VAF, VB0, VB1, VB2, VB3,
/// #   VB4, VB5, VB6, VB7, VB8, VB9, VBA, VBB, VBC, VBD, VBE, VBF, VC0, VC1, VC2,
/// #   VC3, VC4, VC5, VC6, VC7, VC8, VC9, VCA, VCB, VCC, VCD, VCE, VCF, VD0, VD1,
/// #   VD2, VD3, VD4, VD5, VD6, VD7, VD8, VD9, VDA, VDB, VDC, VDD, VDE, VDF, VE0,
/// #   VE1, VE2, VE3, VE4, VE5, VE6, VE7, VE8, VE9, VEA, VEB, VEC, VED, VEE, VEF,
/// #   VF0, VF1, VF2, VF3, VF4, VF5, VF6, VF7, VF8, VF9, VFA, VFB, VFC, VFD, VFE,
/// #   VFF,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(FromBytes, Immutable)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// This derive performs a sophisticated, compile-time safety analysis to
/// determine whether a type is `FromBytes`.
///
/// # Safety
///
/// *This section describes what is required in order for `T: FromBytes`, and
/// what unsafe code may assume of such types. If you don't plan on implementing
/// `FromBytes` manually, and you don't plan on writing unsafe code that
/// operates on `FromBytes` types, then you don't need to read this section.*
///
/// If `T: FromBytes`, then unsafe code may assume that it is sound to produce a
/// `T` whose bytes are initialized to any sequence of valid `u8`s (in other
/// words, any byte value which is not uninitialized). If a type is marked as
/// `FromBytes` which violates this contract, it may cause undefined behavior.
///
/// `#[derive(FromBytes)]` only permits [types which satisfy these
/// requirements][derive-analysis].
///
#[cfg_attr(
    feature = "derive",
    doc = "[derive]: zerocopy_derive::FromBytes",
    doc = "[derive-analysis]: zerocopy_derive::FromBytes#analysis"
)]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.FromBytes.html"),
    doc = concat!("[derive-analysis]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.FromBytes.html#analysis"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(FromBytes)]` to `{Self}`")
)]
pub unsafe trait FromBytes: FromZeros {
    // The `Self: Sized` bound makes it so that `FromBytes` is still object
    // safe.
    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// Interprets the given `source` as a `&Self`.
    ///
    /// This method attempts to return a reference to `source` interpreted as a
    /// `Self`. If the length of `source` is not a [valid size of
    /// `Self`][valid-size], or if `source` is not appropriately aligned, this
    /// returns `Err`. If [`Self: Unaligned`][self-unaligned], you can
    /// [infallibly discard the alignment error][size-error-from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = ZSTy::ref_from_bytes(0u16.as_bytes()); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// #[derive(FromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     header: PacketHeader,
    ///     body: [u8],
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11][..];
    ///
    /// let packet = Packet::ref_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.header.src_port, [0, 1]);
    /// assert_eq!(packet.header.dst_port, [2, 3]);
    /// assert_eq!(packet.header.length, [4, 5]);
    /// assert_eq!(packet.header.checksum, [6, 7]);
    /// assert_eq!(packet.body, [8, 9, 10, 11]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_from_bytes",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn ref_from_bytes(source: &[u8]) -> Result<&Self, CastError<&[u8], Self>>
    where
        Self: KnownLayout + Immutable,
    {
        static_assert_dst_is_not_zst!(Self);
        match Ptr::from_ref(source).try_cast_into_no_leftover::<_, BecauseImmutable>(None) {
            Ok(ptr) => Ok(ptr.recall_validity().as_ref()),
            Err(err) => Err(err.map_src(|src| src.as_ref())),
        }
    }

    /// Interprets the prefix of the given `source` as a `&Self` without
    /// copying.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the leading bytes of `source`, then attempts to return
    /// both a reference to those bytes interpreted as a `Self`, and a reference
    /// to the remaining bytes. If there are insufficient bytes, or if `source`
    /// is not appropriately aligned, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. See [`ref_from_prefix_with_elems`], which does
    /// support such types. Attempting to use this method on such types results
    /// in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = ZSTy::ref_from_prefix(0u16.as_bytes()); // ⚠ Compile Error!
    /// ```
    ///
    /// [`ref_from_prefix_with_elems`]: FromBytes::ref_from_prefix_with_elems
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// #[derive(FromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     header: PacketHeader,
    ///     body: [[u8; 2]],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `Packet`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14][..];
    ///
    /// let (packet, suffix) = Packet::ref_from_prefix(bytes).unwrap();
    ///
    /// assert_eq!(packet.header.src_port, [0, 1]);
    /// assert_eq!(packet.header.dst_port, [2, 3]);
    /// assert_eq!(packet.header.length, [4, 5]);
    /// assert_eq!(packet.header.checksum, [6, 7]);
    /// assert_eq!(packet.body, [[8, 9], [10, 11], [12, 13]]);
    /// assert_eq!(suffix, &[14u8][..]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_from_prefix",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn ref_from_prefix(source: &[u8]) -> Result<(&Self, &[u8]), CastError<&[u8], Self>>
    where
        Self: KnownLayout + Immutable,
    {
        static_assert_dst_is_not_zst!(Self);
        ref_from_prefix_suffix(source, None, CastType::Prefix)
    }

    /// Interprets the suffix of the given bytes as a `&Self`.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the trailing bytes of `source`, then attempts to return
    /// both a reference to those bytes interpreted as a `Self`, and a reference
    /// to the preceding bytes. If there are insufficient bytes, or if that
    /// suffix of `source` is not appropriately aligned, this returns `Err`. If
    /// [`Self: Unaligned`][self-unaligned], you can [infallibly discard the
    /// alignment error][size-error-from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. See [`ref_from_suffix_with_elems`], which does
    /// support such types. Attempting to use this method on such types results
    /// in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = ZSTy::ref_from_suffix(0u16.as_bytes()); // ⚠ Compile Error!
    /// ```
    ///
    /// [`ref_from_suffix_with_elems`]: FromBytes::ref_from_suffix_with_elems
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct PacketTrailer {
    ///     frame_check_sequence: [u8; 4],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `PacketTrailer`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (prefix, trailer) = PacketTrailer::ref_from_suffix(bytes).unwrap();
    ///
    /// assert_eq!(prefix, &[0, 1, 2, 3, 4, 5][..]);
    /// assert_eq!(trailer.frame_check_sequence, [6, 7, 8, 9]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_from_suffix",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn ref_from_suffix(source: &[u8]) -> Result<(&[u8], &Self), CastError<&[u8], Self>>
    where
        Self: Immutable + KnownLayout,
    {
        static_assert_dst_is_not_zst!(Self);
        ref_from_prefix_suffix(source, None, CastType::Suffix).map(swap)
    }

    /// Interprets the given `source` as a `&mut Self`.
    ///
    /// This method attempts to return a reference to `source` interpreted as a
    /// `Self`. If the length of `source` is not a [valid size of
    /// `Self`][valid-size], or if `source` is not appropriately aligned, this
    /// returns `Err`. If [`Self: Unaligned`][self-unaligned], you can
    /// [infallibly discard the alignment error][size-error-from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. See [`mut_from_prefix_with_elems`], which does
    /// support such types. Attempting to use this method on such types results
    /// in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut source = [85, 85];
    /// let _ = ZSTy::mut_from_bytes(&mut source[..]); // ⚠ Compile Error!
    /// ```
    ///
    /// [`mut_from_prefix_with_elems`]: FromBytes::mut_from_prefix_with_elems
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// // These bytes encode a `PacketHeader`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let header = PacketHeader::mut_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(header.src_port, [0, 1]);
    /// assert_eq!(header.dst_port, [2, 3]);
    /// assert_eq!(header.length, [4, 5]);
    /// assert_eq!(header.checksum, [6, 7]);
    ///
    /// header.checksum = [0, 0];
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 4, 5, 0, 0]);
    ///
    /// ```
    ///
    #[doc = codegen_header!("h5", "mut_from_bytes")]
    ///
    /// See [`FromBytes::ref_from_bytes`](#method.ref_from_bytes.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn mut_from_bytes(source: &mut [u8]) -> Result<&mut Self, CastError<&mut [u8], Self>>
    where
        Self: IntoBytes + KnownLayout,
    {
        static_assert_dst_is_not_zst!(Self);
        match Ptr::from_mut(source).try_cast_into_no_leftover::<_, BecauseExclusive>(None) {
            Ok(ptr) => Ok(ptr.recall_validity::<_, (_, (_, _))>().as_mut()),
            Err(err) => Err(err.map_src(|src| src.as_mut())),
        }
    }

    /// Interprets the prefix of the given `source` as a `&mut Self` without
    /// copying.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the leading bytes of `source`, then attempts to return
    /// both a reference to those bytes interpreted as a `Self`, and a reference
    /// to the remaining bytes. If there are insufficient bytes, or if `source`
    /// is not appropriately aligned, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. See [`mut_from_suffix_with_elems`], which does
    /// support such types. Attempting to use this method on such types results
    /// in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut source = [85, 85];
    /// let _ = ZSTy::mut_from_prefix(&mut source[..]); // ⚠ Compile Error!
    /// ```
    ///
    /// [`mut_from_suffix_with_elems`]: FromBytes::mut_from_suffix_with_elems
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `PacketHeader`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (header, body) = PacketHeader::mut_from_prefix(bytes).unwrap();
    ///
    /// assert_eq!(header.src_port, [0, 1]);
    /// assert_eq!(header.dst_port, [2, 3]);
    /// assert_eq!(header.length, [4, 5]);
    /// assert_eq!(header.checksum, [6, 7]);
    /// assert_eq!(body, &[8, 9][..]);
    ///
    /// header.checksum = [0, 0];
    /// body.fill(1);
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 4, 5, 0, 0, 1, 1]);
    /// ```
    ///
    #[doc = codegen_header!("h5", "mut_from_prefix")]
    ///
    /// See [`FromBytes::ref_from_prefix`](#method.ref_from_prefix.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn mut_from_prefix(
        source: &mut [u8],
    ) -> Result<(&mut Self, &mut [u8]), CastError<&mut [u8], Self>>
    where
        Self: IntoBytes + KnownLayout,
    {
        static_assert_dst_is_not_zst!(Self);
        mut_from_prefix_suffix(source, None, CastType::Prefix)
    }

    /// Interprets the suffix of the given `source` as a `&mut Self` without
    /// copying.
    ///
    /// This method computes the [largest possible size of `Self`][valid-size]
    /// that can fit in the trailing bytes of `source`, then attempts to return
    /// both a reference to those bytes interpreted as a `Self`, and a reference
    /// to the preceding bytes. If there are insufficient bytes, or if that
    /// suffix of `source` is not appropriately aligned, this returns `Err`. If
    /// [`Self: Unaligned`][self-unaligned], you can [infallibly discard the
    /// alignment error][size-error-from].
    ///
    /// `Self` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, IntoBytes, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let mut source = [85, 85];
    /// let _ = ZSTy::mut_from_suffix(&mut source[..]); // ⚠ Compile Error!
    /// ```
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct PacketTrailer {
    ///     frame_check_sequence: [u8; 4],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `PacketTrailer`.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (prefix, trailer) = PacketTrailer::mut_from_suffix(bytes).unwrap();
    ///
    /// assert_eq!(prefix, &[0u8, 1, 2, 3, 4, 5][..]);
    /// assert_eq!(trailer.frame_check_sequence, [6, 7, 8, 9]);
    ///
    /// prefix.fill(0);
    /// trailer.frame_check_sequence.fill(1);
    ///
    /// assert_eq!(bytes, [0, 0, 0, 0, 0, 0, 1, 1, 1, 1]);
    /// ```
    ///
    #[doc = codegen_header!("h5", "mut_from_suffix")]
    ///
    /// See [`FromBytes::ref_from_suffix`](#method.ref_from_suffix.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn mut_from_suffix(
        source: &mut [u8],
    ) -> Result<(&mut [u8], &mut Self), CastError<&mut [u8], Self>>
    where
        Self: IntoBytes + KnownLayout,
    {
        static_assert_dst_is_not_zst!(Self);
        mut_from_prefix_suffix(source, None, CastType::Suffix).map(swap)
    }

    /// Interprets the given `source` as a `&Self` with a DST length equal to
    /// `count`.
    ///
    /// This method attempts to return a reference to `source` interpreted as a
    /// `Self` with `count` trailing elements. If the length of `source` is not
    /// equal to the size of `Self` with `count` elements, or if `source` is not
    /// appropriately aligned, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// # #[derive(Debug, PartialEq, Eq)]
    /// #[derive(FromBytes, Immutable)]
    /// #[repr(C)]
    /// struct Pixel {
    ///     r: u8,
    ///     g: u8,
    ///     b: u8,
    ///     a: u8,
    /// }
    ///
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let pixels = <[Pixel]>::ref_from_bytes_with_elems(bytes, 2).unwrap();
    ///
    /// assert_eq!(pixels, &[
    ///     Pixel { r: 0, g: 1, b: 2, a: 3 },
    ///     Pixel { r: 4, g: 5, b: 6, a: 7 },
    /// ]);
    ///
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`ref_from_bytes`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = &[85, 85][..];
    /// let zsty = ZSTy::ref_from_bytes_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`ref_from_bytes`]: FromBytes::ref_from_bytes
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_from_bytes_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn ref_from_bytes_with_elems(
        source: &[u8],
        count: usize,
    ) -> Result<&Self, CastError<&[u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + Immutable,
    {
        let source = Ptr::from_ref(source);
        let maybe_slf = source.try_cast_into_no_leftover::<_, BecauseImmutable>(Some(count));
        match maybe_slf {
            Ok(slf) => Ok(slf.recall_validity().as_ref()),
            Err(err) => Err(err.map_src(|s| s.as_ref())),
        }
    }

    /// Interprets the prefix of the given `source` as a DST `&Self` with length
    /// equal to `count`.
    ///
    /// This method attempts to return a reference to the prefix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the remaining bytes. If there are insufficient bytes, or if `source`
    /// is not appropriately aligned, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// # #[derive(Debug, PartialEq, Eq)]
    /// #[derive(FromBytes, Immutable)]
    /// #[repr(C)]
    /// struct Pixel {
    ///     r: u8,
    ///     g: u8,
    ///     b: u8,
    ///     a: u8,
    /// }
    ///
    /// // These are more bytes than are needed to encode two `Pixel`s.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (pixels, suffix) = <[Pixel]>::ref_from_prefix_with_elems(bytes, 2).unwrap();
    ///
    /// assert_eq!(pixels, &[
    ///     Pixel { r: 0, g: 1, b: 2, a: 3 },
    ///     Pixel { r: 4, g: 5, b: 6, a: 7 },
    /// ]);
    ///
    /// assert_eq!(suffix, &[8, 9]);
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`ref_from_prefix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = &[85, 85][..];
    /// let (zsty, _) = ZSTy::ref_from_prefix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`ref_from_prefix`]: FromBytes::ref_from_prefix
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_from_prefix_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn ref_from_prefix_with_elems(
        source: &[u8],
        count: usize,
    ) -> Result<(&Self, &[u8]), CastError<&[u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + Immutable,
    {
        ref_from_prefix_suffix(source, Some(count), CastType::Prefix)
    }

    /// Interprets the suffix of the given `source` as a DST `&Self` with length
    /// equal to `count`.
    ///
    /// This method attempts to return a reference to the suffix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the preceding bytes. If there are insufficient bytes, or if that
    /// suffix of `source` is not appropriately aligned, this returns `Err`. If
    /// [`Self: Unaligned`][self-unaligned], you can [infallibly discard the
    /// alignment error][size-error-from].
    ///
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// # #[derive(Debug, PartialEq, Eq)]
    /// #[derive(FromBytes, Immutable)]
    /// #[repr(C)]
    /// struct Pixel {
    ///     r: u8,
    ///     g: u8,
    ///     b: u8,
    ///     a: u8,
    /// }
    ///
    /// // These are more bytes than are needed to encode two `Pixel`s.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (prefix, pixels) = <[Pixel]>::ref_from_suffix_with_elems(bytes, 2).unwrap();
    ///
    /// assert_eq!(prefix, &[0, 1]);
    ///
    /// assert_eq!(pixels, &[
    ///     Pixel { r: 2, g: 3, b: 4, a: 5 },
    ///     Pixel { r: 6, g: 7, b: 8, a: 9 },
    /// ]);
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`ref_from_suffix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = &[85, 85][..];
    /// let (_, zsty) = ZSTy::ref_from_suffix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`ref_from_suffix`]: FromBytes::ref_from_suffix
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_from_suffix_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn ref_from_suffix_with_elems(
        source: &[u8],
        count: usize,
    ) -> Result<(&[u8], &Self), CastError<&[u8], Self>>
    where
        Self: KnownLayout<PointerMetadata = usize> + Immutable,
    {
        ref_from_prefix_suffix(source, Some(count), CastType::Suffix).map(swap)
    }

    /// Interprets the given `source` as a `&mut Self` with a DST length equal
    /// to `count`.
    ///
    /// This method attempts to return a reference to `source` interpreted as a
    /// `Self` with `count` trailing elements. If the length of `source` is not
    /// equal to the size of `Self` with `count` elements, or if `source` is not
    /// appropriately aligned, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// # #[derive(Debug, PartialEq, Eq)]
    /// #[derive(KnownLayout, FromBytes, IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct Pixel {
    ///     r: u8,
    ///     g: u8,
    ///     b: u8,
    ///     a: u8,
    /// }
    ///
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let pixels = <[Pixel]>::mut_from_bytes_with_elems(bytes, 2).unwrap();
    ///
    /// assert_eq!(pixels, &[
    ///     Pixel { r: 0, g: 1, b: 2, a: 3 },
    ///     Pixel { r: 4, g: 5, b: 6, a: 7 },
    /// ]);
    ///
    /// pixels[1] = Pixel { r: 0, g: 0, b: 0, a: 0 };
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 0, 0, 0, 0]);
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`mut_from_bytes`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, Immutable, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = &mut [85, 85][..];
    /// let zsty = ZSTy::mut_from_bytes_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`mut_from_bytes`]: FromBytes::mut_from_bytes
    ///
    #[doc = codegen_header!("h5", "mut_from_bytes_with_elems")]
    ///
    /// See [`TryFromBytes::ref_from_bytes_with_elems`](#method.ref_from_bytes_with_elems.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn mut_from_bytes_with_elems(
        source: &mut [u8],
        count: usize,
    ) -> Result<&mut Self, CastError<&mut [u8], Self>>
    where
        Self: IntoBytes + KnownLayout<PointerMetadata = usize> + Immutable,
    {
        let source = Ptr::from_mut(source);
        let maybe_slf = source.try_cast_into_no_leftover::<_, BecauseImmutable>(Some(count));
        match maybe_slf {
            Ok(slf) => Ok(slf.recall_validity::<_, (_, (_, BecauseExclusive))>().as_mut()),
            Err(err) => Err(err.map_src(|s| s.as_mut())),
        }
    }

    /// Interprets the prefix of the given `source` as a `&mut Self` with DST
    /// length equal to `count`.
    ///
    /// This method attempts to return a reference to the prefix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the preceding bytes. If there are insufficient bytes, or if `source`
    /// is not appropriately aligned, this returns `Err`. If [`Self:
    /// Unaligned`][self-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// # #[derive(Debug, PartialEq, Eq)]
    /// #[derive(KnownLayout, FromBytes, IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct Pixel {
    ///     r: u8,
    ///     g: u8,
    ///     b: u8,
    ///     a: u8,
    /// }
    ///
    /// // These are more bytes than are needed to encode two `Pixel`s.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (pixels, suffix) = <[Pixel]>::mut_from_prefix_with_elems(bytes, 2).unwrap();
    ///
    /// assert_eq!(pixels, &[
    ///     Pixel { r: 0, g: 1, b: 2, a: 3 },
    ///     Pixel { r: 4, g: 5, b: 6, a: 7 },
    /// ]);
    ///
    /// assert_eq!(suffix, &[8, 9]);
    ///
    /// pixels[1] = Pixel { r: 0, g: 0, b: 0, a: 0 };
    /// suffix.fill(1);
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 0, 0, 0, 0, 1, 1]);
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`mut_from_prefix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, Immutable, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = &mut [85, 85][..];
    /// let (zsty, _) = ZSTy::mut_from_prefix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`mut_from_prefix`]: FromBytes::mut_from_prefix
    ///
    #[doc = codegen_header!("h5", "mut_from_prefix_with_elems")]
    ///
    /// See [`TryFromBytes::ref_from_prefix_with_elems`](#method.ref_from_prefix_with_elems.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn mut_from_prefix_with_elems(
        source: &mut [u8],
        count: usize,
    ) -> Result<(&mut Self, &mut [u8]), CastError<&mut [u8], Self>>
    where
        Self: IntoBytes + KnownLayout<PointerMetadata = usize>,
    {
        mut_from_prefix_suffix(source, Some(count), CastType::Prefix)
    }

    /// Interprets the suffix of the given `source` as a `&mut Self` with DST
    /// length equal to `count`.
    ///
    /// This method attempts to return a reference to the suffix of `source`
    /// interpreted as a `Self` with `count` trailing elements, and a reference
    /// to the remaining bytes. If there are insufficient bytes, or if that
    /// suffix of `source` is not appropriately aligned, this returns `Err`. If
    /// [`Self: Unaligned`][self-unaligned], you can [infallibly discard the
    /// alignment error][size-error-from].
    ///
    /// [self-unaligned]: Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// # #[derive(Debug, PartialEq, Eq)]
    /// #[derive(FromBytes, IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct Pixel {
    ///     r: u8,
    ///     g: u8,
    ///     b: u8,
    ///     a: u8,
    /// }
    ///
    /// // These are more bytes than are needed to encode two `Pixel`s.
    /// let bytes = &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (prefix, pixels) = <[Pixel]>::mut_from_suffix_with_elems(bytes, 2).unwrap();
    ///
    /// assert_eq!(prefix, &[0, 1]);
    ///
    /// assert_eq!(pixels, &[
    ///     Pixel { r: 2, g: 3, b: 4, a: 5 },
    ///     Pixel { r: 6, g: 7, b: 8, a: 9 },
    /// ]);
    ///
    /// prefix.fill(9);
    /// pixels[1] = Pixel { r: 0, g: 0, b: 0, a: 0 };
    ///
    /// assert_eq!(bytes, [9, 9, 2, 3, 4, 5, 0, 0, 0, 0]);
    /// ```
    ///
    /// Since an explicit `count` is provided, this method supports types with
    /// zero-sized trailing slice elements. Methods such as [`mut_from_suffix`]
    /// which do not take an explicit count do not support such types.
    ///
    /// ```
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, Immutable, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct ZSTy {
    ///     leading_sized: [u8; 2],
    ///     trailing_dst: [()],
    /// }
    ///
    /// let src = &mut [85, 85][..];
    /// let (_, zsty) = ZSTy::mut_from_suffix_with_elems(src, 42).unwrap();
    /// assert_eq!(zsty.trailing_dst.len(), 42);
    /// ```
    ///
    /// [`mut_from_suffix`]: FromBytes::mut_from_suffix
    ///
    #[doc = codegen_header!("h5", "mut_from_suffix_with_elems")]
    ///
    /// See [`TryFromBytes::ref_from_suffix_with_elems`](#method.ref_from_suffix_with_elems.codegen).
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn mut_from_suffix_with_elems(
        source: &mut [u8],
        count: usize,
    ) -> Result<(&mut [u8], &mut Self), CastError<&mut [u8], Self>>
    where
        Self: IntoBytes + KnownLayout<PointerMetadata = usize>,
    {
        mut_from_prefix_suffix(source, Some(count), CastType::Suffix).map(swap)
    }

    /// Reads a copy of `Self` from the given `source`.
    ///
    /// If `source.len() != size_of::<Self>()`, `read_from_bytes` returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// // These bytes encode a `PacketHeader`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7][..];
    ///
    /// let header = PacketHeader::read_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(header.src_port, [0, 1]);
    /// assert_eq!(header.dst_port, [2, 3]);
    /// assert_eq!(header.length, [4, 5]);
    /// assert_eq!(header.checksum, [6, 7]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "read_from_bytes",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn read_from_bytes(source: &[u8]) -> Result<Self, SizeError<&[u8], Self>>
    where
        Self: Sized,
    {
        match Ref::<_, Unalign<Self>>::sized_from(source) {
            Ok(r) => Ok(Ref::read(&r).into_inner()),
            Err(CastError::Size(e)) => Err(e.with_dst()),
            Err(CastError::Alignment(_)) => {
                // SAFETY: `Unalign<Self>` is trivially aligned, so
                // `Ref::sized_from` cannot fail due to unmet alignment
                // requirements.
                unsafe { core::hint::unreachable_unchecked() }
            }
            Err(CastError::Validity(i)) => match i {},
        }
    }

    /// Reads a copy of `Self` from the prefix of the given `source`.
    ///
    /// This attempts to read a `Self` from the first `size_of::<Self>()` bytes
    /// of `source`, returning that `Self` and any remaining bytes. If
    /// `source.len() < size_of::<Self>()`, it returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `PacketHeader`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (header, body) = PacketHeader::read_from_prefix(bytes).unwrap();
    ///
    /// assert_eq!(header.src_port, [0, 1]);
    /// assert_eq!(header.dst_port, [2, 3]);
    /// assert_eq!(header.length, [4, 5]);
    /// assert_eq!(header.checksum, [6, 7]);
    /// assert_eq!(body, [8, 9]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "read_from_prefix",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn read_from_prefix(source: &[u8]) -> Result<(Self, &[u8]), SizeError<&[u8], Self>>
    where
        Self: Sized,
    {
        match Ref::<_, Unalign<Self>>::sized_from_prefix(source) {
            Ok((r, suffix)) => Ok((Ref::read(&r).into_inner(), suffix)),
            Err(CastError::Size(e)) => Err(e.with_dst()),
            Err(CastError::Alignment(_)) => {
                // SAFETY: `Unalign<Self>` is trivially aligned, so
                // `Ref::sized_from_prefix` cannot fail due to unmet alignment
                // requirements.
                unsafe { core::hint::unreachable_unchecked() }
            }
            Err(CastError::Validity(i)) => match i {},
        }
    }

    /// Reads a copy of `Self` from the suffix of the given `source`.
    ///
    /// This attempts to read a `Self` from the last `size_of::<Self>()` bytes
    /// of `source`, returning that `Self` and any preceding bytes. If
    /// `source.len() < size_of::<Self>()`, it returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::FromBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes)]
    /// #[repr(C)]
    /// struct PacketTrailer {
    ///     frame_check_sequence: [u8; 4],
    /// }
    ///
    /// // These are more bytes than are needed to encode a `PacketTrailer`.
    /// let bytes = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let (prefix, trailer) = PacketTrailer::read_from_suffix(bytes).unwrap();
    ///
    /// assert_eq!(prefix, [0, 1, 2, 3, 4, 5]);
    /// assert_eq!(trailer.frame_check_sequence, [6, 7, 8, 9]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "read_from_suffix",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    fn read_from_suffix(source: &[u8]) -> Result<(&[u8], Self), SizeError<&[u8], Self>>
    where
        Self: Sized,
    {
        match Ref::<_, Unalign<Self>>::sized_from_suffix(source) {
            Ok((prefix, r)) => Ok((prefix, Ref::read(&r).into_inner())),
            Err(CastError::Size(e)) => Err(e.with_dst()),
            Err(CastError::Alignment(_)) => {
                // SAFETY: `Unalign<Self>` is trivially aligned, so
                // `Ref::sized_from_suffix` cannot fail due to unmet alignment
                // requirements.
                unsafe { core::hint::unreachable_unchecked() }
            }
            Err(CastError::Validity(i)) => match i {},
        }
    }

    /// Reads a copy of `self` from an `io::Read`.
    ///
    /// This is useful for interfacing with operating system byte sinks (files,
    /// sockets, etc.).
    ///
    /// # Examples
    ///
    /// ```no_run
    /// use zerocopy::{byteorder::big_endian::*, FromBytes};
    /// use std::fs::File;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes)]
    /// #[repr(C)]
    /// struct BitmapFileHeader {
    ///     signature: [u8; 2],
    ///     size: U32,
    ///     reserved: U64,
    ///     offset: U64,
    /// }
    ///
    /// let mut file = File::open("image.bin").unwrap();
    /// let header = BitmapFileHeader::read_from_io(&mut file).unwrap();
    /// ```
    #[cfg(feature = "std")]
    #[cfg_attr(doc_cfg, doc(cfg(feature = "std")))]
    #[inline(always)]
    fn read_from_io<R>(mut src: R) -> io::Result<Self>
    where
        Self: Sized,
        R: io::Read,
    {
        // NOTE(#2319, #2320): We do `buf.zero()` separately rather than
        // constructing `let buf = CoreMaybeUninit::zeroed()` because, if `Self`
        // contains padding bytes, then a typed copy of `CoreMaybeUninit<Self>`
        // will not necessarily preserve zeros written to those padding byte
        // locations, and so `buf` could contain uninitialized bytes.
        let mut buf = CoreMaybeUninit::<Self>::uninit();
        buf.zero();

        let ptr = Ptr::from_mut(&mut buf);
        // SAFETY: After `buf.zero()`, `buf` consists entirely of initialized,
        // zeroed bytes. Since `MaybeUninit` has no validity requirements, `ptr`
        // cannot be used to write values which will violate `buf`'s bit
        // validity. Since `ptr` has `Exclusive` aliasing, nothing other than
        // `ptr` may be used to mutate `ptr`'s referent, and so its bit validity
        // cannot be violated even though `buf` may have more permissive bit
        // validity than `ptr`.
        let ptr = unsafe { ptr.assume_validity::<invariant::Initialized>() };
        let ptr = ptr.as_bytes();
        src.read_exact(ptr.as_mut())?;
        // SAFETY: `buf` entirely consists of initialized bytes, and `Self` is
        // `FromBytes`.
        Ok(unsafe { buf.assume_init() })
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromBytes::ref_from_bytes`")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn ref_from(source: &[u8]) -> Option<&Self>
    where
        Self: KnownLayout + Immutable,
    {
        Self::ref_from_bytes(source).ok()
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromBytes::mut_from_bytes`")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn mut_from(source: &mut [u8]) -> Option<&mut Self>
    where
        Self: KnownLayout + IntoBytes,
    {
        Self::mut_from_bytes(source).ok()
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromBytes::ref_from_prefix_with_elems`")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn slice_from_prefix(source: &[u8], count: usize) -> Option<(&[Self], &[u8])>
    where
        Self: Sized + Immutable,
    {
        <[Self]>::ref_from_prefix_with_elems(source, count).ok()
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromBytes::ref_from_suffix_with_elems`")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn slice_from_suffix(source: &[u8], count: usize) -> Option<(&[u8], &[Self])>
    where
        Self: Sized + Immutable,
    {
        <[Self]>::ref_from_suffix_with_elems(source, count).ok()
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromBytes::mut_from_prefix_with_elems`")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn mut_slice_from_prefix(source: &mut [u8], count: usize) -> Option<(&mut [Self], &mut [u8])>
    where
        Self: Sized + IntoBytes,
    {
        <[Self]>::mut_from_prefix_with_elems(source, count).ok()
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromBytes::mut_from_suffix_with_elems`")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn mut_slice_from_suffix(source: &mut [u8], count: usize) -> Option<(&mut [u8], &mut [Self])>
    where
        Self: Sized + IntoBytes,
    {
        <[Self]>::mut_from_suffix_with_elems(source, count).ok()
    }

    #[deprecated(since = "0.8.0", note = "renamed to `FromBytes::read_from_bytes`")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn read_from(source: &[u8]) -> Option<Self>
    where
        Self: Sized,
    {
        Self::read_from_bytes(source).ok()
    }
}

/// Interprets the given affix of the given bytes as a `&Self`.
///
/// This method computes the largest possible size of `Self` that can fit in the
/// prefix or suffix bytes of `source`, then attempts to return both a reference
/// to those bytes interpreted as a `Self`, and a reference to the excess bytes.
/// If there are insufficient bytes, or if that affix of `source` is not
/// appropriately aligned, this returns `Err`.
#[inline(always)]
fn ref_from_prefix_suffix<T: FromBytes + KnownLayout + Immutable + ?Sized>(
    source: &[u8],
    meta: Option<T::PointerMetadata>,
    cast_type: CastType,
) -> Result<(&T, &[u8]), CastError<&[u8], T>> {
    let (slf, prefix_suffix) = Ptr::from_ref(source)
        .try_cast_into::<_, BecauseImmutable>(cast_type, meta)
        .map_err(|err| err.map_src(|s| s.as_ref()))?;
    Ok((slf.recall_validity().as_ref(), prefix_suffix.as_ref()))
}

/// Interprets the given affix of the given bytes as a `&mut Self` without
/// copying.
///
/// This method computes the largest possible size of `Self` that can fit in the
/// prefix or suffix bytes of `source`, then attempts to return both a reference
/// to those bytes interpreted as a `Self`, and a reference to the excess bytes.
/// If there are insufficient bytes, or if that affix of `source` is not
/// appropriately aligned, this returns `Err`.
#[inline(always)]
fn mut_from_prefix_suffix<T: FromBytes + IntoBytes + KnownLayout + ?Sized>(
    source: &mut [u8],
    meta: Option<T::PointerMetadata>,
    cast_type: CastType,
) -> Result<(&mut T, &mut [u8]), CastError<&mut [u8], T>> {
    let (slf, prefix_suffix) = Ptr::from_mut(source)
        .try_cast_into::<_, BecauseExclusive>(cast_type, meta)
        .map_err(|err| err.map_src(|s| s.as_mut()))?;
    Ok((slf.recall_validity::<_, (_, (_, _))>().as_mut(), prefix_suffix.as_mut()))
}

/// Analyzes whether a type is [`IntoBytes`].
///
/// This derive analyzes, at compile time, whether the annotated type satisfies
/// the [safety conditions] of `IntoBytes` and implements `IntoBytes` if it is
/// sound to do so. This derive can be applied to structs and enums (see below
/// for union support); e.g.:
///
/// ```
/// # use zerocopy_derive::{IntoBytes};
/// #[derive(IntoBytes)]
/// #[repr(C)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(IntoBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// [safety conditions]: trait@IntoBytes#safety
///
/// # Error Messages
///
/// On Rust toolchains prior to 1.78.0, due to the way that the custom derive
/// for `IntoBytes` is implemented, you may get an error like this:
///
/// ```text
/// error[E0277]: the trait bound `(): PaddingFree<Foo, true>` is not satisfied
///   --> lib.rs:23:10
///    |
///  1 | #[derive(IntoBytes)]
///    |          ^^^^^^^^^ the trait `PaddingFree<Foo, true>` is not implemented for `()`
///    |
///    = help: the following implementations were found:
///                   <() as PaddingFree<T, false>>
/// ```
///
/// This error indicates that the type being annotated has padding bytes, which
/// is illegal for `IntoBytes` types. Consider reducing the alignment of some
/// fields by using types in the [`byteorder`] module, wrapping field types in
/// [`Unalign`], adding explicit struct fields where those padding bytes would
/// be, or using `#[repr(packed)]`. See the Rust Reference's page on [type
/// layout] for more information about type layout and padding.
///
/// [type layout]: https://doc.rust-lang.org/reference/type-layout.html
///
/// # Unions
///
/// Currently, union bit validity is [up in the air][union-validity], and so
/// zerocopy does not support `#[derive(IntoBytes)]` on unions by default.
/// However, implementing `IntoBytes` on a union type is likely sound on all
/// existing Rust toolchains - it's just that it may become unsound in the
/// future. You can opt-in to `#[derive(IntoBytes)]` support on unions by
/// passing the unstable `zerocopy_derive_union_into_bytes` cfg:
///
/// ```shell
/// $ RUSTFLAGS='--cfg zerocopy_derive_union_into_bytes' cargo build
/// ```
///
/// However, it is your responsibility to ensure that this derive is sound on
/// the specific versions of the Rust toolchain you are using! We make no
/// stability or soundness guarantees regarding this cfg, and may remove it at
/// any point.
///
/// We are actively working with Rust to stabilize the necessary language
/// guarantees to support this in a forwards-compatible way, which will enable
/// us to remove the cfg gate. As part of this effort, we need to know how much
/// demand there is for this feature. If you would like to use `IntoBytes` on
/// unions, [please let us know][discussion].
///
/// [union-validity]: https://github.com/rust-lang/unsafe-code-guidelines/issues/438
/// [discussion]: https://github.com/google/zerocopy/discussions/1802
///
/// # Analysis
///
/// *This section describes, roughly, the analysis performed by this derive to
/// determine whether it is sound to implement `IntoBytes` for a given type.
/// Unless you are modifying the implementation of this derive, or attempting to
/// manually implement `IntoBytes` for a type yourself, you don't need to read
/// this section.*
///
/// If a type has the following properties, then this derive can implement
/// `IntoBytes` for that type:
///
/// - If the type is a struct, its fields must be [`IntoBytes`]. Additionally:
///     - if the type is `repr(transparent)` or `repr(packed)`, it is
///       [`IntoBytes`] if its fields are [`IntoBytes`]; else,
///     - if the type is `repr(C)` with at most one field, it is [`IntoBytes`]
///       if its field is [`IntoBytes`]; else,
///     - if the type has no generic parameters, it is [`IntoBytes`] if the type
///       is sized and has no padding bytes; else,
///     - if the type is `repr(C)`, its fields must be [`Unaligned`].
/// - If the type is an enum:
///   - It must have a defined representation (`repr`s `C`, `u8`, `u16`, `u32`,
///     `u64`, `usize`, `i8`, `i16`, `i32`, `i64`, or `isize`).
///   - It must have no padding bytes.
///   - Its fields must be [`IntoBytes`].
///
/// This analysis is subject to change. Unsafe code may *only* rely on the
/// documented [safety conditions] of `FromBytes`, and must *not* rely on the
/// implementation details of this derive.
///
/// [Rust Reference]: https://doc.rust-lang.org/reference/type-layout.html
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::IntoBytes;

/// Types that can be converted to an immutable slice of initialized bytes.
///
/// Any `IntoBytes` type can be converted to a slice of initialized bytes of the
/// same size. This is useful for efficiently serializing structured data as raw
/// bytes.
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(IntoBytes)]`][derive]; e.g.:
///
/// ```
/// # use zerocopy_derive::IntoBytes;
/// #[derive(IntoBytes)]
/// #[repr(C)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(IntoBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant0,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// This derive performs a sophisticated, compile-time safety analysis to
/// determine whether a type is `IntoBytes`. See the [derive
/// documentation][derive] for guidance on how to interpret error messages
/// produced by the derive's analysis.
///
/// # Safety
///
/// *This section describes what is required in order for `T: IntoBytes`, and
/// what unsafe code may assume of such types. If you don't plan on implementing
/// `IntoBytes` manually, and you don't plan on writing unsafe code that
/// operates on `IntoBytes` types, then you don't need to read this section.*
///
/// If `T: IntoBytes`, then unsafe code may assume that it is sound to treat any
/// `t: T` as an immutable `[u8]` of length `size_of_val(t)`. If a type is
/// marked as `IntoBytes` which violates this contract, it may cause undefined
/// behavior.
///
/// `#[derive(IntoBytes)]` only permits [types which satisfy these
/// requirements][derive-analysis].
///
#[cfg_attr(
    feature = "derive",
    doc = "[derive]: zerocopy_derive::IntoBytes",
    doc = "[derive-analysis]: zerocopy_derive::IntoBytes#analysis"
)]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.IntoBytes.html"),
    doc = concat!("[derive-analysis]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.IntoBytes.html#analysis"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(IntoBytes)]` to `{Self}`")
)]
pub unsafe trait IntoBytes {
    // The `Self: Sized` bound makes it so that this function doesn't prevent
    // `IntoBytes` from being object safe. Note that other `IntoBytes` methods
    // prevent object safety, but those provide a benefit in exchange for object
    // safety. If at some point we remove those methods, change their type
    // signatures, or move them out of this trait so that `IntoBytes` is object
    // safe again, it's important that this function not prevent object safety.
    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// Gets the bytes of this value.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::IntoBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// let header = PacketHeader {
    ///     src_port: [0, 1],
    ///     dst_port: [2, 3],
    ///     length: [4, 5],
    ///     checksum: [6, 7],
    /// };
    ///
    /// let bytes = header.as_bytes();
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 4, 5, 6, 7]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "as_bytes",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn as_bytes(&self) -> &[u8]
    where
        Self: Immutable,
    {
        // Note that this method does not have a `Self: Sized` bound;
        // `size_of_val` works for unsized values too.
        let len = mem::size_of_val(self);
        let slf: *const Self = self;

        // SAFETY:
        // - `slf.cast::<u8>()` is valid for reads for `len * size_of::<u8>()`
        //   many bytes because...
        //   - `slf` is the same pointer as `self`, and `self` is a reference
        //     which points to an object whose size is `len`. Thus...
        //     - The entire region of `len` bytes starting at `slf` is contained
        //       within a single allocation.
        //     - `slf` is non-null.
        //   - `slf` is trivially aligned to `align_of::<u8>() == 1`.
        // - `Self: IntoBytes` ensures that all of the bytes of `slf` are
        //   initialized.
        // - Since `slf` is derived from `self`, and `self` is an immutable
        //   reference, the only other references to this memory region that
        //   could exist are other immutable references, which by `Self:
        //   Immutable` don't permit mutation.
        // - The total size of the resulting slice is no larger than
        //   `isize::MAX` because no allocation produced by safe code can be
        //   larger than `isize::MAX`.
        //
        // FIXME(#429): Add references to docs and quotes.
        unsafe { slice::from_raw_parts(slf.cast::<u8>(), len) }
    }

    /// Gets the bytes of this value mutably.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::IntoBytes;
    /// # use zerocopy_derive::*;
    ///
    /// # #[derive(Eq, PartialEq, Debug)]
    /// #[derive(FromBytes, IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// let mut header = PacketHeader {
    ///     src_port: [0, 1],
    ///     dst_port: [2, 3],
    ///     length: [4, 5],
    ///     checksum: [6, 7],
    /// };
    ///
    /// let bytes = header.as_mut_bytes();
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 4, 5, 6, 7]);
    ///
    /// bytes.reverse();
    ///
    /// assert_eq!(header, PacketHeader {
    ///     src_port: [7, 6],
    ///     dst_port: [5, 4],
    ///     length: [3, 2],
    ///     checksum: [1, 0],
    /// });
    /// ```
    ///
    #[doc = codegen_header!("h5", "as_mut_bytes")]
    ///
    /// See [`IntoBytes::as_bytes`](#method.as_bytes.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    fn as_mut_bytes(&mut self) -> &mut [u8]
    where
        Self: FromBytes,
    {
        // Note that this method does not have a `Self: Sized` bound;
        // `size_of_val` works for unsized values too.
        let len = mem::size_of_val(self);
        let slf: *mut Self = self;

        // SAFETY:
        // - `slf.cast::<u8>()` is valid for reads and writes for `len *
        //   size_of::<u8>()` many bytes because...
        //   - `slf` is the same pointer as `self`, and `self` is a reference
        //     which points to an object whose size is `len`. Thus...
        //     - The entire region of `len` bytes starting at `slf` is contained
        //       within a single allocation.
        //     - `slf` is non-null.
        //   - `slf` is trivially aligned to `align_of::<u8>() == 1`.
        // - `Self: IntoBytes` ensures that all of the bytes of `slf` are
        //   initialized.
        // - `Self: FromBytes` ensures that no write to this memory region
        //   could result in it containing an invalid `Self`.
        // - Since `slf` is derived from `self`, and `self` is a mutable
        //   reference, no other references to this memory region can exist.
        // - The total size of the resulting slice is no larger than
        //   `isize::MAX` because no allocation produced by safe code can be
        //   larger than `isize::MAX`.
        //
        // FIXME(#429): Add references to docs and quotes.
        unsafe { slice::from_raw_parts_mut(slf.cast::<u8>(), len) }
    }

    /// Writes a copy of `self` to `dst`.
    ///
    /// If `dst.len() != size_of_val(self)`, `write_to` returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::IntoBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// let header = PacketHeader {
    ///     src_port: [0, 1],
    ///     dst_port: [2, 3],
    ///     length: [4, 5],
    ///     checksum: [6, 7],
    /// };
    ///
    /// let mut bytes = [0, 0, 0, 0, 0, 0, 0, 0];
    ///
    /// header.write_to(&mut bytes[..]);
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 4, 5, 6, 7]);
    /// ```
    ///
    /// If too many or too few target bytes are provided, `write_to` returns
    /// `Err` and leaves the target bytes unmodified:
    ///
    /// ```
    /// # use zerocopy::IntoBytes;
    /// # let header = u128::MAX;
    /// let mut excessive_bytes = &mut [0u8; 128][..];
    ///
    /// let write_result = header.write_to(excessive_bytes);
    ///
    /// assert!(write_result.is_err());
    /// assert_eq!(excessive_bytes, [0u8; 128]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "write_to",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ]
    )]
    #[must_use = "callers should check the return value to see if the operation succeeded"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    #[allow(clippy::mut_from_ref)] // False positive: `&self -> &mut [u8]`
    fn write_to(&self, dst: &mut [u8]) -> Result<(), SizeError<&Self, &mut [u8]>>
    where
        Self: Immutable,
    {
        let src = self.as_bytes();
        if dst.len() == src.len() {
            // SAFETY: Within this branch of the conditional, we have ensured
            // that `dst.len()` is equal to `src.len()`. Neither the size of the
            // source nor the size of the destination change between the above
            // size check and the invocation of `copy_unchecked`.
            unsafe { util::copy_unchecked(src, dst) }
            Ok(())
        } else {
            Err(SizeError::new(self))
        }
    }

    /// Writes a copy of `self` to the prefix of `dst`.
    ///
    /// `write_to_prefix` writes `self` to the first `size_of_val(self)` bytes
    /// of `dst`. If `dst.len() < size_of_val(self)`, it returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::IntoBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// let header = PacketHeader {
    ///     src_port: [0, 1],
    ///     dst_port: [2, 3],
    ///     length: [4, 5],
    ///     checksum: [6, 7],
    /// };
    ///
    /// let mut bytes = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    ///
    /// header.write_to_prefix(&mut bytes[..]);
    ///
    /// assert_eq!(bytes, [0, 1, 2, 3, 4, 5, 6, 7, 0, 0]);
    /// ```
    ///
    /// If insufficient target bytes are provided, `write_to_prefix` returns
    /// `Err` and leaves the target bytes unmodified:
    ///
    /// ```
    /// # use zerocopy::IntoBytes;
    /// # let header = u128::MAX;
    /// let mut insufficient_bytes = &mut [0, 0][..];
    ///
    /// let write_result = header.write_to_suffix(insufficient_bytes);
    ///
    /// assert!(write_result.is_err());
    /// assert_eq!(insufficient_bytes, [0, 0]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "write_to_prefix",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ]
    )]
    #[must_use = "callers should check the return value to see if the operation succeeded"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    #[allow(clippy::mut_from_ref)] // False positive: `&self -> &mut [u8]`
    fn write_to_prefix(&self, dst: &mut [u8]) -> Result<(), SizeError<&Self, &mut [u8]>>
    where
        Self: Immutable,
    {
        let src = self.as_bytes();
        match dst.get_mut(..src.len()) {
            Some(dst) => {
                // SAFETY: Within this branch of the `match`, we have ensured
                // through fallible subslicing that `dst.len()` is equal to
                // `src.len()`. Neither the size of the source nor the size of
                // the destination change between the above subslicing operation
                // and the invocation of `copy_unchecked`.
                unsafe { util::copy_unchecked(src, dst) }
                Ok(())
            }
            None => Err(SizeError::new(self)),
        }
    }

    /// Writes a copy of `self` to the suffix of `dst`.
    ///
    /// `write_to_suffix` writes `self` to the last `size_of_val(self)` bytes of
    /// `dst`. If `dst.len() < size_of_val(self)`, it returns `Err`.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::IntoBytes;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(IntoBytes, Immutable)]
    /// #[repr(C)]
    /// struct PacketHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// let header = PacketHeader {
    ///     src_port: [0, 1],
    ///     dst_port: [2, 3],
    ///     length: [4, 5],
    ///     checksum: [6, 7],
    /// };
    ///
    /// let mut bytes = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    ///
    /// header.write_to_suffix(&mut bytes[..]);
    ///
    /// assert_eq!(bytes, [0, 0, 0, 1, 2, 3, 4, 5, 6, 7]);
    ///
    /// let mut insufficient_bytes = &mut [0, 0][..];
    ///
    /// let write_result = header.write_to_suffix(insufficient_bytes);
    ///
    /// assert!(write_result.is_err());
    /// assert_eq!(insufficient_bytes, [0, 0]);
    /// ```
    ///
    /// If insufficient target bytes are provided, `write_to_suffix` returns
    /// `Err` and leaves the target bytes unmodified:
    ///
    /// ```
    /// # use zerocopy::IntoBytes;
    /// # let header = u128::MAX;
    /// let mut insufficient_bytes = &mut [0, 0][..];
    ///
    /// let write_result = header.write_to_suffix(insufficient_bytes);
    ///
    /// assert!(write_result.is_err());
    /// assert_eq!(insufficient_bytes, [0, 0]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "write_to_suffix",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ]
    )]
    #[must_use = "callers should check the return value to see if the operation succeeded"]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    #[allow(clippy::mut_from_ref)] // False positive: `&self -> &mut [u8]`
    fn write_to_suffix(&self, dst: &mut [u8]) -> Result<(), SizeError<&Self, &mut [u8]>>
    where
        Self: Immutable,
    {
        let src = self.as_bytes();
        let start = if let Some(start) = dst.len().checked_sub(src.len()) {
            start
        } else {
            return Err(SizeError::new(self));
        };
        let dst = if let Some(dst) = dst.get_mut(start..) {
            dst
        } else {
            // get_mut() should never return None here. We return a `SizeError`
            // rather than .unwrap() because in the event the branch is not
            // optimized away, returning a value is generally lighter-weight
            // than panicking.
            return Err(SizeError::new(self));
        };
        // SAFETY: Through fallible subslicing of `dst`, we have ensured that
        // `dst.len()` is equal to `src.len()`. Neither the size of the source
        // nor the size of the destination change between the above subslicing
        // operation and the invocation of `copy_unchecked`.
        unsafe {
            util::copy_unchecked(src, dst);
        }
        Ok(())
    }

    /// Writes a copy of `self` to an `io::Write`.
    ///
    /// This is a shorthand for `dst.write_all(self.as_bytes())`, and is useful
    /// for interfacing with operating system byte sinks (files, sockets, etc.).
    ///
    /// # Examples
    ///
    /// ```no_run
    /// use zerocopy::{byteorder::big_endian::U16, FromBytes, IntoBytes};
    /// use std::fs::File;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, Immutable, KnownLayout)]
    /// #[repr(C, packed)]
    /// struct GrayscaleImage {
    ///     height: U16,
    ///     width: U16,
    ///     pixels: [U16],
    /// }
    ///
    /// let image = GrayscaleImage::ref_from_bytes(&[0, 0, 0, 0][..]).unwrap();
    /// let mut file = File::create("image.bin").unwrap();
    /// image.write_to_io(&mut file).unwrap();
    /// ```
    ///
    /// If the write fails, `write_to_io` returns `Err` and a partial write may
    /// have occurred; e.g.:
    ///
    /// ```
    /// # use zerocopy::IntoBytes;
    ///
    /// let src = u128::MAX;
    /// let mut dst = [0u8; 2];
    ///
    /// let write_result = src.write_to_io(&mut dst[..]);
    ///
    /// assert!(write_result.is_err());
    /// assert_eq!(dst, [255, 255]);
    /// ```
    #[cfg(feature = "std")]
    #[cfg_attr(doc_cfg, doc(cfg(feature = "std")))]
    #[inline(always)]
    fn write_to_io<W>(&self, mut dst: W) -> io::Result<()>
    where
        Self: Immutable,
        W: io::Write,
    {
        dst.write_all(self.as_bytes())
    }

    #[deprecated(since = "0.8.0", note = "`IntoBytes::as_bytes_mut` was renamed to `as_mut_bytes`")]
    #[doc(hidden)]
    #[inline]
    fn as_bytes_mut(&mut self) -> &mut [u8]
    where
        Self: FromBytes,
    {
        self.as_mut_bytes()
    }
}

/// Analyzes whether a type is [`Unaligned`].
///
/// This derive analyzes, at compile time, whether the annotated type satisfies
/// the [safety conditions] of `Unaligned` and implements `Unaligned` if it is
/// sound to do so. This derive can be applied to structs, enums, and unions;
/// e.g.:
///
/// ```
/// # use zerocopy_derive::Unaligned;
/// #[derive(Unaligned)]
/// #[repr(C)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Unaligned)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant0,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Unaligned)]
/// #[repr(packed)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// # Analysis
///
/// *This section describes, roughly, the analysis performed by this derive to
/// determine whether it is sound to implement `Unaligned` for a given type.
/// Unless you are modifying the implementation of this derive, or attempting to
/// manually implement `Unaligned` for a type yourself, you don't need to read
/// this section.*
///
/// If a type has the following properties, then this derive can implement
/// `Unaligned` for that type:
///
/// - If the type is a struct or union:
///   - If `repr(align(N))` is provided, `N` must equal 1.
///   - If the type is `repr(C)` or `repr(transparent)`, all fields must be
///     [`Unaligned`].
///   - If the type is not `repr(C)` or `repr(transparent)`, it must be
///     `repr(packed)` or `repr(packed(1))`.
/// - If the type is an enum:
///   - If `repr(align(N))` is provided, `N` must equal 1.
///   - It must be a field-less enum (meaning that all variants have no fields).
///   - It must be `repr(i8)` or `repr(u8)`.
///
/// [safety conditions]: trait@Unaligned#safety
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::Unaligned;

/// Types with no alignment requirement.
///
/// If `T: Unaligned`, then `align_of::<T>() == 1`.
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(Unaligned)]`][derive]; e.g.:
///
/// ```
/// # use zerocopy_derive::Unaligned;
/// #[derive(Unaligned)]
/// #[repr(C)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Unaligned)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant0,
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(Unaligned)]
/// #[repr(packed)]
/// union MyUnion {
/// #   variant: u8,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// This derive performs a sophisticated, compile-time safety analysis to
/// determine whether a type is `Unaligned`.
///
/// # Safety
///
/// *This section describes what is required in order for `T: Unaligned`, and
/// what unsafe code may assume of such types. If you don't plan on implementing
/// `Unaligned` manually, and you don't plan on writing unsafe code that
/// operates on `Unaligned` types, then you don't need to read this section.*
///
/// If `T: Unaligned`, then unsafe code may assume that it is sound to produce a
/// reference to `T` at any memory location regardless of alignment. If a type
/// is marked as `Unaligned` which violates this contract, it may cause
/// undefined behavior.
///
/// `#[derive(Unaligned)]` only permits [types which satisfy these
/// requirements][derive-analysis].
///
#[cfg_attr(
    feature = "derive",
    doc = "[derive]: zerocopy_derive::Unaligned",
    doc = "[derive-analysis]: zerocopy_derive::Unaligned#analysis"
)]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.Unaligned.html"),
    doc = concat!("[derive-analysis]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.Unaligned.html#analysis"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(Unaligned)]` to `{Self}`")
)]
pub unsafe trait Unaligned {
    // The `Self: Sized` bound makes it so that `Unaligned` is still object
    // safe.
    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;
}

/// Derives optimized [`PartialEq`] and [`Eq`] implementations.
///
/// This derive can be applied to structs and enums implementing both
/// [`Immutable`] and [`IntoBytes`]; e.g.:
///
/// ```
/// # use zerocopy_derive::{ByteEq, Immutable, IntoBytes};
/// #[derive(ByteEq, Immutable, IntoBytes)]
/// #[repr(C)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(ByteEq, Immutable, IntoBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// The standard library's [`derive(Eq, PartialEq)`][derive@PartialEq] computes
/// equality by individually comparing each field. Instead, the implementation
/// of [`PartialEq::eq`] emitted by `derive(ByteHash)` converts the entirety of
/// `self` and `other` to byte slices and compares those slices for equality.
/// This may have performance advantages.
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::ByteEq;
/// Derives an optimized [`Hash`] implementation.
///
/// This derive can be applied to structs and enums implementing both
/// [`Immutable`] and [`IntoBytes`]; e.g.:
///
/// ```
/// # use zerocopy_derive::{ByteHash, Immutable, IntoBytes};
/// #[derive(ByteHash, Immutable, IntoBytes)]
/// #[repr(C)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
///
/// #[derive(ByteHash, Immutable, IntoBytes)]
/// #[repr(u8)]
/// enum MyEnum {
/// #   Variant,
/// # /*
///     ...
/// # */
/// }
/// ```
///
/// The standard library's [`derive(Hash)`][derive@Hash] produces hashes by
/// individually hashing each field and combining the results. Instead, the
/// implementations of [`Hash::hash()`] and [`Hash::hash_slice()`] generated by
/// `derive(ByteHash)` convert the entirety of `self` to a byte slice and hashes
/// it in a single call to [`Hasher::write()`]. This may have performance
/// advantages.
///
/// [`Hash`]: core::hash::Hash
/// [`Hash::hash()`]: core::hash::Hash::hash()
/// [`Hash::hash_slice()`]: core::hash::Hash::hash_slice()
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::ByteHash;
/// Implements [`SplitAt`].
///
/// This derive can be applied to structs; e.g.:
///
/// ```
/// # use zerocopy_derive::{ByteEq, Immutable, IntoBytes};
/// #[derive(ByteEq, Immutable, IntoBytes)]
/// #[repr(C)]
/// struct MyStruct {
/// # /*
///     ...
/// # */
/// }
/// ```
#[cfg(any(feature = "derive", test))]
#[cfg_attr(doc_cfg, doc(cfg(feature = "derive")))]
pub use zerocopy_derive::SplitAt;

#[cfg(feature = "alloc")]
#[cfg_attr(doc_cfg, doc(cfg(feature = "alloc")))]
#[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
mod alloc_support {
    use super::*;

    /// Extends a `Vec<T>` by pushing `additional` new items onto the end of the
    /// vector. The new items are initialized with zeros.
    #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
    #[doc(hidden)]
    #[deprecated(since = "0.8.0", note = "moved to `FromZeros`")]
    #[inline(always)]
    pub fn extend_vec_zeroed<T: FromZeros>(
        v: &mut Vec<T>,
        additional: usize,
    ) -> Result<(), AllocError> {
        <T as FromZeros>::extend_vec_zeroed(v, additional)
    }

    /// Inserts `additional` new items into `Vec<T>` at `position`. The new
    /// items are initialized with zeros.
    ///
    /// # Panics
    ///
    /// Panics if `position > v.len()`.
    #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
    #[doc(hidden)]
    #[deprecated(since = "0.8.0", note = "moved to `FromZeros`")]
    #[inline(always)]
    pub fn insert_vec_zeroed<T: FromZeros>(
        v: &mut Vec<T>,
        position: usize,
        additional: usize,
    ) -> Result<(), AllocError> {
        <T as FromZeros>::insert_vec_zeroed(v, position, additional)
    }
}

#[cfg(feature = "alloc")]
#[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
#[doc(hidden)]
pub use alloc_support::*;

#[cfg(test)]
#[allow(clippy::assertions_on_result_states, clippy::unreadable_literal)]
mod tests {
    use static_assertions::assert_impl_all;

    use super::*;
    use crate::util::testutil::*;

    // An unsized type.
    //
    // This is used to test the custom derives of our traits. The `[u8]` type
    // gets a hand-rolled impl, so it doesn't exercise our custom derives.
    #[derive(Debug, Eq, PartialEq, FromBytes, IntoBytes, Unaligned, Immutable)]
    #[repr(transparent)]
    struct Unsized([u8]);

    impl Unsized {
        fn from_mut_slice(slc: &mut [u8]) -> &mut Unsized {
            // SAFETY: This *probably* sound - since the layouts of `[u8]` and
            // `Unsized` are the same, so are the layouts of `&mut [u8]` and
            // `&mut Unsized`. [1] Even if it turns out that this isn't actually
            // guaranteed by the language spec, we can just change this since
            // it's in test code.
            //
            // [1] https://github.com/rust-lang/unsafe-code-guidelines/issues/375
            unsafe { mem::transmute(slc) }
        }
    }

    #[test]
    fn test_known_layout() {
        // Test that `$ty` and `ManuallyDrop<$ty>` have the expected layout.
        // Test that `PhantomData<$ty>` has the same layout as `()` regardless
        // of `$ty`.
        macro_rules! test {
            ($ty:ty, $expect:expr) => {
                let expect = $expect;
                assert_eq!(<$ty as KnownLayout>::LAYOUT, expect);
                assert_eq!(<ManuallyDrop<$ty> as KnownLayout>::LAYOUT, expect);
                assert_eq!(<PhantomData<$ty> as KnownLayout>::LAYOUT, <() as KnownLayout>::LAYOUT);
            };
        }

        let layout =
            |offset, align, trailing_slice_elem_size, statically_shallow_unpadded| DstLayout {
                align: NonZeroUsize::new(align).unwrap(),
                size_info: match trailing_slice_elem_size {
                    None => SizeInfo::Sized { size: offset },
                    Some(elem_size) => {
                        SizeInfo::SliceDst(TrailingSliceLayout { offset, elem_size })
                    }
                },
                statically_shallow_unpadded,
            };

        test!((), layout(0, 1, None, false));
        test!(u8, layout(1, 1, None, false));
        // Use `align_of` because `u64` alignment may be smaller than 8 on some
        // platforms.
        test!(u64, layout(8, mem::align_of::<u64>(), None, false));
        test!(AU64, layout(8, 8, None, false));

        test!(Option<&'static ()>, usize::LAYOUT);

        test!([()], layout(0, 1, Some(0), true));
        test!([u8], layout(0, 1, Some(1), true));
        test!(str, layout(0, 1, Some(1), true));
    }

    #[cfg(feature = "derive")]
    #[test]
    fn test_known_layout_derive() {
        // In this and other files (`late_compile_pass.rs`,
        // `mid_compile_pass.rs`, and `struct.rs`), we test success and failure
        // modes of `derive(KnownLayout)` for the following combination of
        // properties:
        //
        // +------------+--------------------------------------+-----------+
        // |            |      trailing field properties       |           |
        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |------------+----------+----------------+----------+-----------|
        // |          N |        N |              N |        N |      KL00 |
        // |          N |        N |              N |        Y |      KL01 |
        // |          N |        N |              Y |        N |      KL02 |
        // |          N |        N |              Y |        Y |      KL03 |
        // |          N |        Y |              N |        N |      KL04 |
        // |          N |        Y |              N |        Y |      KL05 |
        // |          N |        Y |              Y |        N |      KL06 |
        // |          N |        Y |              Y |        Y |      KL07 |
        // |          Y |        N |              N |        N |      KL08 |
        // |          Y |        N |              N |        Y |      KL09 |
        // |          Y |        N |              Y |        N |      KL10 |
        // |          Y |        N |              Y |        Y |      KL11 |
        // |          Y |        Y |              N |        N |      KL12 |
        // |          Y |        Y |              N |        Y |      KL13 |
        // |          Y |        Y |              Y |        N |      KL14 |
        // |          Y |        Y |              Y |        Y |      KL15 |
        // +------------+----------+----------------+----------+-----------+

        struct NotKnownLayout<T = ()> {
            _t: T,
        }

        #[derive(KnownLayout)]
        #[repr(C)]
        struct AlignSize<const ALIGN: usize, const SIZE: usize>
        where
            elain::Align<ALIGN>: elain::Alignment,
        {
            _align: elain::Align<ALIGN>,
            size: [u8; SIZE],
        }

        type AU16 = AlignSize<2, 2>;
        type AU32 = AlignSize<4, 4>;

        fn _assert_kl<T: ?Sized + KnownLayout>(_: &T) {}

        let sized_layout = |align, size| DstLayout {
            align: NonZeroUsize::new(align).unwrap(),
            size_info: SizeInfo::Sized { size },
            statically_shallow_unpadded: false,
        };

        let unsized_layout = |align, elem_size, offset, statically_shallow_unpadded| DstLayout {
            align: NonZeroUsize::new(align).unwrap(),
            size_info: SizeInfo::SliceDst(TrailingSliceLayout { offset, elem_size }),
            statically_shallow_unpadded,
        };

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          N |        N |              N |        Y |      KL01 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        struct KL01(NotKnownLayout<AU32>, NotKnownLayout<AU16>);

        let expected = DstLayout::for_type::<KL01>();

        assert_eq!(<KL01 as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL01 as KnownLayout>::LAYOUT, sized_layout(4, 8));

        // ...with `align(N)`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(align(64))]
        struct KL01Align(NotKnownLayout<AU32>, NotKnownLayout<AU16>);

        let expected = DstLayout::for_type::<KL01Align>();

        assert_eq!(<KL01Align as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL01Align as KnownLayout>::LAYOUT, sized_layout(64, 64));

        // ...with `packed`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(packed)]
        struct KL01Packed(NotKnownLayout<AU32>, NotKnownLayout<AU16>);

        let expected = DstLayout::for_type::<KL01Packed>();

        assert_eq!(<KL01Packed as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL01Packed as KnownLayout>::LAYOUT, sized_layout(1, 6));

        // ...with `packed(N)`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(packed(2))]
        struct KL01PackedN(NotKnownLayout<AU32>, NotKnownLayout<AU16>);

        assert_impl_all!(KL01PackedN: KnownLayout);

        let expected = DstLayout::for_type::<KL01PackedN>();

        assert_eq!(<KL01PackedN as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL01PackedN as KnownLayout>::LAYOUT, sized_layout(2, 6));

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          N |        N |              Y |        Y |      KL03 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        struct KL03(NotKnownLayout, u8);

        let expected = DstLayout::for_type::<KL03>();

        assert_eq!(<KL03 as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL03 as KnownLayout>::LAYOUT, sized_layout(1, 1));

        // ... with `align(N)`
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(align(64))]
        struct KL03Align(NotKnownLayout<AU32>, u8);

        let expected = DstLayout::for_type::<KL03Align>();

        assert_eq!(<KL03Align as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL03Align as KnownLayout>::LAYOUT, sized_layout(64, 64));

        // ... with `packed`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(packed)]
        struct KL03Packed(NotKnownLayout<AU32>, u8);

        let expected = DstLayout::for_type::<KL03Packed>();

        assert_eq!(<KL03Packed as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL03Packed as KnownLayout>::LAYOUT, sized_layout(1, 5));

        // ... with `packed(N)`
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(packed(2))]
        struct KL03PackedN(NotKnownLayout<AU32>, u8);

        assert_impl_all!(KL03PackedN: KnownLayout);

        let expected = DstLayout::for_type::<KL03PackedN>();

        assert_eq!(<KL03PackedN as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL03PackedN as KnownLayout>::LAYOUT, sized_layout(2, 6));

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          N |        Y |              N |        Y |      KL05 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        struct KL05<T>(u8, T);

        fn _test_kl05<T>(t: T) -> impl KnownLayout {
            KL05(0u8, t)
        }

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          N |        Y |              Y |        Y |      KL07 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        struct KL07<T: KnownLayout>(u8, T);

        fn _test_kl07<T: KnownLayout>(t: T) -> impl KnownLayout {
            let _ = KL07(0u8, t);
        }

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          Y |        N |              Y |        N |      KL10 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C)]
        struct KL10(NotKnownLayout<AU32>, [u8]);

        let expected = DstLayout::new_zst(None)
            .extend(DstLayout::for_type::<NotKnownLayout<AU32>>(), None)
            .extend(<[u8] as KnownLayout>::LAYOUT, None)
            .pad_to_align();

        assert_eq!(<KL10 as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL10 as KnownLayout>::LAYOUT, unsized_layout(4, 1, 4, false));

        // ...with `align(N)`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C, align(64))]
        struct KL10Align(NotKnownLayout<AU32>, [u8]);

        let repr_align = NonZeroUsize::new(64);

        let expected = DstLayout::new_zst(repr_align)
            .extend(DstLayout::for_type::<NotKnownLayout<AU32>>(), None)
            .extend(<[u8] as KnownLayout>::LAYOUT, None)
            .pad_to_align();

        assert_eq!(<KL10Align as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL10Align as KnownLayout>::LAYOUT, unsized_layout(64, 1, 4, false));

        // ...with `packed`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C, packed)]
        struct KL10Packed(NotKnownLayout<AU32>, [u8]);

        let repr_packed = NonZeroUsize::new(1);

        let expected = DstLayout::new_zst(None)
            .extend(DstLayout::for_type::<NotKnownLayout<AU32>>(), repr_packed)
            .extend(<[u8] as KnownLayout>::LAYOUT, repr_packed)
            .pad_to_align();

        assert_eq!(<KL10Packed as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL10Packed as KnownLayout>::LAYOUT, unsized_layout(1, 1, 4, false));

        // ...with `packed(N)`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C, packed(2))]
        struct KL10PackedN(NotKnownLayout<AU32>, [u8]);

        let repr_packed = NonZeroUsize::new(2);

        let expected = DstLayout::new_zst(None)
            .extend(DstLayout::for_type::<NotKnownLayout<AU32>>(), repr_packed)
            .extend(<[u8] as KnownLayout>::LAYOUT, repr_packed)
            .pad_to_align();

        assert_eq!(<KL10PackedN as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL10PackedN as KnownLayout>::LAYOUT, unsized_layout(2, 1, 4, false));

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          Y |        N |              Y |        Y |      KL11 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C)]
        struct KL11(NotKnownLayout<AU64>, u8);

        let expected = DstLayout::new_zst(None)
            .extend(DstLayout::for_type::<NotKnownLayout<AU64>>(), None)
            .extend(<u8 as KnownLayout>::LAYOUT, None)
            .pad_to_align();

        assert_eq!(<KL11 as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL11 as KnownLayout>::LAYOUT, sized_layout(8, 16));

        // ...with `align(N)`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C, align(64))]
        struct KL11Align(NotKnownLayout<AU64>, u8);

        let repr_align = NonZeroUsize::new(64);

        let expected = DstLayout::new_zst(repr_align)
            .extend(DstLayout::for_type::<NotKnownLayout<AU64>>(), None)
            .extend(<u8 as KnownLayout>::LAYOUT, None)
            .pad_to_align();

        assert_eq!(<KL11Align as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL11Align as KnownLayout>::LAYOUT, sized_layout(64, 64));

        // ...with `packed`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C, packed)]
        struct KL11Packed(NotKnownLayout<AU64>, u8);

        let repr_packed = NonZeroUsize::new(1);

        let expected = DstLayout::new_zst(None)
            .extend(DstLayout::for_type::<NotKnownLayout<AU64>>(), repr_packed)
            .extend(<u8 as KnownLayout>::LAYOUT, repr_packed)
            .pad_to_align();

        assert_eq!(<KL11Packed as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL11Packed as KnownLayout>::LAYOUT, sized_layout(1, 9));

        // ...with `packed(N)`:
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C, packed(2))]
        struct KL11PackedN(NotKnownLayout<AU64>, u8);

        let repr_packed = NonZeroUsize::new(2);

        let expected = DstLayout::new_zst(None)
            .extend(DstLayout::for_type::<NotKnownLayout<AU64>>(), repr_packed)
            .extend(<u8 as KnownLayout>::LAYOUT, repr_packed)
            .pad_to_align();

        assert_eq!(<KL11PackedN as KnownLayout>::LAYOUT, expected);
        assert_eq!(<KL11PackedN as KnownLayout>::LAYOUT, sized_layout(2, 10));

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          Y |        Y |              Y |        N |      KL14 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C)]
        struct KL14<T: ?Sized + KnownLayout>(u8, T);

        fn _test_kl14<T: ?Sized + KnownLayout>(kl: &KL14<T>) {
            _assert_kl(kl)
        }

        // | `repr(C)`? | generic? | `KnownLayout`? | `Sized`? | Type Name |
        // |          Y |        Y |              Y |        Y |      KL15 |
        #[allow(dead_code)]
        #[derive(KnownLayout)]
        #[repr(C)]
        struct KL15<T: KnownLayout>(u8, T);

        fn _test_kl15<T: KnownLayout>(t: T) -> impl KnownLayout {
            let _ = KL15(0u8, t);
        }

        // Test a variety of combinations of field types:
        //  - ()
        //  - u8
        //  - AU16
        //  - [()]
        //  - [u8]
        //  - [AU16]

        #[allow(clippy::upper_case_acronyms, dead_code)]
        #[derive(KnownLayout)]
        #[repr(C)]
        struct KLTU<T, U: ?Sized>(T, U);

        assert_eq!(<KLTU<(), ()> as KnownLayout>::LAYOUT, sized_layout(1, 0));

        assert_eq!(<KLTU<(), u8> as KnownLayout>::LAYOUT, sized_layout(1, 1));

        assert_eq!(<KLTU<(), AU16> as KnownLayout>::LAYOUT, sized_layout(2, 2));

        assert_eq!(<KLTU<(), [()]> as KnownLayout>::LAYOUT, unsized_layout(1, 0, 0, false));

        assert_eq!(<KLTU<(), [u8]> as KnownLayout>::LAYOUT, unsized_layout(1, 1, 0, false));

        assert_eq!(<KLTU<(), [AU16]> as KnownLayout>::LAYOUT, unsized_layout(2, 2, 0, false));

        assert_eq!(<KLTU<u8, ()> as KnownLayout>::LAYOUT, sized_layout(1, 1));

        assert_eq!(<KLTU<u8, u8> as KnownLayout>::LAYOUT, sized_layout(1, 2));

        assert_eq!(<KLTU<u8, AU16> as KnownLayout>::LAYOUT, sized_layout(2, 4));

        assert_eq!(<KLTU<u8, [()]> as KnownLayout>::LAYOUT, unsized_layout(1, 0, 1, false));

        assert_eq!(<KLTU<u8, [u8]> as KnownLayout>::LAYOUT, unsized_layout(1, 1, 1, false));

        assert_eq!(<KLTU<u8, [AU16]> as KnownLayout>::LAYOUT, unsized_layout(2, 2, 2, false));

        assert_eq!(<KLTU<AU16, ()> as KnownLayout>::LAYOUT, sized_layout(2, 2));

        assert_eq!(<KLTU<AU16, u8> as KnownLayout>::LAYOUT, sized_layout(2, 4));

        assert_eq!(<KLTU<AU16, AU16> as KnownLayout>::LAYOUT, sized_layout(2, 4));

        assert_eq!(<KLTU<AU16, [()]> as KnownLayout>::LAYOUT, unsized_layout(2, 0, 2, false));

        assert_eq!(<KLTU<AU16, [u8]> as KnownLayout>::LAYOUT, unsized_layout(2, 1, 2, false));

        assert_eq!(<KLTU<AU16, [AU16]> as KnownLayout>::LAYOUT, unsized_layout(2, 2, 2, false));

        // Test a variety of field counts.

        #[derive(KnownLayout)]
        #[repr(C)]
        struct KLF0;

        assert_eq!(<KLF0 as KnownLayout>::LAYOUT, sized_layout(1, 0));

        #[derive(KnownLayout)]
        #[repr(C)]
        struct KLF1([u8]);

        assert_eq!(<KLF1 as KnownLayout>::LAYOUT, unsized_layout(1, 1, 0, true));

        #[derive(KnownLayout)]
        #[repr(C)]
        struct KLF2(NotKnownLayout<u8>, [u8]);

        assert_eq!(<KLF2 as KnownLayout>::LAYOUT, unsized_layout(1, 1, 1, false));

        #[derive(KnownLayout)]
        #[repr(C)]
        struct KLF3(NotKnownLayout<u8>, NotKnownLayout<AU16>, [u8]);

        assert_eq!(<KLF3 as KnownLayout>::LAYOUT, unsized_layout(2, 1, 4, false));

        #[derive(KnownLayout)]
        #[repr(C)]
        struct KLF4(NotKnownLayout<u8>, NotKnownLayout<AU16>, NotKnownLayout<AU32>, [u8]);

        assert_eq!(<KLF4 as KnownLayout>::LAYOUT, unsized_layout(4, 1, 8, false));
    }

    #[test]
    fn test_object_safety() {
        fn _takes_immutable(_: &dyn Immutable) {}
        fn _takes_unaligned(_: &dyn Unaligned) {}
    }

    #[test]
    fn test_from_zeros_only() {
        // Test types that implement `FromZeros` but not `FromBytes`.

        assert!(!bool::new_zeroed());
        assert_eq!(char::new_zeroed(), '\0');

        #[cfg(feature = "alloc")]
        {
            assert_eq!(bool::new_box_zeroed(), Ok(Box::new(false)));
            assert_eq!(char::new_box_zeroed(), Ok(Box::new('\0')));

            assert_eq!(
                <[bool]>::new_box_zeroed_with_elems(3).unwrap().as_ref(),
                [false, false, false]
            );
            assert_eq!(
                <[char]>::new_box_zeroed_with_elems(3).unwrap().as_ref(),
                ['\0', '\0', '\0']
            );

            assert_eq!(bool::new_vec_zeroed(3).unwrap().as_ref(), [false, false, false]);
            assert_eq!(char::new_vec_zeroed(3).unwrap().as_ref(), ['\0', '\0', '\0']);
        }

        let mut string = "hello".to_string();
        let s: &mut str = string.as_mut();
        assert_eq!(s, "hello");
        s.zero();
        assert_eq!(s, "\0\0\0\0\0");
    }

    #[test]
    fn test_zst_count_preserved() {
        // Test that, when an explicit count is provided to for a type with a
        // ZST trailing slice element, that count is preserved. This is
        // important since, for such types, all element counts result in objects
        // of the same size, and so the correct behavior is ambiguous. However,
        // preserving the count as requested by the user is the behavior that we
        // document publicly.

        // FromZeros methods
        #[cfg(feature = "alloc")]
        assert_eq!(<[()]>::new_box_zeroed_with_elems(3).unwrap().len(), 3);
        #[cfg(feature = "alloc")]
        assert_eq!(<()>::new_vec_zeroed(3).unwrap().len(), 3);

        // FromBytes methods
        assert_eq!(<[()]>::ref_from_bytes_with_elems(&[][..], 3).unwrap().len(), 3);
        assert_eq!(<[()]>::ref_from_prefix_with_elems(&[][..], 3).unwrap().0.len(), 3);
        assert_eq!(<[()]>::ref_from_suffix_with_elems(&[][..], 3).unwrap().1.len(), 3);
        assert_eq!(<[()]>::mut_from_bytes_with_elems(&mut [][..], 3).unwrap().len(), 3);
        assert_eq!(<[()]>::mut_from_prefix_with_elems(&mut [][..], 3).unwrap().0.len(), 3);
        assert_eq!(<[()]>::mut_from_suffix_with_elems(&mut [][..], 3).unwrap().1.len(), 3);
    }

    #[test]
    fn test_read_write() {
        const VAL: u64 = 0x12345678;
        #[cfg(target_endian = "big")]
        const VAL_BYTES: [u8; 8] = VAL.to_be_bytes();
        #[cfg(target_endian = "little")]
        const VAL_BYTES: [u8; 8] = VAL.to_le_bytes();
        const ZEROS: [u8; 8] = [0u8; 8];

        // Test `FromBytes::{read_from, read_from_prefix, read_from_suffix}`.

        assert_eq!(u64::read_from_bytes(&VAL_BYTES[..]), Ok(VAL));
        // The first 8 bytes are from `VAL_BYTES` and the second 8 bytes are all
        // zeros.
        let bytes_with_prefix: [u8; 16] = transmute!([VAL_BYTES, [0; 8]]);
        assert_eq!(u64::read_from_prefix(&bytes_with_prefix[..]), Ok((VAL, &ZEROS[..])));
        assert_eq!(u64::read_from_suffix(&bytes_with_prefix[..]), Ok((&VAL_BYTES[..], 0)));
        // The first 8 bytes are all zeros and the second 8 bytes are from
        // `VAL_BYTES`
        let bytes_with_suffix: [u8; 16] = transmute!([[0; 8], VAL_BYTES]);
        assert_eq!(u64::read_from_prefix(&bytes_with_suffix[..]), Ok((0, &VAL_BYTES[..])));
        assert_eq!(u64::read_from_suffix(&bytes_with_suffix[..]), Ok((&ZEROS[..], VAL)));

        // Test `IntoBytes::{write_to, write_to_prefix, write_to_suffix}`.

        let mut bytes = [0u8; 8];
        assert_eq!(VAL.write_to(&mut bytes[..]), Ok(()));
        assert_eq!(bytes, VAL_BYTES);
        let mut bytes = [0u8; 16];
        assert_eq!(VAL.write_to_prefix(&mut bytes[..]), Ok(()));
        let want: [u8; 16] = transmute!([VAL_BYTES, [0; 8]]);
        assert_eq!(bytes, want);
        let mut bytes = [0u8; 16];
        assert_eq!(VAL.write_to_suffix(&mut bytes[..]), Ok(()));
        let want: [u8; 16] = transmute!([[0; 8], VAL_BYTES]);
        assert_eq!(bytes, want);
    }

    #[test]
    #[cfg(feature = "std")]
    fn test_read_io_with_padding_soundness() {
        // This test is designed to exhibit potential UB in
        // `FromBytes::read_from_io`. (see #2319, #2320).

        // On most platforms (where `align_of::<u16>() == 2`), `WithPadding`
        // will have inter-field padding between `x` and `y`.
        #[derive(FromBytes)]
        #[repr(C)]
        struct WithPadding {
            x: u8,
            y: u16,
        }
        struct ReadsInRead;
        impl std::io::Read for ReadsInRead {
            fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
                // This body branches on every byte of `buf`, ensuring that it
                // exhibits UB if any byte of `buf` is uninitialized.
                if buf.iter().all(|&x| x == 0) {
                    Ok(buf.len())
                } else {
                    buf.iter_mut().for_each(|x| *x = 0);
                    Ok(buf.len())
                }
            }
        }
        assert!(matches!(WithPadding::read_from_io(ReadsInRead), Ok(WithPadding { x: 0, y: 0 })));
    }

    #[test]
    #[cfg(feature = "std")]
    fn test_read_write_io() {
        let mut long_buffer = [0, 0, 0, 0];
        assert!(matches!(u16::MAX.write_to_io(&mut long_buffer[..]), Ok(())));
        assert_eq!(long_buffer, [255, 255, 0, 0]);
        assert!(matches!(u16::read_from_io(&long_buffer[..]), Ok(u16::MAX)));

        let mut short_buffer = [0, 0];
        assert!(u32::MAX.write_to_io(&mut short_buffer[..]).is_err());
        assert_eq!(short_buffer, [255, 255]);
        assert!(u32::read_from_io(&short_buffer[..]).is_err());
    }

    #[test]
    fn test_try_from_bytes_try_read_from() {
        assert_eq!(<bool as TryFromBytes>::try_read_from_bytes(&[0]), Ok(false));
        assert_eq!(<bool as TryFromBytes>::try_read_from_bytes(&[1]), Ok(true));

        assert_eq!(<bool as TryFromBytes>::try_read_from_prefix(&[0, 2]), Ok((false, &[2][..])));
        assert_eq!(<bool as TryFromBytes>::try_read_from_prefix(&[1, 2]), Ok((true, &[2][..])));

        assert_eq!(<bool as TryFromBytes>::try_read_from_suffix(&[2, 0]), Ok((&[2][..], false)));
        assert_eq!(<bool as TryFromBytes>::try_read_from_suffix(&[2, 1]), Ok((&[2][..], true)));

        // If we don't pass enough bytes, it fails.
        assert!(matches!(
            <u8 as TryFromBytes>::try_read_from_bytes(&[]),
            Err(TryReadError::Size(_))
        ));
        assert!(matches!(
            <u8 as TryFromBytes>::try_read_from_prefix(&[]),
            Err(TryReadError::Size(_))
        ));
        assert!(matches!(
            <u8 as TryFromBytes>::try_read_from_suffix(&[]),
            Err(TryReadError::Size(_))
        ));

        // If we pass too many bytes, it fails.
        assert!(matches!(
            <u8 as TryFromBytes>::try_read_from_bytes(&[0, 0]),
            Err(TryReadError::Size(_))
        ));

        // If we pass an invalid value, it fails.
        assert!(matches!(
            <bool as TryFromBytes>::try_read_from_bytes(&[2]),
            Err(TryReadError::Validity(_))
        ));
        assert!(matches!(
            <bool as TryFromBytes>::try_read_from_prefix(&[2, 0]),
            Err(TryReadError::Validity(_))
        ));
        assert!(matches!(
            <bool as TryFromBytes>::try_read_from_suffix(&[0, 2]),
            Err(TryReadError::Validity(_))
        ));

        // Reading from a misaligned buffer should still succeed. Since `AU64`'s
        // alignment is 8, and since we read from two adjacent addresses one
        // byte apart, it is guaranteed that at least one of them (though
        // possibly both) will be misaligned.
        let bytes: [u8; 9] = [0, 0, 0, 0, 0, 0, 0, 0, 0];
        assert_eq!(<AU64 as TryFromBytes>::try_read_from_bytes(&bytes[..8]), Ok(AU64(0)));
        assert_eq!(<AU64 as TryFromBytes>::try_read_from_bytes(&bytes[1..9]), Ok(AU64(0)));

        assert_eq!(
            <AU64 as TryFromBytes>::try_read_from_prefix(&bytes[..8]),
            Ok((AU64(0), &[][..]))
        );
        assert_eq!(
            <AU64 as TryFromBytes>::try_read_from_prefix(&bytes[1..9]),
            Ok((AU64(0), &[][..]))
        );

        assert_eq!(
            <AU64 as TryFromBytes>::try_read_from_suffix(&bytes[..8]),
            Ok((&[][..], AU64(0)))
        );
        assert_eq!(
            <AU64 as TryFromBytes>::try_read_from_suffix(&bytes[1..9]),
            Ok((&[][..], AU64(0)))
        );
    }

    #[test]
    fn test_ref_from_mut_from_bytes() {
        // Test `FromBytes::{ref_from_bytes, mut_from_bytes}{,_prefix,Suffix}`
        // success cases. Exhaustive coverage for these methods is covered by
        // the `Ref` tests above, which these helper methods defer to.

        let mut buf =
            Align::<[u8; 16], AU64>::new([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);

        assert_eq!(
            AU64::ref_from_bytes(&buf.t[8..]).unwrap().0.to_ne_bytes(),
            [8, 9, 10, 11, 12, 13, 14, 15]
        );
        let suffix = AU64::mut_from_bytes(&mut buf.t[8..]).unwrap();
        suffix.0 = 0x0101010101010101;
        // The `[u8:9]` is a non-half size of the full buffer, which would catch
        // `from_prefix` having the same implementation as `from_suffix` (issues #506, #511).
        assert_eq!(
            <[u8; 9]>::ref_from_suffix(&buf.t[..]).unwrap(),
            (&[0, 1, 2, 3, 4, 5, 6][..], &[7u8, 1, 1, 1, 1, 1, 1, 1, 1])
        );
        let (prefix, suffix) = AU64::mut_from_suffix(&mut buf.t[1..]).unwrap();
        assert_eq!(prefix, &mut [1u8, 2, 3, 4, 5, 6, 7][..]);
        suffix.0 = 0x0202020202020202;
        let (prefix, suffix) = <[u8; 10]>::mut_from_suffix(&mut buf.t[..]).unwrap();
        assert_eq!(prefix, &mut [0u8, 1, 2, 3, 4, 5][..]);
        suffix[0] = 42;
        assert_eq!(
            <[u8; 9]>::ref_from_prefix(&buf.t[..]).unwrap(),
            (&[0u8, 1, 2, 3, 4, 5, 42, 7, 2], &[2u8, 2, 2, 2, 2, 2, 2][..])
        );
        <[u8; 2]>::mut_from_prefix(&mut buf.t[..]).unwrap().0[1] = 30;
        assert_eq!(buf.t, [0, 30, 2, 3, 4, 5, 42, 7, 2, 2, 2, 2, 2, 2, 2, 2]);
    }

    #[test]
    fn test_ref_from_mut_from_bytes_error() {
        // Test `FromBytes::{ref_from_bytes, mut_from_bytes}{,_prefix,Suffix}`
        // error cases.

        // Fail because the buffer is too large.
        let mut buf = Align::<[u8; 16], AU64>::default();
        // `buf.t` should be aligned to 8, so only the length check should fail.
        assert!(AU64::ref_from_bytes(&buf.t[..]).is_err());
        assert!(AU64::mut_from_bytes(&mut buf.t[..]).is_err());
        assert!(<[u8; 8]>::ref_from_bytes(&buf.t[..]).is_err());
        assert!(<[u8; 8]>::mut_from_bytes(&mut buf.t[..]).is_err());

        // Fail because the buffer is too small.
        let mut buf = Align::<[u8; 4], AU64>::default();
        assert!(AU64::ref_from_bytes(&buf.t[..]).is_err());
        assert!(AU64::mut_from_bytes(&mut buf.t[..]).is_err());
        assert!(<[u8; 8]>::ref_from_bytes(&buf.t[..]).is_err());
        assert!(<[u8; 8]>::mut_from_bytes(&mut buf.t[..]).is_err());
        assert!(AU64::ref_from_prefix(&buf.t[..]).is_err());
        assert!(AU64::mut_from_prefix(&mut buf.t[..]).is_err());
        assert!(AU64::ref_from_suffix(&buf.t[..]).is_err());
        assert!(AU64::mut_from_suffix(&mut buf.t[..]).is_err());
        assert!(<[u8; 8]>::ref_from_prefix(&buf.t[..]).is_err());
        assert!(<[u8; 8]>::mut_from_prefix(&mut buf.t[..]).is_err());
        assert!(<[u8; 8]>::ref_from_suffix(&buf.t[..]).is_err());
        assert!(<[u8; 8]>::mut_from_suffix(&mut buf.t[..]).is_err());

        // Fail because the alignment is insufficient.
        let mut buf = Align::<[u8; 13], AU64>::default();
        assert!(AU64::ref_from_bytes(&buf.t[1..]).is_err());
        assert!(AU64::mut_from_bytes(&mut buf.t[1..]).is_err());
        assert!(AU64::ref_from_bytes(&buf.t[1..]).is_err());
        assert!(AU64::mut_from_bytes(&mut buf.t[1..]).is_err());
        assert!(AU64::ref_from_prefix(&buf.t[1..]).is_err());
        assert!(AU64::mut_from_prefix(&mut buf.t[1..]).is_err());
        assert!(AU64::ref_from_suffix(&buf.t[..]).is_err());
        assert!(AU64::mut_from_suffix(&mut buf.t[..]).is_err());
    }

    #[test]
    fn test_to_methods() {
        /// Run a series of tests by calling `IntoBytes` methods on `t`.
        ///
        /// `bytes` is the expected byte sequence returned from `t.as_bytes()`
        /// before `t` has been modified. `post_mutation` is the expected
        /// sequence returned from `t.as_bytes()` after `t.as_mut_bytes()[0]`
        /// has had its bits flipped (by applying `^= 0xFF`).
        ///
        /// `N` is the size of `t` in bytes.
        fn test<T: FromBytes + IntoBytes + Immutable + Debug + Eq + ?Sized, const N: usize>(
            t: &mut T,
            bytes: &[u8],
            post_mutation: &T,
        ) {
            // Test that we can access the underlying bytes, and that we get the
            // right bytes and the right number of bytes.
            assert_eq!(t.as_bytes(), bytes);

            // Test that changes to the underlying byte slices are reflected in
            // the original object.
            t.as_mut_bytes()[0] ^= 0xFF;
            assert_eq!(t, post_mutation);
            t.as_mut_bytes()[0] ^= 0xFF;

            // `write_to` rejects slices that are too small or too large.
            assert!(t.write_to(&mut vec![0; N - 1][..]).is_err());
            assert!(t.write_to(&mut vec![0; N + 1][..]).is_err());

            // `write_to` works as expected.
            let mut bytes = [0; N];
            assert_eq!(t.write_to(&mut bytes[..]), Ok(()));
            assert_eq!(bytes, t.as_bytes());

            // `write_to_prefix` rejects slices that are too small.
            assert!(t.write_to_prefix(&mut vec![0; N - 1][..]).is_err());

            // `write_to_prefix` works with exact-sized slices.
            let mut bytes = [0; N];
            assert_eq!(t.write_to_prefix(&mut bytes[..]), Ok(()));
            assert_eq!(bytes, t.as_bytes());

            // `write_to_prefix` works with too-large slices, and any bytes past
            // the prefix aren't modified.
            let mut too_many_bytes = vec![0; N + 1];
            too_many_bytes[N] = 123;
            assert_eq!(t.write_to_prefix(&mut too_many_bytes[..]), Ok(()));
            assert_eq!(&too_many_bytes[..N], t.as_bytes());
            assert_eq!(too_many_bytes[N], 123);

            // `write_to_suffix` rejects slices that are too small.
            assert!(t.write_to_suffix(&mut vec![0; N - 1][..]).is_err());

            // `write_to_suffix` works with exact-sized slices.
            let mut bytes = [0; N];
            assert_eq!(t.write_to_suffix(&mut bytes[..]), Ok(()));
            assert_eq!(bytes, t.as_bytes());

            // `write_to_suffix` works with too-large slices, and any bytes
            // before the suffix aren't modified.
            let mut too_many_bytes = vec![0; N + 1];
            too_many_bytes[0] = 123;
            assert_eq!(t.write_to_suffix(&mut too_many_bytes[..]), Ok(()));
            assert_eq!(&too_many_bytes[1..], t.as_bytes());
            assert_eq!(too_many_bytes[0], 123);
        }

        #[derive(Debug, Eq, PartialEq, FromBytes, IntoBytes, Immutable)]
        #[repr(C)]
        struct Foo {
            a: u32,
            b: Wrapping<u32>,
            c: Option<NonZeroU32>,
        }

        let expected_bytes: Vec<u8> = if cfg!(target_endian = "little") {
            vec![1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0]
        } else {
            vec![0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0]
        };
        let post_mutation_expected_a =
            if cfg!(target_endian = "little") { 0x00_00_00_FE } else { 0xFF_00_00_01 };
        test::<_, 12>(
            &mut Foo { a: 1, b: Wrapping(2), c: None },
            expected_bytes.as_bytes(),
            &Foo { a: post_mutation_expected_a, b: Wrapping(2), c: None },
        );
        test::<_, 3>(
            Unsized::from_mut_slice(&mut [1, 2, 3]),
            &[1, 2, 3],
            Unsized::from_mut_slice(&mut [0xFE, 2, 3]),
        );
    }

    #[test]
    fn test_array() {
        #[derive(FromBytes, IntoBytes, Immutable)]
        #[repr(C)]
        struct Foo {
            a: [u16; 33],
        }

        let foo = Foo { a: [0xFFFF; 33] };
        let expected = [0xFFu8; 66];
        assert_eq!(foo.as_bytes(), &expected[..]);
    }

    #[test]
    fn test_new_zeroed() {
        assert!(!bool::new_zeroed());
        assert_eq!(u64::new_zeroed(), 0);
        // This test exists in order to exercise unsafe code, especially when
        // running under Miri.
        #[allow(clippy::unit_cmp)]
        {
            assert_eq!(<()>::new_zeroed(), ());
        }
    }

    #[test]
    fn test_transparent_packed_generic_struct() {
        #[derive(IntoBytes, FromBytes, Unaligned)]
        #[repr(transparent)]
        #[allow(dead_code)] // We never construct this type
        struct Foo<T> {
            _t: T,
            _phantom: PhantomData<()>,
        }

        assert_impl_all!(Foo<u32>: FromZeros, FromBytes, IntoBytes);
        assert_impl_all!(Foo<u8>: Unaligned);

        #[derive(IntoBytes, FromBytes, Unaligned)]
        #[repr(C, packed)]
        #[allow(dead_code)] // We never construct this type
        struct Bar<T, U> {
            _t: T,
            _u: U,
        }

        assert_impl_all!(Bar<u8, AU64>: FromZeros, FromBytes, IntoBytes, Unaligned);
    }

    #[cfg(feature = "alloc")]
    mod alloc {
        use super::*;

        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        #[test]
        fn test_extend_vec_zeroed() {
            // Test extending when there is an existing allocation.
            let mut v = vec![100u16, 200, 300];
            FromZeros::extend_vec_zeroed(&mut v, 3).unwrap();
            assert_eq!(v.len(), 6);
            assert_eq!(&*v, &[100, 200, 300, 0, 0, 0]);
            drop(v);

            // Test extending when there is no existing allocation.
            let mut v: Vec<u64> = Vec::new();
            FromZeros::extend_vec_zeroed(&mut v, 3).unwrap();
            assert_eq!(v.len(), 3);
            assert_eq!(&*v, &[0, 0, 0]);
            drop(v);
        }

        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        #[test]
        fn test_extend_vec_zeroed_zst() {
            // Test extending when there is an existing (fake) allocation.
            let mut v = vec![(), (), ()];
            <()>::extend_vec_zeroed(&mut v, 3).unwrap();
            assert_eq!(v.len(), 6);
            assert_eq!(&*v, &[(), (), (), (), (), ()]);
            drop(v);

            // Test extending when there is no existing (fake) allocation.
            let mut v: Vec<()> = Vec::new();
            <()>::extend_vec_zeroed(&mut v, 3).unwrap();
            assert_eq!(&*v, &[(), (), ()]);
            drop(v);
        }

        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        #[test]
        fn test_insert_vec_zeroed() {
            // Insert at start (no existing allocation).
            let mut v: Vec<u64> = Vec::new();
            u64::insert_vec_zeroed(&mut v, 0, 2).unwrap();
            assert_eq!(v.len(), 2);
            assert_eq!(&*v, &[0, 0]);
            drop(v);

            // Insert at start.
            let mut v = vec![100u64, 200, 300];
            u64::insert_vec_zeroed(&mut v, 0, 2).unwrap();
            assert_eq!(v.len(), 5);
            assert_eq!(&*v, &[0, 0, 100, 200, 300]);
            drop(v);

            // Insert at middle.
            let mut v = vec![100u64, 200, 300];
            u64::insert_vec_zeroed(&mut v, 1, 1).unwrap();
            assert_eq!(v.len(), 4);
            assert_eq!(&*v, &[100, 0, 200, 300]);
            drop(v);

            // Insert at end.
            let mut v = vec![100u64, 200, 300];
            u64::insert_vec_zeroed(&mut v, 3, 1).unwrap();
            assert_eq!(v.len(), 4);
            assert_eq!(&*v, &[100, 200, 300, 0]);
            drop(v);
        }

        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        #[test]
        fn test_insert_vec_zeroed_zst() {
            // Insert at start (no existing fake allocation).
            let mut v: Vec<()> = Vec::new();
            <()>::insert_vec_zeroed(&mut v, 0, 2).unwrap();
            assert_eq!(v.len(), 2);
            assert_eq!(&*v, &[(), ()]);
            drop(v);

            // Insert at start.
            let mut v = vec![(), (), ()];
            <()>::insert_vec_zeroed(&mut v, 0, 2).unwrap();
            assert_eq!(v.len(), 5);
            assert_eq!(&*v, &[(), (), (), (), ()]);
            drop(v);

            // Insert at middle.
            let mut v = vec![(), (), ()];
            <()>::insert_vec_zeroed(&mut v, 1, 1).unwrap();
            assert_eq!(v.len(), 4);
            assert_eq!(&*v, &[(), (), (), ()]);
            drop(v);

            // Insert at end.
            let mut v = vec![(), (), ()];
            <()>::insert_vec_zeroed(&mut v, 3, 1).unwrap();
            assert_eq!(v.len(), 4);
            assert_eq!(&*v, &[(), (), (), ()]);
            drop(v);
        }

        #[test]
        fn test_new_box_zeroed() {
            assert_eq!(u64::new_box_zeroed(), Ok(Box::new(0)));
        }

        #[test]
        fn test_new_box_zeroed_array() {
            drop(<[u32; 0x1000]>::new_box_zeroed());
        }

        #[test]
        fn test_new_box_zeroed_zst() {
            // This test exists in order to exercise unsafe code, especially
            // when running under Miri.
            #[allow(clippy::unit_cmp)]
            {
                assert_eq!(<()>::new_box_zeroed(), Ok(Box::new(())));
            }
        }

        #[test]
        fn test_new_box_zeroed_with_elems() {
            let mut s: Box<[u64]> = <[u64]>::new_box_zeroed_with_elems(3).unwrap();
            assert_eq!(s.len(), 3);
            assert_eq!(&*s, &[0, 0, 0]);
            s[1] = 3;
            assert_eq!(&*s, &[0, 3, 0]);
        }

        #[test]
        fn test_new_box_zeroed_with_elems_empty() {
            let s: Box<[u64]> = <[u64]>::new_box_zeroed_with_elems(0).unwrap();
            assert_eq!(s.len(), 0);
        }

        #[test]
        fn test_new_box_zeroed_with_elems_zst() {
            let mut s: Box<[()]> = <[()]>::new_box_zeroed_with_elems(3).unwrap();
            assert_eq!(s.len(), 3);
            assert!(s.get(10).is_none());
            // This test exists in order to exercise unsafe code, especially
            // when running under Miri.
            #[allow(clippy::unit_cmp)]
            {
                assert_eq!(s[1], ());
            }
            s[2] = ();
        }

        #[test]
        fn test_new_box_zeroed_with_elems_zst_empty() {
            let s: Box<[()]> = <[()]>::new_box_zeroed_with_elems(0).unwrap();
            assert_eq!(s.len(), 0);
        }

        #[test]
        fn new_box_zeroed_with_elems_errors() {
            assert_eq!(<[u16]>::new_box_zeroed_with_elems(usize::MAX), Err(AllocError));

            let max = <usize as core::convert::TryFrom<_>>::try_from(isize::MAX).unwrap();
            assert_eq!(
                <[u16]>::new_box_zeroed_with_elems((max / mem::size_of::<u16>()) + 1),
                Err(AllocError)
            );
        }
    }

    #[test]
    #[allow(deprecated)]
    fn test_deprecated_from_bytes() {
        let val = 0u32;
        let bytes = val.as_bytes();

        assert!(u32::ref_from(bytes).is_some());
        // mut_from needs mut bytes
        let mut val = 0u32;
        let mut_bytes = val.as_mut_bytes();
        assert!(u32::mut_from(mut_bytes).is_some());

        assert!(u32::read_from(bytes).is_some());

        let (slc, rest) = <u32>::slice_from_prefix(bytes, 0).unwrap();
        assert!(slc.is_empty());
        assert_eq!(rest.len(), 4);

        let (rest, slc) = <u32>::slice_from_suffix(bytes, 0).unwrap();
        assert!(slc.is_empty());
        assert_eq!(rest.len(), 4);

        let (slc, rest) = <u32>::mut_slice_from_prefix(mut_bytes, 0).unwrap();
        assert!(slc.is_empty());
        assert_eq!(rest.len(), 4);

        let (rest, slc) = <u32>::mut_slice_from_suffix(mut_bytes, 0).unwrap();
        assert!(slc.is_empty());
        assert_eq!(rest.len(), 4);
    }

    #[test]
    fn test_try_ref_from_prefix_suffix() {
        use crate::util::testutil::Align;
        let bytes = &Align::<[u8; 4], u32>::new([0u8; 4]).t[..];
        let (r, rest): (&u32, &[u8]) = u32::try_ref_from_prefix(bytes).unwrap();
        assert_eq!(*r, 0);
        assert_eq!(rest.len(), 0);

        let (rest, r): (&[u8], &u32) = u32::try_ref_from_suffix(bytes).unwrap();
        assert_eq!(*r, 0);
        assert_eq!(rest.len(), 0);
    }

    #[test]
    fn test_raw_dangling() {
        use crate::util::AsAddress;
        let ptr: NonNull<u32> = u32::raw_dangling();
        assert_eq!(AsAddress::addr(ptr), 1);

        let ptr: NonNull<[u32]> = <[u32]>::raw_dangling();
        assert_eq!(AsAddress::addr(ptr), 1);
    }

    #[test]
    fn test_try_ref_from_prefix_with_elems() {
        use crate::util::testutil::Align;
        let bytes = &Align::<[u8; 8], u32>::new([0u8; 8]).t[..];
        let (r, rest): (&[u32], &[u8]) = <[u32]>::try_ref_from_prefix_with_elems(bytes, 2).unwrap();
        assert_eq!(r.len(), 2);
        assert_eq!(rest.len(), 0);
    }

    #[test]
    fn test_try_ref_from_suffix_with_elems() {
        use crate::util::testutil::Align;
        let bytes = &Align::<[u8; 8], u32>::new([0u8; 8]).t[..];
        let (rest, r): (&[u8], &[u32]) = <[u32]>::try_ref_from_suffix_with_elems(bytes, 2).unwrap();
        assert_eq!(r.len(), 2);
        assert_eq!(rest.len(), 0);
    }
}
