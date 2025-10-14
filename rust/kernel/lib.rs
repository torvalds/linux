// SPDX-License-Identifier: GPL-2.0

//! The `kernel` crate.
//!
//! This crate contains the kernel APIs that have been ported or wrapped for
//! usage by Rust code in the kernel and is shared by all of them.
//!
//! In other words, all the rest of the Rust code in the kernel (e.g. kernel
//! modules written in Rust) depends on [`core`] and this crate.
//!
//! If you need a kernel C API that is not ported or wrapped yet here, then
//! do so first instead of bypassing this crate.

#![no_std]
//
// Please see https://github.com/Rust-for-Linux/linux/issues/2 for details on
// the unstable features in use.
//
// Stable since Rust 1.79.0.
#![feature(generic_nonzero)]
#![feature(inline_const)]
#![feature(pointer_is_aligned)]
//
// Stable since Rust 1.81.0.
#![feature(lint_reasons)]
//
// Stable since Rust 1.82.0.
#![feature(raw_ref_op)]
//
// Stable since Rust 1.83.0.
#![feature(const_maybe_uninit_as_mut_ptr)]
#![feature(const_mut_refs)]
#![feature(const_option)]
#![feature(const_ptr_write)]
#![feature(const_refs_to_cell)]
//
// Expected to become stable.
#![feature(arbitrary_self_types)]
//
// To be determined.
#![feature(used_with_arg)]
//
// `feature(derive_coerce_pointee)` is expected to become stable. Before Rust
// 1.84.0, it did not exist, so enable the predecessor features.
#![cfg_attr(CONFIG_RUSTC_HAS_COERCE_POINTEE, feature(derive_coerce_pointee))]
#![cfg_attr(not(CONFIG_RUSTC_HAS_COERCE_POINTEE), feature(coerce_unsized))]
#![cfg_attr(not(CONFIG_RUSTC_HAS_COERCE_POINTEE), feature(dispatch_from_dyn))]
#![cfg_attr(not(CONFIG_RUSTC_HAS_COERCE_POINTEE), feature(unsize))]
//
// `feature(file_with_nul)` is expected to become stable. Before Rust 1.89.0, it did not exist, so
// enable it conditionally.
#![cfg_attr(CONFIG_RUSTC_HAS_FILE_WITH_NUL, feature(file_with_nul))]

// Ensure conditional compilation based on the kernel configuration works;
// otherwise we may silently break things like initcall handling.
#[cfg(not(CONFIG_RUST))]
compile_error!("Missing kernel configuration for conditional compilation");

// Allow proc-macros to refer to `::kernel` inside the `kernel` crate (this crate).
extern crate self as kernel;

pub use ffi;

pub mod acpi;
pub mod alloc;
#[cfg(CONFIG_AUXILIARY_BUS)]
pub mod auxiliary;
pub mod bitmap;
pub mod bits;
#[cfg(CONFIG_BLOCK)]
pub mod block;
pub mod bug;
#[doc(hidden)]
pub mod build_assert;
pub mod clk;
#[cfg(CONFIG_CONFIGFS_FS)]
pub mod configfs;
pub mod cpu;
#[cfg(CONFIG_CPU_FREQ)]
pub mod cpufreq;
pub mod cpumask;
pub mod cred;
pub mod debugfs;
pub mod device;
pub mod device_id;
pub mod devres;
pub mod dma;
pub mod driver;
#[cfg(CONFIG_DRM = "y")]
pub mod drm;
pub mod error;
pub mod faux;
#[cfg(CONFIG_RUST_FW_LOADER_ABSTRACTIONS)]
pub mod firmware;
pub mod fmt;
pub mod fs;
pub mod id_pool;
pub mod init;
pub mod io;
pub mod ioctl;
pub mod iov;
pub mod irq;
pub mod jump_label;
#[cfg(CONFIG_KUNIT)]
pub mod kunit;
pub mod list;
pub mod maple_tree;
pub mod miscdevice;
pub mod mm;
#[cfg(CONFIG_NET)]
pub mod net;
pub mod of;
#[cfg(CONFIG_PM_OPP)]
pub mod opp;
pub mod page;
#[cfg(CONFIG_PCI)]
pub mod pci;
pub mod pid_namespace;
pub mod platform;
pub mod prelude;
pub mod print;
pub mod processor;
pub mod ptr;
pub mod rbtree;
pub mod regulator;
pub mod revocable;
pub mod scatterlist;
pub mod security;
pub mod seq_file;
pub mod sizes;
mod static_assert;
#[doc(hidden)]
pub mod std_vendor;
pub mod str;
pub mod sync;
pub mod task;
pub mod time;
pub mod tracepoint;
pub mod transmute;
pub mod types;
pub mod uaccess;
pub mod workqueue;
pub mod xarray;

#[doc(hidden)]
pub use bindings;
pub use macros;
pub use uapi;

/// Prefix to appear before log messages printed from within the `kernel` crate.
const __LOG_PREFIX: &[u8] = b"rust_kernel\0";

/// The top level entrypoint to implementing a kernel module.
///
/// For any teardown or cleanup operations, your type may implement [`Drop`].
pub trait Module: Sized + Sync + Send {
    /// Called at module initialization time.
    ///
    /// Use this method to perform whatever setup or registration your module
    /// should do.
    ///
    /// Equivalent to the `module_init` macro in the C API.
    fn init(module: &'static ThisModule) -> error::Result<Self>;
}

/// A module that is pinned and initialised in-place.
pub trait InPlaceModule: Sync + Send {
    /// Creates an initialiser for the module.
    ///
    /// It is called when the module is loaded.
    fn init(module: &'static ThisModule) -> impl pin_init::PinInit<Self, error::Error>;
}

impl<T: Module> InPlaceModule for T {
    fn init(module: &'static ThisModule) -> impl pin_init::PinInit<Self, error::Error> {
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
}

/// Equivalent to `THIS_MODULE` in the C API.
///
/// C header: [`include/linux/init.h`](srctree/include/linux/init.h)
pub struct ThisModule(*mut bindings::module);

// SAFETY: `THIS_MODULE` may be used from all threads within a module.
unsafe impl Sync for ThisModule {}

impl ThisModule {
    /// Creates a [`ThisModule`] given the `THIS_MODULE` pointer.
    ///
    /// # Safety
    ///
    /// The pointer must be equal to the right `THIS_MODULE`.
    pub const unsafe fn from_ptr(ptr: *mut bindings::module) -> ThisModule {
        ThisModule(ptr)
    }

    /// Access the raw pointer for this module.
    ///
    /// It is up to the user to use it correctly.
    pub const fn as_ptr(&self) -> *mut bindings::module {
        self.0
    }
}

#[cfg(not(testlib))]
#[panic_handler]
fn panic(info: &core::panic::PanicInfo<'_>) -> ! {
    pr_emerg!("{}\n", info);
    // SAFETY: FFI call.
    unsafe { bindings::BUG() };
}

/// Produces a pointer to an object from a pointer to one of its fields.
///
/// If you encounter a type mismatch due to the [`Opaque`] type, then use [`Opaque::cast_into`] or
/// [`Opaque::cast_from`] to resolve the mismatch.
///
/// [`Opaque`]: crate::types::Opaque
/// [`Opaque::cast_into`]: crate::types::Opaque::cast_into
/// [`Opaque::cast_from`]: crate::types::Opaque::cast_from
///
/// # Safety
///
/// The pointer passed to this macro, and the pointer returned by this macro, must both be in
/// bounds of the same allocation.
///
/// # Examples
///
/// ```
/// # use kernel::container_of;
/// struct Test {
///     a: u64,
///     b: u32,
/// }
///
/// let test = Test { a: 10, b: 20 };
/// let b_ptr: *const _ = &test.b;
/// // SAFETY: The pointer points at the `b` field of a `Test`, so the resulting pointer will be
/// // in-bounds of the same allocation as `b_ptr`.
/// let test_alias = unsafe { container_of!(b_ptr, Test, b) };
/// assert!(core::ptr::eq(&test, test_alias));
/// ```
#[macro_export]
macro_rules! container_of {
    ($field_ptr:expr, $Container:ty, $($fields:tt)*) => {{
        let offset: usize = ::core::mem::offset_of!($Container, $($fields)*);
        let field_ptr = $field_ptr;
        let container_ptr = field_ptr.byte_sub(offset).cast::<$Container>();
        $crate::assert_same_type(field_ptr, (&raw const (*container_ptr).$($fields)*).cast_mut());
        container_ptr
    }}
}

/// Helper for [`container_of!`].
#[doc(hidden)]
pub fn assert_same_type<T>(_: T, _: T) {}

/// Helper for `.rs.S` files.
#[doc(hidden)]
#[macro_export]
macro_rules! concat_literals {
    ($( $asm:literal )* ) => {
        ::core::concat!($($asm),*)
    };
}

/// Wrapper around `asm!` configured for use in the kernel.
///
/// Uses a semicolon to avoid parsing ambiguities, even though this does not match native `asm!`
/// syntax.
// For x86, `asm!` uses intel syntax by default, but we want to use at&t syntax in the kernel.
#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
#[macro_export]
macro_rules! asm {
    ($($asm:expr),* ; $($rest:tt)*) => {
        ::core::arch::asm!( $($asm)*, options(att_syntax), $($rest)* )
    };
}

/// Wrapper around `asm!` configured for use in the kernel.
///
/// Uses a semicolon to avoid parsing ambiguities, even though this does not match native `asm!`
/// syntax.
// For non-x86 arches we just pass through to `asm!`.
#[cfg(not(any(target_arch = "x86", target_arch = "x86_64")))]
#[macro_export]
macro_rules! asm {
    ($($asm:expr),* ; $($rest:tt)*) => {
        ::core::arch::asm!( $($asm)*, $($rest)* )
    };
}

/// Gets the C string file name of a [`Location`].
///
/// If `Location::file_as_c_str()` is not available, returns a string that warns about it.
///
/// [`Location`]: core::panic::Location
///
/// # Examples
///
/// ```
/// # use kernel::file_from_location;
///
/// #[track_caller]
/// fn foo() {
///     let caller = core::panic::Location::caller();
///
///     // Output:
///     // - A path like "rust/kernel/example.rs" if `file_as_c_str()` is available.
///     // - "<Location::file_as_c_str() not supported>" otherwise.
///     let caller_file = file_from_location(caller);
///
///     // Prints out the message with caller's file name.
///     pr_info!("foo() called in file {caller_file:?}\n");
///
///     # if cfg!(CONFIG_RUSTC_HAS_FILE_WITH_NUL) {
///     #     assert_eq!(Ok(caller.file()), caller_file.to_str());
///     # }
/// }
///
/// # foo();
/// ```
#[inline]
pub fn file_from_location<'a>(loc: &'a core::panic::Location<'a>) -> &'a core::ffi::CStr {
    #[cfg(CONFIG_RUSTC_HAS_FILE_AS_C_STR)]
    {
        loc.file_as_c_str()
    }

    #[cfg(all(CONFIG_RUSTC_HAS_FILE_WITH_NUL, not(CONFIG_RUSTC_HAS_FILE_AS_C_STR)))]
    {
        loc.file_with_nul()
    }

    #[cfg(not(CONFIG_RUSTC_HAS_FILE_WITH_NUL))]
    {
        let _ = loc;
        c"<Location::file_as_c_str() not supported>"
    }
}
