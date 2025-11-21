// SPDX-License-Identifier: GPL-2.0

//! Rust configfs sample.

use kernel::alloc::flags;
use kernel::c_str;
use kernel::configfs;
use kernel::configfs::configfs_attrs;
use kernel::new_mutex;
use kernel::page::PAGE_SIZE;
use kernel::prelude::*;
use kernel::sync::Mutex;

module! {
    type: RustConfigfs,
    name: "rust_configfs",
    authors: ["Rust for Linux Contributors"],
    description: "Rust configfs sample",
    license: "GPL",
}

#[pin_data]
struct RustConfigfs {
    #[pin]
    config: configfs::Subsystem<Configuration>,
}

#[pin_data]
struct Configuration {
    message: &'static CStr,
    #[pin]
    bar: Mutex<(KBox<[u8; PAGE_SIZE]>, usize)>,
}

impl Configuration {
    fn new() -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            message: c_str!("Hello World\n"),
            bar <- new_mutex!((KBox::new([0; PAGE_SIZE], flags::GFP_KERNEL)?, 0)),
        })
    }
}

impl kernel::InPlaceModule for RustConfigfs {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        pr_info!("Rust configfs sample (init)\n");

        // Define a subsystem with the data type `Configuration`, two
        // attributes, `message` and `bar` and child group type `Child`. `mkdir`
        // in the directory representing this subsystem will create directories
        // backed by the `Child` type.
        let item_type = configfs_attrs! {
            container: configfs::Subsystem<Configuration>,
            data: Configuration,
            child: Child,
            attributes: [
                message: 0,
                bar: 1,
            ],
        };

        try_pin_init!(Self {
            config <- configfs::Subsystem::new(
                c_str!("rust_configfs"), item_type, Configuration::new()
            ),
        })
    }
}

#[vtable]
impl configfs::GroupOperations for Configuration {
    type Child = Child;

    fn make_group(&self, name: &CStr) -> Result<impl PinInit<configfs::Group<Child>, Error>> {
        // Define a group with data type `Child`, one attribute `baz` and child
        // group type `GrandChild`. `mkdir` in the directory representing this
        // group will create directories backed by the `GrandChild` type.
        let tpe = configfs_attrs! {
            container: configfs::Group<Child>,
            data: Child,
            child: GrandChild,
            attributes: [
                baz: 0,
            ],
        };

        Ok(configfs::Group::new(name.try_into()?, tpe, Child::new()))
    }
}

#[vtable]
impl configfs::AttributeOperations<0> for Configuration {
    type Data = Configuration;

    fn show(container: &Configuration, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        pr_info!("Show message\n");
        let data = container.message.to_bytes();
        page[0..data.len()].copy_from_slice(data);
        Ok(data.len())
    }
}

#[vtable]
impl configfs::AttributeOperations<1> for Configuration {
    type Data = Configuration;

    fn show(container: &Configuration, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        pr_info!("Show bar\n");
        let guard = container.bar.lock();
        let data = guard.0.as_slice();
        let len = guard.1;
        page[0..len].copy_from_slice(&data[0..len]);
        Ok(len)
    }

    fn store(container: &Configuration, page: &[u8]) -> Result {
        pr_info!("Store bar\n");
        let mut guard = container.bar.lock();
        guard.0[0..page.len()].copy_from_slice(page);
        guard.1 = page.len();
        Ok(())
    }
}

// `pin_data` cannot handle structs without braces.
#[pin_data]
struct Child {}

impl Child {
    fn new() -> impl PinInit<Self, Error> {
        try_pin_init!(Self {})
    }
}

#[vtable]
impl configfs::GroupOperations for Child {
    type Child = GrandChild;

    fn make_group(&self, name: &CStr) -> Result<impl PinInit<configfs::Group<GrandChild>, Error>> {
        // Define a group with data type `GrandChild`, one attribute `gc`. As no
        // child type is specified, it will not be possible to create subgroups
        // in this group, and `mkdir`in the directory representing this group
        // will return an error.
        let tpe = configfs_attrs! {
            container: configfs::Group<GrandChild>,
            data: GrandChild,
            attributes: [
                gc: 0,
            ],
        };

        Ok(configfs::Group::new(
            name.try_into()?,
            tpe,
            GrandChild::new(),
        ))
    }
}

#[vtable]
impl configfs::AttributeOperations<0> for Child {
    type Data = Child;

    fn show(_container: &Child, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        pr_info!("Show baz\n");
        let data = c"Hello Baz\n".to_bytes();
        page[0..data.len()].copy_from_slice(data);
        Ok(data.len())
    }
}

// `pin_data` cannot handle structs without braces.
#[pin_data]
struct GrandChild {}

impl GrandChild {
    fn new() -> impl PinInit<Self, Error> {
        try_pin_init!(Self {})
    }
}

#[vtable]
impl configfs::AttributeOperations<0> for GrandChild {
    type Data = GrandChild;

    fn show(_container: &GrandChild, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        pr_info!("Show grand child\n");
        let data = c"Hello GC\n".to_bytes();
        page[0..data.len()].copy_from_slice(data);
        Ok(data.len())
    }
}
