// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright 2025 Collabora ltd.

//! This module provides types like [`Registration`] and
//! [`ThreadedRegistration`], which allow users to register handlers for a given
//! IRQ line.

use core::marker::{
    PhantomData,
    PhantomPinned, //
};

use crate::{
    device::{
        Bound,
        Device, //
    },
    error::to_result,
    irq::flags::Flags,
    prelude::*,
    str::CStr,
};

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
    fn handle(&self) -> IrqReturn;
}

/// A request for an IRQ line for a given device.
///
/// # Invariants
///
/// - `ìrq` is the number of an interrupt source of `dev`.
/// - `irq` has not been registered yet; this is consumed by [`Registration::new()`].
pub struct IrqRequest<'a> {
    irq: u32,
    /// Proves the device is bound at registration time and ties `'a` to the device's bound
    /// lifetime, ensuring the [`Registration`] cannot outlive it.
    _dev: PhantomData<&'a Device<Bound>>,
}

impl<'a> IrqRequest<'a> {
    /// Creates a new IRQ request for the given device and IRQ number.
    ///
    /// # Safety
    ///
    /// - `irq` should be a valid IRQ number for `dev`.
    pub(crate) unsafe fn new(_dev: &'a Device<Bound>, irq: u32) -> Self {
        // INVARIANT: `irq` is a valid IRQ number for `dev`.
        IrqRequest {
            irq,
            _dev: PhantomData,
        }
    }

    /// Returns the IRQ number of an [`IrqRequest`].
    #[inline]
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
/// use core::pin::Pin;
/// use kernel::{
///     irq::{
///         self,
///         Flags,
///         IrqRequest,
///         IrqReturn,
///         Registration,
///     },
///     prelude::*,
///     sync::Completion,
/// };
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
///     request: IrqRequest<'_>,
/// ) -> Result<Pin<KBox<Registration<'_, Data>>>> {
///     // SAFETY: The returned Registration is not leaked.
///     let registration = unsafe {
///         Registration::new(
///             request,
///             Flags::SHARED,
///             c"my_device",
///             try_pin_init!(Data {
///                 completion <- Completion::new(),
///             }? Error),
///         )
///     };
///
///     let registration = KBox::pin_init(registration, GFP_KERNEL)?;
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
/// * We own an irq handler registered via `request_irq` whose cookie is a pointer to `Self`.
#[pin_data(PinnedDrop)]
pub struct Registration<'a, T: Handler> {
    request: IrqRequest<'a>,

    #[pin]
    handler: T,

    /// Pinned because we need address stability so that we can pass a pointer
    /// to the callback.
    #[pin]
    _pin: PhantomPinned,
}

impl<'a, T: Handler> Registration<'a, T> {
    /// Registers the IRQ handler with the system for the given IRQ number.
    ///
    /// # Safety
    ///
    /// Callers must not `mem::forget()` the returned [`Registration`] or otherwise prevent its
    /// [`Drop`] implementation from running.
    pub unsafe fn new(
        request: IrqRequest<'a>,
        flags: Flags,
        name: &'static CStr,
        handler: impl PinInit<T, Error> + 'a,
    ) -> impl PinInit<Self, Error> + 'a
    where
        T: 'a,
    {
        // INVARIANT: If initialization completes successfully, we own an IRQ handler registered
        // via `request_irq` whose cookie is a pointer to `Self`.
        try_pin_init!(&this in Self {
            handler <- handler,
            request,
            _pin: PhantomPinned,
            _: {
                // SAFETY:
                // - The callbacks are valid for use with request_irq.
                // - If this succeeds, the slot is guaranteed to be valid until the destructor of
                //   Self runs, which will deregister the callbacks before the memory location
                //   becomes invalid.
                // - All fields are already initialized, so it's safe for the callback to be
                //   called immediately.
                to_result(unsafe {
                    bindings::request_irq(
                        request.irq,
                        Some(handle_irq_callback::<T>),
                        flags.into_inner(),
                        name.as_char_ptr(),
                        this.as_ptr().cast::<c_void>(),
                    )
                })?;
            },
        })
    }

    /// Returns a reference to the handler that was registered with the system.
    pub fn handler(&self) -> &T {
        &self.handler
    }

    /// Wait for pending IRQ handlers on other CPUs.
    #[inline]
    pub fn synchronize(&self) {
        // SAFETY: `self.request.irq` is a valid registered IRQ number (type invariant).
        unsafe { bindings::synchronize_irq(self.request.irq) };
    }
}

#[pinned_drop]
impl<T: Handler> PinnedDrop for Registration<'_, T> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: The cookie was set to a pointer to `Self` in `Registration::new()`. This blocks
        // until all in-flight handlers complete, so no references to `self` remain after this
        // returns.
        unsafe {
            bindings::free_irq(
                self.request.irq,
                core::ptr::from_mut::<Self>(self.get_unchecked_mut()).cast::<c_void>(),
            )
        };
    }
}

/// # Safety
///
/// This function should be only used as the callback in `request_irq`.
unsafe extern "C" fn handle_irq_callback<T: Handler>(_irq: i32, ptr: *mut c_void) -> c_uint {
    let ptr = ptr.cast_const().cast::<Registration<'_, T>>();
    // SAFETY: `ptr` is a pointer to `Registration<'_, T>` set in `Registration::new()`.
    let registration = unsafe { &*ptr };

    T::handle(&registration.handler) as c_uint
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
    fn handle(&self) -> ThreadedIrqReturn {
        ThreadedIrqReturn::WakeThread
    }

    /// The threaded IRQ handler.
    ///
    /// This is executed in process context. The kernel creates a dedicated
    /// `kthread` for this purpose.
    fn handle_threaded(&self) -> IrqReturn;
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
/// use core::pin::Pin;
/// use kernel::{
///     irq::{
///         self,
///         Flags,
///         IrqRequest,
///         IrqReturn,
///         ThreadedHandler,
///         ThreadedIrqReturn,
///         ThreadedRegistration,
///     },
///     prelude::*,
///     sync::Mutex,
/// };
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
///     fn handle_threaded(&self) -> IrqReturn {
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
///     request: IrqRequest<'_>,
/// ) -> Result<Pin<KBox<ThreadedRegistration<'_, Data>>>> {
///     // SAFETY: The returned Registration is not leaked.
///     let registration = unsafe {
///         ThreadedRegistration::new(
///             request,
///             Flags::SHARED,
///             c"my_device",
///             try_pin_init!(Data {
///                 value <- kernel::new_mutex!(0),
///             }? Error),
///         )
///     };
///
///     let registration = KBox::pin_init(registration, GFP_KERNEL)?;
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
/// * We own an irq handler registered via `request_threaded_irq` whose cookie is a pointer to
///   `Self`.
#[pin_data(PinnedDrop)]
pub struct ThreadedRegistration<'a, T: ThreadedHandler> {
    request: IrqRequest<'a>,

    #[pin]
    handler: T,

    /// Pinned because we need address stability so that we can pass a pointer
    /// to the callback.
    #[pin]
    _pin: PhantomPinned,
}

impl<'a, T: ThreadedHandler> ThreadedRegistration<'a, T> {
    /// Registers the IRQ handler with the system for the given IRQ number.
    ///
    /// # Safety
    ///
    /// Callers must not `mem::forget()` the returned [`ThreadedRegistration`] or otherwise prevent
    /// its [`Drop`] implementation from running.
    pub unsafe fn new(
        request: IrqRequest<'a>,
        flags: Flags,
        name: &'static CStr,
        handler: impl PinInit<T, Error> + 'a,
    ) -> impl PinInit<Self, Error> + 'a
    where
        T: 'a,
    {
        // INVARIANT: If initialization completes successfully, we own an IRQ handler registered
        // via `request_threaded_irq` whose cookie is a pointer to `Self`.
        try_pin_init!(&this in Self {
            handler <- handler,
            request,
            _pin: PhantomPinned,
            _: {
                // SAFETY:
                // - The callbacks are valid for use with request_threaded_irq.
                // - If this succeeds, the slot is guaranteed to be valid until the destructor of
                //   Self runs, which will deregister the callbacks before the memory location
                //   becomes invalid.
                // - All fields are already initialized, so it's safe for the callbacks to be
                //   called immediately.
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
            },
        })
    }

    /// Returns a reference to the handler that was registered with the system.
    pub fn handler(&self) -> &T {
        &self.handler
    }

    /// Wait for pending IRQ handlers on other CPUs.
    #[inline]
    pub fn synchronize(&self) {
        // SAFETY: `self.request.irq` is a valid registered IRQ number (type invariant).
        unsafe { bindings::synchronize_irq(self.request.irq) };
    }
}

#[pinned_drop]
impl<T: ThreadedHandler> PinnedDrop for ThreadedRegistration<'_, T> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: The cookie was set to a pointer to `Self` in `ThreadedRegistration::new()`. This
        // blocks until all in-flight handlers complete, so no references to `self` remain after
        // this returns.
        unsafe {
            bindings::free_irq(
                self.request.irq,
                core::ptr::from_mut::<Self>(self.get_unchecked_mut()).cast::<c_void>(),
            )
        };
    }
}

/// # Safety
///
/// This function should be only used as the callback in `request_threaded_irq`.
unsafe extern "C" fn handle_threaded_irq_callback<T: ThreadedHandler>(
    _irq: i32,
    ptr: *mut c_void,
) -> c_uint {
    let ptr = ptr.cast_const().cast::<ThreadedRegistration<'_, T>>();
    // SAFETY: `ptr` is a pointer to `ThreadedRegistration<'_, T>` set in
    // `ThreadedRegistration::new()`.
    let registration = unsafe { &*ptr };

    T::handle(&registration.handler) as c_uint
}

/// # Safety
///
/// This function should be only used as the callback in `request_threaded_irq`.
unsafe extern "C" fn thread_fn_callback<T: ThreadedHandler>(_irq: i32, ptr: *mut c_void) -> c_uint {
    let ptr = ptr.cast_const().cast::<ThreadedRegistration<'_, T>>();
    // SAFETY: `ptr` is a pointer to `ThreadedRegistration<'_, T>` set in
    // `ThreadedRegistration::new()`.
    let registration = unsafe { &*ptr };

    T::handle_threaded(&registration.handler) as c_uint
}
