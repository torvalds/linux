// SPDX-License-Identifier: GPL-2.0

//! Rust SoC Platform driver sample.

use kernel::{
    acpi,
    device::Core,
    of,
    platform,
    prelude::*,
    soc,
    str::CString,
    sync::aref::ARef, //
};
use pin_init::pin_init_scope;

#[pin_data]
struct SampleSocDriver {
    pdev: ARef<platform::Device>,
    #[pin]
    _dev_reg: soc::Registration,
}

kernel::of_device_table!(
    OF_TABLE,
    MODULE_OF_TABLE,
    <SampleSocDriver as platform::Driver>::IdInfo,
    [(of::DeviceId::new(c"test,rust-device"), ())]
);

kernel::acpi_device_table!(
    ACPI_TABLE,
    MODULE_ACPI_TABLE,
    <SampleSocDriver as platform::Driver>::IdInfo,
    [(acpi::DeviceId::new(c"LNUXBEEF"), ())]
);

impl platform::Driver for SampleSocDriver {
    type IdInfo = ();
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);
    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        _info: Option<&Self::IdInfo>,
    ) -> impl PinInit<Self, Error> {
        dev_dbg!(pdev, "Probe Rust SoC driver sample.\n");

        let pdev = pdev.into();
        pin_init_scope(move || {
            let machine = CString::try_from(c"My cool ACME15 dev board")?;
            let family = CString::try_from(c"ACME")?;
            let revision = CString::try_from(c"1.2")?;
            let serial_number = CString::try_from(c"12345")?;
            let soc_id = CString::try_from(c"ACME15")?;

            let attr = soc::Attributes {
                machine: Some(machine),
                family: Some(family),
                revision: Some(revision),
                serial_number: Some(serial_number),
                soc_id: Some(soc_id),
            };

            Ok(try_pin_init!(SampleSocDriver {
                pdev: pdev,
                _dev_reg <- soc::Registration::new(attr),
            }? Error))
        })
    }
}

kernel::module_platform_driver! {
    type: SampleSocDriver,
    name: "rust_soc",
    authors: ["Matthew Maurer"],
    description: "Rust SoC Driver",
    license: "GPL",
}
