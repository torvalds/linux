// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025-2026 Red Hat Inc.
 * Author: Philipp Stanner <pstanner@redhat.com>
 */

//! DMA Fence support.
//!
//! Reference: <https://docs.kernel.org/driver-api/dma-buf.html#c.dma_fence>
//!
//! header: [`include/linux/dma-fence.h`](srctree/include/linux/dma-fence.h)

use crate::{
    alloc::AllocError,
    bindings,
    container_of,
    error::to_result,
    prelude::*,
    types::ForeignOwnable,
    types::Opaque, //
};

use core::{
    marker::PhantomData,
    mem::ManuallyDrop,
    ops::Deref,
    ptr,
    ptr::{
        drop_in_place,
        NonNull, //
    }, //
};

use kernel::{
    str::CString,
    sync::{
        aref::{
            ARef,
            AlwaysRefCounted, //
        },
        atomic::{
            Atomic,
            Relaxed, //
        },
        rcu::rcu_barrier, //
    }, //
};

/// VTable for dma_fence backend_ops callbacks.
//
// Mandatory dma_fence backend_ops are implemented implicitly through
// [`FenceContext`]. Additional ones shall get implemented on this trait.
pub trait FenceContextOps {
    /// The generic payload data for [`DriverFence`]s created on this fctx.
    type FenceDataType: Send + Sync;
}

/// A dma-fence context. A fence context takes care of associating related fences
/// with each other, providing each with raising sequence numbers and a common
/// identifier.
#[pin_data(PinnedDrop)]
pub struct FenceContext<T: FenceContextOps + Send + Sync> {
    /// The fence context number.
    nr: u64,
    /// The sequence number for the next fence created.
    seqno: Atomic<u64>,
    // The name parameters can be accessed by the dma_fence backend_ops. UAF
    // errors are prevented by the `call_rcu()` in `drop_driver_fence_data()`.
    /// The name of the driver this FenceContext's fences belong to.
    driver_name: CString,
    /// The name of the timeline this FenceContext's fences belong to.
    timeline_name: CString,
    /// The number of all unsignaled fences on this context.
    // Used to prevent bugs due to forgotten fences.
    //
    // The lifetime on `DriverFence`s should typically prevent this from
    // happening.
    //
    // However, we cannot fully guarantee in Rust that `DriverFence`s will not
    // be forgotten, e.g., through `core::mem::forget()`. This could circumvent
    // the lifetime which intends to enforce that all fences disappear before
    // their context.
    nr_of_unsignaled_fences: Atomic<usize>,
    /// The user's data.
    #[pin]
    data: T,
}

impl<'a, T: Send + Sync + FenceContextOps> FenceContext<T> {
    // This can later be extended as a vtable in case other parties need support
    // for the more "exotic" callbacks.
    const OPS: bindings::dma_fence_ops = bindings::dma_fence_ops {
        get_driver_name: Some(Self::get_driver_name),
        get_timeline_name: Some(Self::get_timeline_name),
        enable_signaling: None,
        signaled: None,
        // Deprecated.
        wait: None,
        // Must never be implemented for these abstractions.
        release: None,
        set_deadline: None,
    };

    /// Create a new `FenceContext`.
    pub fn new<E>(
        initial_seqno: u64,
        driver_name: &CStr,
        timeline_name: &CStr,
        data: impl PinInit<T, E>,
    ) -> impl PinInit<Self, Error>
    where
        Error: From<E>,
    {
        let driver_name = CString::try_from(driver_name);
        let timeline_name = CString::try_from(timeline_name);
        try_pin_init!(Self {
            // SAFETY: `dma_fence_context_alloc()` merely works on a global
            // atomic. Parameter `1` is the number of contexts we want to
            // allocate.
            nr: unsafe { bindings::dma_fence_context_alloc(1) },
            seqno: Atomic::new(initial_seqno),
            driver_name: driver_name?,
            timeline_name: timeline_name?,
            nr_of_unsignaled_fences: Atomic::new(0),
            data <- data,
        })
    }

    fn next_seqno(&self) -> u64 {
        self.seqno.fetch_add(1, Relaxed)
    }

    /// Allocate the memory for a [`DriverFence`] and already store `data` inside.
    ///
    /// This is needed because many times, creation of a [`DriverFence`] must not
    /// fail, and allocating might deadlock in some situations.
    ///
    /// The `data` you pass here must not perform any operations that are illegal
    /// in atomic context in its [`Drop`] implementation.
    pub fn new_fence_allocation(
        &self,
        data: T::FenceDataType,
    ) -> Result<DriverFenceAllocation<'_, T>> {
        let fence_data = DriverFenceData {
            rcu_head: Default::default(),
            // `inner` remains uninitialized until a `DriverFence` takes over.
            inner: Fence {
                inner: Opaque::uninit(),
            },
            fctx: self,
            data,
        };

        // In order to support the C dma_fence callbacks, it is necessary for
        // a `Fence` and a `DriverFence` to live in the same allocation,
        // because the C backend passes a dma_fence, from which the driver most
        // likely wants to be able to access its `data` in `DriverFence`.
        //
        // Hence, we need the manage the memory manually. It will be freed by the
        // C backend automatically once the refcount within `Fence` drops to 0.
        let data = KBox::new(fence_data, GFP_KERNEL | __GFP_ZERO)?;

        Ok(DriverFenceAllocation {
            data,
            ops: &Self::OPS,
        })
    }

    extern "C" fn get_driver_name(ptr: *mut bindings::dma_fence) -> *const c_char {
        // SAFETY: The C backend only invokes this callback with `ptr` pointing
        // to a valid, unsignaled `bindings::dma_fence`. All fences created in
        // this module always reside within `Fence` which always resides in a
        // `DriverFenceData`, thus satisfying the function's safety
        // requirements.
        let fctx = unsafe { Self::from_raw_fence(ptr) };

        fctx.driver_name.as_char_ptr()
    }

    extern "C" fn get_timeline_name(ptr: *mut bindings::dma_fence) -> *const c_char {
        // SAFETY: The C backend only invokes this callback with `ptr` pointing
        // to a valid, unsignaled `bindings::dma_fence`. All fences created in
        // this module always reside within `Fence` which always resides in a
        // `DriverFenceData`, thus satisfying the function's safety
        // requirements.
        let fctx = unsafe { Self::from_raw_fence(ptr) };

        fctx.timeline_name.as_char_ptr()
    }

    /// Create a [`FenceContext`] from an associated [`bindings::dma_fence`].
    ///
    /// # Safety
    ///
    /// `ptr` must be a valid pointer to a [`bindings::dma_fence`] which resides
    /// within a [`Fence`], which in turn resides in a [`DriverFenceData`].
    unsafe fn from_raw_fence(ptr: *mut bindings::dma_fence) -> &'a Self {
        let opaque_fence = Opaque::cast_from(ptr);

        // SAFETY: Safe due to the function's overall safety requirements.
        let fence_ptr = unsafe { container_of!(opaque_fence, Fence, inner) };

        // CAST: `DriverFenceData` is `repr(C)` and a `Fence` is its first member.
        let fence_data_ptr: *const DriverFenceData<'a, T> = fence_ptr.cast();

        // SAFETY: Safe because of the comments directly above.
        let fence_data = unsafe { &*fence_data_ptr };

        fence_data.fctx
    }
}

#[pinned_drop]
impl<T: FenceContextOps + Send + Sync> PinnedDrop for FenceContext<T> {
    fn drop(self: Pin<&mut Self>) {
        // Fence ops callbacks can be called on unsignaled fences. Since these
        // callbacks can access the fence context and its data, it needs to be
        // guaranteed that a context only drops after all associated
        // `DriverFence`s have been dropped. This is unlikely to occur, but
        // would result in silent UAF. Throw a panic to prevent that.
        //
        // TODO:
        // It would be better if the fence context signals all forgotten fences
        // itself. To do so, it would keep a list of unsignaled fences. That
        // list's members would have to be pre-allocated (see
        // `FenceCallback::new_fence_allocation()`).
        if self.nr_of_unsignaled_fences.load(Relaxed) != 0 {
            panic!("Forgotten fences in FenceContext.");
        }

        // Ensure that the driver cannot unload while there are still dma_fence
        // callbacks running. At the same time, the RCU barrier addresses the
        // problem inherited by the C backend, in which backend ops callbacks
        // might be accessing the fence while it is being signaled (or shortly
        // after). This could cause UAF access on the fence context's
        // `fctx.driver_name` and `fctx.timeline_name`.
        //
        // Wait for the RCU callbacks in `DriverFence::drop`.
        rcu_barrier();
    }
}

/// Error type for fence callback registration.
///
/// Generic over `T` so that `AlreadySignaled` can return the callback to the
/// caller, allowing it to reclaim any resources owned by the callback (e.g.,
/// a fence handle that needs to be signaled).
#[derive(Debug)]
pub enum CallbackError<T> {
    /// The fence was already signaled. The callback is returned so the caller
    /// can extract owned resources without losing them.
    AlreadySignaled(T),
    /// Some other error occurred during registration.
    Other(Error),
}

impl<T> From<CallbackError<T>> for Error {
    #[inline]
    fn from(err: CallbackError<T>) -> Self {
        match err {
            CallbackError::AlreadySignaled(_) => ENOENT,
            CallbackError::Other(e) => e,
        }
    }
}

impl<T> From<AllocError> for CallbackError<T> {
    #[inline]
    fn from(e: AllocError) -> Self {
        CallbackError::Other(Error::from(e))
    }
}

/// Trait for callbacks that can be registered on fences.
///
/// When the fence signals, the callback will be invoked.
///
/// # Example
///
/// ```rust
/// use kernel::dma_buf::FenceCallback;
///
/// struct MyCallback {
///     // Your callback state here
/// }
///
/// impl FenceCallback for MyCallback {
///     fn on_signal(&mut self) {
///         pr_info!("Fence signaled!\n");
///         // Handle fence completion
///     }
/// }
/// ```
pub trait FenceCallback: Send + 'static {
    /// Called when the fence is signaled.
    ///
    /// This is called from the fence signaling path, which may be in interrupt
    /// context or with locks held, which is why `self` is only borrowed, so that
    /// it cannot drop. Implementations must not sleep or perform
    /// long-running operations.
    ///
    /// An implementation likely wants to inform itself (e.g., through a work item)
    /// within this callback that the associated [`FenceCallbackRegistration`]
    /// can now be dropped.
    fn on_signal(&mut self);
}

/// A callback registration on a fence.
///
/// When this object is dropped, the callback is automatically removed if it
/// hasn't been called yet.
#[pin_data(PinnedDrop)]
pub struct FenceCallbackRegistration<T: FenceCallback + 'static> {
    #[pin]
    callback_foreign: Opaque<bindings::dma_fence_cb>,
    callback: ManuallyDrop<T>,
    fence: ARef<Fence>,
}

impl<T: FenceCallback> FenceCallbackRegistration<T> {
    /// Create a [`PinInit`] closure for registering a callback on a fence.
    ///
    /// The actual attempt at registering the callback will take place once you
    /// call an allocator's `pin_init()` function.
    ///
    /// On success the callback is pinned in place and will fire when the fence
    /// signals. On `AlreadySignaled` the callback is returned to the caller so
    /// that owned resources can be reclaimed.
    pub fn new<'a>(fence: &'a Fence, callback: T) -> impl PinInit<Self, CallbackError<T>> + 'a
    where
        T: 'a,
    {
        try_pin_init!(Self {
            // We need to fully initialize the fence because after
            // `dma_fence_add_callback()` ran, the callback might immediately
            // get invoked.
            callback: ManuallyDrop::new(callback),
            fence: ARef::from(fence),
            callback_foreign <- Opaque::try_ffi_init(|ptr| {
                // SAFETY: `fence.inner.get()` is a valid, initialized `struct
                // dma_fence`. `ptr` points to the `struct dma_fence_cb` field
                // within the pinned allocation, so it remains valid until
                // `dma_fence_remove_callback()` in `PinnedDrop` or until the
                // callback fires.
                let ret = unsafe {
                    to_result(bindings::dma_fence_add_callback(
                        fence.inner.get(),
                        ptr,
                        Some(Self::dma_fence_callback),
                    ))
                };
                match ret {
                    Ok(()) => Ok(()),
                    Err(e) => {
                        // SAFETY: We could not register the callback. Thus,
                        // C will not use it. So we can just take it back
                        // and pass it to the user again.
                        let cb_back = unsafe { ManuallyDrop::take(callback) };
                        if e == ENOENT {
                            Err(CallbackError::AlreadySignaled(cb_back))
                        } else {
                            Err(CallbackError::Other(e))
                        }
                    },
                }
            }),
        }? CallbackError<T>)
    }

    /// Raw dma fence callback that is called by the C code.
    ///
    /// # Safety
    ///
    /// This is only called by the dma_fence subsystem with valid pointers.
    unsafe extern "C" fn dma_fence_callback(
        _fence: *mut bindings::dma_fence,
        callback_foreign: *mut bindings::dma_fence_cb,
    ) {
        let ptr = Opaque::cast_from(callback_foreign).cast_mut();

        // SAFETY: All callbacks we can receive here have been created in such a way that they are
        // embedded into a `FenceCallbackRegistration`.
        let reg: *mut Self = unsafe { container_of!(ptr, Self, callback_foreign) };

        // SAFETY: `reg` is a valid `Self` pointer.
        //
        // The backend ensures synchronisation so whoever holds the registration object cannot drop
        // it while this code is running. See `FenceCallbackRegistration::drop`.
        unsafe { (*reg).callback.on_signal() };
    }

    /// Returns a reference to the fence this callback is registered on.
    #[inline]
    pub fn fence(&self) -> &Fence {
        &self.fence
    }
}

#[pinned_drop]
impl<T: FenceCallback> PinnedDrop for FenceCallbackRegistration<T> {
    fn drop(self: Pin<&mut Self>) {
        // Always call `dma_fence_remove_callback()`, even if the callback
        // already ran. This is necessary for synchronization:
        // `dma_fence_remove_callback()` acquires `fence->lock`, which ensures
        // that any in-flight `dma_fence_signal()` (which calls our callback
        // while holding the same lock) has completed before we free the struct.
        //
        // Without this, Drop can race with a concurrent signal:
        //   CPU0 (signal, lock held): take() -> on_signal(fence_ref) (in progress)
        //   CPU1 (drop): skips lock -> frees struct
        //   CPU0: accesses fence_ref -> use-after-free
        //
        // When the callback has already fired, the signal path detached the
        // list node via `INIT_LIST_HEAD()`, so dma_fence_remove_callback just
        // sees an empty node and returns false — the lock acquisition is the
        // only thing that matters.
        //
        // SAFETY: The fence pointer is valid and the cb was initialized by
        // `dma_fence_add_callback()` during construction.
        unsafe {
            bindings::dma_fence_remove_callback(self.fence.as_raw(), self.callback_foreign.get())
        };

        // SAFETY: This is literally the drop implementation, so no one has
        // dropped this so far; so we can do it now.
        unsafe { ManuallyDrop::<T>::drop(self.project().callback) };
    }
}

// SAFETY: FenceCallbackRegistration can be sent between threads.
unsafe impl<T: FenceCallback> Send for FenceCallbackRegistration<T> {}

// SAFETY: &FenceCallbackRegistration can be shared between threads if &T can.
unsafe impl<T: FenceCallback> Sync for FenceCallbackRegistration<T> where T: Sync {}

/// The receiving counterpart of a [`DriverFence`].
///
/// The Rust DMA fence implementation has a dualistic design: [`DriverFence`]s
/// are the producer-side, intended to be always owned by only one party. That
/// party has the monopoly on signaling the fence.
///
/// A [`Fence`] is the counterpart for consumers. Thus, [`Fence`]s are always
/// refcounted and can shared with an arbitrary number of parties, including
/// userspace. A [`Fence`] can only be used for actions such as checking the
/// fence's status or for registering callbacks on it.
///
/// Once the associated [`DriverFence`] signals, all
/// [`FenceCallbackRegistration`]s registered on the [`Fence`] will be executed.
///
/// A [`Fence`] can arbitrarily outlive its [`DriverFence`] and the
/// [`FenceContext`]. Signaling a [`DriverFence`] decouples it from its
/// [`Fence`]s.
#[repr(transparent)]
pub struct Fence {
    /// The actual dma_fence passed to C.
    inner: Opaque<bindings::dma_fence>,
}

/// Guard helper for locking within this module.
///
/// Its only purpose for now is to avoid a number of unsafe lock-unlock cycles.
/// It is never used outside of this module.
// TODO: This should be made more canonical, probably by basing it on a
// SpinLockIrqGuard once available.
struct FenceGuard<'a> {
    inner: &'a Fence,
    flags: usize,
}

impl<'a> Deref for FenceGuard<'a> {
    type Target = &'a Fence;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl Drop for FenceGuard<'_> {
    fn drop(&mut self) {
        // SAFETY: `fence` is valid because `self` is valid. `flag_ptr` is
        // merely a pointer to an integer, which lives as long as this function.
        // When a `FenceGuard` exists, the lock has been taken by definition.
        unsafe { bindings::dma_fence_unlock_irqrestore(self.as_raw(), &raw mut self.flags) };
    }
}

// SAFETY: Fences are literally designed to be shared between threads.
unsafe impl Send for Fence {}
// SAFETY: Fences are literally designed to be shared between threads.
unsafe impl Sync for Fence {}

impl Fence {
    /// Check whether the fence was signaled at the moment of the function call.
    ///
    /// Note that this can return `true` for a [`Fence`] whose [`DriverFence`]
    /// has not yet been dropped. The reason is that the fence ops callbacks can
    /// cause the fence to get signaled by the C backend.
    #[inline]
    pub fn is_signaled(&self) -> bool {
        // We should not use `dma_fence_is_signaled_locked()` here, because
        // according to the C backend's recommendations, that function is
        // problematic and we should avoid calling that function with a lock
        // held.

        // SAFETY: Inner `fence` is valid because `self` is valid.
        let ret = unsafe { bindings::dma_fence_is_signaled(self.as_raw()) };

        // To be as robust as possible for the future we guarantee that an API
        // caller can 100% rely on the signaling being completed (i.e., all
        // fence callbacks ran), so we have to take the lock.
        //
        // The reason is that the C dma_fence backend currently does not
        // carefully synchronize the `dma_fence_is_signaled()` function with the
        // proper spinlock. This can lead to the function returning `true` while
        // fence callbacks are still being executed. This can be mitigated by
        // guarding the entire function with the spinlock.
        //
        // The fundamental reason is that the C backend currently does guard
        // setting of the fence's signaled-bit with the fence's spinlock, but
        // reading is done locklessly.
        //
        // See commit c8a5d5ea3ba6a.
        let _ = self.lock();

        ret
    }

    /// Lock the fence. A helper only to be used internally in this module.
    fn lock(&self) -> FenceGuard<'_> {
        let mut guard = FenceGuard {
            inner: self,
            flags: 0,
        };

        // SAFETY: `fence` is valid because `self` is valid. `flag_ptr` is
        // merely a pointer to an integer, whose lifetime is tied to the guard
        // object.
        unsafe { bindings::dma_fence_lock_irqsave(self.as_raw(), &raw mut guard.flags) };

        guard
    }

    /// Get the fence's sequence number.
    #[inline]
    pub fn seqno(&self) -> u64 {
        // SAFETY: Valid because `self` is valid.
        unsafe { (*self.as_raw()).seqno }
    }

    fn as_raw(&self) -> *mut bindings::dma_fence {
        self.inner.get()
    }

    /// Create a [`Fence`] from a raw C [`bindings::dma_fence`].
    ///
    /// # Safety
    ///
    /// `ptr` must point to an initialized fence that is embedded into a [`Fence`].
    #[inline]
    pub unsafe fn from_raw<'a>(ptr: *mut bindings::dma_fence) -> &'a Self {
        // SAFETY: Safe as per the function's overall safety requirements.
        unsafe { &*ptr.cast() }
    }
}

// SAFETY: These implement the C backends refcounting methods which are proven
// to work correctly.
unsafe impl AlwaysRefCounted for Fence {
    fn inc_ref(&self) {
        // SAFETY: `self.as_raw()` is a pointer to a valid `struct dma_fence`.
        unsafe { bindings::dma_fence_get(self.as_raw()) }
    }

    unsafe fn dec_ref(ptr: NonNull<Self>) {
        // SAFETY: `ptr` is never a NULL pointer; and when `dec_ref()` is called
        // the fence is by definition still valid.
        let fence = unsafe { (*ptr.as_ptr()).inner.get() };

        // SAFETY: `fence` was created validly above. When `dec_ref()` is called,
        // there is by definition still a reference alive that can be put.
        unsafe { bindings::dma_fence_put(fence) }
    }
}

// Necessary to guarantee that `inner` always comes first and can be freed by C.
// Also useful for using casts instead of container_of().
#[repr(C)]
#[pin_data]
struct DriverFenceData<'a, T: Send + Sync + FenceContextOps> {
    #[pin]
    /// The inner fence.
    // Must always be the first member so that unsafe casting works; but also
    // necessary so that the C backend can free the allocation (coming from our
    // Rust code) with kfree_rcu().
    inner: Fence,
    /// Callback head for dropping this in a deferred manner through RCU.
    rcu_head: bindings::callback_head,
    /// Reference to access the FenceContext.
    fctx: &'a FenceContext<T>,
    /// The API user's data. It is essential that the data only performs
    /// operations legal in atomic context in its [`Drop`] implementation.
    #[pin]
    data: T::FenceDataType,
}

/// A synchronization primitive mainly for GPU drivers.
///
/// The Rust DMA fence implementation has a dualistic design: [`DriverFence`]s
/// are the producer-side, intended to be always owned by only one party. That
/// party has the monopoly on signaling the fence.
///
/// A [`Fence`] is the counterpart for consumers. Thus, [`Fence`]s are always
/// refcounted and can be shared with an arbitrary number of parties, including
/// userspace. A [`Fence`] can only be used for actions such as checking the
/// fence's status or for registering callbacks on it.
///
/// Once the associated [`DriverFence`] signals, all
/// [`FenceCallbackRegistration`]s registered on a [`Fence`] will be executed.
///
/// A [`Fence`] can arbitrarily outlive its [`DriverFence`] and the
/// [`FenceContext`]. Signaling a [`DriverFence`] decouples it from its
/// [`Fence`]s.
///
/// It is crucial that a [`DriverFence`] always correctly represents the state
/// of the associated job on the hardware. Especially, it is strictly necessary
/// that the owner ensures that all [`DriverFence`]s eventually get signaled.
/// As a last resort, a [`DriverFence`] will signal itself if it drops
/// unsignaled and print a warning.
///
/// This design intends to implement the [`bindings::dma_fence_ops`] in such a
/// way that the driver-data necessary to implement the callback's functionality
/// resides in the [`FenceContext`]. Thus, a [`DriverFence`] contains a
/// reference to the context, which can be accessed in the callbacks. The
/// implementation, therefore, ensures that a [`DriverFence`] cannot outlive its
/// [`FenceContext`]. Unfortunately, this can be circumvented under certain
/// circumstances in Rust (e.g., usage of [`core::mem::forget`]).
///
/// In the unlikely case of such violations, a panic is thrown.
///
/// # Examples
///
/// ```
/// use kernel::{
///     dma_buf::{
///         DriverFence,
///         FenceContext,
///         FenceContextOps,
///         FenceCallback,
///         FenceCallbackRegistration,
///     },
///     str::CString,
///     sync::aref::ARef, //
/// };
/// use core::fmt::Display;
///
/// struct CallbackData { }
///
/// impl FenceCallback for CallbackData {
///     fn on_signal(&mut self) {
///         pr_info!("DmaFence callback executed.\n");
///     }
/// }
///
/// #[pin_data]
/// struct FenceContextData {}
///
/// impl FenceContextData {
///     fn new() -> impl PinInit<Self> {
///         pin_init!(Self {})
///     }
/// }
///
/// impl FenceContextOps for FenceContextData {
///     type FenceDataType = FenceData;
/// }
///
/// let fctx_data = FenceContextData::new();
///
///
/// let mut fctx = KBox::pin_init(
///     FenceContext::new(0, c"dummy_driver", c"dummy_timeline", fctx_data),
///     GFP_KERNEL
/// )?;
///
/// struct FenceData {
///     data: CString,
/// }
///
/// let fence_data = FenceData { data: c"dummy_data".try_into()? };
///
/// let fence_alloc = fctx.new_fence_allocation(fence_data)?;
/// let mut fence = fence_alloc.new_fence();
///
/// let cb_data = CallbackData { };
/// let waiting_fence = ARef::from(fence.as_fence());
/// let cb_reg = FenceCallbackRegistration::new(&waiting_fence, cb_data);
/// let cb_reg = KBox::pin_init(cb_reg, GFP_KERNEL)?;
///
/// // TODO signalling guards
/// assert_eq!(waiting_fence.is_signaled(), false);
/// fence.signal(Ok(()));
/// assert_eq!(waiting_fence.is_signaled(), true);
///
/// Ok::<(), Error>(())
/// ```
pub struct DriverFence<'a, T: Send + Sync + FenceContextOps> {
    /// The actual content of the fence. Lives in a [`NonNull`] so that its
    /// memory can be managed independently. Valid until both the [`DriverFence`]
    /// and all associated [`Fence`]s have disappeared.
    data: NonNull<DriverFenceData<'a, T>>,
}

/// A pre-prepared DMA fence, carrying the user's data and the memory it and the
/// fence reside in. Only useful for creating a [`DriverFence`]. Splitting
/// allocation and full initialization is necessary because fences cannot be
/// allocated dynamically in some circumstances (deadlock).
pub struct DriverFenceAllocation<'a, T: Send + Sync + FenceContextOps> {
    /// The memory for the actual content of the fence.
    /// Handed over to a [`DriverFence`], or deallocated once the
    /// [`DriverFenceAllocation`] drops.
    data: KBox<DriverFenceData<'a, T>>,
    /// Reference for the ops for the associated [`FenceContext`]
    ops: &'static bindings::dma_fence_ops,
}

impl<'a, T: Send + Sync + FenceContextOps> DriverFenceAllocation<'a, T> {
    /// Create a new [`DriverFence`], the signalable counterpart of a [`Fence`].
    ///
    /// This increments the sequence number in the associated [`FenceContext`].
    pub fn new_fence(self) -> DriverFence<'a, T> {
        // We feed the C dma_fence backend a NULL for the spinlock so that it
        // uses per-fence locks automatically.
        let null_ptr: *mut bindings::spinlock = ptr::null_mut();
        let seqno = self.data.fctx.next_seqno();
        let fence_ptr = self.as_raw();
        // SAFETY: `fence_ptr` has been created directly above. It will live
        // at least as long as `Self`. The same applies to `&Self::OPS`.
        unsafe {
            bindings::dma_fence_init(fence_ptr, self.ops, null_ptr, self.data.fctx.nr, seqno)
        };

        self.data.fctx.nr_of_unsignaled_fences.fetch_add(1, Relaxed);

        // A `DriverFenceAllocation`'s purpose is to carry allocated memory, so
        // that `DriverFence`s can always be created without allocating. In this
        // method, ownership over that memory is transferred to the new
        // `DriverFence` and managed through refcounting. The C dma_fence
        // backend will ultimately free the memory once the refcount reaches 0.
        let ptr = KBox::into_raw(self.data);
        // SAFETY: `ptr` was just created validly directly above.
        let ptr = unsafe { NonNull::new_unchecked(ptr) };

        DriverFence { data: ptr }
    }

    fn as_raw(&self) -> *mut bindings::dma_fence {
        self.data.inner.inner.get()
    }
}

impl<'a, T: Send + Sync + FenceContextOps> DriverFence<'a, T> {
    fn as_raw(&self) -> *mut bindings::dma_fence {
        // SAFETY: Valid because `self` is valid.
        let fence_data = unsafe { &*self.data.as_ptr() };

        fence_data.inner.inner.get()
    }

    /// Create a [`DriverFence`] from a raw pointer to a [`bindings::dma_fence`].
    ///
    /// # Safety
    ///
    /// `ptr` must be a valid pointer to a `dma_fence` that was obtained through
    /// a [`DriverFence`] with matching generic data for both fence and associated
    /// [`FenceContext`].
    unsafe fn from_raw(ptr: *mut bindings::dma_fence) -> Self {
        let opaque_fence = Opaque::cast_from(ptr);

        // SAFETY: Safe due to the function's overall safety requirements.
        let fence_ptr = unsafe { container_of!(opaque_fence, Fence, inner) };

        // DriverFenceData is `repr(C)` and a Fence is its first member.
        let fence_data_ptr = fence_ptr as *mut DriverFenceData<'a, T>;

        // SAFETY: `fence_data_ptr` was created validly above.
        let data = unsafe { NonNull::new_unchecked(fence_data_ptr) };

        Self { data }
    }

    /// Return the underlying [`Fence`].
    #[inline]
    pub fn as_fence(&self) -> &Fence {
        // SAFETY: `self` is by definition still valid, and it cannot drop until
        // this new reference is gone.
        unsafe { Fence::from_raw(self.as_raw()) }
    }

    /// Signal the fence. This will invoke all registered callbacks.
    pub fn signal(self, res: Result) {
        let fence = self.as_fence().lock();

        // SAFETY: `fence` is valid because `self` is valid. The lock must be
        // held, which we acquired directly above.
        if !unsafe { bindings::dma_fence_test_signaled_flag(fence.as_raw()) } {
            if let Err(err) = res {
                // SAFETY: `fence` is valid because `self` is valid. The fence
                // must not have been signaled yet, which we check directly above.
                unsafe { bindings::dma_fence_set_error(fence.as_raw(), err.to_errno()) };
            }
            // SAFETY: `fence` is valid because `self` is valid. The lock must
            // be held, which we acquired above.
            unsafe { bindings::dma_fence_signal_locked(fence.as_raw()) };
        }

        // SAFETY: `self.data` is valid because `self` is valid.
        let fctx = unsafe { self.data.as_ref().fctx };
        let _ = fctx.nr_of_unsignaled_fences.fetch_sub(1, Relaxed);
    }
}

// SAFETY: Fences are literally designed to be shared between threads.
unsafe impl<'a, T: Send + Sync + FenceContextOps> Send for DriverFence<'a, T> {}
// SAFETY: Fences are literally designed to be shared between threads.
unsafe impl<'a, T: Send + Sync + FenceContextOps> Sync for DriverFence<'a, T> {}

impl<'a, T: Send + Sync + FenceContextOps> Deref for DriverFence<'a, T> {
    type Target = T::FenceDataType;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Thanks to refcounting, `data` is always valid as long as `self` is.
        let data = unsafe { &*self.data.as_ptr() };

        &data.data
    }
}

/// A borrow wrapper for [`DriverFence`]. Implements [`Deref`].
pub struct DriverFenceBorrow<'a, T: Send + Sync + FenceContextOps> {
    driver_fence: ManuallyDrop<DriverFence<'a, T>>,
    _lifetime: PhantomData<&'a T>,
}

impl<'a, T: Send + Sync + FenceContextOps> Deref for DriverFenceBorrow<'a, T> {
    type Target = DriverFence<'a, T>;

    fn deref(&self) -> &Self::Target {
        self.driver_fence.deref()
    }
}

// SAFETY: The Rust dma_fence abstractions are already designed around the inner
// C `dma_fence`, which can serve safely as the identification point when being
// owned by C. Moreover, safety is ensured by not dropping `DriverFence` and by
// only allowing operations without side effects on the Borrowed type.
unsafe impl<T: Send + Sync + FenceContextOps> ForeignOwnable for DriverFence<'_, T> {
    type Borrowed<'a>
        = DriverFenceBorrow<'a, T>
    where
        Self: 'a;
    type BorrowedMut<'a>
        = DriverFenceBorrow<'a, T>
    where
        Self: 'a;

    const FOREIGN_ALIGN: usize = core::mem::align_of::<bindings::dma_fence>();

    fn into_foreign(self) -> *mut c_void {
        let fence = self;

        let ptr = fence.as_raw();

        // DriverFence must not drop.
        let _ = ManuallyDrop::new(fence);

        ptr.cast()
    }

    unsafe fn from_foreign(ptr: *mut c_void) -> Self {
        // SAFETY: Safe because the trait implementation only invokes this with
        // a valid `ptr`, associated to a `DriverFence` with matching generic data.
        unsafe { Self::from_raw(ptr.cast()) }
    }

    unsafe fn borrow<'a>(ptr: *mut c_void) -> Self::Borrowed<'a>
    where
        Self: 'a,
    {
        // SAFETY: The trait implementation ensures that `ptr` always resides
        // within a [`Fence`] within a [`DriverFenceData`].
        let driver_fence = unsafe { Self::from_raw(ptr.cast()) };

        let driver_fence = ManuallyDrop::new(driver_fence);

        DriverFenceBorrow {
            driver_fence,
            _lifetime: PhantomData,
        }
    }

    unsafe fn borrow_mut<'a>(ptr: *mut c_void) -> Self::BorrowedMut<'a>
    // FIXME: The bound below and the one above in `borrow` should actually be
    // unnecessary since the compiler should be able to completely derive all
    // necessary information automatically. There is currently a compiler bug
    // preventing that, though:
    //
    // https://github.com/rust-lang/rust/issues/155430.
    //
    // (Help to) fix the compiler bug and remove the bounds afterwards.
    where
        Self: 'a,
    {
        // SAFETY: The trait implementation ensures that `ptr` always resides
        // within a [`Fence`] within a [`DriverFenceData`].
        let driver_fence = unsafe { Self::from_raw(ptr.cast()) };

        let driver_fence = ManuallyDrop::new(driver_fence);

        DriverFenceBorrow {
            driver_fence,
            _lifetime: PhantomData,
        }
    }
}

impl<'a, T: Send + Sync + FenceContextOps> Drop for DriverFence<'a, T> {
    fn drop(&mut self) {
        let guard = self.as_fence().lock();

        // Use dma_fence_test_signaled_flag() instead of
        // dma_fence_is_signaled_locked() because the C backend wants to get rid
        // of the latter.

        // SAFETY: `guard` is valid until the `call_rcu()` below.
        let signaled: bool = unsafe { bindings::dma_fence_test_signaled_flag(guard.as_raw()) };
        if !signaled {
            pr_err!("DriverFence drops unsignaled. Danger of memory corruption!\n");
            // SAFETY: `guard` is valid until the `call_rcu()` below. The fence
            // must not have been signaled yet, which we check directly above.
            unsafe { bindings::dma_fence_set_error(guard.as_raw(), ECANCELED.to_errno()) };
            // SAFETY: `guard` is valid until the `call_rcu()` below. The lock
            // must be held, which we acquired above.
            unsafe { bindings::dma_fence_signal_locked(guard.as_raw()) };

            // SAFETY: `self.data` is valid because `self` is valid.
            let fctx = unsafe { self.data.as_ref().fctx };
            let _ = fctx.nr_of_unsignaled_fences.fetch_sub(1, Relaxed);
        }
        drop(guard);

        // `DriverFenceData` could be accessed through some dma_fence
        // callbacks right now. Access is being revoked in principle above by
        // signaling the fence, but since the C backend does not guarantee
        // perfect full synchronization, we have to wait for one grace period to
        // ensure that all accessors of `DriverFenceData` (through the
        // dma_fence_ops accessible through a `Fence`) are gone.

        if !core::mem::needs_drop::<T::FenceDataType>() {
            // SAFETY: Once a `DriverFence` is initialized, the inner `fence` is
            // valid and initialized. It is valid until the refcount drops to 0,
            // which can earliest happen once we drop the `DriverFence`'s
            // reference here.
            unsafe { bindings::dma_fence_put(self.as_raw()) };
            return;
        }

        // SAFETY: Valid because `self` is valid.
        let rcu_head_ptr = unsafe { &raw mut (*self.data.as_ptr()).rcu_head };

        // SAFETY: `call_rcu()` is always safe to be called. `rcu_head_ptr` was
        // created validly above. The module must perform a `synchronize_rcu()`
        // or `rcu_barrier()` call to guard against module unload.
        unsafe { bindings::call_rcu(rcu_head_ptr, Some(drop_driver_fence_data::<T>)) };
    }
}

// TODO:
// The entire call_rcu() mechanism in the drop above and the code below would be
// unnecessary if C's dma_fence_signal() could be reworked in a way that after it
// ran, the caller knows that no fence_ops callbacks can be running anymore.
// In other words, if the dma_fence backend would use its spinlock for full
// synchronization.
//
// Then we could move the drop_in_place() and dma_fence_put() upwards into the
// drop() implementation and call it a day.

/// Finally really drop this `DriverFence<T>`
///
/// # Safety
///
/// `head` references the `rcu_head` field of an `DriverFenceData<T>`. All
/// accessors to that `DriverFenceData<T>` must be gone by now. This must be
/// ensured by signalling the associated `DriverFence<T>` and then waiting
/// for a grace period until calling this function here.
unsafe extern "C" fn drop_driver_fence_data<T: Send + Sync + FenceContextOps>(
    head: *mut bindings::callback_head,
) {
    // SAFETY: Caller provides a pointer to the `rcu_head` field of a `DriverFenceData<C>`.
    let fence_data = unsafe { container_of!(head, DriverFenceData<'_, T>, rcu_head) };

    // SAFETY: `fence_data` was created validly above. All the fence's data will
    // only drop below, but the raw pointer to the raw C `dma_fence` remains
    // valid because the reference count is only decremented at the end of the
    // function.
    let fence = unsafe { (*fence_data).inner.inner.get() };

    // SAFETY: `fence_data` was created validly above. The user has already
    // dropped the only conventional accessor to the user data, the `DriverFence`,
    // one grace period ago. All accessors are gone now.
    unsafe { drop_in_place(&raw mut (*fence_data).data) };

    // The inner `Fence` explicitly does not get dropped because there may be
    // many more users / consumers, each holding their own reference.

    // SAFETY: Once a `DriverFence` is initialized, the inner `fence` is valid
    // and initialized. It is valid until the refcount drops to 0, which can
    // earliest happen once we drop the `DriverFence`'s reference here.
    unsafe { bindings::dma_fence_put(fence) };

    // The actual memory the data associated with a `DriverFence` lives in
    // gets freed by the C dma_fence backend once the fence's refcount reaches 0.
}
