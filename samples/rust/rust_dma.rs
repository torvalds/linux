// SPDX-License-Identifier: GPL-2.0

//! Rust DMA api test (based on QEMU's `pci-testdev`).
//!
//! To make this driver probe, QEMU must be run with `-device pci-testdev`.

use kernel::{
    device::Core,
    dma::{CoherentAllocation, DataDirection, Device, DmaMask},
    page, pci,
    prelude::*,
    scatterlist::{Owned, SGTable},
    sync::aref::ARef,
};

#[pin_data(PinnedDrop)]
struct DmaSampleDriver {
    pdev: ARef<pci::Device>,
    ca: CoherentAllocation<MyStruct>,
    #[pin]
    sgt: SGTable<Owned<VVec<u8>>>,
}

const TEST_VALUES: [(u32, u32); 5] = [
    (0xa, 0xb),
    (0xc, 0xd),
    (0xe, 0xf),
    (0xab, 0xba),
    (0xcd, 0xef),
];

struct MyStruct {
    h: u32,
    b: u32,
}

impl MyStruct {
    fn new(h: u32, b: u32) -> Self {
        Self { h, b }
    }
}
// SAFETY: All bit patterns are acceptable values for `MyStruct`.
unsafe impl kernel::transmute::AsBytes for MyStruct {}
// SAFETY: Instances of `MyStruct` have no uninitialized portions.
unsafe impl kernel::transmute::FromBytes for MyStruct {}

kernel::pci_device_table!(
    PCI_TABLE,
    MODULE_PCI_TABLE,
    <DmaSampleDriver as pci::Driver>::IdInfo,
    [(pci::DeviceId::from_id(pci::Vendor::REDHAT, 0x5), ())]
);

impl pci::Driver for DmaSampleDriver {
    type IdInfo = ();
    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe(pdev: &pci::Device<Core>, _info: &Self::IdInfo) -> Result<Pin<KBox<Self>>> {
        dev_info!(pdev.as_ref(), "Probe DMA test driver.\n");

        let mask = DmaMask::new::<64>();

        // SAFETY: There are no concurrent calls to DMA allocation and mapping primitives.
        unsafe { pdev.dma_set_mask_and_coherent(mask)? };

        let ca: CoherentAllocation<MyStruct> =
            CoherentAllocation::alloc_coherent(pdev.as_ref(), TEST_VALUES.len(), GFP_KERNEL)?;

        for (i, value) in TEST_VALUES.into_iter().enumerate() {
            kernel::dma_write!(ca[i] = MyStruct::new(value.0, value.1))?;
        }

        let size = 4 * page::PAGE_SIZE;
        let pages = VVec::with_capacity(size, GFP_KERNEL)?;

        let sgt = SGTable::new(pdev.as_ref(), pages, DataDirection::ToDevice, GFP_KERNEL);

        let drvdata = KBox::pin_init(
            try_pin_init!(Self {
                pdev: pdev.into(),
                ca,
                sgt <- sgt,
            }),
            GFP_KERNEL,
        )?;

        Ok(drvdata)
    }
}

#[pinned_drop]
impl PinnedDrop for DmaSampleDriver {
    fn drop(self: Pin<&mut Self>) {
        let dev = self.pdev.as_ref();

        dev_info!(dev, "Unload DMA test driver.\n");

        for (i, value) in TEST_VALUES.into_iter().enumerate() {
            let val0 = kernel::dma_read!(self.ca[i].h);
            let val1 = kernel::dma_read!(self.ca[i].b);
            assert!(val0.is_ok());
            assert!(val1.is_ok());

            if let Ok(val0) = val0 {
                assert_eq!(val0, value.0);
            }
            if let Ok(val1) = val1 {
                assert_eq!(val1, value.1);
            }
        }

        for (i, entry) in self.sgt.iter().enumerate() {
            dev_info!(dev, "Entry[{}]: DMA address: {:#x}", i, entry.dma_address());
        }
    }
}

kernel::module_pci_driver! {
    type: DmaSampleDriver,
    name: "rust_dma",
    authors: ["Abdiel Janulgue"],
    description: "Rust DMA test",
    license: "GPL v2",
}
