// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (C) 2025 Collabora Ltd.

//! Rust USB driver sample.

use kernel::{device, device::Core, prelude::*, sync::aref::ARef, usb};

struct SampleDriver {
    _intf: ARef<usb::Interface>,
}

kernel::usb_device_table!(
    USB_TABLE,
    MODULE_USB_TABLE,
    <SampleDriver as usb::Driver>::IdInfo,
    [(usb::DeviceId::from_id(0x1234, 0x5678), ()),]
);

impl usb::Driver for SampleDriver {
    type IdInfo = ();
    const ID_TABLE: usb::IdTable<Self::IdInfo> = &USB_TABLE;

    fn probe(
        intf: &usb::Interface<Core>,
        _id: &usb::DeviceId,
        _info: &Self::IdInfo,
    ) -> Result<Pin<KBox<Self>>> {
        let dev: &device::Device<Core> = intf.as_ref();
        dev_info!(dev, "Rust USB driver sample probed\n");

        let drvdata = KBox::new(Self { _intf: intf.into() }, GFP_KERNEL)?;
        Ok(drvdata.into())
    }

    fn disconnect(intf: &usb::Interface<Core>, _data: Pin<&Self>) {
        let dev: &device::Device<Core> = intf.as_ref();
        dev_info!(dev, "Rust USB driver sample disconnected\n");
    }
}

kernel::module_usb_driver! {
    type: SampleDriver,
    name: "rust_driver_usb",
    authors: ["Daniel Almeida"],
    description: "Rust USB driver sample",
    license: "GPL v2",
}
