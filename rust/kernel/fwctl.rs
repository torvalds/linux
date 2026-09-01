// SPDX-License-Identifier: GPL-2.0-only

//! Abstractions for the fwctl subsystem.
//!
//! C header: `include/linux/fwctl.h`

use crate::{
    bindings,
    container_of,
    device,
    prelude::*,
    sync::aref::{
        ARef,
        AlwaysRefCounted, //
    },
    types::Opaque, //
};
use core::{
    alloc::Layout,
    cell::UnsafeCell,
    marker::PhantomData,
    ptr::NonNull,
    slice, //
};

/// Returns a kmalloc-compatible allocation size for `T`.
const fn kmalloc_aligned_size<T>() -> usize {
    Layout::new::<T>().pad_to_align().size()
}

/// Represents a fwctl device type.
///
/// Corresponds to the C `enum fwctl_device_type`. All non-error UAPI values are represented so
/// Rust drivers can select a device type without passing an untyped integer, while
/// `FWCTL_DEVICE_TYPE_ERROR` remains unrepresentable.
#[repr(u32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum DeviceType {
    /// Mellanox ConnectX (mlx5) device.
    Mlx5 = bindings::fwctl_device_type_FWCTL_DEVICE_TYPE_MLX5,
    /// CXL (Compute Express Link) device.
    Cxl = bindings::fwctl_device_type_FWCTL_DEVICE_TYPE_CXL,
    /// AMD/Pensando PDS device.
    Pds = bindings::fwctl_device_type_FWCTL_DEVICE_TYPE_PDS,
    /// Broadcom NetXtreme (bnxt) device.
    Bnxt = bindings::fwctl_device_type_FWCTL_DEVICE_TYPE_BNXT,
}

/// Scope of access for an RPC request.
///
/// Corresponds to the C `enum fwctl_rpc_scope`.
#[repr(u32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum RpcScope {
    /// Read/write access to device configuration.
    Configuration = bindings::fwctl_rpc_scope_FWCTL_RPC_CONFIGURATION,
    /// Read-only access to debug information.
    DebugReadOnly = bindings::fwctl_rpc_scope_FWCTL_RPC_DEBUG_READ_ONLY,
    /// Write access to lockdown-compatible debug information.
    DebugWrite = bindings::fwctl_rpc_scope_FWCTL_RPC_DEBUG_WRITE,
    /// Full read/write access to all debug information (requires `CAP_SYS_RAWIO`).
    DebugWriteFull = bindings::fwctl_rpc_scope_FWCTL_RPC_DEBUG_WRITE_FULL,
}

impl TryFrom<u32> for RpcScope {
    type Error = Error;

    #[inline]
    fn try_from(value: u32) -> Result<Self, Error> {
        match value {
            v if v == Self::Configuration as u32 => Ok(Self::Configuration),
            v if v == Self::DebugReadOnly as u32 => Ok(Self::DebugReadOnly),
            v if v == Self::DebugWrite as u32 => Ok(Self::DebugWrite),
            v if v == Self::DebugWriteFull as u32 => Ok(Self::DebugWriteFull),
            _ => Err(EINVAL),
        }
    }
}

/// Response from a [`Operations::fw_rpc`] call.
pub enum FwRpcResponse {
    /// Reuse the input buffer as the output, with the given output length.
    ///
    /// The callback returns `EINVAL` if the output length exceeds the input buffer length.
    InPlace(usize),
    /// Return a newly allocated buffer as the output.
    NewBuffer(KVVec<u8>),
}

/// Trait implemented by each Rust driver that integrates with the fwctl subsystem.
///
/// The implementing type **is** the per-FD user context: one instance is
/// created for each `open()` call and dropped when the FD is closed.
///
/// Each implementation corresponds to a specific device type and provides the
/// vtable used by the core `fwctl` layer to manage per-FD user contexts and
/// handle RPC requests.
pub trait Operations: Sized + Send + Sync + 'static {
    /// Data owned by the [`Registration`] and accessible during callbacks.
    ///
    /// The lifetime `'a` is tied to the [`Registration`] scope (which lives within the parent bus
    /// device binding scope). Drivers use it to store references to resources bound to this scope,
    /// such as PCI BARs or typed bus device references.
    type RegistrationData<'a>: Send + Sync + 'a
    where
        Self: 'a;

    /// fwctl device type identifier.
    const DEVICE_TYPE: DeviceType;

    /// Called when a new user context is opened.
    ///
    /// Returns a [`PinInit`] initializer for `Self`. The instance is dropped
    /// automatically when the FD is closed (after [`close`](Self::close)).
    fn open<'a>(
        device: &Device<Self>,
        reg_data: &Self::RegistrationData<'a>,
    ) -> impl PinInit<Self, Error>;

    /// Called when the user context is closed.
    ///
    /// The driver may perform additional cleanup here that requires access
    /// to the owning [`Device`]. `Self` is dropped automatically after this
    /// returns.
    fn close<'a>(
        _this: Pin<&mut Self>,
        _device: &Device<Self>,
        _reg_data: &Self::RegistrationData<'a>,
    ) {
    }

    /// Return device information to userspace.
    ///
    /// The default implementation returns no device-specific data.
    fn info<'a>(
        _this: Pin<&Self>,
        _device: &Device<Self>,
        _reg_data: &Self::RegistrationData<'a>,
    ) -> Result<KVec<u8>, Error> {
        Ok(KVec::new())
    }

    /// Handle a userspace RPC request.
    ///
    /// `max_output_len` is the size of the userspace output buffer. A driver may return a larger
    /// response to report the required size; the fwctl core copies only the bytes that fit and
    /// reports the full response length to userspace.
    fn fw_rpc<'a>(
        this: Pin<&Self>,
        device: &Device<Self>,
        reg_data: &Self::RegistrationData<'a>,
        scope: RpcScope,
        rpc_buf: &mut [u8],
        max_output_len: usize,
    ) -> Result<FwRpcResponse, Error>;
}

/// A fwctl device.
///
/// `#[repr(C)]` with the `fwctl_device` at offset 0, matching the C `fwctl_alloc_device()` layout
/// convention. Contains a pointer to the [`Registration`]'s data, set at registration time and
/// cleared on unregistration.
///
/// # Invariants
///
/// - `dev` is embedded at offset 0 and is initialised by fwctl.
/// - The fwctl refcount owns the allocation lifetime.
/// - `registration_data` is either [`NonNull::dangling()`] (before registration / after
///   unregistration) or points to valid data owned by the [`Registration`].
#[repr(C)]
pub struct Device<T: Operations> {
    dev: Opaque<bindings::fwctl_device>,
    registration_data: UnsafeCell<NonNull<T::RegistrationData<'static>>>,
}

impl<T: Operations> Device<T> {
    /// Allocate a new fwctl device.
    ///
    /// Returns an [`ARef`] that can be passed to [`Registration::new()`]
    /// to make the device visible to userspace.
    pub fn new(parent: &device::Device<device::Bound>) -> Result<ARef<Self>> {
        const_assert!(
            core::mem::offset_of!(Self, dev) == 0,
            "struct fwctl_device must be at offset 0"
        );

        let size = kmalloc_aligned_size::<Self>();
        let ops = core::ptr::from_ref::<bindings::fwctl_ops>(&VTable::<T>::VTABLE).cast_mut();

        // SAFETY: `ops` is static, `parent` is bound, and `size` is padded so the allocation made
        // by `_fwctl_alloc_device` satisfies the size and alignment required by `Device<T>`.
        let raw = unsafe { bindings::_fwctl_alloc_device(parent.as_raw(), ops, size) };
        let this = NonNull::new(raw.cast::<Self>()).ok_or(ENOMEM)?;

        // INVARIANT: Set `registration_data` to dangling (no registration yet).
        // SAFETY: `this` points to the allocation just returned by fwctl.
        unsafe {
            (&raw mut (*this.as_ptr()).registration_data)
                .write(UnsafeCell::new(NonNull::dangling()));
        };

        // SAFETY: `this` owns the initial reference.
        Ok(unsafe { ARef::from_raw(this) })
    }

    /// Returns the underlying `fwctl_device` pointer.
    #[inline]
    fn as_raw(&self) -> *mut bindings::fwctl_device {
        self.dev.get()
    }

    /// Borrows a Rust fwctl device from its raw C pointer.
    ///
    /// # Safety
    ///
    /// `ptr` must point to a valid `fwctl_device` embedded in a [`Device<T>`].
    #[inline]
    unsafe fn from_raw<'a>(ptr: *mut bindings::fwctl_device) -> &'a Self {
        // SAFETY: The caller upholds the offset-0 `Device<T>` invariant.
        unsafe { &*ptr.cast() }
    }

    /// Invokes `f` with the registration data.
    ///
    /// The higher-ranked callback prevents the erased registration lifetime from escaping and
    /// permits registration data that is invariant over its lifetime parameter.
    ///
    /// # Safety
    ///
    /// The caller must ensure that the device is registered and that this is called from a fwctl
    /// callback protected by `registration_lock`.
    #[inline]
    unsafe fn with_registration_data<R>(
        &self,
        f: impl for<'a> FnOnce(&Device<T>, &'a T::RegistrationData<'a>) -> R,
    ) -> R {
        // SAFETY: Caller guarantees the device is registered, so the pointer is valid.
        // Lifetimes do not affect layout. The higher-ranked callback prevents the shortened
        // lifetime from escaping or being selected by the caller.
        let reg_data = unsafe {
            (*self.registration_data.get())
                .cast::<T::RegistrationData<'_>>()
                .as_ref()
        };

        f(self, reg_data)
    }
}

impl<T: Operations> AsRef<device::Device> for Device<T> {
    #[inline]
    fn as_ref(&self) -> &device::Device {
        // SAFETY: `self` contains a live fwctl_device.
        let dev = unsafe { &raw mut (*self.as_raw()).dev };
        // SAFETY: The embedded device is initialised by fwctl.
        unsafe { device::Device::from_raw(dev) }
    }
}

// SAFETY: `fwctl_get` increments the refcount of a valid fwctl_device.
// `fwctl_put` decrements it and frees the device when it reaches zero.
unsafe impl<T: Operations> AlwaysRefCounted for Device<T> {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: `self` holds a live reference.
        unsafe { bindings::fwctl_get(self.as_raw()) };
    }

    #[inline]
    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The caller owns a live reference.
        unsafe { bindings::fwctl_put(obj.cast().as_ptr()) };
    }
}

// SAFETY: `Device<T>` is refcounted by the fwctl core and may be released from any thread.
unsafe impl<T: Operations> Send for Device<T> {}

// SAFETY: Shared access to the embedded `fwctl_device` is protected by the fwctl core. The
// `registration_data` field is only mutated before registration and after unregistration (both
// single-threaded with respect to callbacks).
unsafe impl<T: Operations> Sync for Device<T> {}

/// A registered fwctl device.
///
/// Owns the [`RegistrationData`](Operations::RegistrationData) made available to driver callbacks.
/// The parent device lifetime ensures that [`fwctl_unregister`] runs before the parent driver
/// unbinds.
///
/// On drop the device is unregistered (all user contexts are closed and `ops` is set to `NULL`)
/// and the registration data is dropped.
///
/// [`fwctl_unregister`]: srctree/drivers/fwctl/main.c
pub struct Registration<'a, T: Operations> {
    dev: ARef<Device<T>>,
    _reg_data: Pin<KBox<T::RegistrationData<'a>>>,
}

impl<'a, T: Operations> Registration<'a, T> {
    /// Register a previously allocated fwctl device with the given registration data.
    ///
    /// The `reg_data` is owned by the registration and accessible during callbacks.
    ///
    /// # Safety
    ///
    /// Callers must not `mem::forget()` the returned [`Registration`] or otherwise prevent its
    /// [`Drop`] implementation from running, since `fwctl_unregister` must be called before the
    /// parent device is unbound.
    ///
    /// `dev` must be an unregistered [`Device`] that is not associated with any live
    /// [`Registration`], and no other thread may attempt to register the same device concurrently.
    pub unsafe fn new(
        parent: &'a device::Device<device::Bound>,
        dev: &Device<T>,
        reg_data: impl PinInit<T::RegistrationData<'a>, Error>,
    ) -> Result<Self> {
        let actual_parent = dev.as_ref().parent().ok_or(EINVAL)?;
        let parent_device: &device::Device = parent;
        if !core::ptr::eq(actual_parent, parent_device) {
            return Err(EINVAL);
        }

        let reg_data: Pin<KBox<T::RegistrationData<'a>>> = KBox::pin_init(reg_data, GFP_KERNEL)?;

        // Store the registration data pointer in the device before registration, so that it is
        // visible once callbacks can be invoked. The `'static` type is only an erased storage
        // handle; callbacks access the pointer through a higher-ranked closure.
        let ptr: NonNull<T::RegistrationData<'static>> =
            NonNull::from(Pin::get_ref(reg_data.as_ref())).cast();

        // SAFETY: No concurrent access; the device is not yet registered.
        unsafe { *dev.registration_data.get() = ptr };

        // SAFETY: `dev` is a valid fwctl_device backed by an ARef.
        let ret = unsafe { bindings::fwctl_register(dev.as_raw()) };
        if ret != 0 {
            // SAFETY: No concurrent readers; registration failed.
            unsafe { *dev.registration_data.get() = NonNull::dangling() };
            return Err(Error::from_errno(ret));
        }

        Ok(Self {
            dev: dev.into(),
            _reg_data: reg_data,
        })
    }
}

impl<T: Operations> Drop for Registration<'_, T> {
    fn drop(&mut self) {
        // SAFETY: The Registration lifetime guarantees that the parent device is still bound.
        // `fwctl_unregister` takes the write lock, closes all user contexts, and sets ops=NULL.
        // After it returns, no callbacks can be running or will run.
        unsafe { bindings::fwctl_unregister(self.dev.as_raw()) };

        // SAFETY: `fwctl_unregister` guarantees no concurrent readers.
        unsafe { *self.dev.registration_data.get() = NonNull::dangling() };

        // `self._reg_data` is dropped here, after callbacks have stopped.
    }
}

/// Internal per-FD user context wrapping `struct fwctl_uctx` and `T`.
///
/// Not exposed to drivers; they work with `&T` / `Pin<&mut T>` directly.
#[repr(C)]
#[pin_data]
struct UserCtx<T: Operations> {
    #[pin]
    fwctl_uctx: Opaque<bindings::fwctl_uctx>,
    #[pin]
    uctx: T,
}

impl<T: Operations> UserCtx<T> {
    /// Borrows a pinned Rust user context from its raw C pointer.
    ///
    /// # Safety
    ///
    /// `ptr` must point to a `fwctl_uctx` embedded in a live, pinned `UserCtx<T>` that remains
    /// valid and does not move for the duration of `'a`.
    #[inline]
    unsafe fn from_raw<'a>(ptr: *mut bindings::fwctl_uctx) -> Pin<&'a Self> {
        // SAFETY: The caller upholds the `UserCtx<T>` embedding, lifetime, and pinning invariants.
        unsafe { Pin::new_unchecked(&*container_of!(Opaque::cast_from(ptr), Self, fwctl_uctx)) }
    }

    /// Mutably borrows a pinned Rust user context from its raw C pointer.
    ///
    /// # Safety
    ///
    /// - `ptr` must point to a `fwctl_uctx` embedded in a live, pinned `UserCtx<T>` that remains
    ///   valid and does not move for the duration of `'a`.
    /// - The caller must ensure exclusive access to the `UserCtx<T>` for the duration of `'a`.
    #[inline]
    unsafe fn from_raw_mut<'a>(ptr: *mut bindings::fwctl_uctx) -> Pin<&'a mut Self> {
        // SAFETY: The caller upholds the embedding, lifetime, pinning, and exclusivity invariants.
        unsafe {
            Pin::new_unchecked(
                &mut *container_of!(Opaque::cast_from(ptr), Self, fwctl_uctx).cast_mut(),
            )
        }
    }

    /// Returns a reference to the fwctl [`Device`] that owns this context.
    #[inline]
    fn device(self: Pin<&Self>) -> &Device<T> {
        // SAFETY: fwctl initialises this pointer before any driver callback.
        let raw_fwctl = unsafe { (*self.fwctl_uctx.get()).fwctl };
        // SAFETY: Rust fwctl devices use the offset-0 `Device<T>` layout.
        unsafe { Device::from_raw(raw_fwctl) }
    }

    /// Returns a pinned reference to the driver context.
    #[inline]
    fn uctx(self: Pin<&Self>) -> Pin<&T> {
        ::pin_init::assert_pinned!(UserCtx<T>, uctx, T, inline);

        // SAFETY: `uctx` is structurally pinned.
        unsafe { self.map_unchecked(|ctx| &ctx.uctx) }
    }
}

/// Static vtable mapping Rust trait methods to C callbacks.
struct VTable<T: Operations>(PhantomData<T>);

impl<T: Operations> VTable<T> {
    /// The fwctl operations vtable for this driver type.
    const VTABLE: bindings::fwctl_ops = bindings::fwctl_ops {
        // CAST: `DeviceType` has the same `u32` representation as the C enum field.
        device_type: T::DEVICE_TYPE as u32,
        uctx_size: kmalloc_aligned_size::<UserCtx<T>>(),
        open_uctx: Some(Self::open_uctx_callback),
        close_uctx: Some(Self::close_uctx_callback),
        info: Some(Self::info_callback),
        fw_rpc: Some(Self::fw_rpc_callback),
    };

    /// Initialises a newly opened Rust user context.
    ///
    /// # Safety
    ///
    /// `uctx` must be a valid `fwctl_uctx` embedded in a `UserCtx<T>` with
    /// sufficient allocated space for the uctx field.
    unsafe extern "C" fn open_uctx_callback(uctx: *mut bindings::fwctl_uctx) -> ffi::c_int {
        const_assert!(
            core::mem::offset_of!(UserCtx<T>, fwctl_uctx) == 0,
            "struct fwctl_uctx must be at offset 0"
        );

        // SAFETY: fwctl sets this pointer before calling `open_uctx`.
        let raw_fwctl = unsafe { (*uctx).fwctl };
        // SAFETY: Rust fwctl devices use the offset-0 `Device<T>` layout.
        let device = unsafe { Device::<T>::from_raw(raw_fwctl) };

        let uctx_offset = core::mem::offset_of!(UserCtx<T>, uctx);
        // SAFETY: `uctx_size` reserves space for the full `UserCtx<T>`.
        let uctx_ptr: *mut T = unsafe { uctx.byte_add(uctx_offset).cast() };

        // SAFETY: `open_uctx` is called under `registration_lock` read, so the device is
        // registered. `uctx_ptr` addresses the uninitialised pinned context reserved by
        // `uctx_size`.
        unsafe {
            device.with_registration_data(|device, reg_data| {
                match pin_init::raw_try_init(uctx_ptr, T::open(device, reg_data)) {
                    Ok(()) => 0,
                    Err(e) => e.to_errno(),
                }
            })
        }
    }

    /// Closes and drops an opened Rust user context.
    ///
    /// # Safety
    ///
    /// `uctx` must point to a fully initialised `UserCtx<T>`.
    unsafe extern "C" fn close_uctx_callback(uctx: *mut bindings::fwctl_uctx) {
        // SAFETY: fwctl keeps the owning device live for this callback.
        let device = unsafe { Device::<T>::from_raw((*uctx).fwctl) };

        // SAFETY: close is called for an opened Rust user context.
        let mut ctx = unsafe { UserCtx::<T>::from_raw_mut(uctx) };

        // SAFETY: `close_uctx` is called under `registration_lock` write (from
        // `fwctl_unregister`) or read (from `fwctl_fops_release`), so the device is registered.
        unsafe {
            device.with_registration_data(|device, reg_data| {
                T::close(ctx.as_mut().project().uctx, device, reg_data);
            });
        }

        // SAFETY: close is the last callback before fwctl frees the allocation.
        unsafe { core::ptr::drop_in_place(ctx.project().uctx.get_unchecked_mut()) };
    }

    /// Returns device-specific information for an opened Rust user context.
    ///
    /// # Safety
    ///
    /// - `uctx` must point to a fully initialised `UserCtx<T>`.
    /// - `length` must be a valid pointer.
    unsafe extern "C" fn info_callback(
        uctx: *mut bindings::fwctl_uctx,
        length: *mut usize,
    ) -> *mut ffi::c_void {
        // SAFETY: info is called for an opened Rust user context.
        let ctx = unsafe { UserCtx::<T>::from_raw(uctx) };
        let device = ctx.device();

        // SAFETY: `info` is called under `registration_lock` read, so the device is registered.
        let result = unsafe {
            device.with_registration_data(|device, reg_data| T::info(ctx.uctx(), device, reg_data))
        };

        match result {
            Ok(kvec) if kvec.is_empty() => {
                // SAFETY: `length` is a valid out-parameter.
                unsafe { *length = 0 };
                // Return NULL for empty data; kfree(NULL) is safe.
                core::ptr::null_mut()
            }
            Ok(kvec) => {
                let (ptr, len, _cap) = kvec.into_raw_parts();
                // SAFETY: `length` is a valid out-parameter.
                unsafe { *length = len };
                ptr.cast::<ffi::c_void>()
            }
            Err(e) => Error::to_ptr(e),
        }
    }

    /// Dispatches a firmware RPC for an opened Rust user context.
    ///
    /// # Safety
    ///
    /// - `uctx` must point to a fully initialised `UserCtx<T>`.
    /// - `rpc_in` must be valid, initialised, and exclusively accessible for `in_len` bytes.
    /// - `out_len` must be valid for reading and writing an initialised `usize`.
    unsafe extern "C" fn fw_rpc_callback(
        uctx: *mut bindings::fwctl_uctx,
        scope: u32,
        rpc_in: *mut ffi::c_void,
        in_len: usize,
        out_len: *mut usize,
    ) -> *mut ffi::c_void {
        let scope = match RpcScope::try_from(scope) {
            Ok(s) => s,
            Err(e) => return Error::to_ptr(e),
        };

        // SAFETY: `out_len` points to an initialised `usize` supplied by fwctl.
        let max_output_len = unsafe { *out_len };

        // SAFETY: RPC is called for an opened Rust user context.
        let ctx = unsafe { UserCtx::<T>::from_raw(uctx) };
        let device = ctx.device();

        // SAFETY: fwctl passes an exclusively owned buffer that is valid and initialised for
        // `in_len` bytes. It remains live for the duration of this callback.
        let rpc_buf = unsafe { slice::from_raw_parts_mut(rpc_in.cast::<u8>(), in_len) };

        // SAFETY: `fw_rpc` is called under `registration_lock` read, so the device is registered.
        let result = unsafe {
            device.with_registration_data(|device, reg_data| {
                T::fw_rpc(ctx.uctx(), device, reg_data, scope, rpc_buf, max_output_len)
            })
        };

        let (response, response_len) = match result {
            Ok(FwRpcResponse::InPlace(len)) => {
                if len > in_len {
                    return Error::to_ptr(EINVAL);
                }

                (rpc_in, len)
            }
            Ok(FwRpcResponse::NewBuffer(kvec)) if kvec.is_empty() => {
                // Return NULL for empty data; kvfree(NULL) is safe.
                (core::ptr::null_mut(), 0)
            }
            Ok(FwRpcResponse::NewBuffer(kvec)) => {
                let (ptr, len, _cap) = kvec.into_raw_parts();
                (ptr.cast::<ffi::c_void>(), len)
            }
            Err(e) => return Error::to_ptr(e),
        };

        // SAFETY: `out_len` is a valid out-parameter.
        unsafe { *out_len = response_len };
        response
    }
}
