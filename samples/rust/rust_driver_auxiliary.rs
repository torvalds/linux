// SPDX-License-Identifier: GPL-2.0

//! Rust auxiliary driver sample (based on a PCI driver for QEMU's `pci-testdev`).
//!
//! To make this driver probe, QEMU must be run with `-device pci-testdev`.

use kernel::{
    auxiliary,
    device::{
        Bound,
        Core, //
    },
    devres::Devres,
    driver,
    pci,
    prelude::*,
    InPlaceModule, //
};

use core::any::TypeId;

const MODULE_NAME: &CStr = <LocalModule as kernel::ModuleMetadata>::NAME;
const AUXILIARY_NAME: &CStr = c"auxiliary";

struct AuxiliaryDriver;

kernel::auxiliary_device_table!(
    AUX_TABLE,
    MODULE_AUX_TABLE,
    <AuxiliaryDriver as auxiliary::Driver>::IdInfo,
    [(auxiliary::DeviceId::new(MODULE_NAME, AUXILIARY_NAME), ())]
);

impl auxiliary::Driver for AuxiliaryDriver {
    type IdInfo = ();

    const ID_TABLE: auxiliary::IdTable<Self::IdInfo> = &AUX_TABLE;

    fn probe(adev: &auxiliary::Device<Core>, _info: &Self::IdInfo) -> impl PinInit<Self, Error> {
        dev_info!(
            adev,
            "Probing auxiliary driver for auxiliary device with id={}\n",
            adev.id()
        );

        ParentDriver::connect(adev)?;

        Ok(Self)
    }
}

#[pin_data]
struct ParentDriver {
    private: TypeId,
    #[pin]
    _reg0: Devres<auxiliary::Registration>,
    #[pin]
    _reg1: Devres<auxiliary::Registration>,
}

kernel::pci_device_table!(
    PCI_TABLE,
    MODULE_PCI_TABLE,
    <ParentDriver as pci::Driver>::IdInfo,
    [(pci::DeviceId::from_id(pci::Vendor::REDHAT, 0x5), ())]
);

impl pci::Driver for ParentDriver {
    type IdInfo = ();

    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe(pdev: &pci::Device<Core>, _info: &Self::IdInfo) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            private: TypeId::of::<Self>(),
            _reg0 <- auxiliary::Registration::new(pdev.as_ref(), AUXILIARY_NAME, 0, MODULE_NAME),
            _reg1 <- auxiliary::Registration::new(pdev.as_ref(), AUXILIARY_NAME, 1, MODULE_NAME),
        })
    }
}

impl ParentDriver {
    fn connect(adev: &auxiliary::Device<Bound>) -> Result {
        let dev = adev.parent();
        let pdev: &pci::Device<Bound> = dev.try_into()?;
        let drvdata = dev.drvdata::<Self>()?;

        dev_info!(
            dev,
            "Connect auxiliary {} with parent: VendorID={}, DeviceID={:#x}\n",
            adev.id(),
            pdev.vendor_id(),
            pdev.device_id()
        );

        dev_info!(
            dev,
            "We have access to the private data of {:?}.\n",
            drvdata.private
        );

        Ok(())
    }
}

#[pin_data]
struct SampleModule {
    #[pin]
    _pci_driver: driver::Registration<pci::Adapter<ParentDriver>>,
    #[pin]
    _aux_driver: driver::Registration<auxiliary::Adapter<AuxiliaryDriver>>,
}

impl InPlaceModule for SampleModule {
    fn init(module: &'static kernel::ThisModule) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            _pci_driver <- driver::Registration::new(MODULE_NAME, module),
            _aux_driver <- driver::Registration::new(MODULE_NAME, module),
        })
    }
}

module! {
    type: SampleModule,
    name: "rust_driver_auxiliary",
    authors: ["Danilo Krummrich"],
    description: "Rust auxiliary driver",
    license: "GPL v2",
}
