// SPDX-License-Identifier: GPL-2.0

//! Commonly used sizes.
//!
//! C headers: [`include/linux/sizes.h`](srctree/include/linux/sizes.h).

/// 0x00000400
pub const SZ_1K: usize = bindings::SZ_1K as usize;
/// 0x00000800
pub const SZ_2K: usize = bindings::SZ_2K as usize;
/// 0x00001000
pub const SZ_4K: usize = bindings::SZ_4K as usize;
/// 0x00002000
pub const SZ_8K: usize = bindings::SZ_8K as usize;
/// 0x00004000
pub const SZ_16K: usize = bindings::SZ_16K as usize;
/// 0x00008000
pub const SZ_32K: usize = bindings::SZ_32K as usize;
/// 0x00010000
pub const SZ_64K: usize = bindings::SZ_64K as usize;
/// 0x00020000
pub const SZ_128K: usize = bindings::SZ_128K as usize;
/// 0x00040000
pub const SZ_256K: usize = bindings::SZ_256K as usize;
/// 0x00080000
pub const SZ_512K: usize = bindings::SZ_512K as usize;
