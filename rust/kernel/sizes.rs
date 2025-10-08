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
/// 0x00100000
pub const SZ_1M: usize = bindings::SZ_1M as usize;
/// 0x00200000
pub const SZ_2M: usize = bindings::SZ_2M as usize;
/// 0x00400000
pub const SZ_4M: usize = bindings::SZ_4M as usize;
/// 0x00800000
pub const SZ_8M: usize = bindings::SZ_8M as usize;
/// 0x01000000
pub const SZ_16M: usize = bindings::SZ_16M as usize;
/// 0x02000000
pub const SZ_32M: usize = bindings::SZ_32M as usize;
/// 0x04000000
pub const SZ_64M: usize = bindings::SZ_64M as usize;
/// 0x08000000
pub const SZ_128M: usize = bindings::SZ_128M as usize;
/// 0x10000000
pub const SZ_256M: usize = bindings::SZ_256M as usize;
/// 0x20000000
pub const SZ_512M: usize = bindings::SZ_512M as usize;
/// 0x40000000
pub const SZ_1G: usize = bindings::SZ_1G as usize;
/// 0x80000000
pub const SZ_2G: usize = bindings::SZ_2G as usize;
