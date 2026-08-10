// SPDX-License-Identifier: GPL-2.0

//! Formatting utilities.
//!
//! This module is intended to be used in place of `core::fmt` in kernel code.

use kernel::prelude::*;

pub use core::fmt::{
    Arguments,
    Debug,
    Error,
    Formatter,
    Result,
    Write, //
};

/// Internal adapter used to route and allow implementations of formatting traits for foreign types.
///
/// It is inserted automatically by the [`fmt!`] macro and is not meant to be used directly.
///
/// [`fmt!`]: crate::prelude::fmt!
#[doc(hidden)]
pub struct Adapter<T>(pub T);

macro_rules! impl_fmt_adapter_forward {
    ($($trait:ident),* $(,)?) => {
        $(
            impl<T: $trait> $trait for Adapter<T> {
                fn fmt(&self, f: &mut Formatter<'_>) -> Result {
                    let Self(t) = self;
                    $trait::fmt(t, f)
                }
            }
        )*
    };
}

use core::fmt::{
    Binary,
    LowerExp,
    LowerHex,
    Octal,
    UpperExp,
    UpperHex, //
};
use core::ptr::NonNull;
impl_fmt_adapter_forward!(Debug, LowerHex, UpperHex, Octal, Binary, LowerExp, UpperExp);

/// A copy of [`core::fmt::Pointer`] that allows implementing pointer formatting for foreign types.
///
/// Together with the [`Adapter`] type and [`fmt!`] macro, it enables raw pointer formatting to be
/// intercepted and routed to [`HashedPtr`] (kernel's `%p` hashed format), preventing kernel address
/// leaks.
///
/// [`fmt!`]: crate::prelude::fmt!
pub trait Pointer {
    /// Same as [`core::fmt::Pointer::fmt`].
    fn fmt(&self, f: &mut Formatter<'_>) -> Result;
}

/// A wrapper for pointers that formats them using kernel's `%p` format specifier.
///
/// By default, `%p` prints a hashed representation of the pointer address to prevent kernel address
/// leaks. When the `no_hash_pointers` kernel command-line parameter is enabled, the real address is
/// printed instead (for debugging purposes).
pub struct HashedPtr<T: ?Sized>(pub *const T);

impl<T: ?Sized> Pointer for HashedPtr<T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        use crate::str::CStrExt as _;

        let mut buf = [0u8; 32];

        // Use `%#0*p` for the `0x` prefix and zero-padding; `+2` compensates for
        // the prefix counting toward the field width.
        let default_width = (2 * size_of::<usize>() + 2) as c_int;
        let width = match (f.sign_aware_zero_pad(), f.width()) {
            (true, Some(w)) if w > 0 => w.min(buf.len() - 1) as c_int,
            _ => default_width,
        };

        // SAFETY: `buf` is a valid, writable 32-byte buffer, sufficient for
        // all architectures (max 19 bytes for 64-bit under the default width).
        // The format string is null-terminated; `width` (c_int) and pointer
        // match the `%*` and `%p` specifiers.
        let len = unsafe {
            crate::bindings::scnprintf(
                buf.as_mut_ptr().cast(),
                buf.len(),
                c"%#0*p".as_char_ptr(),
                width,
                self.0.cast::<c_void>(),
            )
        };

        // SAFETY: `%#0*p` produces only ASCII, which is valid UTF-8.
        let s = unsafe { core::str::from_utf8_unchecked(&buf[..len as usize]) };

        if f.sign_aware_zero_pad() {
            // `scnprintf` already applied the width and zero-padding via `%#0*p`.
            f.write_str(s)
        } else {
            f.pad(s)
        }
    }
}

// Raw pointers are formatted via `HashedPtr` (kernel `%p`: hashed by default, plain with
// `no_hash_pointers`).
impl<T: ?Sized> Pointer for *const T {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        Pointer::fmt(&HashedPtr(*self), f)
    }
}

impl<T: ?Sized> Pointer for *mut T {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        Pointer::fmt(&HashedPtr(*self), f)
    }
}

impl<T: ?Sized> Pointer for &T {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        Pointer::fmt(&HashedPtr(*self), f)
    }
}

impl<T: ?Sized> Pointer for &mut T {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        Pointer::fmt(&HashedPtr(core::ptr::from_ref(*self)), f)
    }
}

impl<T: ?Sized> Pointer for NonNull<T> {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        Pointer::fmt(&HashedPtr(self.as_ptr()), f)
    }
}

// `Adapter<&T>` bridges our `Pointer` trait to `core::fmt::Pointer`
impl<T: Pointer> core::fmt::Pointer for Adapter<&T> {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        Pointer::fmt(self.0, f)
    }
}

/// A copy of [`core::fmt::Display`] that allows us to implement it for foreign types.
///
/// Types should implement this trait rather than [`core::fmt::Display`]. Together with the
/// [`Adapter`] type and [`fmt!`] macro, it allows for formatting foreign types (e.g. types from
/// core) which do not implement [`core::fmt::Display`] directly.
///
/// [`fmt!`]: crate::prelude::fmt!
pub trait Display {
    /// Same as [`core::fmt::Display::fmt`].
    fn fmt(&self, f: &mut Formatter<'_>) -> Result;
}

impl<T: ?Sized + Display> Display for &T {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        Display::fmt(*self, f)
    }
}

impl<T: ?Sized + Display> core::fmt::Display for Adapter<&T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        let Self(t) = self;
        Display::fmt(t, f)
    }
}

macro_rules! impl_display_forward {
    ($(
        $( { $($generics:tt)* } )? $ty:ty $( { where $($where:tt)* } )?
    ),* $(,)?) => {
        $(
            impl$($($generics)*)? Display for $ty $(where $($where)*)? {
                fn fmt(&self, f: &mut Formatter<'_>) -> Result {
                    core::fmt::Display::fmt(self, f)
                }
            }
        )*
    };
}

impl_display_forward!(
    bool,
    char,
    core::panic::PanicInfo<'_>,
    Arguments<'_>,
    i128,
    i16,
    i32,
    i64,
    i8,
    isize,
    str,
    u128,
    u16,
    u32,
    u64,
    u8,
    usize,
    {<T: ?Sized>} crate::sync::Arc<T> {where crate::sync::Arc<T>: core::fmt::Display},
    {<T: ?Sized>} crate::sync::UniqueArc<T> {where crate::sync::UniqueArc<T>: core::fmt::Display},
);

#[macros::kunit_tests(rust_kernel_fmt)]
mod tests {
    use crate::{
        bindings,
        prelude::fmt,
        str::CString, //
    };

    #[cfg(CONFIG_64BIT)]
    mod expected {
        pub(super) const PTR_VALUE: usize = 0xffffffffdeadbeef;
        pub(super) const PTR_VAL_NO_CRNG: &str = "(____ptrval____)";
        pub(super) const HASHED_PREFIX: &str = "0x00000000";
        pub(super) const RAW_POINTER: &str = "0xffffffffdeadbeef";
        pub(super) const PADDED_RIGHT: &str = "      0xffffffffdeadbeef";
        pub(super) const ZERO_PADDED: &str = "0x000000ffffffffdeadbeef";
        pub(super) const HASHED_PADDED_RIGHT_PREFIX: &str = "      ";
        pub(super) const HASHED_ZERO_PADDED_PREFIX: &str = "0x00000000000000";
        pub(super) const CLAMPED: &str = "0x0000000000000ffffffffdeadbeef";
    }

    #[cfg(not(CONFIG_64BIT))]
    mod expected {
        pub(super) const PTR_VALUE: usize = 0xdeadbeef;
        pub(super) const PTR_VAL_NO_CRNG: &str = "(ptrval)";
        pub(super) const HASHED_PREFIX: &str = "0x";
        pub(super) const RAW_POINTER: &str = "0xdeadbeef";
        pub(super) const PADDED_RIGHT: &str = "              0xdeadbeef";
        pub(super) const ZERO_PADDED: &str = "0x00000000000000deadbeef";
        pub(super) const HASHED_PADDED_RIGHT_PREFIX: &str = "              ";
        pub(super) const HASHED_ZERO_PADDED_PREFIX: &str = "0x00000000000000";
        pub(super) const CLAMPED: &str = "0x0000000000000000000000deadbeef";
    }

    #[test]
    fn test_ptr_formatting() -> core::result::Result<(), crate::error::Error> {
        let ptr: *const u8 = core::ptr::without_provenance(expected::PTR_VALUE);

        // SAFETY: `no_hash_pointers` is a global variable that is never concurrently modified —
        // KUnit tests may run at boot (before `mark_readonly()`) or manually afterwards (when the
        // variable is read-only). Reading is always safe.
        let no_hash = unsafe { bindings::no_hash_pointers };

        if no_hash {
            let cstr = CString::try_from_fmt(fmt!("{:p}", ptr))?;
            assert_eq!(cstr.to_str()?, expected::RAW_POINTER);

            let cstr = CString::try_from_fmt(fmt!("{:>24p}", ptr))?;
            assert_eq!(cstr.to_str()?, expected::PADDED_RIGHT);

            let cstr = CString::try_from_fmt(fmt!("{:024p}", ptr))?;
            assert_eq!(cstr.to_str()?, expected::ZERO_PADDED);

            let cstr = CString::try_from_fmt(fmt!("{:0100p}", ptr))?;
            assert_eq!(cstr.to_str()?, expected::CLAMPED);
        } else {
            let cstr = CString::try_from_fmt(fmt!("{:p}", ptr))?;
            let formatted = cstr.to_str()?;
            // If the RNG is not yet ready, `%p` falls back to a placeholder.
            if formatted == expected::PTR_VAL_NO_CRNG {
                return Ok(());
            }
            assert!(formatted.starts_with(expected::HASHED_PREFIX));
            assert_ne!(formatted, expected::RAW_POINTER);

            let cstr = CString::try_from_fmt(fmt!("{:>24p}", ptr))?;
            assert!(cstr
                .to_str()?
                .starts_with(expected::HASHED_PADDED_RIGHT_PREFIX));

            let cstr = CString::try_from_fmt(fmt!("{:024p}", ptr))?;
            assert!(cstr
                .to_str()?
                .starts_with(expected::HASHED_ZERO_PADDED_PREFIX));

            let cstr = CString::try_from_fmt(fmt!("{:0100p}", ptr))?;
            let output = cstr.to_str()?;
            assert!(output.starts_with("0x"));
            assert!(!output[2..].chars().all(|c| c == '0'));
        }

        Ok(())
    }
}
