// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright 2025 Collabora ltd.

//! This module provides types like [`Registration`] and
//! [`ThreadedRegistration`], which allow users to register handlers for a given
//! IRQ line.

use core::marker::PhantomPinned;

use crate::alloc::Allocator;
use crate::device::{Bound, Device};
use crate::devres::Devres;
use crate::error::to_result;
use crate::irq::flags::Flags;
use crate::prelude::*;
use crate::str::CStr;
use crate::sync::Arc;

/// The value that can be returned from a [`Handler`] or a [`ThreadedHandler`].
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
    /// See also [`ThreadedRegistration`].
    fn handle(&self, device: &Device<Bound>) -> IrqReturn;
}

impl<T: ?Sized + Handler + Send> Handler for Arc<T> {
    fn handle(&self, device: &Device<Bound>) -> IrqReturn {
        T::handle(self, device)
    }
}

impl<T: ?Sized + Handler, A: Allocator> Handler for Box<T, A> {
    fn handle(&self, device: &Device<Bound>) -> IrqReturn {
        T::handle(self, device)
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
/// use kernel::device::{Bound, Device};
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
///     fn handle(&self, _dev: &Device<Bound>) -> IrqReturn {
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
/// * We own an irq handler whose cookie is a pointer to `Self`.
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
                    // INVARIANT: `this` is a valid pointer to the `Registration` instance
                    cookie: this.as_ptr().cast::<c_void>(),
                    irq: {
                        // SAFETY:
                        // - The callbacks are valid for use with request_irq.
                        // - If this succeeds, the slot is guaranteed to be valid until the
                        //   destructor of Self runs, which will deregister the callbacks
                        //   before the memory location becomes invalid.
                        // - When request_irq is called, everything that handle_irq_callback will
                        //   touch has already been initialized, so it's safe for the callback to
                        //   be called immediately.
                        to_result(unsafe {
                            bindings::request_irq(
                                request.irq,
                                Some(handle_irq_callback::<T>),
                                flags.into_inner(),
                                name.as_char_ptr(),
                                this.as_ptr().cast::<c_void>(),
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
    // SAFETY: `ptr` is a pointer to `Registration<T>` set in `Registration::new`
    let registration = unsafe { &*(ptr as *const Registration<T>) };
    // SAFETY: The irq callback is removed before the device is unbound, so the fact that the irq
    // callback is running implies that the device has not yet been unbound.
    let device = unsafe { registration.inner.device().as_bound() };

    T::handle(&registration.handler, device) as c_uint
}

/// The value that can be returned from [`ThreadedHandler::handle`].
#[repr(u32)]
pub enum ThreadedIrqReturn {
    /// The interrupt was not from this device or was not handled.
    None = bindings::irqreturn_IRQ_NONE,

    /// The interrupt was handled by this device.
    Handled = bindings::irqreturn_IRQ_HANDLED,

    /// The handler wants the handler thread to wake up.
    WakeThread = bindings::irqreturn_IRQ_WAKE_THREAD,
}

/// Callbacks for a threaded IRQ handler.
pub trait ThreadedHandler: Sync {
    /// The hard IRQ handler.
    ///
    /// This is executed in interrupt context, hence all corresponding
    /// limitations do apply. All work that does not necessarily need to be
    /// executed from interrupt context, should be deferred to the threaded
    /// handler, i.e. [`ThreadedHandler::handle_threaded`].
    ///
    /// The default implementation returns [`ThreadedIrqReturn::WakeThread`].
    #[expect(unused_variables)]
    fn handle(&self, device: &Device<Bound>) -> ThreadedIrqReturn {
        ThreadedIrqReturn::WakeThread
    }

    /// The threaded IRQ handler.
    ///
    /// This is executed in process context. The kernel creates a dedicated
    /// `kthread` for this purpose.
    fn handle_threaded(&self, device: &Device<Bound>) -> IrqReturn;
}

impl<T: ?Sized + ThreadedHandler + Send> ThreadedHandler for Arc<T> {
    fn handle(&self, device: &Device<Bound>) -> ThreadedIrqReturn {
        T::handle(self, device)
    }

    fn handle_threaded(&self, device: &Device<Bound>) -> IrqReturn {
        T::handle_threaded(self, device)
    }
}

impl<T: ?Sized + ThreadedHandler, A: Allocator> ThreadedHandler for Box<T, A> {
    fn handle(&self, device: &Device<Bound>) -> ThreadedIrqReturn {
        T::handle(self, device)
    }

    fn handle_threaded(&self, device: &Device<Bound>) -> IrqReturn {
        T::handle_threaded(self, device)
    }
}

/// A registration of a threaded IRQ handler for a given IRQ line.
///
/// Two callbacks are required: one to handle the IRQ, and one to handle any
/// other work in a separate thread.
///
/// The thread handler is only called if the IRQ handler returns
/// [`ThreadedIrqReturn::WakeThread`].
///
/// # Examples
///
/// The following is an example of using [`ThreadedRegistration`]. It uses a
/// [`Mutex`](kernel::sync::Mutex) to provide interior mutability.
///
/// ```
/// use kernel::c_str;
/// use kernel::device::{Bound, Device};
/// use kernel::irq::{
///   self, Flags, IrqRequest, IrqReturn, ThreadedHandler, ThreadedIrqReturn,
///   ThreadedRegistration,
/// };
/// use kernel::prelude::*;
/// use kernel::sync::{Arc, Mutex};
///
/// // Declare a struct that will be passed in when the interrupt fires. The u32
/// // merely serves as an example of some internal data.
/// //
/// // [`irq::ThreadedHandler::handle`] takes `&self`. This example
/// // illustrates how interior mutability can be used when sharing the data
/// // between process context and IRQ context.
/// #[pin_data]
/// struct Data {
///     #[pin]
///     value: Mutex<u32>,
/// }
///
/// impl ThreadedHandler for Data {
///     // This will run (in a separate kthread) if and only if
///     // [`ThreadedHandler::handle`] returns [`WakeThread`], which it does by
///     // default.
///     fn handle_threaded(&self, _dev: &Device<Bound>) -> IrqReturn {
///         let mut data = self.value.lock();
///         *data += 1;
///         IrqReturn::Handled
///     }
/// }
///
/// // Registers a threaded IRQ handler for the given [`IrqRequest`].
/// //
/// // This is executing in process context and assumes that `request` was
/// // previously acquired from a device.
/// fn register_threaded_irq(
///     handler: impl PinInit<Data, Error>,
///     request: IrqRequest<'_>,
/// ) -> Result<Arc<ThreadedRegistration<Data>>> {
///     let registration =
///         ThreadedRegistration::new(request, Flags::SHARED, c_str!("my_device"), handler);
///
///     let registration = Arc::pin_init(registration, GFP_KERNEL)?;
///
///     {
///         // The data can be accessed from process context too.
///         let mut data = registration.handler().value.lock();
///         *data += 1;
///     }
///
///     Ok(registration)
/// }
/// # Ok::<(), Error>(())
/// ```
///
/// # Invariants
///
/// * We own an irq handler whose cookie is a pointer to `Self`.
#[pin_data]
pub struct ThreadedRegistration<T: ThreadedHandler + 'static> {
    #[pin]
    inner: Devres<RegistrationInner>,

    #[pin]
    handler: T,

    /// Pinned because we need address stability so that we can pass a pointer
    /// to the callback.
    #[pin]
    _pin: PhantomPinned,
}

impl<T: ThreadedHandler + 'static> ThreadedRegistration<T> {
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
                    // INVARIANT: `this` is a valid pointer to the `ThreadedRegistration` instance.
                    cookie: this.as_ptr().cast::<c_void>(),
                    irq: {
                        // SAFETY:
                        // - The callbacks are valid for use with request_threaded_irq.
                        // - If this succeeds, the slot is guaranteed to be valid until the
                        //   destructor of Self runs, which will deregister the callbacks
                        //   before the memory location becomes invalid.
                        // - When request_threaded_irq is called, everything that the two callbacks
                        //   will touch has already been initialized, so it's safe for the
                        //   callbacks to be called immediately.
                        to_result(unsafe {
                            bindings::request_threaded_irq(
                                request.irq,
                                Some(handle_threaded_irq_callback::<T>),
                                Some(thread_fn_callback::<T>),
                                flags.into_inner(),
                                name.as_char_ptr(),
                                this.as_ptr().cast::<c_void>(),
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
/// This function should be only used as the callback in `request_threaded_irq`.
unsafe extern "C" fn handle_threaded_irq_callback<T: ThreadedHandler>(
    _irq: i32,
    ptr: *mut c_void,
) -> c_uint {
    // SAFETY: `ptr` is a pointer to `ThreadedRegistration<T>` set in `ThreadedRegistration::new`
    let registration = unsafe { &*(ptr as *const ThreadedRegistration<T>) };
    // SAFETY: The irq callback is removed before the device is unbound, so the fact that the irq
    // callback is running implies that the device has not yet been unbound.
    let device = unsafe { registration.inner.device().as_bound() };

    T::handle(&registration.handler, device) as c_uint
}

/// # Safety
///
/// This function should be only used as the callback in `request_threaded_irq`.
unsafe extern "C" fn thread_fn_callback<T: ThreadedHandler>(_irq: i32, ptr: *mut c_void) -> c_uint {
    // SAFETY: `ptr` is a pointer to `ThreadedRegistration<T>` set in `ThreadedRegistration::new`
    let registration = unsafe { &*(ptr as *const ThreadedRegistration<T>) };
    // SAFETY: The irq callback is removed before the device is unbound, so the fact that the irq
    // callback is running implies that the device has not yet been unbound.
    let device = unsafe { registration.inner.device().as_bound() };

    T::handle_threaded(&registration.handler, device) as c_uint
}
