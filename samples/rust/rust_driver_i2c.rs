// SPDX-License-Identifier: GPL-2.0

//! Rust I2C driver sample.

use kernel::{
    acpi,
    device::Core,
    i2c,
    of,
    prelude::*, //
};

struct SampleDriver;

kernel::acpi_device_table! {
    ACPI_TABLE,
    MODULE_ACPI_TABLE,
    <SampleDriver as i2c::Driver>::IdInfo,
    [(acpi::DeviceId::new(c"LNUXBEEF"), 0)]
}

kernel::i2c_device_table! {
    I2C_TABLE,
    MODULE_I2C_TABLE,
    <SampleDriver as i2c::Driver>::IdInfo,
    [(i2c::DeviceId::new(c"rust_driver_i2c"), 0)]
}

kernel::of_device_table! {
    OF_TABLE,
    MODULE_OF_TABLE,
    <SampleDriver as i2c::Driver>::IdInfo,
    [(of::DeviceId::new(c"test,rust_driver_i2c"), 0)]
}

impl i2c::Driver for SampleDriver {
    type IdInfo = u32;

    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);
    const I2C_ID_TABLE: Option<i2c::IdTable<Self::IdInfo>> = Some(&I2C_TABLE);
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);

    fn probe(
        idev: &i2c::I2cClient<Core>,
        info: Option<&Self::IdInfo>,
    ) -> impl PinInit<Self, Error> {
        let dev = idev.as_ref();

        dev_info!(dev, "Probe Rust I2C driver sample.\n");

        if let Some(info) = info {
            dev_info!(dev, "Probed with info: '{}'.\n", info);
        }

        Ok(Self)
    }

    fn shutdown(idev: &i2c::I2cClient<Core>, _this: Pin<&Self>) {
        dev_info!(idev.as_ref(), "Shutdown Rust I2C driver sample.\n");
    }

    fn unbind(idev: &i2c::I2cClient<Core>, _this: Pin<&Self>) {
        dev_info!(idev.as_ref(), "Unbind Rust I2C driver sample.\n");
    }
}

kernel::module_i2c_driver! {
    type: SampleDriver,
    name: "rust_driver_i2c",
    authors: ["Igor Korotin"],
    description: "Rust I2C driver",
    license: "GPL v2",
}
