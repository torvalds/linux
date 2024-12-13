// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Rust misc device sample.

use kernel::{
    c_str,
    device::Device,
    fs::File,
    ioctl::_IO,
    miscdevice::{MiscDevice, MiscDeviceOptions, MiscDeviceRegistration},
    prelude::*,
    types::ARef,
};

const RUST_MISC_DEV_HELLO: u32 = _IO('|' as u32, 0x80);

module! {
    type: RustMiscDeviceModule,
    name: "rust_misc_device",
    author: "Lee Jones",
    description: "Rust misc device sample",
    license: "GPL",
}

#[pin_data]
struct RustMiscDeviceModule {
    #[pin]
    _miscdev: MiscDeviceRegistration<RustMiscDevice>,
}

impl kernel::InPlaceModule for RustMiscDeviceModule {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        pr_info!("Initialising Rust Misc Device Sample\n");

        let options = MiscDeviceOptions {
            name: c_str!("rust-misc-device"),
        };

        try_pin_init!(Self {
            _miscdev <- MiscDeviceRegistration::register(options),
        })
    }
}

struct RustMiscDevice {
    dev: ARef<Device>,
}

#[vtable]
impl MiscDevice for RustMiscDevice {
    type Ptr = KBox<Self>;

    fn open(_file: &File, misc: &MiscDeviceRegistration<Self>) -> Result<KBox<Self>> {
        let dev = ARef::from(misc.device());

        dev_info!(dev, "Opening Rust Misc Device Sample\n");

        Ok(KBox::new(RustMiscDevice { dev }, GFP_KERNEL)?)
    }

    fn ioctl(
        me: <Self::Ptr as kernel::types::ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        cmd: u32,
        _arg: usize,
    ) -> Result<isize> {
        dev_info!(me.dev, "IOCTLing Rust Misc Device Sample\n");

        match cmd {
            RUST_MISC_DEV_HELLO => dev_info!(me.dev, "Hello from the Rust Misc Device\n"),
            _ => {
                dev_err!(me.dev, "-> IOCTL not recognised: {}\n", cmd);
                return Err(ENOTTY);
            }
        }

        Ok(0)
    }
}

impl Drop for RustMiscDevice {
    fn drop(&mut self) {
        dev_info!(self.dev, "Exiting the Rust Misc Device Sample\n");
    }
}
