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
use core::sync::atomic::AtomicUsize;
use core::sync::atomic::Ordering;
use kernel::c_str;
use kernel::debugfs::{Dir, File};
use kernel::new_mutex;
use kernel::prelude::*;
use kernel::sync::Mutex;

use kernel::{acpi, device::Core, of, platform, str::CString, types::ARef};

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
    counter: File<AtomicUsize>,
    #[pin]
    inner: File<Mutex<Inner>>,
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
    [(acpi::DeviceId::new(c_str!("LNUXBEEF")), ())]
);

impl platform::Driver for RustDebugFs {
    type IdInfo = ();
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = None;
    const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = Some(&ACPI_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        _info: Option<&Self::IdInfo>,
    ) -> Result<Pin<KBox<Self>>> {
        let result = KBox::try_pin_init(RustDebugFs::new(pdev), GFP_KERNEL)?;
        // We can still mutate fields through the files which are atomic or mutexed:
        result.counter.store(91, Ordering::Relaxed);
        {
            let mut guard = result.inner.lock();
            guard.x = guard.y;
            guard.y = 42;
        }
        Ok(result)
    }
}

impl RustDebugFs {
    fn build_counter(dir: &Dir) -> impl PinInit<File<AtomicUsize>> + '_ {
        dir.read_write_file(c_str!("counter"), AtomicUsize::new(0))
    }

    fn build_inner(dir: &Dir) -> impl PinInit<File<Mutex<Inner>>> + '_ {
        dir.read_write_file(c_str!("pair"), new_mutex!(Inner { x: 3, y: 10 }))
    }

    fn new(pdev: &platform::Device<Core>) -> impl PinInit<Self, Error> + '_ {
        let debugfs = Dir::new(c_str!("sample_debugfs"));
        let dev = pdev.as_ref();

        try_pin_init! {
            Self {
                _compatible <- debugfs.read_only_file(
                    c_str!("compatible"),
                    dev.fwnode()
                        .ok_or(ENOENT)?
                        .property_read::<CString>(c_str!("compatible"))
                        .required_by(dev)?,
                ),
                counter <- Self::build_counter(&debugfs),
                inner <- Self::build_inner(&debugfs),
                _debugfs: debugfs,
                pdev: pdev.into(),
            }
        }
    }
}
