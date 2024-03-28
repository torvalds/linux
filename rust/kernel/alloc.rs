// SPDX-License-Identifier: GPL-2.0

//! Extensions to the [`alloc`] crate.

#[cfg(not(test))]
#[cfg(not(testlib))]
mod allocator;
pub mod vec_ext;
