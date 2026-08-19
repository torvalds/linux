// SPDX-License-Identifier: GPL-2.0

//! Rust Serial device bus device driver sample.

use kernel::{
    acpi,
    device::{
        Bound,
        Core, //
    },
    of,
    prelude::*,
    serdev,
    sync::aref::ARef, //
};

struct SampleDriver {
    sdev: ARef<serdev::Device>,
}

kernel::of_device_table!(
    OF_TABLE,
    <SampleDriver as serdev::Driver>::IdInfo,
    [(of::DeviceId::new(c"test,rust_driver_serdev"), ())]
);

kernel::acpi_device_table!(
    ACPI_TABLE,
    <SampleDriver as serdev::Driver>::IdInfo,
    [(acpi::DeviceId::new(c"LNUXBEEF"), ())]
);

#[vtable]
impl serdev::Driver for SampleDriver {
    type IdInfo = ();
    type Data<'bound> = Self;
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);
    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);

    fn probe<'bound>(
        sdev: &'bound serdev::Device<Core<'_>>,
        _info: Option<&'bound Self::IdInfo>,
    ) -> impl PinInit<Self, Error> + 'bound {
        let dev = sdev.as_ref();

        dev_dbg!(dev, "Probe Rust Serial device bus device driver sample.\n");

        if sdev
            .set_baudrate(
                dev.fwnode()
                    .and_then(|fwnode| fwnode.property_read(c"baudrate").optional())
                    .unwrap_or(115200),
            )
            .is_err()
        {
            return Err(EINVAL);
        }
        sdev.set_flow_control(false);
        sdev.set_parity(serdev::Parity::None)?;

        Ok(Self { sdev: sdev.into() })
    }

    fn receive<'bound>(
        sdev: &'bound serdev::Device<Bound>,
        _this: Pin<&Self>,
        data: &[u8],
    ) -> usize {
        sdev.write(data).unwrap_or_default() as usize
    }
}

impl Drop for SampleDriver {
    fn drop(&mut self) {
        dev_dbg!(
            self.sdev.as_ref(),
            "Remove Rust Serial device bus device driver sample.\n"
        );
    }
}

kernel::module_serdev_device_driver! {
    type: SampleDriver,
    name: "rust_driver_serdev",
    authors: ["Markus Probst"],
    description: "Rust Serial device bus device driver",
    license: "GPL v2",
}
