// SPDX-License-Identifier: GPL-2.0

//! Rust Platform driver sample.

//! ACPI match table test
//!
//! This demonstrates how to test an ACPI-based Rust platform driver using QEMU
//! with a custom SSDT.
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
    acpi, c_str,
    device::{
        self,
        property::{FwNodeReferenceArgs, NArgs},
        Core,
    },
    of, platform,
    prelude::*,
    str::CString,
    sync::aref::ARef,
};

struct SampleDriver {
    pdev: ARef<platform::Device>,
}

struct Info(u32);

kernel::of_device_table!(
    OF_TABLE,
    MODULE_OF_TABLE,
    <SampleDriver as platform::Driver>::IdInfo,
    [(of::DeviceId::new(c_str!("test,rust-device")), Info(42))]
);

kernel::acpi_device_table!(
    ACPI_TABLE,
    MODULE_ACPI_TABLE,
    <SampleDriver as platform::Driver>::IdInfo,
    [(acpi::DeviceId::new(c_str!("LNUXBEEF")), Info(0))]
);

impl platform::Driver for SampleDriver {
    type IdInfo = Info;
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);
    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        info: Option<&Self::IdInfo>,
    ) -> Result<Pin<KBox<Self>>> {
        let dev = pdev.as_ref();

        dev_dbg!(dev, "Probe Rust Platform driver sample.\n");

        if let Some(info) = info {
            dev_info!(dev, "Probed with info: '{}'.\n", info.0);
        }

        if dev.fwnode().is_some_and(|node| node.is_of_node()) {
            Self::properties_parse(dev)?;
        }

        let drvdata = KBox::new(Self { pdev: pdev.into() }, GFP_KERNEL)?;

        Ok(drvdata.into())
    }
}

impl SampleDriver {
    fn properties_parse(dev: &device::Device) -> Result {
        let fwnode = dev.fwnode().ok_or(ENOENT)?;

        if let Ok(idx) =
            fwnode.property_match_string(c_str!("compatible"), c_str!("test,rust-device"))
        {
            dev_info!(dev, "matched compatible string idx = {}\n", idx);
        }

        let name = c_str!("compatible");
        let prop = fwnode.property_read::<CString>(name).required_by(dev)?;
        dev_info!(dev, "'{name}'='{prop:?}'\n");

        let name = c_str!("test,bool-prop");
        let prop = fwnode.property_read_bool(c_str!("test,bool-prop"));
        dev_info!(dev, "'{name}'='{prop}'\n");

        if fwnode.property_present(c_str!("test,u32-prop")) {
            dev_info!(dev, "'test,u32-prop' is present\n");
        }

        let name = c_str!("test,u32-optional-prop");
        let prop = fwnode.property_read::<u32>(name).or(0x12);
        dev_info!(dev, "'{name}'='{prop:#x}' (default = 0x12)\n",);

        // A missing required property will print an error. Discard the error to
        // prevent properties_parse from failing in that case.
        let name = c_str!("test,u32-required-prop");
        let _ = fwnode.property_read::<u32>(name).required_by(dev);

        let name = c_str!("test,u32-prop");
        let prop: u32 = fwnode.property_read(name).required_by(dev)?;
        dev_info!(dev, "'{name}'='{prop:#x}'\n");

        let name = c_str!("test,i16-array");
        let prop: [i16; 4] = fwnode.property_read(name).required_by(dev)?;
        dev_info!(dev, "'{name}'='{prop:?}'\n");
        let len = fwnode.property_count_elem::<u16>(name)?;
        dev_info!(dev, "'{name}' length is {len}\n",);

        let name = c_str!("test,i16-array");
        let prop: KVec<i16> = fwnode.property_read_array_vec(name, 4)?.required_by(dev)?;
        dev_info!(dev, "'{name}'='{prop:?}' (KVec)\n");

        for child in fwnode.children() {
            let name = c_str!("test,ref-arg");
            let nargs = NArgs::N(2);
            let prop: FwNodeReferenceArgs = child.property_get_reference_args(name, nargs, 0)?;
            dev_info!(dev, "'{name}'='{prop:?}'\n");
        }

        Ok(())
    }
}

impl Drop for SampleDriver {
    fn drop(&mut self) {
        dev_dbg!(self.pdev.as_ref(), "Remove Rust Platform driver sample.\n");
    }
}

kernel::module_platform_driver! {
    type: SampleDriver,
    name: "rust_driver_platform",
    authors: ["Danilo Krummrich"],
    description: "Rust Platform driver",
    license: "GPL v2",
}
