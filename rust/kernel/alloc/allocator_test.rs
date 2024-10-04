// SPDX-License-Identifier: GPL-2.0

#![allow(missing_docs)]

use super::{AllocError, Allocator, Flags};
use core::alloc::Layout;
use core::ptr::NonNull;

pub struct Kmalloc;

unsafe impl Allocator for Kmalloc {
    unsafe fn realloc(
        _ptr: Option<NonNull<u8>>,
        _layout: Layout,
        _old_layout: Layout,
        _flags: Flags,
    ) -> Result<NonNull<[u8]>, AllocError> {
        panic!();
    }
}
