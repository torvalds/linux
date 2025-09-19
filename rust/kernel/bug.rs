// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024, 2025 FUJITA Tomonori <fujita.tomonori@gmail.com>

//! Support for BUG and WARN functionality.
//!
//! C header: [`include/asm-generic/bug.h`](srctree/include/asm-generic/bug.h)

#[macro_export]
#[doc(hidden)]
#[cfg(all(CONFIG_BUG, not(CONFIG_UML), not(CONFIG_LOONGARCH), not(CONFIG_ARM)))]
#[cfg(CONFIG_DEBUG_BUGVERBOSE)]
macro_rules! warn_flags {
    ($flags:expr) => {
        const FLAGS: u32 = $crate::bindings::BUGFLAG_WARNING | $flags;
        const _FILE: &[u8] = file!().as_bytes();
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
#[cfg(all(CONFIG_BUG, not(CONFIG_UML), not(CONFIG_LOONGARCH), not(CONFIG_ARM)))]
#[cfg(not(CONFIG_DEBUG_BUGVERBOSE))]
macro_rules! warn_flags {
    ($flags:expr) => {
        const FLAGS: u32 = $crate::bindings::BUGFLAG_WARNING | $flags;

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
#[cfg(all(CONFIG_BUG, CONFIG_UML))]
macro_rules! warn_flags {
    ($flags:expr) => {
        // SAFETY: It is always safe to call `warn_slowpath_fmt()`
        // with a valid null-terminated string.
        unsafe {
            $crate::bindings::warn_slowpath_fmt(
                $crate::c_str!(::core::file!()).as_char_ptr(),
                line!() as $crate::ffi::c_int,
                $flags as $crate::ffi::c_uint,
                ::core::ptr::null(),
            );
        }
    };
}

#[macro_export]
#[doc(hidden)]
#[cfg(all(CONFIG_BUG, any(CONFIG_LOONGARCH, CONFIG_ARM)))]
macro_rules! warn_flags {
    ($flags:expr) => {
        // SAFETY: It is always safe to call `WARN_ON()`.
        unsafe { $crate::bindings::WARN_ON(true) }
    };
}

#[macro_export]
#[doc(hidden)]
#[cfg(not(CONFIG_BUG))]
macro_rules! warn_flags {
    ($flags:expr) => {};
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
        if cond {
            const WARN_ON_FLAGS: u32 = $crate::bug::bugflag_taint($crate::bindings::TAINT_WARN);

            $crate::warn_flags!(WARN_ON_FLAGS);
        }
        cond
    }};
}
