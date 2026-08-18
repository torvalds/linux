// SPDX-License-Identifier: GPL-2.0

//! Rust PCI driver sample (based on QEMU's `pci-testdev`).
//!
//! To make this driver probe, QEMU must be run with `-device pci-testdev`.

use kernel::{
    device::{
        Bound,
        Core, //
    },
    io::{
        register,
        register::Array,
        Io, //
    },
    num::Bounded,
    pci,
    prelude::*, //
};

mod regs {
    use super::*;

    register! {
        pub(super) TEST(u8) @ 0x0 {
            7:0 index => TestIndex;
        }

        pub(super) OFFSET(u32) @ 0x4 {
            31:0 offset;
        }

        pub(super) DATA(u8) @ 0x8 {
            7:0 data;
        }

        pub(super) COUNT(u32) @ 0xC {
            31:0 count;
        }
    }

    pub(super) const END: usize = 0x10;
}

type Bar0<'bound> = pci::Bar<'bound, { regs::END }>;

#[derive(Copy, Clone, Debug)]
struct TestIndex(u8);

impl From<Bounded<u8, 8>> for TestIndex {
    fn from(value: Bounded<u8, 8>) -> Self {
        Self(value.into())
    }
}

impl From<TestIndex> for Bounded<u8, 8> {
    fn from(value: TestIndex) -> Self {
        value.0.into()
    }
}

impl TestIndex {
    const NO_EVENTFD: Self = Self(0);
}

struct SampleDriverData<'bound> {
    pdev: &'bound pci::Device,
    bar: Bar0<'bound>,
    index: TestIndex,
}

struct SampleDriver;

kernel::pci_device_table!(
    PCI_TABLE,
    MODULE_PCI_TABLE,
    <SampleDriver as pci::Driver>::IdInfo,
    [(
        pci::DeviceId::from_id(pci::Vendor::REDHAT, 0x5),
        TestIndex::NO_EVENTFD
    )]
);

impl SampleDriverData<'_> {
    fn testdev(index: &TestIndex, bar: &Bar0<'_>) -> Result<u32> {
        // Select the test.
        bar.write_reg(regs::TEST::zeroed().with_index(*index));

        let offset = bar.read(regs::OFFSET).into_raw() as usize;
        let data = bar.read(regs::DATA).into();

        // Write `data` to `offset` to increase `count` by one.
        //
        // Note that we need `try_write8`, since `offset` can't be checked at compile-time.
        bar.try_write8(data, offset)?;

        Ok(bar.read(regs::COUNT).into())
    }

    fn config_space(pdev: &pci::Device<Bound>) {
        let config = pdev.config_space();

        // Some PCI configuration space registers.
        register! {
            VENDOR_ID(u16) @ 0x0 {
                15:0 vendor_id;
            }

            REVISION_ID(u8) @ 0x8 {
                7:0 revision_id;
            }

            BAR(u32)[6] @ 0x10 {
                31:0 value;
            }
        }

        dev_info!(
            pdev,
            "pci-testdev config space read8 rev ID: {:x}\n",
            config.read(REVISION_ID).revision_id()
        );

        dev_info!(
            pdev,
            "pci-testdev config space read16 vendor ID: {:x}\n",
            config.read(VENDOR_ID).vendor_id()
        );

        dev_info!(
            pdev,
            "pci-testdev config space read32 BAR 0: {:x}\n",
            config.read(BAR::at(0)).value()
        );
    }
}

impl pci::Driver for SampleDriver {
    type IdInfo = TestIndex;
    type Data<'bound> = SampleDriverData<'bound>;

    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe<'bound>(
        pdev: &'bound pci::Device<Core<'_>>,
        info: &'bound Self::IdInfo,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound {
        let vendor = pdev.vendor_id();
        dev_dbg!(
            pdev,
            "Probe Rust PCI driver sample (PCI ID: {}, 0x{:x}).\n",
            vendor,
            pdev.device_id()
        );

        pdev.enable_device_mem()?;
        pdev.set_master();

        let bar = pdev.iomap_region_sized::<{ regs::END }>(0, c"rust_driver_pci")?;

        dev_info!(
            pdev,
            "pci-testdev data-match count: {}\n",
            SampleDriverData::testdev(info, &bar)?
        );
        SampleDriverData::config_space(pdev);

        Ok(SampleDriverData {
            pdev,
            bar,
            index: *info,
        })
    }

    fn unbind<'bound>(_pdev: &'bound pci::Device<Core<'_>>, this: Pin<&Self::Data<'bound>>) {
        this.bar
            .write_reg(regs::TEST::zeroed().with_index(this.index));
    }
}

impl Drop for SampleDriverData<'_> {
    fn drop(&mut self) {
        dev_dbg!(self.pdev, "Remove Rust PCI driver sample.\n");
    }
}

kernel::module_pci_driver! {
    type: SampleDriver,
    name: "rust_driver_pci",
    authors: ["Danilo Krummrich"],
    description: "Rust PCI driver",
    license: "GPL v2",
}
