// SPDX-License-Identifier: GPL-2.0

//! Rust printing macros sample.

use kernel::pr_cont;
use kernel::prelude::*;

module! {
    type: RustPrint,
    name: "rust_print",
    author: "Rust for Linux Contributors",
    description: "Rust printing macros sample",
    license: "GPL",
}

struct RustPrint;

#[expect(clippy::disallowed_macros)]
fn arc_print() -> Result {
    use kernel::sync::*;

    let a = Arc::new(1, GFP_KERNEL)?;
    let b = UniqueArc::new("hello, world", GFP_KERNEL)?;

    // Prints the value of data in `a`.
    pr_info!("{}", a);

    // Uses ":?" to print debug fmt of `b`.
    pr_info!("{:?}", b);

    let a: Arc<&str> = b.into();
    let c = a.clone();

    // Uses `dbg` to print, will move `c` (for temporary debugging purposes).
    dbg!(c);

    {
        // `Arc` can be used to delegate dynamic dispatch and the following is an example.
        // Both `i32` and `&str` implement `Display`. This enables us to express a unified
        // behaviour, contract or protocol on both `i32` and `&str` into a single `Arc` of
        // type `Arc<dyn Display>`.

        use core::fmt::Display;
        fn arc_dyn_print(arc: &Arc<dyn Display>) {
            pr_info!("Arc<dyn Display> says {arc}");
        }

        let a_i32_display: Arc<dyn Display> = Arc::new(42i32, GFP_KERNEL)?;
        let a_str_display: Arc<dyn Display> = a.clone();

        arc_dyn_print(&a_i32_display);
        arc_dyn_print(&a_str_display);
    }

    // Pretty-prints the debug formatting with lower-case hexadecimal integers.
    pr_info!("{:#x?}", a);

    Ok(())
}

impl kernel::Module for RustPrint {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        pr_info!("Rust printing macros sample (init)\n");

        pr_emerg!("Emergency message (level 0) without args\n");
        pr_alert!("Alert message (level 1) without args\n");
        pr_crit!("Critical message (level 2) without args\n");
        pr_err!("Error message (level 3) without args\n");
        pr_warn!("Warning message (level 4) without args\n");
        pr_notice!("Notice message (level 5) without args\n");
        pr_info!("Info message (level 6) without args\n");

        pr_info!("A line that");
        pr_cont!(" is continued");
        pr_cont!(" without args\n");

        pr_emerg!("{} message (level {}) with args\n", "Emergency", 0);
        pr_alert!("{} message (level {}) with args\n", "Alert", 1);
        pr_crit!("{} message (level {}) with args\n", "Critical", 2);
        pr_err!("{} message (level {}) with args\n", "Error", 3);
        pr_warn!("{} message (level {}) with args\n", "Warning", 4);
        pr_notice!("{} message (level {}) with args\n", "Notice", 5);
        pr_info!("{} message (level {}) with args\n", "Info", 6);

        pr_info!("A {} that", "line");
        pr_cont!(" is {}", "continued");
        pr_cont!(" with {}\n", "args");

        arc_print()?;

        trace::trace_rust_sample_loaded(42);

        Ok(RustPrint)
    }
}

impl Drop for RustPrint {
    fn drop(&mut self) {
        pr_info!("Rust printing macros sample (exit)\n");
    }
}

mod trace {
    use kernel::ffi::c_int;

    kernel::declare_trace! {
        /// # Safety
        ///
        /// Always safe to call.
        unsafe fn rust_sample_loaded(magic: c_int);
    }

    pub(crate) fn trace_rust_sample_loaded(magic: i32) {
        // SAFETY: Always safe to call.
        unsafe { rust_sample_loaded(magic as c_int) }
    }
}
