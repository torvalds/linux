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
    driver,
    new_mutex,
    pci,
    prelude::*,
    sync::Mutex,
    types::{
        CovariantForLt,
        ForLt, //
    },
    InPlaceModule, //
};

const MODULE_NAME: &CStr = <LocalModule as kernel::ModuleMetadata>::NAME;
const AUXILIARY_NAME: &CStr = c"auxiliary";
const COVARIANT_DEV_ID: u32 = 0;
const INVARIANT_DEV_ID: u32 = 1;

struct AuxiliaryDriver;

kernel::auxiliary_device_table!(
    AUX_TABLE,
    <AuxiliaryDriver as auxiliary::Driver>::IdInfo,
    [(auxiliary::DeviceId::new(MODULE_NAME, AUXILIARY_NAME), ())]
);

impl auxiliary::Driver for AuxiliaryDriver {
    type IdInfo = ();
    type Data<'bound> = Self;

    const ID_TABLE: auxiliary::IdTable<Self::IdInfo> = &AUX_TABLE;

    fn probe<'bound>(
        adev: &'bound auxiliary::Device<Core<'_>>,
        _info: &'bound Self::IdInfo,
    ) -> impl PinInit<Self, Error> + 'bound {
        dev_info!(
            adev,
            "Probing auxiliary driver for auxiliary device with id={}\n",
            adev.id()
        );

        ParentDriver::connect(adev)?;

        Ok(Self)
    }
}

struct Data<'bound> {
    index: u32,
    parent: &'bound pci::Device<Bound>,
}

/// Registration data with interior mutability.
///
/// `Mutex<&'bound T>` is invariant over `'bound`, so this type cannot implement
/// [`CovariantForLt`](trait@CovariantForLt). Access must go through the closure-based
/// [`auxiliary::Device::registration_data_with()`].
#[pin_data]
struct MutexData<'bound> {
    #[pin]
    parent: Mutex<&'bound pci::Device<Bound>>,
    index: u32,
}

struct ParentDriver;

#[allow(clippy::type_complexity)]
#[pin_data]
struct ParentData<'bound> {
    _reg0: auxiliary::Registration<'bound, CovariantForLt!(Data<'_>)>,
    #[pin]
    _reg1: auxiliary::Registration<'bound, ForLt!(MutexData<'_>)>,
}

kernel::pci_device_table!(
    PCI_TABLE,
    <ParentDriver as pci::Driver>::IdInfo,
    [(pci::DeviceId::from_id(pci::Vendor::REDHAT, 0x5), ())]
);

impl pci::Driver for ParentDriver {
    type IdInfo = ();
    type Data<'bound> = ParentData<'bound>;

    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe<'bound>(
        pdev: &'bound pci::Device<Core<'_>>,
        _info: Option<&'bound Self::IdInfo>,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound {
        try_pin_init!(ParentData {
            // SAFETY: `ParentData` is the driver's private data, which is dropped when the
            // device is unbound; i.e. `mem::forget()` is never called on it.
            _reg0: unsafe {
                auxiliary::Registration::new_with_lt(
                    pdev.as_ref(),
                    AUXILIARY_NAME,
                    COVARIANT_DEV_ID,
                    MODULE_NAME,
                    Data {
                        index: COVARIANT_DEV_ID,
                        parent: pdev,
                    },
                )?
            },
            // SAFETY: See `_reg0` above.
            _reg1: unsafe {
                auxiliary::Registration::new_with_lt(
                    pdev.as_ref(),
                    AUXILIARY_NAME,
                    INVARIANT_DEV_ID,
                    MODULE_NAME,
                    pin_init!(MutexData {
                        parent <- {
                            let pdev: &pci::Device<Bound> = pdev;

                            new_mutex!(pdev)
                        },
                        index: INVARIANT_DEV_ID,
                    }),
                )?
            },
        })
    }
}

impl ParentDriver {
    fn connect(adev: &auxiliary::Device<Bound>) -> Result {
        match adev.id() {
            // CovariantForLt types can use the direct-reference accessor.
            COVARIANT_DEV_ID => {
                let data = adev.registration_data::<CovariantForLt!(Data<'_>)>()?;
                let pdev = data.parent;

                dev_info!(
                    pdev,
                    "Connect auxiliary {} with parent: VendorID={}, DeviceID={:#x}\n",
                    adev.id(),
                    pdev.vendor_id(),
                    pdev.device_id()
                );

                dev_info!(
                    pdev,
                    "Connected to auxiliary device with index {}.\n",
                    data.index
                );
            }
            // Invariant ForLt types (e.g. containing a Mutex) require the closure-based accessor.
            INVARIANT_DEV_ID => {
                adev.registration_data_with::<ForLt!(MutexData<'_>), _>(|data| {
                    let pdev = *data.parent.lock();
                    dev_info!(
                        pdev,
                        "Connected to auxiliary device with index {} (via Mutex).\n",
                        data.index
                    );
                })?;
            }
            _ => return Err(EINVAL),
        }

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
