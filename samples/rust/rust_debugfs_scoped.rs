// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! Sample DebugFS exporting platform driver that demonstrates the use of
//! `Scope::dir` to create a variety of files without the need to separately
//! track them all.

use core::sync::atomic::AtomicUsize;
use kernel::debugfs::{Dir, Scope};
use kernel::prelude::*;
use kernel::sync::Mutex;
use kernel::{c_str, new_mutex, str::CString};

module! {
    type: RustScopedDebugFs,
    name: "rust_debugfs_scoped",
    authors: ["Matthew Maurer"],
    description: "Rust Scoped DebugFS usage sample",
    license: "GPL",
}

fn remove_file_write(
    mod_data: &ModuleData,
    reader: &mut kernel::uaccess::UserSliceReader,
) -> Result {
    let mut buf = [0u8; 128];
    if reader.len() >= buf.len() {
        return Err(EINVAL);
    }
    let n = reader.len();
    reader.read_slice(&mut buf[..n])?;

    let s = core::str::from_utf8(&buf[..n]).map_err(|_| EINVAL)?.trim();
    let nul_idx = s.len();
    buf[nul_idx] = 0;
    let to_remove = CStr::from_bytes_with_nul(&buf[..nul_idx + 1]).map_err(|_| EINVAL)?;
    mod_data
        .devices
        .lock()
        .retain(|device| device.name.as_bytes() != to_remove.as_bytes());
    Ok(())
}

fn create_file_write(
    mod_data: &ModuleData,
    reader: &mut kernel::uaccess::UserSliceReader,
) -> Result {
    let mut buf = [0u8; 128];
    if reader.len() > buf.len() {
        return Err(EINVAL);
    }
    let n = reader.len();
    reader.read_slice(&mut buf[..n])?;

    let mut nums = KVec::new();

    let s = core::str::from_utf8(&buf[..n]).map_err(|_| EINVAL)?.trim();
    let mut items = s.split_whitespace();
    let name_str = items.next().ok_or(EINVAL)?;
    let name = CString::try_from_fmt(fmt!("{name_str}"))?;
    let file_name = CString::try_from_fmt(fmt!("{name_str}"))?;
    for sub in items {
        nums.push(
            AtomicUsize::new(sub.parse().map_err(|_| EINVAL)?),
            GFP_KERNEL,
        )?;
    }

    let scope = KBox::pin_init(
        mod_data
            .device_dir
            .scope(DeviceData { name, nums }, &file_name, |dev_data, dir| {
                for (idx, val) in dev_data.nums.iter().enumerate() {
                    let Ok(name) = CString::try_from_fmt(fmt!("{idx}")) else {
                        return;
                    };
                    dir.read_write_file(&name, val);
                }
            }),
        GFP_KERNEL,
    )?;
    (*mod_data.devices.lock()).push(scope, GFP_KERNEL)?;

    Ok(())
}

struct RustScopedDebugFs {
    _data: Pin<KBox<Scope<ModuleData>>>,
}

#[pin_data]
struct ModuleData {
    device_dir: Dir,
    #[pin]
    devices: Mutex<KVec<Pin<KBox<Scope<DeviceData>>>>>,
}

impl ModuleData {
    fn init(device_dir: Dir) -> impl PinInit<Self> {
        pin_init! {
            Self {
                device_dir: device_dir,
                devices <- new_mutex!(KVec::new())
            }
        }
    }
}

struct DeviceData {
    name: CString,
    nums: KVec<AtomicUsize>,
}

fn init_control(base_dir: &Dir, dyn_dirs: Dir) -> impl PinInit<Scope<ModuleData>> + '_ {
    base_dir.scope(
        ModuleData::init(dyn_dirs),
        c_str!("control"),
        |data, dir| {
            dir.write_only_callback_file(c_str!("create"), data, &create_file_write);
            dir.write_only_callback_file(c_str!("remove"), data, &remove_file_write);
        },
    )
}

impl kernel::Module for RustScopedDebugFs {
    fn init(_module: &'static kernel::ThisModule) -> Result<Self> {
        let base_dir = Dir::new(c_str!("rust_scoped_debugfs"));
        let dyn_dirs = base_dir.subdir(c_str!("dynamic"));
        Ok(Self {
            _data: KBox::pin_init(init_control(&base_dir, dyn_dirs), GFP_KERNEL)?,
        })
    }
}
