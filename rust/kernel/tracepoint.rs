// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Logic for tracepoints.

/// Declare the Rust entry point for a tracepoint.
///
/// This macro generates an unsafe function that calls into C, and its safety requirements will be
/// whatever the relevant C code requires. To document these safety requirements, you may add
/// doc-comments when invoking the macro.
#[macro_export]
macro_rules! declare_trace {
    ($($(#[$attr:meta])* $pub:vis unsafe fn $name:ident($($argname:ident : $argtyp:ty),* $(,)?);)*) => {$(
        $( #[$attr] )*
        #[inline(always)]
        $pub unsafe fn $name($($argname : $argtyp),*) {
            #[cfg(CONFIG_TRACEPOINTS)]
            {
                // SAFETY: It's always okay to query the static key for a tracepoint.
                let should_trace = unsafe {
                    $crate::macros::paste! {
                        $crate::jump_label::static_branch_unlikely!(
                            $crate::bindings::[< __tracepoint_ $name >],
                            $crate::bindings::tracepoint,
                            key
                        )
                    }
                };

                if should_trace {
                    $crate::macros::paste! {
                        // SAFETY: The caller guarantees that it is okay to call this tracepoint.
                        unsafe { $crate::bindings::[< rust_do_trace_ $name >]($($argname),*) };
                    }
                }
            }

            #[cfg(not(CONFIG_TRACEPOINTS))]
            {
                // If tracepoints are disabled, insert a trivial use of each argument
                // to avoid unused argument warnings.
                $( let _unused = $argname; )*
            }
        }
    )*}
}

pub use declare_trace;
