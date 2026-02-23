// SPDX-License-Identifier: GPL-2.0

//! Rust I2C client registration sample.
//!
//! An I2C client in Rust cannot exist on its own. To register a new I2C client,
//! it must be bound to a parent device. In this sample driver, a platform device
//! is used as the parent.
//!

//! ACPI match table test
//!
//! This demonstrates how to test an ACPI-based Rust I2C client registration driver
//! using QEMU with a custom SSDT.
//!
//! Steps:
//!
//! 1. **Create an SSDT source file** (`ssdt.dsl`) with the following content:
//!
//!     ```asl
//!     DefinitionBlock ("", "SSDT", 2, "TEST", "VIRTACPI", 0x00000001)
//!     {
//!         Scope (\_SB)
//!         {
//!             Device (T432)
//!             {
//!                 Name (_HID, "LNUXBEEF")  // ACPI hardware ID to match
//!                 Name (_UID, 1)
//!                 Name (_STA, 0x0F)        // Device present, enabled
//!                 Name (_CRS, ResourceTemplate ()
//!                 {
//!                     Memory32Fixed (ReadWrite, 0xFED00000, 0x1000)
//!                 })
//!             }
//!         }
//!     }
//!     ```
//!
//! 2. **Compile the table**:
//!
//!     ```sh
//!     iasl -tc ssdt.dsl
//!     ```
//!
//!     This generates `ssdt.aml`
//!
//! 3. **Run QEMU** with the compiled AML file:
//!
//!     ```sh
//!     qemu-system-x86_64 -m 512M \
//!         -enable-kvm \
//!         -kernel path/to/bzImage \
//!         -append "root=/dev/sda console=ttyS0" \
//!         -hda rootfs.img \
//!         -serial stdio \
//!         -acpitable file=ssdt.aml
//!     ```
//!
//!     Requirements:
//!     - The `rust_driver_platform` must be present either:
//!         - built directly into the kernel (`bzImage`), or
//!         - available as a `.ko` file and loadable from `rootfs.img`
//!
//! 4. **Verify it worked** by checking `dmesg`:
//!
//!     ```
//!     rust_driver_platform LNUXBEEF:00: Probed with info: '0'.
//!     ```
//!

use kernel::{
    acpi,
    device,
    devres::Devres,
    i2c,
    of,
    platform,
    prelude::*,
    sync::aref::ARef, //
};

#[pin_data]
struct SampleDriver {
    parent_dev: ARef<platform::Device>,
    #[pin]
    _reg: Devres<i2c::Registration>,
}

kernel::of_device_table!(
    OF_TABLE,
    MODULE_OF_TABLE,
    <SampleDriver as platform::Driver>::IdInfo,
    [(of::DeviceId::new(c"test,rust-device"), ())]
);

kernel::acpi_device_table!(
    ACPI_TABLE,
    MODULE_ACPI_TABLE,
    <SampleDriver as platform::Driver>::IdInfo,
    [(acpi::DeviceId::new(c"LNUXBEEF"), ())]
);

const SAMPLE_I2C_CLIENT_ADDR: u16 = 0x30;
const SAMPLE_I2C_ADAPTER_INDEX: i32 = 0;
const BOARD_INFO: i2c::I2cBoardInfo =
    i2c::I2cBoardInfo::new(c"rust_driver_i2c", SAMPLE_I2C_CLIENT_ADDR);

impl platform::Driver for SampleDriver {
    type IdInfo = ();
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);
    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);

    fn probe(
        pdev: &platform::Device<device::Core>,
        _info: Option<&Self::IdInfo>,
    ) -> impl PinInit<Self, Error> {
        dev_info!(
            pdev.as_ref(),
            "Probe Rust I2C Client registration sample.\n"
        );

        kernel::try_pin_init!( Self {
            parent_dev: pdev.into(),

            _reg <- {
                let adapter = i2c::I2cAdapter::get(SAMPLE_I2C_ADAPTER_INDEX)?;

                i2c::Registration::new(&adapter, &BOARD_INFO, pdev.as_ref())
            }
        })
    }

    fn unbind(pdev: &platform::Device<device::Core>, _this: Pin<&Self>) {
        dev_info!(
            pdev.as_ref(),
            "Unbind Rust I2C Client registration sample.\n"
        );
    }
}

kernel::module_platform_driver! {
    type: SampleDriver,
    name: "rust_device_i2c",
    authors: ["Danilo Krummrich", "Igor Korotin"],
    description: "Rust I2C client registration",
    license: "GPL v2",
}
