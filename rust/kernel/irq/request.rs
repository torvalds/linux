// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright 2025 Collabora ltd.

//! This module provides types like [`Registration`] which allow users to
//! register handlers for a given IRQ line.

use core::marker::PhantomPinned;

use crate::alloc::Allocator;
use crate::device::{Bound, Device};
use crate::devres::Devres;
use crate::error::to_result;
use crate::irq::flags::Flags;
use crate::prelude::*;
use crate::str::CStr;
use crate::sync::Arc;

/// The value that can be returned from a [`Handler`] or a `ThreadedHandler`.
#[repr(u32)]
pub enum IrqReturn {
    /// The interrupt was not from this device or was not handled.
    None = bindings::irqreturn_IRQ_NONE,

    /// The interrupt was handled by this device.
    Handled = bindings::irqreturn_IRQ_HANDLED,
}

/// Callbacks for an IRQ handler.
pub trait Handler: Sync {
    /// The hard IRQ handler.
    ///
    /// This is executed in interrupt context, hence all corresponding
    /// limitations do apply.
    ///
    /// All work that does not necessarily need to be executed from
    /// interrupt context, should be deferred to a threaded handler.
    /// See also `ThreadedRegistration`.
    fn handle(&self) -> IrqReturn;
}

impl<T: ?Sized + Handler + Send> Handler for Arc<T> {
    fn handle(&self) -> IrqReturn {
        T::handle(self)
    }
}

impl<T: ?Sized + Handler, A: Allocator> Handler for Box<T, A> {
    fn handle(&self) -> IrqReturn {
        T::handle(self)
    }
}

/// # Invariants
///
/// - `self.irq` is the same as the one passed to `request_{threaded}_irq`.
/// - `cookie` was passed to `request_{threaded}_irq` as the cookie. It is guaranteed to be unique
///   by the type system, since each call to `new` will return a different instance of
///   `Registration`.
#[pin_data(PinnedDrop)]
struct RegistrationInner {
    irq: u32,
    cookie: *mut c_void,
}

impl RegistrationInner {
    fn synchronize(&self) {
        // SAFETY: safe as per the invariants of `RegistrationInner`
        unsafe { bindings::synchronize_irq(self.irq) };
    }
}

#[pinned_drop]
impl PinnedDrop for RegistrationInner {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY:
        //
        // Safe as per the invariants of `RegistrationInner` and:
        //
        // - The containing struct is `!Unpin` and was initialized using
        // pin-init, so it occupied the same memory location for the entirety of
        // its lifetime.
        //
        // Notice that this will block until all handlers finish executing,
        // i.e.: at no point will &self be invalid while the handler is running.
        unsafe { bindings::free_irq(self.irq, self.cookie) };
    }
}

// SAFETY: We only use `inner` on drop, which called at most once with no
// concurrent access.
unsafe impl Sync for RegistrationInner {}

// SAFETY: It is safe to send `RegistrationInner` across threads.
unsafe impl Send for RegistrationInner {}

/// A request for an IRQ line for a given device.
///
/// # Invariants
///
/// - `ìrq` is the number of an interrupt source of `dev`.
/// - `irq` has not been registered yet.
pub struct IrqRequest<'a> {
    dev: &'a Device<Bound>,
    irq: u32,
}

impl<'a> IrqRequest<'a> {
    /// Creates a new IRQ request for the given device and IRQ number.
    ///
    /// # Safety
    ///
    /// - `irq` should be a valid IRQ number for `dev`.
    #[expect(dead_code)]
    pub(crate) unsafe fn new(dev: &'a Device<Bound>, irq: u32) -> Self {
        // INVARIANT: `irq` is a valid IRQ number for `dev`.
        IrqRequest { dev, irq }
    }

    /// Returns the IRQ number of an [`IrqRequest`].
    pub fn irq(&self) -> u32 {
        self.irq
    }
}

/// A registration of an IRQ handler for a given IRQ line.
///
/// # Examples
///
/// The following is an example of using `Registration`. It uses a
/// [`Completion`] to coordinate between the IRQ
/// handler and process context. [`Completion`] uses interior mutability, so the
/// handler can signal with [`Completion::complete_all()`] and the process
/// context can wait with [`Completion::wait_for_completion()`] even though
/// there is no way to get a mutable reference to the any of the fields in
/// `Data`.
///
/// [`Completion`]: kernel::sync::Completion
/// [`Completion::complete_all()`]: kernel::sync::Completion::complete_all
/// [`Completion::wait_for_completion()`]: kernel::sync::Completion::wait_for_completion
///
/// ```
/// use kernel::c_str;
/// use kernel::device::Bound;
/// use kernel::irq::{self, Flags, IrqRequest, IrqReturn, Registration};
/// use kernel::prelude::*;
/// use kernel::sync::{Arc, Completion};
///
/// // Data shared between process and IRQ context.
/// #[pin_data]
/// struct Data {
///     #[pin]
///     completion: Completion,
/// }
///
/// impl irq::Handler for Data {
///     // Executed in IRQ context.
///     fn handle(&self) -> IrqReturn {
///         self.completion.complete_all();
///         IrqReturn::Handled
///     }
/// }
///
/// // Registers an IRQ handler for the given IrqRequest.
/// //
/// // This runs in process context and assumes `request` was previously acquired from a device.
/// fn register_irq(
///     handler: impl PinInit<Data, Error>,
///     request: IrqRequest<'_>,
/// ) -> Result<Arc<Registration<Data>>> {
///     let registration = Registration::new(request, Flags::SHARED, c_str!("my_device"), handler);
///
///     let registration = Arc::pin_init(registration, GFP_KERNEL)?;
///
///     registration.handler().completion.wait_for_completion();
///
///     Ok(registration)
/// }
/// # Ok::<(), Error>(())
/// ```
///
/// # Invariants
///
/// * We own an irq handler using `&self.handler` as its private data.
#[pin_data]
pub struct Registration<T: Handler + 'static> {
    #[pin]
    inner: Devres<RegistrationInner>,

    #[pin]
    handler: T,

    /// Pinned because we need address stability so that we can pass a pointer
    /// to the callback.
    #[pin]
    _pin: PhantomPinned,
}

impl<T: Handler + 'static> Registration<T> {
    /// Registers the IRQ handler with the system for the given IRQ number.
    pub fn new<'a>(
        request: IrqRequest<'a>,
        flags: Flags,
        name: &'static CStr,
        handler: impl PinInit<T, Error> + 'a,
    ) -> impl PinInit<Self, Error> + 'a {
        try_pin_init!(&this in Self {
            handler <- handler,
            inner <- Devres::new(
                request.dev,
                try_pin_init!(RegistrationInner {
                    // SAFETY: `this` is a valid pointer to the `Registration` instance
                    cookie: unsafe { &raw mut (*this.as_ptr()).handler }.cast(),
                    irq: {
                        // SAFETY:
                        // - The callbacks are valid for use with request_irq.
                        // - If this succeeds, the slot is guaranteed to be valid until the
                        //   destructor of Self runs, which will deregister the callbacks
                        //   before the memory location becomes invalid.
                        to_result(unsafe {
                            bindings::request_irq(
                                request.irq,
                                Some(handle_irq_callback::<T>),
                                flags.into_inner(),
                                name.as_char_ptr(),
                                (&raw mut (*this.as_ptr()).handler).cast(),
                            )
                        })?;
                        request.irq
                    }
                })
            ),
            _pin: PhantomPinned,
        })
    }

    /// Returns a reference to the handler that was registered with the system.
    pub fn handler(&self) -> &T {
        &self.handler
    }

    /// Wait for pending IRQ handlers on other CPUs.
    ///
    /// This will attempt to access the inner [`Devres`] container.
    pub fn try_synchronize(&self) -> Result {
        let inner = self.inner.try_access().ok_or(ENODEV)?;
        inner.synchronize();
        Ok(())
    }

    /// Wait for pending IRQ handlers on other CPUs.
    pub fn synchronize(&self, dev: &Device<Bound>) -> Result {
        let inner = self.inner.access(dev)?;
        inner.synchronize();
        Ok(())
    }
}

/// # Safety
///
/// This function should be only used as the callback in `request_irq`.
unsafe extern "C" fn handle_irq_callback<T: Handler>(_irq: i32, ptr: *mut c_void) -> c_uint {
    // SAFETY: `ptr` is a pointer to T set in `Registration::new`
    let handler = unsafe { &*(ptr as *const T) };
    T::handle(handler) as c_uint
}
