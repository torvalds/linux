// SPDX-License-Identifier: GPL-2.0

//! This module provides the `TagSet` struct to wrap the C `struct blk_mq_tag_set`.
//!
//! C header: [`include/linux/blk-mq.h`](srctree/include/linux/blk-mq.h)

use crate::{
    bindings,
    block::mq::{
        operations::OperationsVTable,
        request::RequestDataWrapper,
        Operations, //
    },
    error::to_result,
    prelude::*,
    types::Opaque, //
};
use core::{
    convert::TryInto,
    marker::PhantomData, //
};

/// A wrapper for the C `struct blk_mq_tag_set`.
///
/// `struct blk_mq_tag_set` contains a `struct list_head` and so must be pinned.
///
/// # Invariants
///
/// - `inner` is initialized and valid.
#[pin_data(PinnedDrop)]
#[repr(transparent)]
pub struct TagSet<T: Operations> {
    #[pin]
    inner: Opaque<bindings::blk_mq_tag_set>,
    _p: PhantomData<T>,
}

impl<T: Operations> TagSet<T> {
    /// Try to create a new tag set
    pub fn new(nr_hw_queues: u32, num_tags: u32, num_maps: u32) -> impl PinInit<Self, Error> {
        let tag_set: bindings::blk_mq_tag_set = pin_init::zeroed();
        let tag_set: Result<_> = core::mem::size_of::<RequestDataWrapper>()
            .try_into()
            .map(|cmd_size| {
                bindings::blk_mq_tag_set {
                    ops: OperationsVTable::<T>::build(),
                    nr_hw_queues,
                    timeout: 0, // 0 means default which is 30Hz in C
                    numa_node: bindings::NUMA_NO_NODE,
                    queue_depth: num_tags,
                    cmd_size,
                    flags: 0,
                    driver_data: core::ptr::null_mut::<crate::ffi::c_void>(),
                    nr_maps: num_maps,
                    ..tag_set
                }
            })
            .map(Opaque::new)
            .map_err(|e| e.into());

        try_pin_init!(TagSet {
            inner <- tag_set.pin_chain(|tag_set| {
                // SAFETY: we do not move out of `tag_set`.
                let tag_set: &mut Opaque<_> = unsafe { Pin::get_unchecked_mut(tag_set) };
                // SAFETY: `tag_set` is a reference to an initialized `blk_mq_tag_set`.
                to_result( unsafe { bindings::blk_mq_alloc_tag_set(tag_set.get())})
            }),
            _p: PhantomData,
        })
    }

    /// Return the pointer to the wrapped `struct blk_mq_tag_set`
    pub(crate) fn raw_tag_set(&self) -> *mut bindings::blk_mq_tag_set {
        self.inner.get()
    }
}

#[pinned_drop]
impl<T: Operations> PinnedDrop for TagSet<T> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: By type invariant `inner` is valid and has been properly
        // initialized during construction.
        unsafe { bindings::blk_mq_free_tag_set(self.inner.get()) };
    }
}

// SAFETY: It is safe to transfer ownership of `TagSet` between threads.
unsafe impl<T: Operations> Send for TagSet<T> {}

// SAFETY: It is safe to share `&TagSet` across threads. `TagSet` exposes no
// `&self` methods that mutate the contained `blk_mq_tag_set`.
unsafe impl<T: Operations> Sync for TagSet<T> {}
