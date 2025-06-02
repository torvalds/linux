// SPDX-License-Identifier: GPL-2.0-only

//! Rust faux device sample.

use kernel::{c_str, faux, prelude::*, Module};

module! {
    type: SampleModule,
    name: "rust_faux_driver",
    authors: ["Lyude Paul"],
    description: "Rust faux device sample",
    license: "GPL",
}

struct SampleModule {
    _reg: faux::Registration,
}

impl Module for SampleModule {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        pr_info!("Initialising Rust Faux Device Sample\n");

        let reg = faux::Registration::new(c_str!("rust-faux-sample-device"), None)?;

        dev_info!(reg.as_ref(), "Hello from faux device!\n");

        Ok(Self { _reg: reg })
    }
}
