// SPDX-License-Identifier: GPL-2.0

//! Helper crate for KASAN testing.
//!
//! Provides behavior to check the sanitization of Rust code.

use core::ptr::addr_of_mut;
use kernel::prelude::*;

/// Trivial UAF - allocate a big vector, grab a pointer partway through,
/// drop the vector, and touch it.
#[no_mangle]
pub extern "C" fn kasan_test_rust_uaf() -> u8 {
    let mut v: KVec<u8> = KVec::new();
    for _ in 0..4096 {
        v.push(0x42, GFP_KERNEL).unwrap();
    }
    let ptr: *mut u8 = addr_of_mut!(v[2048]);
    drop(v);
    // SAFETY: Incorrect, on purpose.
    unsafe { *ptr }
}
