// SPDX-License-Identifier: GPL-2.0

//! Devres abstraction
//!
//! [`Devres`] represents an abstraction for the kernel devres (device resource management)
//! implementation.

use crate::{
    alloc::Flags,
    bindings,
    device::{
        Bound,
        Device, //
    },
    error::to_result,
    prelude::*,
    revocable::{
        Revocable,
        RevocableGuard, //
    },
    sync::{
        aref::ARef,
        rcu,
        Arc, //
    },
    types::{
        ForeignOwnable,
        Opaque, //
    },
};

/// Inner type that embeds a `struct devres_node` and the `Revocable<T>`.
#[repr(C)]
#[pin_data]
struct Inner<T> {
    #[pin]
    node: Opaque<bindings::devres_node>,
    #[pin]
    data: Revocable<T>,
}

/// This abstraction is meant to be used by subsystems to containerize [`Device`] bound resources to
/// manage their lifetime.
///
/// [`Device`] bound resources should be freed when either the resource goes out of scope or the
/// [`Device`] is unbound respectively, depending on what happens first. In any case, it is always
/// guaranteed that revoking the device resource is completed before the corresponding [`Device`]
/// is unbound.
///
/// To achieve that [`Devres`] registers a devres callback on creation, which is called once the
/// [`Device`] is unbound, revoking access to the encapsulated resource (see also [`Revocable`]).
///
/// After the [`Devres`] has been unbound it is not possible to access the encapsulated resource
/// anymore.
///
/// [`Devres`] users should make sure to simply free the corresponding backing resource in `T`'s
/// [`Drop`] implementation.
///
/// # Examples
///
/// ```no_run
/// use kernel::{
///     bindings,
///     device::{
///         Bound,
///         Device,
///     },
///     devres::Devres,
///     io::{
///         Io,
///         IoKnownSize,
///         Mmio,
///         MmioRaw,
///         PhysAddr, //
///     },
///     prelude::*,
/// };
/// use core::ops::Deref;
///
/// // See also [`pci::Bar`] for a real example.
/// struct IoMem<const SIZE: usize>(MmioRaw<SIZE>);
///
/// impl<const SIZE: usize> IoMem<SIZE> {
///     /// # Safety
///     ///
///     /// [`paddr`, `paddr` + `SIZE`) must be a valid MMIO region that is mappable into the CPUs
///     /// virtual address space.
///     unsafe fn new(paddr: usize) -> Result<Self>{
///         // SAFETY: By the safety requirements of this function [`paddr`, `paddr` + `SIZE`) is
///         // valid for `ioremap`.
///         let addr = unsafe { bindings::ioremap(paddr as PhysAddr, SIZE) };
///         if addr.is_null() {
///             return Err(ENOMEM);
///         }
///
///         Ok(IoMem(MmioRaw::new(addr as usize, SIZE)?))
///     }
/// }
///
/// impl<const SIZE: usize> Drop for IoMem<SIZE> {
///     fn drop(&mut self) {
///         // SAFETY: `self.0.addr()` is guaranteed to be properly mapped by `Self::new`.
///         unsafe { bindings::iounmap(self.0.addr() as *mut c_void); };
///     }
/// }
///
/// impl<const SIZE: usize> Deref for IoMem<SIZE> {
///    type Target = Mmio<SIZE>;
///
///    fn deref(&self) -> &Self::Target {
///         // SAFETY: The memory range stored in `self` has been properly mapped in `Self::new`.
///         unsafe { Mmio::from_raw(&self.0) }
///    }
/// }
/// # fn no_run(dev: &Device<Bound>) -> Result<(), Error> {
/// // SAFETY: Invalid usage for example purposes.
/// let iomem = unsafe { IoMem::<{ core::mem::size_of::<u32>() }>::new(0xBAAAAAAD)? };
/// let devres = Devres::new(dev, iomem)?;
///
/// let res = devres.try_access().ok_or(ENXIO)?;
/// res.write8(0x42, 0x0);
/// # Ok(())
/// # }
/// ```
pub struct Devres<T: Send> {
    dev: ARef<Device>,
    inner: Arc<Inner<T>>,
}

// Calling the FFI functions from the `base` module directly from the `Devres<T>` impl may result in
// them being called directly from driver modules. This happens since the Rust compiler will use
// monomorphisation, so it might happen that functions are instantiated within the calling driver
// module. For now, work around this with `#[inline(never)]` helpers.
//
// TODO: Remove once a more generic solution has been implemented. For instance, we may be able to
// leverage `bindgen` to take care of this depending on whether a symbol is (already) exported.
mod base {
    use kernel::{
        bindings,
        prelude::*, //
    };

    #[inline(never)]
    #[allow(clippy::missing_safety_doc)]
    pub(super) unsafe fn devres_node_init(
        node: *mut bindings::devres_node,
        release: bindings::dr_node_release_t,
        free: bindings::dr_node_free_t,
    ) {
        // SAFETY: Safety requirements are the same as `bindings::devres_node_init`.
        unsafe { bindings::devres_node_init(node, release, free) }
    }

    #[inline(never)]
    #[allow(clippy::missing_safety_doc)]
    pub(super) unsafe fn devres_set_node_dbginfo(
        node: *mut bindings::devres_node,
        name: *const c_char,
        size: usize,
    ) {
        // SAFETY: Safety requirements are the same as `bindings::devres_set_node_dbginfo`.
        unsafe { bindings::devres_set_node_dbginfo(node, name, size) }
    }

    #[inline(never)]
    #[allow(clippy::missing_safety_doc)]
    pub(super) unsafe fn devres_node_add(
        dev: *mut bindings::device,
        node: *mut bindings::devres_node,
    ) {
        // SAFETY: Safety requirements are the same as `bindings::devres_node_add`.
        unsafe { bindings::devres_node_add(dev, node) }
    }

    #[must_use]
    #[inline(never)]
    #[allow(clippy::missing_safety_doc)]
    pub(super) unsafe fn devres_node_remove(
        dev: *mut bindings::device,
        node: *mut bindings::devres_node,
    ) -> bool {
        // SAFETY: Safety requirements are the same as `bindings::devres_node_remove`.
        unsafe { bindings::devres_node_remove(dev, node) }
    }
}

impl<T: Send> Devres<T> {
    /// Creates a new [`Devres`] instance of the given `data`.
    ///
    /// The `data` encapsulated within the returned `Devres` instance' `data` will be
    /// (revoked)[`Revocable`] once the device is detached.
    pub fn new<E>(dev: &Device<Bound>, data: impl PinInit<T, E>) -> Result<Self>
    where
        Error: From<E>,
    {
        let inner = Arc::pin_init::<Error>(
            try_pin_init!(Inner {
                node <- Opaque::ffi_init(|node: *mut bindings::devres_node| {
                    // SAFETY: `node` is a valid pointer to an uninitialized `struct devres_node`.
                    unsafe {
                        base::devres_node_init(
                            node,
                            Some(Self::devres_node_release),
                            Some(Self::devres_node_free_node),
                        )
                    };

                    // SAFETY: `node` is a valid pointer to an uninitialized `struct devres_node`.
                    unsafe {
                        base::devres_set_node_dbginfo(
                            node,
                            // TODO: Use `core::any::type_name::<T>()` once it is a `const fn`,
                            // such that we can convert the `&str` to a `&CStr` at compile-time.
                            c"Devres<T>".as_char_ptr(),
                            core::mem::size_of::<Revocable<T>>(),
                        )
                    };
                }),
                data <- Revocable::new(data),
            }),
            GFP_KERNEL,
        )?;

        // SAFETY:
        // - `dev` is a valid pointer to a bound `struct device`.
        // - `node` is a valid pointer to a `struct devres_node`.
        // - `devres_node_add()` is guaranteed not to call `devres_node_release()` for the entire
        //    lifetime of `dev`.
        unsafe { base::devres_node_add(dev.as_raw(), inner.node.get()) };

        // Take additional reference count for `devres_node_add()`.
        core::mem::forget(inner.clone());

        Ok(Self {
            dev: dev.into(),
            inner,
        })
    }

    fn data(&self) -> &Revocable<T> {
        &self.inner.data
    }

    #[allow(clippy::missing_safety_doc)]
    unsafe extern "C" fn devres_node_release(
        _dev: *mut bindings::device,
        node: *mut bindings::devres_node,
    ) {
        let node = Opaque::cast_from(node);

        // SAFETY: `node` is in the same allocation as its container.
        let inner = unsafe { kernel::container_of!(node, Inner<T>, node) };

        // SAFETY: `inner` is a valid `Inner<T>` pointer.
        let inner = unsafe { &*inner };

        inner.data.revoke();
    }

    #[allow(clippy::missing_safety_doc)]
    unsafe extern "C" fn devres_node_free_node(node: *mut bindings::devres_node) {
        let node = Opaque::cast_from(node);

        // SAFETY: `node` is in the same allocation as its container.
        let inner = unsafe { kernel::container_of!(node, Inner<T>, node) };

        // SAFETY: `inner` points to the entire `Inner<T>` allocation.
        drop(unsafe { Arc::from_raw(inner) });
    }

    fn remove_node(&self) -> bool {
        // SAFETY:
        // - `self.device().as_raw()` is a valid pointer to a bound `struct device`.
        // - `self.inner.node.get()` is a valid pointer to a `struct devres_node`.
        unsafe { base::devres_node_remove(self.device().as_raw(), self.inner.node.get()) }
    }

    /// Return a reference of the [`Device`] this [`Devres`] instance has been created with.
    pub fn device(&self) -> &Device {
        &self.dev
    }

    /// Obtain `&'a T`, bypassing the [`Revocable`].
    ///
    /// This method allows to directly obtain a `&'a T`, bypassing the [`Revocable`], by presenting
    /// a `&'a Device<Bound>` of the same [`Device`] this [`Devres`] instance has been created with.
    ///
    /// # Errors
    ///
    /// An error is returned if `dev` does not match the same [`Device`] this [`Devres`] instance
    /// has been created with.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// #![cfg(CONFIG_PCI)]
    /// use kernel::{
    ///     device::Core,
    ///     devres::Devres,
    ///     io::{
    ///         Io,
    ///         IoKnownSize, //
    ///     },
    ///     pci, //
    /// };
    ///
    /// fn from_core(dev: &pci::Device<Core>, devres: Devres<pci::Bar<0x4>>) -> Result {
    ///     let bar = devres.access(dev.as_ref())?;
    ///
    ///     let _ = bar.read32(0x0);
    ///
    ///     // might_sleep()
    ///
    ///     bar.write32(0x42, 0x0);
    ///
    ///     Ok(())
    /// }
    /// ```
    pub fn access<'a>(&'a self, dev: &'a Device<Bound>) -> Result<&'a T> {
        if self.dev.as_raw() != dev.as_raw() {
            return Err(EINVAL);
        }

        // SAFETY: `dev` being the same device as the device this `Devres` has been created for
        // proves that `self.data` hasn't been revoked and is guaranteed to not be revoked as long
        // as `dev` lives; `dev` lives at least as long as `self`.
        Ok(unsafe { self.data().access() })
    }

    /// [`Devres`] accessor for [`Revocable::try_access`].
    pub fn try_access(&self) -> Option<RevocableGuard<'_, T>> {
        self.data().try_access()
    }

    /// [`Devres`] accessor for [`Revocable::try_access_with`].
    pub fn try_access_with<R, F: FnOnce(&T) -> R>(&self, f: F) -> Option<R> {
        self.data().try_access_with(f)
    }

    /// [`Devres`] accessor for [`Revocable::try_access_with_guard`].
    pub fn try_access_with_guard<'a>(&'a self, guard: &'a rcu::Guard) -> Option<&'a T> {
        self.data().try_access_with_guard(guard)
    }
}

// SAFETY: `Devres` can be send to any task, if `T: Send`.
unsafe impl<T: Send> Send for Devres<T> {}

// SAFETY: `Devres` can be shared with any task, if `T: Sync`.
unsafe impl<T: Send + Sync> Sync for Devres<T> {}

impl<T: Send> Drop for Devres<T> {
    fn drop(&mut self) {
        // SAFETY: When `drop` runs, it is guaranteed that nobody is accessing the revocable data
        // anymore, hence it is safe not to wait for the grace period to finish.
        if unsafe { self.data().revoke_nosync() } {
            // We revoked `self.data` before devres did, hence try to remove it.
            if self.remove_node() {
                // SAFETY: In `Self::new` we have taken an additional reference count of `self.data`
                // for `devres_node_add()`. Since `remove_node()` was successful, we have to drop
                // this additional reference count.
                drop(unsafe { Arc::from_raw(Arc::as_ptr(&self.inner)) });
            }
        }
    }
}

/// Consume `data` and [`Drop::drop`] `data` once `dev` is unbound.
fn register_foreign<P>(dev: &Device<Bound>, data: P) -> Result
where
    P: ForeignOwnable + Send + 'static,
{
    let ptr = data.into_foreign();

    #[allow(clippy::missing_safety_doc)]
    unsafe extern "C" fn callback<P: ForeignOwnable>(ptr: *mut kernel::ffi::c_void) {
        // SAFETY: `ptr` is the pointer to the `ForeignOwnable` leaked above and hence valid.
        drop(unsafe { P::from_foreign(ptr.cast()) });
    }

    // SAFETY:
    // - `dev.as_raw()` is a pointer to a valid and bound device.
    // - `ptr` is a valid pointer the `ForeignOwnable` devres takes ownership of.
    to_result(unsafe {
        // `devm_add_action_or_reset()` also calls `callback` on failure, such that the
        // `ForeignOwnable` is released eventually.
        bindings::devm_add_action_or_reset(dev.as_raw(), Some(callback::<P>), ptr.cast())
    })
}

/// Encapsulate `data` in a [`KBox`] and [`Drop::drop`] `data` once `dev` is unbound.
///
/// # Examples
///
/// ```no_run
/// use kernel::{
///     device::{
///         Bound,
///         Device, //
///     },
///     devres, //
/// };
///
/// /// Registration of e.g. a class device, IRQ, etc.
/// struct Registration;
///
/// impl Registration {
///     fn new() -> Self {
///         // register
///
///         Self
///     }
/// }
///
/// impl Drop for Registration {
///     fn drop(&mut self) {
///        // unregister
///     }
/// }
///
/// fn from_bound_context(dev: &Device<Bound>) -> Result {
///     devres::register(dev, Registration::new(), GFP_KERNEL)
/// }
/// ```
pub fn register<T, E>(dev: &Device<Bound>, data: impl PinInit<T, E>, flags: Flags) -> Result
where
    T: Send + 'static,
    Error: From<E>,
{
    let data = KBox::pin_init(data, flags)?;

    register_foreign(dev, data)
}
