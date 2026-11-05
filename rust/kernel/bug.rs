// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024, 2025 FUJITA Tomonori <fujita.tomonori@gmail.com>

//! Support for BUG and WARN functionality.
//!
//! C header: [`include/asm-generic/bug.h`](srctree/include/asm-generic/bug.h)

#[macro_export]
#[doc(hidden)]
#[cfg(not(testlib))]
#[cfg(all(CONFIG_BUG, not(CONFIG_UML), not(CONFIG_LOONGARCH), not(CONFIG_ARM)))]
#[cfg(CONFIG_DEBUG_BUGVERBOSE)]
macro_rules! warn_flags {
    ($file:expr, $flags:expr) => {
        const FLAGS: u32 = $crate::bindings::BUGFLAG_WARNING | $flags;
        const _FILE: &[u8] = $file.as_bytes();
        // Plus one for null-terminator.
        static FILE: [u8; _FILE.len() + 1] = {
            let mut bytes = [0; _FILE.len() + 1];
            let mut i = 0;
            while i < _FILE.len() {
                bytes[i] = _FILE[i];
                i += 1;
            }
            bytes
        };

        // SAFETY:
        // - `file`, `line`, `flags`, and `size` are all compile-time constants or
        // symbols, preventing any invalid memory access.
        // - The asm block has no side effects and does not modify any registers
        // or memory. It is purely for embedding metadata into the ELF section.
        unsafe {
            $crate::asm!(
                concat!(
                    "/* {size} */",
                    include!(concat!(env!("OBJTREE"), "/rust/kernel/generated_arch_warn_asm.rs")),
                    include!(concat!(env!("OBJTREE"), "/rust/kernel/generated_arch_reachable_asm.rs")));
                file = sym FILE,
                line = const line!(),
                flags = const FLAGS,
                size = const ::core::mem::size_of::<$crate::bindings::bug_entry>(),
            );
        }
    }
}

#[macro_export]
#[doc(hidden)]
#[cfg(not(testlib))]
#[cfg(all(CONFIG_BUG, not(CONFIG_UML), not(CONFIG_LOONGARCH), not(CONFIG_ARM)))]
#[cfg(not(CONFIG_DEBUG_BUGVERBOSE))]
macro_rules! warn_flags {
    ($file:expr, $flags:expr) => {
        const FLAGS: u32 = $crate::bindings::BUGFLAG_WARNING | $flags;

        if false {
            _ = $file;
        }

        // SAFETY:
        // - `flags` and `size` are all compile-time constants, preventing
        // any invalid memory access.
        // - The asm block has no side effects and does not modify any registers
        // or memory. It is purely for embedding metadata into the ELF section.
        unsafe {
            $crate::asm!(
                concat!(
                    "/* {size} */",
                    include!(concat!(env!("OBJTREE"), "/rust/kernel/generated_arch_warn_asm.rs")),
                    include!(concat!(env!("OBJTREE"), "/rust/kernel/generated_arch_reachable_asm.rs")));
                flags = const FLAGS,
                size = const ::core::mem::size_of::<$crate::bindings::bug_entry>(),
            );
        }
    }
}

#[macro_export]
#[doc(hidden)]
#[cfg(not(testlib))]
#[cfg(all(CONFIG_BUG, CONFIG_UML))]
macro_rules! warn_flags {
    ($file:expr, $flags:expr) => {
        if false {
            _ = $file;
            _ = $flags;
        }

        // SAFETY:
        // - The file name is a null-terminated string with `'static` lifetime.
        // - `TAINT_WARN` is a valid taint number.
        // - `fmt` can be NULL, and NULL is passed.
        unsafe {
            $crate::bindings::warn_slowpath_fmt(
                $crate::str::CStrExt::as_char_ptr($crate::c_str!(::core::file!())),
                line!() as $crate::ffi::c_int,
                $crate::bindings::TAINT_WARN,
                ::core::ptr::null(),
            );
        }
    };
}

#[macro_export]
#[doc(hidden)]
#[cfg(not(testlib))]
#[cfg(all(CONFIG_BUG, any(CONFIG_LOONGARCH, CONFIG_ARM)))]
macro_rules! warn_flags {
    ($file:expr, $flags:expr) => {
        if false {
            _ = $file;
            _ = $flags;
        }

        // SAFETY: It is always safe to call `WARN_ON()`.
        unsafe { $crate::bindings::WARN_ON(true) }
    };
}

#[macro_export]
#[doc(hidden)]
#[cfg(any(testlib, not(CONFIG_BUG)))]
macro_rules! warn_flags {
    ($file:expr, $flags:expr) => {
        if false {
            _ = $file;
            _ = $flags;
        }
    };
}

#[doc(hidden)]
pub const fn bugflag_taint(value: u32) -> u32 {
    value << 8
}

/// Report a warning if `cond` is true and return the condition's evaluation result.
#[macro_export]
macro_rules! warn_on {
    ($cond:expr) => {{
        let cond = $cond;

        #[cfg(CONFIG_DEBUG_BUGVERBOSE_DETAILED)]
        const COND_STR: &str = concat!("[", stringify!($cond), "] ", file!());
        #[cfg(not(CONFIG_DEBUG_BUGVERBOSE_DETAILED))]
        const COND_STR: &str = file!();

        if cond {
            const WARN_ON_FLAGS: u32 = $crate::bug::bugflag_taint($crate::bindings::TAINT_WARN);

            $crate::warn_flags!(COND_STR, WARN_ON_FLAGS);
        }
        cond
    }};
}

#[cfg(CONFIG_RUST_BUG_KUNIT_TEST)]
#[macros::kunit_tests(rust_kernel_bug)]
mod tests {
    // The counter is incremented by the kernel warning path, not by `warn_on!`
    // itself. A count of one means the warning was really reported.
    #[test]
    fn test_warn_on() {
        // SAFETY: `kunit_get_current_test()` is always safe to call (it has
        // fallbacks for when no KUnit test is running).
        let test = unsafe { bindings::kunit_get_current_test() };

        // SAFETY: This function runs only as a KUnit test case, so `test` is a
        // valid pointer to the running test.
        let handle = unsafe { bindings::kunit_start_suppress_warning(test) };

        assert!(!warn_on!(false));
        assert!(warn_on!(true));

        // SAFETY: `kunit_suppressed_warning_count()` accepts any value returned by
        // `kunit_start_suppress_warning()`.
        let suppressed_count = unsafe { bindings::kunit_suppressed_warning_count(handle) };

        // SAFETY: `test` is valid as above. `kunit_end_suppress_warning()` accepts any
        // value returned by `kunit_start_suppress_warning()`.
        unsafe { bindings::kunit_end_suppress_warning(test, handle) };

        assert_eq!(suppressed_count, 1);
    }
}
