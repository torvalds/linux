// SPDX-License-Identifier: GPL-2.0

//! Printing facilities.
//!
//! C header: [`include/linux/printk.h`](../../../../include/linux/printk.h)
//!
//! Reference: <https://www.kernel.org/doc/html/latest/core-api/printk-basics.html>

use core::{
    ffi::{c_char, c_void},
    fmt,
};

use crate::str::RawFormatter;

#[cfg(CONFIG_PRINTK)]
use crate::bindings;

// Called from `vsprintf` with format specifier `%pA`.
#[no_mangle]
unsafe fn rust_fmt_argument(buf: *mut c_char, end: *mut c_char, ptr: *const c_void) -> *mut c_char {
    use fmt::Write;
    // SAFETY: The C contract guarantees that `buf` is valid if it's less than `end`.
    let mut w = unsafe { RawFormatter::from_ptrs(buf.cast(), end.cast()) };
    let _ = w.write_fmt(unsafe { *(ptr as *const fmt::Arguments<'_>) });
    w.pos().cast()
}

/// Format strings.
///
/// Public but hidden since it should only be used from public macros.
#[doc(hidden)]
pub mod format_strings {
    use crate::bindings;

    /// The length we copy from the `KERN_*` kernel prefixes.
    const LENGTH_PREFIX: usize = 2;

    /// The length of the fixed format strings.
    pub const LENGTH: usize = 10;

    /// Generates a fixed format string for the kernel's [`_printk`].
    ///
    /// The format string is always the same for a given level, i.e. for a
    /// given `prefix`, which are the kernel's `KERN_*` constants.
    ///
    /// [`_printk`]: ../../../../include/linux/printk.h
    const fn generate(is_cont: bool, prefix: &[u8; 3]) -> [u8; LENGTH] {
        // Ensure the `KERN_*` macros are what we expect.
        assert!(prefix[0] == b'\x01');
        if is_cont {
            assert!(prefix[1] == b'c');
        } else {
            assert!(prefix[1] >= b'0' && prefix[1] <= b'7');
        }
        assert!(prefix[2] == b'\x00');

        let suffix: &[u8; LENGTH - LENGTH_PREFIX] = if is_cont {
            b"%pA\0\0\0\0\0"
        } else {
            b"%s: %pA\0"
        };

        [
            prefix[0], prefix[1], suffix[0], suffix[1], suffix[2], suffix[3], suffix[4], suffix[5],
            suffix[6], suffix[7],
        ]
    }

    // Generate the format strings at compile-time.
    //
    // This avoids the compiler generating the contents on the fly in the stack.
    //
    // Furthermore, `static` instead of `const` is used to share the strings
    // for all the kernel.
    pub static EMERG: [u8; LENGTH] = generate(false, bindings::KERN_EMERG);
    pub static INFO: [u8; LENGTH] = generate(false, bindings::KERN_INFO);
}

/// Prints a message via the kernel's [`_printk`].
///
/// Public but hidden since it should only be used from public macros.
///
/// # Safety
///
/// The format string must be one of the ones in [`format_strings`], and
/// the module name must be null-terminated.
///
/// [`_printk`]: ../../../../include/linux/_printk.h
#[doc(hidden)]
#[cfg_attr(not(CONFIG_PRINTK), allow(unused_variables))]
pub unsafe fn call_printk(
    format_string: &[u8; format_strings::LENGTH],
    module_name: &[u8],
    args: fmt::Arguments<'_>,
) {
    // `_printk` does not seem to fail in any path.
    #[cfg(CONFIG_PRINTK)]
    unsafe {
        bindings::_printk(
            format_string.as_ptr() as _,
            module_name.as_ptr(),
            &args as *const _ as *const c_void,
        );
    }
}

/// Performs formatting and forwards the string to [`call_printk`].
///
/// Public but hidden since it should only be used from public macros.
#[doc(hidden)]
#[cfg(not(testlib))]
#[macro_export]
#[allow(clippy::crate_in_macro_def)]
macro_rules! print_macro (
    // The non-continuation cases (most of them, e.g. `INFO`).
    ($format_string:path, $($arg:tt)+) => (
        // To remain sound, `arg`s must be expanded outside the `unsafe` block.
        // Typically one would use a `let` binding for that; however, `format_args!`
        // takes borrows on the arguments, but does not extend the scope of temporaries.
        // Therefore, a `match` expression is used to keep them around, since
        // the scrutinee is kept until the end of the `match`.
        match format_args!($($arg)+) {
            // SAFETY: This hidden macro should only be called by the documented
            // printing macros which ensure the format string is one of the fixed
            // ones. All `__LOG_PREFIX`s are null-terminated as they are generated
            // by the `module!` proc macro or fixed values defined in a kernel
            // crate.
            args => unsafe {
                $crate::print::call_printk(
                    &$format_string,
                    crate::__LOG_PREFIX,
                    args,
                );
            }
        }
    );
);

/// Stub for doctests
#[cfg(testlib)]
#[macro_export]
macro_rules! print_macro (
    ($format_string:path, $e:expr, $($arg:tt)+) => (
        ()
    );
);

// We could use a macro to generate these macros. However, doing so ends
// up being a bit ugly: it requires the dollar token trick to escape `$` as
// well as playing with the `doc` attribute. Furthermore, they cannot be easily
// imported in the prelude due to [1]. So, for the moment, we just write them
// manually, like in the C side; while keeping most of the logic in another
// macro, i.e. [`print_macro`].
//
// [1]: https://github.com/rust-lang/rust/issues/52234

/// Prints an emergency-level message (level 0).
///
/// Use this level if the system is unusable.
///
/// Equivalent to the kernel's [`pr_emerg`] macro.
///
/// Mimics the interface of [`std::print!`]. See [`core::fmt`] and
/// `alloc::format!` for information about the formatting syntax.
///
/// [`pr_emerg`]: https://www.kernel.org/doc/html/latest/core-api/printk-basics.html#c.pr_emerg
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
///
/// # Examples
///
/// ```
/// pr_emerg!("hello {}\n", "there");
/// ```
#[macro_export]
macro_rules! pr_emerg (
    ($($arg:tt)*) => (
        $crate::print_macro!($crate::print::format_strings::EMERG, $($arg)*)
    )
);

/// Prints an info-level message (level 6).
///
/// Use this level for informational messages.
///
/// Equivalent to the kernel's [`pr_info`] macro.
///
/// Mimics the interface of [`std::print!`]. See [`core::fmt`] and
/// `alloc::format!` for information about the formatting syntax.
///
/// [`pr_info`]: https://www.kernel.org/doc/html/latest/core-api/printk-basics.html#c.pr_info
/// [`std::print!`]: https://doc.rust-lang.org/std/macro.print.html
///
/// # Examples
///
/// ```
/// pr_info!("hello {}\n", "there");
/// ```
#[macro_export]
#[doc(alias = "print")]
macro_rules! pr_info (
    ($($arg:tt)*) => (
        $crate::print_macro!($crate::print::format_strings::INFO, $($arg)*)
    )
);
