// SPDX-License-Identifier: GPL-2.0

//! This module provides an interface for blk-mq drivers to implement.
//!
//! C header: [`include/linux/blk-mq.h`](srctree/include/linux/blk-mq.h)

use crate::{
    bindings,
    block::mq::request::RequestDataWrapper,
    block::mq::Request,
    error::{from_result, Result},
    types::ARef,
};
use core::{marker::PhantomData, sync::atomic::AtomicU64, sync::atomic::Ordering};

/// Implement this trait to interface blk-mq as block devices.
///
/// To implement a block device driver, implement this trait as described in the
/// [module level documentation]. The kernel will use the implementation of the
/// functions defined in this trait to interface a block device driver. Note:
/// There is no need for an exit_request() implementation, because the `drop`
/// implementation of the [`Request`] type will be invoked by automatically by
/// the C/Rust glue logic.
///
/// [module level documentation]: kernel::block::mq
#[macros::vtable]
pub trait Operations: Sized {
    /// Called by the kernel to queue a request with the driver. If `is_last` is
    /// `false`, the driver is allowed to defer committing the request.
    fn queue_rq(rq: ARef<Request<Self>>, is_last: bool) -> Result;

    /// Called by the kernel to indicate that queued requests should be submitted.
    fn commit_rqs();

    /// Called by the kernel to poll the device for completed requests. Only
    /// used for poll queues.
    fn poll() -> bool {
        crate::build_error(crate::error::VTABLE_DEFAULT_ERROR)
    }
}

/// A vtable for blk-mq to interact with a block device driver.
///
/// A `bindings::blk_mq_ops` vtable is constructed from pointers to the `extern
/// "C"` functions of this struct, exposed through the `OperationsVTable::VTABLE`.
///
/// For general documentation of these methods, see the kernel source
/// documentation related to `struct blk_mq_operations` in
/// [`include/linux/blk-mq.h`].
///
/// [`include/linux/blk-mq.h`]: srctree/include/linux/blk-mq.h
pub(crate) struct OperationsVTable<T: Operations>(PhantomData<T>);

impl<T: Operations> OperationsVTable<T> {
    /// This function is called by the C kernel. A pointer to this function is
    /// installed in the `blk_mq_ops` vtable for the driver.
    ///
    /// # Safety
    ///
    /// - The caller of this function must ensure that the pointee of `bd` is
    ///   valid for reads for the duration of this function.
    /// - This function must be called for an initialized and live `hctx`. That
    ///   is, `Self::init_hctx_callback` was called and
    ///   `Self::exit_hctx_callback()` was not yet called.
    /// - `(*bd).rq` must point to an initialized and live `bindings:request`.
    ///   That is, `Self::init_request_callback` was called but
    ///   `Self::exit_request_callback` was not yet called for the request.
    /// - `(*bd).rq` must be owned by the driver. That is, the block layer must
    ///   promise to not access the request until the driver calls
    ///   `bindings::blk_mq_end_request` for the request.
    unsafe extern "C" fn queue_rq_callback(
        _hctx: *mut bindings::blk_mq_hw_ctx,
        bd: *const bindings::blk_mq_queue_data,
    ) -> bindings::blk_status_t {
        // SAFETY: `bd.rq` is valid as required by the safety requirement for
        // this function.
        let request = unsafe { &*(*bd).rq.cast::<Request<T>>() };

        // One refcount for the ARef, one for being in flight
        request.wrapper_ref().refcount().store(2, Ordering::Relaxed);

        // SAFETY:
        //  - We own a refcount that we took above. We pass that to `ARef`.
        //  - By the safety requirements of this function, `request` is a valid
        //    `struct request` and the private data is properly initialized.
        //  - `rq` will be alive until `blk_mq_end_request` is called and is
        //    reference counted by `ARef` until then.
        let rq = unsafe { Request::aref_from_raw((*bd).rq) };

        // SAFETY: We have exclusive access and we just set the refcount above.
        unsafe { Request::start_unchecked(&rq) };

        let ret = T::queue_rq(
            rq,
            // SAFETY: `bd` is valid as required by the safety requirement for
            // this function.
            unsafe { (*bd).last },
        );

        if let Err(e) = ret {
            e.to_blk_status()
        } else {
            bindings::BLK_STS_OK as _
        }
    }

    /// This function is called by the C kernel. A pointer to this function is
    /// installed in the `blk_mq_ops` vtable for the driver.
    ///
    /// # Safety
    ///
    /// This function may only be called by blk-mq C infrastructure.
    unsafe extern "C" fn commit_rqs_callback(_hctx: *mut bindings::blk_mq_hw_ctx) {
        T::commit_rqs()
    }

    /// This function is called by the C kernel. It is not currently
    /// implemented, and there is no way to exercise this code path.
    ///
    /// # Safety
    ///
    /// This function may only be called by blk-mq C infrastructure.
    unsafe extern "C" fn complete_callback(_rq: *mut bindings::request) {}

    /// This function is called by the C kernel. A pointer to this function is
    /// installed in the `blk_mq_ops` vtable for the driver.
    ///
    /// # Safety
    ///
    /// This function may only be called by blk-mq C infrastructure.
    unsafe extern "C" fn poll_callback(
        _hctx: *mut bindings::blk_mq_hw_ctx,
        _iob: *mut bindings::io_comp_batch,
    ) -> core::ffi::c_int {
        T::poll().into()
    }

    /// This function is called by the C kernel. A pointer to this function is
    /// installed in the `blk_mq_ops` vtable for the driver.
    ///
    /// # Safety
    ///
    /// This function may only be called by blk-mq C infrastructure. This
    /// function may only be called once before `exit_hctx_callback` is called
    /// for the same context.
    unsafe extern "C" fn init_hctx_callback(
        _hctx: *mut bindings::blk_mq_hw_ctx,
        _tagset_data: *mut core::ffi::c_void,
        _hctx_idx: core::ffi::c_uint,
    ) -> core::ffi::c_int {
        from_result(|| Ok(0))
    }

    /// This function is called by the C kernel. A pointer to this function is
    /// installed in the `blk_mq_ops` vtable for the driver.
    ///
    /// # Safety
    ///
    /// This function may only be called by blk-mq C infrastructure.
    unsafe extern "C" fn exit_hctx_callback(
        _hctx: *mut bindings::blk_mq_hw_ctx,
        _hctx_idx: core::ffi::c_uint,
    ) {
    }

    /// This function is called by the C kernel. A pointer to this function is
    /// installed in the `blk_mq_ops` vtable for the driver.
    ///
    /// # Safety
    ///
    /// - This function may only be called by blk-mq C infrastructure.
    /// - `_set` must point to an initialized `TagSet<T>`.
    /// - `rq` must point to an initialized `bindings::request`.
    /// - The allocation pointed to by `rq` must be at the size of `Request`
    ///   plus the size of `RequestDataWrapper`.
    unsafe extern "C" fn init_request_callback(
        _set: *mut bindings::blk_mq_tag_set,
        rq: *mut bindings::request,
        _hctx_idx: core::ffi::c_uint,
        _numa_node: core::ffi::c_uint,
    ) -> core::ffi::c_int {
        from_result(|| {
            // SAFETY: By the safety requirements of this function, `rq` points
            // to a valid allocation.
            let pdu = unsafe { Request::wrapper_ptr(rq.cast::<Request<T>>()) };

            // SAFETY: The refcount field is allocated but not initialized, so
            // it is valid for writes.
            unsafe { RequestDataWrapper::refcount_ptr(pdu.as_ptr()).write(AtomicU64::new(0)) };

            Ok(0)
        })
    }

    /// This function is called by the C kernel. A pointer to this function is
    /// installed in the `blk_mq_ops` vtable for the driver.
    ///
    /// # Safety
    ///
    /// - This function may only be called by blk-mq C infrastructure.
    /// - `_set` must point to an initialized `TagSet<T>`.
    /// - `rq` must point to an initialized and valid `Request`.
    unsafe extern "C" fn exit_request_callback(
        _set: *mut bindings::blk_mq_tag_set,
        rq: *mut bindings::request,
        _hctx_idx: core::ffi::c_uint,
    ) {
        // SAFETY: The tagset invariants guarantee that all requests are allocated with extra memory
        // for the request data.
        let pdu = unsafe { bindings::blk_mq_rq_to_pdu(rq) }.cast::<RequestDataWrapper>();

        // SAFETY: `pdu` is valid for read and write and is properly initialised.
        unsafe { core::ptr::drop_in_place(pdu) };
    }

    const VTABLE: bindings::blk_mq_ops = bindings::blk_mq_ops {
        queue_rq: Some(Self::queue_rq_callback),
        queue_rqs: None,
        commit_rqs: Some(Self::commit_rqs_callback),
        get_budget: None,
        put_budget: None,
        set_rq_budget_token: None,
        get_rq_budget_token: None,
        timeout: None,
        poll: if T::HAS_POLL {
            Some(Self::poll_callback)
        } else {
            None
        },
        complete: Some(Self::complete_callback),
        init_hctx: Some(Self::init_hctx_callback),
        exit_hctx: Some(Self::exit_hctx_callback),
        init_request: Some(Self::init_request_callback),
        exit_request: Some(Self::exit_request_callback),
        cleanup_rq: None,
        busy: None,
        map_queues: None,
        #[cfg(CONFIG_BLK_DEBUG_FS)]
        show_rq: None,
    };

    pub(crate) const fn build() -> &'static bindings::blk_mq_ops {
        &Self::VTABLE
    }
}
