// SPDX-License-Identifier: GPL-2.0

//! Rust Platform driver sample.

use kernel::{
    c_str,
    device::{self, Core},
    of, platform,
    prelude::*,
    str::CString,
    types::ARef,
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

impl platform::Driver for SampleDriver {
    type IdInfo = Info;
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        info: Option<&Self::IdInfo>,
    ) -> Result<Pin<KBox<Self>>> {
        dev_dbg!(pdev.as_ref(), "Probe Rust Platform driver sample.\n");

        if let Some(info) = info {
            dev_info!(pdev.as_ref(), "Probed with info: '{}'.\n", info.0);
        }

        Self::properties_parse(pdev.as_ref())?;

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
