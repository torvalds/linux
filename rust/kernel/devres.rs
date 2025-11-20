// SPDX-License-Identifier: GPL-2.0

//! Devres abstraction
//!
//! [`Devres`] represents an abstraction for the kernel devres (device resource management)
//! implementation.

use crate::{
    alloc::Flags,
    bindings,
    device::{Bound, Device},
    error::{to_result, Error, Result},
    ffi::c_void,
    prelude::*,
    revocable::{Revocable, RevocableGuard},
    sync::{aref::ARef, rcu, Completion},
    types::{ForeignOwnable, Opaque, ScopeGuard},
};

use pin_init::Wrapper;

/// [`Devres`] inner data accessed from [`Devres::callback`].
#[pin_data]
struct Inner<T: Send> {
    #[pin]
    data: Revocable<T>,
    /// Tracks whether [`Devres::callback`] has been completed.
    #[pin]
    devm: Completion,
    /// Tracks whether revoking [`Self::data`] has been completed.
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
/// # Examples
///
/// ```no_run
/// # use kernel::{bindings, device::{Bound, Device}, devres::Devres, io::{Io, IoRaw}};
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
///         let addr = unsafe { bindings::ioremap(paddr as bindings::phys_addr_t, SIZE) };
///         if addr.is_null() {
///             return Err(ENOMEM);
///         }
///
///         Ok(IoMem(IoRaw::new(addr as usize, SIZE)?))
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
/// let devres = KBox::pin_init(Devres::new(dev, iomem), GFP_KERNEL)?;
///
/// let res = devres.try_access().ok_or(ENXIO)?;
/// res.write8(0x42, 0x0);
/// # Ok(())
/// # }
/// ```
///
/// # Invariants
///
/// `Self::inner` is guaranteed to be initialized and is always accessed read-only.
#[pin_data(PinnedDrop)]
pub struct Devres<T: Send> {
    dev: ARef<Device>,
    /// Pointer to [`Self::devres_callback`].
    ///
    /// Has to be stored, since Rust does not guarantee to always return the same address for a
    /// function. However, the C API uses the address as a key.
    callback: unsafe extern "C" fn(*mut c_void),
    /// Contains all the fields shared with [`Self::callback`].
    // TODO: Replace with `UnsafePinned`, once available.
    //
    // Subsequently, the `drop_in_place()` in `Devres::drop` and `Devres::new` as well as the
    // explicit `Send` and `Sync' impls can be removed.
    #[pin]
    inner: Opaque<Inner<T>>,
    _add_action: (),
}

impl<T: Send> Devres<T> {
    /// Creates a new [`Devres`] instance of the given `data`.
    ///
    /// The `data` encapsulated within the returned `Devres` instance' `data` will be
    /// (revoked)[`Revocable`] once the device is detached.
    pub fn new<'a, E>(
        dev: &'a Device<Bound>,
        data: impl PinInit<T, E> + 'a,
    ) -> impl PinInit<Self, Error> + 'a
    where
        T: 'a,
        Error: From<E>,
    {
        try_pin_init!(&this in Self {
            dev: dev.into(),
            callback: Self::devres_callback,
            // INVARIANT: `inner` is properly initialized.
            inner <- Opaque::pin_init(try_pin_init!(Inner {
                    devm <- Completion::new(),
                    revoke <- Completion::new(),
                    data <- Revocable::new(data),
            })),
            // TODO: Replace with "initializer code blocks" [1] once available.
            //
            // [1] https://github.com/Rust-for-Linux/pin-init/pull/69
            _add_action: {
                // SAFETY: `this` is a valid pointer to uninitialized memory.
                let inner = unsafe { &raw mut (*this.as_ptr()).inner };

                // SAFETY:
                // - `dev.as_raw()` is a pointer to a valid bound device.
                // - `inner` is guaranteed to be a valid for the duration of the lifetime of `Self`.
                // - `devm_add_action()` is guaranteed not to call `callback` until `this` has been
                //    properly initialized, because we require `dev` (i.e. the *bound* device) to
                //    live at least as long as the returned `impl PinInit<Self, Error>`.
                to_result(unsafe {
                    bindings::devm_add_action(dev.as_raw(), Some(*callback), inner.cast())
                }).inspect_err(|_| {
                    let inner = Opaque::cast_into(inner);

                    // SAFETY: `inner` is a valid pointer to an `Inner<T>` and valid for both reads
                    // and writes.
                    unsafe { core::ptr::drop_in_place(inner) };
                })?;
            },
        })
    }

    fn inner(&self) -> &Inner<T> {
        // SAFETY: By the type invairants of `Self`, `inner` is properly initialized and always
        // accessed read-only.
        unsafe { &*self.inner.get() }
    }

    fn data(&self) -> &Revocable<T> {
        &self.inner().data
    }

    #[allow(clippy::missing_safety_doc)]
    unsafe extern "C" fn devres_callback(ptr: *mut kernel::ffi::c_void) {
        // SAFETY: In `Self::new` we've passed a valid pointer to `Inner` to `devm_add_action()`,
        // hence `ptr` must be a valid pointer to `Inner`.
        let inner = unsafe { &*ptr.cast::<Inner<T>>() };

        // Ensure that `inner` can't be used anymore after we signal completion of this callback.
        let inner = ScopeGuard::new_with_data(inner, |inner| inner.devm.complete_all());

        if !inner.data.revoke() {
            // If `revoke()` returns false, it means that `Devres::drop` already started revoking
            // `data` for us. Hence we have to wait until `Devres::drop` signals that it
            // completed revoking `data`.
            inner.revoke.wait_for_completion();
        }
    }

    fn remove_action(&self) -> bool {
        // SAFETY:
        // - `self.dev` is a valid `Device`,
        // - the `action` and `data` pointers are the exact same ones as given to
        //   `devm_add_action()` previously,
        (unsafe {
            bindings::devm_remove_action_nowarn(
                self.dev.as_raw(),
                Some(self.callback),
                core::ptr::from_ref(self.inner()).cast_mut().cast(),
            )
        } == 0)
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

#[pinned_drop]
impl<T: Send> PinnedDrop for Devres<T> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: When `drop` runs, it is guaranteed that nobody is accessing the revocable data
        // anymore, hence it is safe not to wait for the grace period to finish.
        if unsafe { self.data().revoke_nosync() } {
            // We revoked `self.data` before the devres action did, hence try to remove it.
            if !self.remove_action() {
                // We could not remove the devres action, which means that it now runs concurrently,
                // hence signal that `self.data` has been revoked by us successfully.
                self.inner().revoke.complete_all();

                // Wait for `Self::devres_callback` to be done using this object.
                self.inner().devm.wait_for_completion();
            }
        } else {
            // `Self::devres_callback` revokes `self.data` for us, hence wait for it to be done
            // using this object.
            self.inner().devm.wait_for_completion();
        }

        // INVARIANT: At this point it is guaranteed that `inner` can't be accessed any more.
        //
        // SAFETY: `inner` is valid for dropping.
        unsafe { core::ptr::drop_in_place(self.inner.get()) };
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
/// use kernel::{device::{Bound, Device}, devres};
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
