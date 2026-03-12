// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! Sample DebugFS exporting platform driver
//!
//! To successfully probe this driver with ACPI, use an ssdt that looks like
//!
//! ```dsl
//! DefinitionBlock ("", "SSDT", 2, "TEST", "VIRTACPI", 0x00000001)
//! {
//!    Scope (\_SB)
//!    {
//!        Device (T432)
//!        {
//!            Name (_HID, "LNUXBEEF")  // ACPI hardware ID to match
//!            Name (_UID, 1)
//!            Name (_STA, 0x0F)        // Device present, enabled
//!            Name (_DSD, Package () { // Sample attribute
//!                ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
//!                Package() {
//!                    Package(2) {"compatible", "sample-debugfs"}
//!                }
//!            })
//!            Name (_CRS, ResourceTemplate ()
//!            {
//!                Memory32Fixed (ReadWrite, 0xFED00000, 0x1000)
//!            })
//!        }
//!    }
//! }
//! ```

use core::str::FromStr;
use kernel::{
    acpi,
    debugfs::{
        Dir,
        File, //
    },
    device::Core,
    new_mutex,
    of,
    platform,
    prelude::*,
    sizes::*,
    str::CString,
    sync::{
        aref::ARef,
        atomic::{
            Atomic,
            Relaxed, //
        },
        Mutex,
    }, //
};

kernel::module_platform_driver! {
    type: RustDebugFs,
    name: "rust_debugfs",
    authors: ["Matthew Maurer"],
    description: "Rust DebugFS usage sample",
    license: "GPL",
}

#[pin_data]
struct RustDebugFs {
    pdev: ARef<platform::Device>,
    // As we only hold these for drop effect (to remove the directory/files) we have a leading
    // underscore to indicate to the compiler that we don't expect to use this field directly.
    _debugfs: Dir,
    #[pin]
    _compatible: File<CString>,
    #[pin]
    counter: File<Atomic<usize>>,
    #[pin]
    inner: File<Mutex<Inner>>,
    #[pin]
    array_blob: File<Mutex<[u8; 4]>>,
    #[pin]
    vector_blob: File<Mutex<KVec<u8>>>,
}

#[derive(Debug)]
struct Inner {
    x: u32,
    y: u32,
}

impl FromStr for Inner {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        let mut parts = s.split_whitespace();
        let x = parts
            .next()
            .ok_or(EINVAL)?
            .parse::<u32>()
            .map_err(|_| EINVAL)?;
        let y = parts
            .next()
            .ok_or(EINVAL)?
            .parse::<u32>()
            .map_err(|_| EINVAL)?;
        if parts.next().is_some() {
            return Err(EINVAL);
        }
        Ok(Inner { x, y })
    }
}

kernel::acpi_device_table!(
    ACPI_TABLE,
    MODULE_ACPI_TABLE,
    <RustDebugFs as platform::Driver>::IdInfo,
    [(acpi::DeviceId::new(c"LNUXBEEF"), ())]
);

impl platform::Driver for RustDebugFs {
    type IdInfo = ();
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = None;
    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        _info: Option<&Self::IdInfo>,
    ) -> impl PinInit<Self, Error> {
        RustDebugFs::new(pdev).pin_chain(|this| {
            this.counter.store(91, Relaxed);
            {
                let mut guard = this.inner.lock();
                guard.x = guard.y;
                guard.y = 42;
            }

            Ok(())
        })
    }
}

impl RustDebugFs {
    fn build_counter(dir: &Dir) -> impl PinInit<File<Atomic<usize>>> + '_ {
        dir.read_write_file(c"counter", Atomic::<usize>::new(0))
    }

    fn build_inner(dir: &Dir) -> impl PinInit<File<Mutex<Inner>>> + '_ {
        dir.read_write_file(c"pair", new_mutex!(Inner { x: 3, y: 10 }))
    }

    fn new(pdev: &platform::Device<Core>) -> impl PinInit<Self, Error> + '_ {
        let debugfs = Dir::new(c"sample_debugfs");
        let dev = pdev.as_ref();

        try_pin_init! {
            Self {
                _compatible <- debugfs.read_only_file(
                    c"compatible",
                    dev.fwnode()
                        .ok_or(ENOENT)?
                        .property_read::<CString>(c"compatible")
                        .required_by(dev)?,
                ),
                counter <- Self::build_counter(&debugfs),
                inner <- Self::build_inner(&debugfs),
                array_blob <- debugfs.read_write_binary_file(
                    c"array_blob",
                    new_mutex!([0x62, 0x6c, 0x6f, 0x62]),
                ),
                vector_blob <- debugfs.read_write_binary_file(
                    c"vector_blob",
                    new_mutex!(kernel::kvec!(0x42; SZ_4K)?),
                ),
                _debugfs: debugfs,
                pdev: pdev.into(),
            }
        }
    }
}
