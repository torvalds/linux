// SPDX-License-Identifier: GPL-2.0

//! Module-related types and helpers.

/// The entrypoint to implementing a kernel module.
///
/// For any teardown or cleanup operations, your type may implement [`Drop`].
pub trait Module: Sized + Sync + Send {
    /// Called at module initialization time.
    ///
    /// Use this method to perform whatever setup or registration your module
    /// should do.
    ///
    /// Equivalent to the `module_init` macro in the C API.
    fn init(module: &'static ThisModule) -> crate::error::Result<Self>;
}

/// A module that is pinned and initialised in-place.
pub trait InPlaceModule: Sync + Send {
    /// Creates an initialiser for the module.
    ///
    /// It is called when the module is loaded.
    fn init(module: &'static ThisModule) -> impl pin_init::PinInit<Self, crate::error::Error>;
}

impl<T: Module> InPlaceModule for T {
    fn init(module: &'static ThisModule) -> impl pin_init::PinInit<Self, crate::error::Error> {
        let initer = move |slot: *mut Self| {
            let m = <Self as Module>::init(module)?;

            // SAFETY: `slot` is valid for write per the contract with `pin_init_from_closure`.
            unsafe { slot.write(m) };
            Ok(())
        };

        // SAFETY: On success, `initer` always fully initialises an instance of `Self`.
        unsafe { pin_init::pin_init_from_closure(initer) }
    }
}

/// Metadata attached to a [`Module`] or [`InPlaceModule`].
pub trait ModuleMetadata {
    /// The name of the module as specified in the `module!` macro.
    const NAME: &'static crate::str::CStr;

    /// The module's `THIS_MODULE` pointer.
    const THIS_MODULE: ThisModule;
}

/// Returns a reference to the `THIS_MODULE` of the given module type.
#[inline]
pub const fn this_module<M: ModuleMetadata>() -> &'static ThisModule {
    &M::THIS_MODULE
}

/// Equivalent to `THIS_MODULE` in the C API.
///
/// C header: [`include/linux/init.h`](srctree/include/linux/init.h)
pub struct ThisModule(*mut crate::bindings::module);

// SAFETY: `THIS_MODULE` may be used from all threads within a module.
unsafe impl Sync for ThisModule {}

impl ThisModule {
    /// Creates a [`ThisModule`] given the `THIS_MODULE` pointer.
    ///
    /// # Safety
    ///
    /// The pointer must be equal to the right `THIS_MODULE`.
    pub const unsafe fn from_ptr(ptr: *mut crate::bindings::module) -> ThisModule {
        ThisModule(ptr)
    }

    /// Access the raw pointer for this module.
    ///
    /// It is up to the user to use it correctly.
    pub const fn as_ptr(&self) -> *mut crate::bindings::module {
        self.0
    }
}
