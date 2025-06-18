// SPDX-License-Identifier: GPL-2.0

//! Devres abstraction
//!
//! [`Devres`] represents an abstraction for the kernel devres (device resource management)
//! implementation.

use crate::{
    alloc::Flags,
    bindings,
    device::{Bound, Device},
    error::{Error, Result},
    ffi::c_void,
    prelude::*,
    revocable::{Revocable, RevocableGuard},
    sync::{rcu, Arc, Completion},
    types::ARef,
};

#[pin_data]
struct DevresInner<T> {
    dev: ARef<Device>,
    callback: unsafe extern "C" fn(*mut c_void),
    #[pin]
    data: Revocable<T>,
    #[pin]
    revoke: Completion,
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
/// # Example
///
/// ```no_run
/// # use kernel::{bindings, c_str, device::{Bound, Device}, devres::Devres, io::{Io, IoRaw}};
/// # use core::ops::Deref;
///
/// // See also [`pci::Bar`] for a real example.
/// struct IoMem<const SIZE: usize>(IoRaw<SIZE>);
///
/// impl<const SIZE: usize> IoMem<SIZE> {
///     /// # Safety
///     ///
///     /// [`paddr`, `paddr` + `SIZE`) must be a valid MMIO region that is mappable into the CPUs
///     /// virtual address space.
///     unsafe fn new(paddr: usize) -> Result<Self>{
///         // SAFETY: By the safety requirements of this function [`paddr`, `paddr` + `SIZE`) is
///         // valid for `ioremap`.
///         let addr = unsafe { bindings::ioremap(paddr as _, SIZE as _) };
///         if addr.is_null() {
///             return Err(ENOMEM);
///         }
///
///         Ok(IoMem(IoRaw::new(addr as _, SIZE)?))
///     }
/// }
///
/// impl<const SIZE: usize> Drop for IoMem<SIZE> {
///     fn drop(&mut self) {
///         // SAFETY: `self.0.addr()` is guaranteed to be properly mapped by `Self::new`.
///         unsafe { bindings::iounmap(self.0.addr() as _); };
///     }
/// }
///
/// impl<const SIZE: usize> Deref for IoMem<SIZE> {
///    type Target = Io<SIZE>;
///
///    fn deref(&self) -> &Self::Target {
///         // SAFETY: The memory range stored in `self` has been properly mapped in `Self::new`.
///         unsafe { Io::from_raw(&self.0) }
///    }
/// }
/// # fn no_run(dev: &Device<Bound>) -> Result<(), Error> {
/// // SAFETY: Invalid usage for example purposes.
/// let iomem = unsafe { IoMem::<{ core::mem::size_of::<u32>() }>::new(0xBAAAAAAD)? };
/// let devres = Devres::new(dev, iomem, GFP_KERNEL)?;
///
/// let res = devres.try_access().ok_or(ENXIO)?;
/// res.write8(0x42, 0x0);
/// # Ok(())
/// # }
/// ```
pub struct Devres<T>(Arc<DevresInner<T>>);

impl<T> DevresInner<T> {
    fn new(dev: &Device<Bound>, data: T, flags: Flags) -> Result<Arc<DevresInner<T>>> {
        let inner = Arc::pin_init(
            pin_init!( DevresInner {
                dev: dev.into(),
                callback: Self::devres_callback,
                data <- Revocable::new(data),
                revoke <- Completion::new(),
            }),
            flags,
        )?;

        // Convert `Arc<DevresInner>` into a raw pointer and make devres own this reference until
        // `Self::devres_callback` is called.
        let data = inner.clone().into_raw();

        // SAFETY: `devm_add_action` guarantees to call `Self::devres_callback` once `dev` is
        // detached.
        let ret =
            unsafe { bindings::devm_add_action(dev.as_raw(), Some(inner.callback), data as _) };

        if ret != 0 {
            // SAFETY: We just created another reference to `inner` in order to pass it to
            // `bindings::devm_add_action`. If `bindings::devm_add_action` fails, we have to drop
            // this reference accordingly.
            let _ = unsafe { Arc::from_raw(data) };
            return Err(Error::from_errno(ret));
        }

        Ok(inner)
    }

    fn as_ptr(&self) -> *const Self {
        self as _
    }

    fn remove_action(this: &Arc<Self>) -> bool {
        // SAFETY:
        // - `self.inner.dev` is a valid `Device`,
        // - the `action` and `data` pointers are the exact same ones as given to devm_add_action()
        //   previously,
        // - `self` is always valid, even if the action has been released already.
        let success = unsafe {
            bindings::devm_remove_action_nowarn(
                this.dev.as_raw(),
                Some(this.callback),
                this.as_ptr() as _,
            )
        } == 0;

        if success {
            // SAFETY: We leaked an `Arc` reference to devm_add_action() in `DevresInner::new`; if
            // devm_remove_action_nowarn() was successful we can (and have to) claim back ownership
            // of this reference.
            let _ = unsafe { Arc::from_raw(this.as_ptr()) };
        }

        success
    }

    #[allow(clippy::missing_safety_doc)]
    unsafe extern "C" fn devres_callback(ptr: *mut kernel::ffi::c_void) {
        let ptr = ptr as *mut DevresInner<T>;
        // Devres owned this memory; now that we received the callback, drop the `Arc` and hence the
        // reference.
        // SAFETY: Safe, since we leaked an `Arc` reference to devm_add_action() in
        //         `DevresInner::new`.
        let inner = unsafe { Arc::from_raw(ptr) };

        if !inner.data.revoke() {
            // If `revoke()` returns false, it means that `Devres::drop` already started revoking
            // `inner.data` for us. Hence we have to wait until `Devres::drop()` signals that it
            // completed revoking `inner.data`.
            inner.revoke.wait_for_completion();
        }
    }
}

impl<T> Devres<T> {
    /// Creates a new [`Devres`] instance of the given `data`. The `data` encapsulated within the
    /// returned `Devres` instance' `data` will be revoked once the device is detached.
    pub fn new(dev: &Device<Bound>, data: T, flags: Flags) -> Result<Self> {
        let inner = DevresInner::new(dev, data, flags)?;

        Ok(Devres(inner))
    }

    /// Same as [`Devres::new`], but does not return a `Devres` instance. Instead the given `data`
    /// is owned by devres and will be revoked / dropped, once the device is detached.
    pub fn new_foreign_owned(dev: &Device<Bound>, data: T, flags: Flags) -> Result {
        let _ = DevresInner::new(dev, data, flags)?;

        Ok(())
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
    /// # Example
    ///
    /// ```no_run
    /// # #![cfg(CONFIG_PCI)]
    /// # use kernel::{device::Core, devres::Devres, pci};
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
        if self.0.dev.as_raw() != dev.as_raw() {
            return Err(EINVAL);
        }

        // SAFETY: `dev` being the same device as the device this `Devres` has been created for
        // proves that `self.0.data` hasn't been revoked and is guaranteed to not be revoked as
        // long as `dev` lives; `dev` lives at least as long as `self`.
        Ok(unsafe { self.0.data.access() })
    }

    /// [`Devres`] accessor for [`Revocable::try_access`].
    pub fn try_access(&self) -> Option<RevocableGuard<'_, T>> {
        self.0.data.try_access()
    }

    /// [`Devres`] accessor for [`Revocable::try_access_with`].
    pub fn try_access_with<R, F: FnOnce(&T) -> R>(&self, f: F) -> Option<R> {
        self.0.data.try_access_with(f)
    }

    /// [`Devres`] accessor for [`Revocable::try_access_with_guard`].
    pub fn try_access_with_guard<'a>(&'a self, guard: &'a rcu::Guard) -> Option<&'a T> {
        self.0.data.try_access_with_guard(guard)
    }
}

impl<T> Drop for Devres<T> {
    fn drop(&mut self) {
        // SAFETY: When `drop` runs, it is guaranteed that nobody is accessing the revocable data
        // anymore, hence it is safe not to wait for the grace period to finish.
        if unsafe { self.0.data.revoke_nosync() } {
            // We revoked `self.0.data` before the devres action did, hence try to remove it.
            if !DevresInner::remove_action(&self.0) {
                // We could not remove the devres action, which means that it now runs concurrently,
                // hence signal that `self.0.data` has been revoked successfully.
                self.0.revoke.complete_all();
            }
        }
    }
}
