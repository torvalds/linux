// SPDX-License-Identifier: GPL-2.0

//! configfs interface: Userspace-driven Kernel Object Configuration
//!
//! configfs is an in-memory pseudo file system for configuration of kernel
//! modules. Please see the [C documentation] for details and intended use of
//! configfs.
//!
//! This module does not support the following configfs features:
//!
//! - Items. All group children are groups.
//! - Symlink support.
//! - `disconnect_notify` hook.
//! - Default groups.
//!
//! See the [`rust_configfs.rs`] sample for a full example use of this module.
//!
//! C header: [`include/linux/configfs.h`](srctree/include/linux/configfs.h)
//!
//! # Examples
//!
//! ```ignore
//! use kernel::alloc::flags;
//! use kernel::c_str;
//! use kernel::configfs_attrs;
//! use kernel::configfs;
//! use kernel::new_mutex;
//! use kernel::page::PAGE_SIZE;
//! use kernel::sync::Mutex;
//! use kernel::ThisModule;
//!
//! #[pin_data]
//! struct RustConfigfs {
//!     #[pin]
//!     config: configfs::Subsystem<Configuration>,
//! }
//!
//! impl kernel::InPlaceModule for RustConfigfs {
//!     fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
//!         pr_info!("Rust configfs sample (init)\n");
//!
//!         let item_type = configfs_attrs! {
//!             container: configfs::Subsystem<Configuration>,
//!             data: Configuration,
//!             attributes: [
//!                 message: 0,
//!                 bar: 1,
//!             ],
//!         };
//!
//!         try_pin_init!(Self {
//!             config <- configfs::Subsystem::new(
//!                 c_str!("rust_configfs"), item_type, Configuration::new()
//!             ),
//!         })
//!     }
//! }
//!
//! #[pin_data]
//! struct Configuration {
//!     message: &'static CStr,
//!     #[pin]
//!     bar: Mutex<(KBox<[u8; PAGE_SIZE]>, usize)>,
//! }
//!
//! impl Configuration {
//!     fn new() -> impl PinInit<Self, Error> {
//!         try_pin_init!(Self {
//!             message: c_str!("Hello World\n"),
//!             bar <- new_mutex!((KBox::new([0; PAGE_SIZE], flags::GFP_KERNEL)?, 0)),
//!         })
//!     }
//! }
//!
//! #[vtable]
//! impl configfs::AttributeOperations<0> for Configuration {
//!     type Data = Configuration;
//!
//!     fn show(container: &Configuration, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
//!         pr_info!("Show message\n");
//!         let data = container.message;
//!         page[0..data.len()].copy_from_slice(data);
//!         Ok(data.len())
//!     }
//! }
//!
//! #[vtable]
//! impl configfs::AttributeOperations<1> for Configuration {
//!     type Data = Configuration;
//!
//!     fn show(container: &Configuration, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
//!         pr_info!("Show bar\n");
//!         let guard = container.bar.lock();
//!         let data = guard.0.as_slice();
//!         let len = guard.1;
//!         page[0..len].copy_from_slice(&data[0..len]);
//!         Ok(len)
//!     }
//!
//!     fn store(container: &Configuration, page: &[u8]) -> Result {
//!         pr_info!("Store bar\n");
//!         let mut guard = container.bar.lock();
//!         guard.0[0..page.len()].copy_from_slice(page);
//!         guard.1 = page.len();
//!         Ok(())
//!     }
//! }
//! ```
//!
//! [C documentation]: srctree/Documentation/filesystems/configfs.rst
//! [`rust_configfs.rs`]: srctree/samples/rust/rust_configfs.rs

use crate::alloc::flags;
use crate::container_of;
use crate::page::PAGE_SIZE;
use crate::prelude::*;
use crate::str::CString;
use crate::sync::Arc;
use crate::sync::ArcBorrow;
use crate::types::Opaque;
use core::cell::UnsafeCell;
use core::marker::PhantomData;

/// A configfs subsystem.
///
/// This is the top level entrypoint for a configfs hierarchy. To register
/// with configfs, embed a field of this type into your kernel module struct.
#[pin_data(PinnedDrop)]
pub struct Subsystem<Data> {
    #[pin]
    subsystem: Opaque<bindings::configfs_subsystem>,
    #[pin]
    data: Data,
}

// SAFETY: We do not provide any operations on `Subsystem`.
unsafe impl<Data> Sync for Subsystem<Data> {}

// SAFETY: Ownership of `Subsystem` can safely be transferred to other threads.
unsafe impl<Data> Send for Subsystem<Data> {}

impl<Data> Subsystem<Data> {
    /// Create an initializer for a [`Subsystem`].
    ///
    /// The subsystem will appear in configfs as a directory name given by
    /// `name`. The attributes available in directory are specified by
    /// `item_type`.
    pub fn new(
        name: &'static CStr,
        item_type: &'static ItemType<Subsystem<Data>, Data>,
        data: impl PinInit<Data, Error>,
    ) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            subsystem <- pin_init::init_zeroed().chain(
                |place: &mut Opaque<bindings::configfs_subsystem>| {
                    // SAFETY: We initialized the required fields of `place.group` above.
                    unsafe {
                        bindings::config_group_init_type_name(
                            &mut (*place.get()).su_group,
                            name.as_ptr(),
                            item_type.as_ptr(),
                        )
                    };

                    // SAFETY: `place.su_mutex` is valid for use as a mutex.
                    unsafe {
                        bindings::__mutex_init(
                            &mut (*place.get()).su_mutex,
                            kernel::optional_name!().as_char_ptr(),
                            kernel::static_lock_class!().as_ptr(),
                        )
                    }
                    Ok(())
                }
            ),
            data <- data,
        })
        .pin_chain(|this| {
            crate::error::to_result(
                // SAFETY: We initialized `this.subsystem` according to C API contract above.
                unsafe { bindings::configfs_register_subsystem(this.subsystem.get()) },
            )
        })
    }
}

#[pinned_drop]
impl<Data> PinnedDrop for Subsystem<Data> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: We registered `self.subsystem` in the initializer returned by `Self::new`.
        unsafe { bindings::configfs_unregister_subsystem(self.subsystem.get()) };
        // SAFETY: We initialized the mutex in `Subsystem::new`.
        unsafe { bindings::mutex_destroy(&raw mut (*self.subsystem.get()).su_mutex) };
    }
}

/// Trait that allows offset calculations for structs that embed a
/// `bindings::config_group`.
///
/// Users of the configfs API should not need to implement this trait.
///
/// # Safety
///
/// - Implementers of this trait must embed a `bindings::config_group`.
/// - Methods must be implemented according to method documentation.
pub unsafe trait HasGroup<Data> {
    /// Return the address of the `bindings::config_group` embedded in [`Self`].
    ///
    /// # Safety
    ///
    /// - `this` must be a valid allocation of at least the size of [`Self`].
    unsafe fn group(this: *const Self) -> *const bindings::config_group;

    /// Return the address of the [`Self`] that `group` is embedded in.
    ///
    /// # Safety
    ///
    /// - `group` must point to the `bindings::config_group` that is embedded in
    ///   [`Self`].
    unsafe fn container_of(group: *const bindings::config_group) -> *const Self;
}

// SAFETY: `Subsystem<Data>` embeds a field of type `bindings::config_group`
// within the `subsystem` field.
unsafe impl<Data> HasGroup<Data> for Subsystem<Data> {
    unsafe fn group(this: *const Self) -> *const bindings::config_group {
        // SAFETY: By impl and function safety requirement this projection is in bounds.
        unsafe { &raw const (*(*this).subsystem.get()).su_group }
    }

    unsafe fn container_of(group: *const bindings::config_group) -> *const Self {
        // SAFETY: By impl and function safety requirement this projection is in bounds.
        let c_subsys_ptr = unsafe { container_of!(group, bindings::configfs_subsystem, su_group) };
        let opaque_ptr = c_subsys_ptr.cast::<Opaque<bindings::configfs_subsystem>>();
        // SAFETY: By impl and function safety requirement, `opaque_ptr` and the
        // pointer it returns, are within the same allocation.
        unsafe { container_of!(opaque_ptr, Subsystem<Data>, subsystem) }
    }
}

/// A configfs group.
///
/// To add a subgroup to configfs, pass this type as `ctype` to
/// [`crate::configfs_attrs`] when creating a group in [`GroupOperations::make_group`].
#[pin_data]
pub struct Group<Data> {
    #[pin]
    group: Opaque<bindings::config_group>,
    #[pin]
    data: Data,
}

impl<Data> Group<Data> {
    /// Create an initializer for a new group.
    ///
    /// When instantiated, the group will appear as a directory with the name
    /// given by `name` and it will contain attributes specified by `item_type`.
    pub fn new(
        name: CString,
        item_type: &'static ItemType<Group<Data>, Data>,
        data: impl PinInit<Data, Error>,
    ) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            group <- pin_init::init_zeroed().chain(|v: &mut Opaque<bindings::config_group>| {
                let place = v.get();
                let name = name.to_bytes_with_nul().as_ptr();
                // SAFETY: It is safe to initialize a group once it has been zeroed.
                unsafe {
                    bindings::config_group_init_type_name(place, name.cast(), item_type.as_ptr())
                };
                Ok(())
            }),
            data <- data,
        })
    }
}

// SAFETY: `Group<Data>` embeds a field of type `bindings::config_group`
// within the `group` field.
unsafe impl<Data> HasGroup<Data> for Group<Data> {
    unsafe fn group(this: *const Self) -> *const bindings::config_group {
        Opaque::cast_into(
            // SAFETY: By impl and function safety requirements this field
            // projection is within bounds of the allocation.
            unsafe { &raw const (*this).group },
        )
    }

    unsafe fn container_of(group: *const bindings::config_group) -> *const Self {
        let opaque_ptr = group.cast::<Opaque<bindings::config_group>>();
        // SAFETY: By impl and function safety requirement, `opaque_ptr` and
        // pointer it returns will be in the same allocation.
        unsafe { container_of!(opaque_ptr, Self, group) }
    }
}

/// # Safety
///
/// `this` must be a valid pointer.
///
/// If `this` does not represent the root group of a configfs subsystem,
/// `this` must be a pointer to a `bindings::config_group` embedded in a
/// `Group<Parent>`.
///
/// Otherwise, `this` must be a pointer to a `bindings::config_group` that
/// is embedded in a `bindings::configfs_subsystem` that is embedded in a
/// `Subsystem<Parent>`.
unsafe fn get_group_data<'a, Parent>(this: *mut bindings::config_group) -> &'a Parent {
    // SAFETY: `this` is a valid pointer.
    let is_root = unsafe { (*this).cg_subsys.is_null() };

    if !is_root {
        // SAFETY: By C API contact,`this` was returned from a call to
        // `make_group`. The pointer is known to be embedded within a
        // `Group<Parent>`.
        unsafe { &(*Group::<Parent>::container_of(this)).data }
    } else {
        // SAFETY: By C API contract, `this` is a pointer to the
        // `bindings::config_group` field within a `Subsystem<Parent>`.
        unsafe { &(*Subsystem::container_of(this)).data }
    }
}

struct GroupOperationsVTable<Parent, Child>(PhantomData<(Parent, Child)>);

impl<Parent, Child> GroupOperationsVTable<Parent, Child>
where
    Parent: GroupOperations<Child = Child>,
    Child: 'static,
{
    /// # Safety
    ///
    /// `this` must be a valid pointer.
    ///
    /// If `this` does not represent the root group of a configfs subsystem,
    /// `this` must be a pointer to a `bindings::config_group` embedded in a
    /// `Group<Parent>`.
    ///
    /// Otherwise, `this` must be a pointer to a `bindings::config_group` that
    /// is embedded in a `bindings::configfs_subsystem` that is embedded in a
    /// `Subsystem<Parent>`.
    ///
    /// `name` must point to a null terminated string.
    unsafe extern "C" fn make_group(
        this: *mut bindings::config_group,
        name: *const kernel::ffi::c_char,
    ) -> *mut bindings::config_group {
        // SAFETY: By function safety requirements of this function, this call
        // is safe.
        let parent_data = unsafe { get_group_data(this) };

        let group_init = match Parent::make_group(
            parent_data,
            // SAFETY: By function safety requirements, name points to a null
            // terminated string.
            unsafe { CStr::from_char_ptr(name) },
        ) {
            Ok(init) => init,
            Err(e) => return e.to_ptr(),
        };

        let child_group = <Arc<Group<Child>> as InPlaceInit<Group<Child>>>::try_pin_init(
            group_init,
            flags::GFP_KERNEL,
        );

        match child_group {
            Ok(child_group) => {
                let child_group_ptr = child_group.into_raw();
                // SAFETY: We allocated the pointee of `child_ptr` above as a
                // `Group<Child>`.
                unsafe { Group::<Child>::group(child_group_ptr) }.cast_mut()
            }
            Err(e) => e.to_ptr(),
        }
    }

    /// # Safety
    ///
    /// If `this` does not represent the root group of a configfs subsystem,
    /// `this` must be a pointer to a `bindings::config_group` embedded in a
    /// `Group<Parent>`.
    ///
    /// Otherwise, `this` must be a pointer to a `bindings::config_group` that
    /// is embedded in a `bindings::configfs_subsystem` that is embedded in a
    /// `Subsystem<Parent>`.
    ///
    /// `item` must point to a `bindings::config_item` within a
    /// `bindings::config_group` within a `Group<Child>`.
    unsafe extern "C" fn drop_item(
        this: *mut bindings::config_group,
        item: *mut bindings::config_item,
    ) {
        // SAFETY: By function safety requirements of this function, this call
        // is safe.
        let parent_data = unsafe { get_group_data(this) };

        // SAFETY: By function safety requirements, `item` is embedded in a
        // `config_group`.
        let c_child_group_ptr = unsafe { container_of!(item, bindings::config_group, cg_item) };
        // SAFETY: By function safety requirements, `c_child_group_ptr` is
        // embedded within a `Group<Child>`.
        let r_child_group_ptr = unsafe { Group::<Child>::container_of(c_child_group_ptr) };

        if Parent::HAS_DROP_ITEM {
            // SAFETY: We called `into_raw` to produce `r_child_group_ptr` in
            // `make_group`.
            let arc: Arc<Group<Child>> = unsafe { Arc::from_raw(r_child_group_ptr.cast_mut()) };

            Parent::drop_item(parent_data, arc.as_arc_borrow());
            arc.into_raw();
        }

        // SAFETY: By C API contract, we are required to drop a refcount on
        // `item`.
        unsafe { bindings::config_item_put(item) };
    }

    const VTABLE: bindings::configfs_group_operations = bindings::configfs_group_operations {
        make_item: None,
        make_group: Some(Self::make_group),
        disconnect_notify: None,
        drop_item: Some(Self::drop_item),
        is_visible: None,
        is_bin_visible: None,
    };

    const fn vtable_ptr() -> *const bindings::configfs_group_operations {
        &Self::VTABLE
    }
}

struct ItemOperationsVTable<Container, Data>(PhantomData<(Container, Data)>);

impl<Data> ItemOperationsVTable<Group<Data>, Data>
where
    Data: 'static,
{
    /// # Safety
    ///
    /// `this` must be a pointer to a `bindings::config_group` embedded in a
    /// `Group<Parent>`.
    ///
    /// This function will destroy the pointee of `this`. The pointee of `this`
    /// must not be accessed after the function returns.
    unsafe extern "C" fn release(this: *mut bindings::config_item) {
        // SAFETY: By function safety requirements, `this` is embedded in a
        // `config_group`.
        let c_group_ptr = unsafe { kernel::container_of!(this, bindings::config_group, cg_item) };
        // SAFETY: By function safety requirements, `c_group_ptr` is
        // embedded within a `Group<Data>`.
        let r_group_ptr = unsafe { Group::<Data>::container_of(c_group_ptr) };

        // SAFETY: We called `into_raw` on `r_group_ptr` in
        // `make_group`.
        let pin_self: Arc<Group<Data>> = unsafe { Arc::from_raw(r_group_ptr.cast_mut()) };
        drop(pin_self);
    }

    const VTABLE: bindings::configfs_item_operations = bindings::configfs_item_operations {
        release: Some(Self::release),
        allow_link: None,
        drop_link: None,
    };

    const fn vtable_ptr() -> *const bindings::configfs_item_operations {
        &Self::VTABLE
    }
}

impl<Data> ItemOperationsVTable<Subsystem<Data>, Data> {
    const VTABLE: bindings::configfs_item_operations = bindings::configfs_item_operations {
        release: None,
        allow_link: None,
        drop_link: None,
    };

    const fn vtable_ptr() -> *const bindings::configfs_item_operations {
        &Self::VTABLE
    }
}

/// Operations implemented by configfs groups that can create subgroups.
///
/// Implement this trait on structs that embed a [`Subsystem`] or a [`Group`].
#[vtable]
pub trait GroupOperations {
    /// The child data object type.
    ///
    /// This group will create subgroups (subdirectories) backed by this kind of
    /// object.
    type Child: 'static;

    /// Creates a new subgroup.
    ///
    /// The kernel will call this method in response to `mkdir(2)` in the
    /// directory representing `this`.
    ///
    /// To accept the request to create a group, implementations should
    /// return an initializer of a `Group<Self::Child>`. To prevent creation,
    /// return a suitable error.
    fn make_group(&self, name: &CStr) -> Result<impl PinInit<Group<Self::Child>, Error>>;

    /// Prepares the group for removal from configfs.
    ///
    /// The kernel will call this method before the directory representing `_child` is removed from
    /// configfs.
    ///
    /// Implementations can use this method to do house keeping before configfs drops its
    /// reference to `Child`.
    ///
    /// NOTE: "drop" in the name of this function is not related to the Rust drop term. Rather, the
    /// name is inherited from the callback name in the underlying C code.
    fn drop_item(&self, _child: ArcBorrow<'_, Group<Self::Child>>) {
        kernel::build_error!(kernel::error::VTABLE_DEFAULT_ERROR)
    }
}

/// A configfs attribute.
///
/// An attribute appears as a file in configfs, inside a folder that represent
/// the group that the attribute belongs to.
#[repr(transparent)]
pub struct Attribute<const ID: u64, O, Data> {
    attribute: Opaque<bindings::configfs_attribute>,
    _p: PhantomData<(O, Data)>,
}

// SAFETY: We do not provide any operations on `Attribute`.
unsafe impl<const ID: u64, O, Data> Sync for Attribute<ID, O, Data> {}

// SAFETY: Ownership of `Attribute` can safely be transferred to other threads.
unsafe impl<const ID: u64, O, Data> Send for Attribute<ID, O, Data> {}

impl<const ID: u64, O, Data> Attribute<ID, O, Data>
where
    O: AttributeOperations<ID, Data = Data>,
{
    /// # Safety
    ///
    /// `item` must be embedded in a `bindings::config_group`.
    ///
    /// If `item` does not represent the root group of a configfs subsystem,
    /// the group must be embedded in a `Group<Data>`.
    ///
    /// Otherwise, the group must be a embedded in a
    /// `bindings::configfs_subsystem` that is embedded in a `Subsystem<Data>`.
    ///
    /// `page` must point to a writable buffer of size at least [`PAGE_SIZE`].
    unsafe extern "C" fn show(
        item: *mut bindings::config_item,
        page: *mut kernel::ffi::c_char,
    ) -> isize {
        let c_group: *mut bindings::config_group =
            // SAFETY: By function safety requirements, `item` is embedded in a
            // `config_group`.
            unsafe { container_of!(item, bindings::config_group, cg_item) };

        // SAFETY: The function safety requirements for this function satisfy
        // the conditions for this call.
        let data: &Data = unsafe { get_group_data(c_group) };

        // SAFETY: By function safety requirements, `page` is writable for `PAGE_SIZE`.
        let ret = O::show(data, unsafe { &mut *(page.cast::<[u8; PAGE_SIZE]>()) });

        match ret {
            Ok(size) => size as isize,
            Err(err) => err.to_errno() as isize,
        }
    }

    /// # Safety
    ///
    /// `item` must be embedded in a `bindings::config_group`.
    ///
    /// If `item` does not represent the root group of a configfs subsystem,
    /// the group must be embedded in a `Group<Data>`.
    ///
    /// Otherwise, the group must be a embedded in a
    /// `bindings::configfs_subsystem` that is embedded in a `Subsystem<Data>`.
    ///
    /// `page` must point to a readable buffer of size at least `size`.
    unsafe extern "C" fn store(
        item: *mut bindings::config_item,
        page: *const kernel::ffi::c_char,
        size: usize,
    ) -> isize {
        let c_group: *mut bindings::config_group =
        // SAFETY: By function safety requirements, `item` is embedded in a
        // `config_group`.
            unsafe { container_of!(item, bindings::config_group, cg_item) };

        // SAFETY: The function safety requirements for this function satisfy
        // the conditions for this call.
        let data: &Data = unsafe { get_group_data(c_group) };

        let ret = O::store(
            data,
            // SAFETY: By function safety requirements, `page` is readable
            // for at least `size`.
            unsafe { core::slice::from_raw_parts(page.cast(), size) },
        );

        match ret {
            Ok(()) => size as isize,
            Err(err) => err.to_errno() as isize,
        }
    }

    /// Create a new attribute.
    ///
    /// The attribute will appear as a file with name given by `name`.
    pub const fn new(name: &'static CStr) -> Self {
        Self {
            attribute: Opaque::new(bindings::configfs_attribute {
                ca_name: crate::str::as_char_ptr_in_const_context(name),
                ca_owner: core::ptr::null_mut(),
                ca_mode: 0o660,
                show: Some(Self::show),
                store: if O::HAS_STORE {
                    Some(Self::store)
                } else {
                    None
                },
            }),
            _p: PhantomData,
        }
    }
}

/// Operations supported by an attribute.
///
/// Implement this trait on type and pass that type as generic parameter when
/// creating an [`Attribute`]. The type carrying the implementation serve no
/// purpose other than specifying the attribute operations.
///
/// This trait must be implemented on the `Data` type of for types that
/// implement `HasGroup<Data>`. The trait must be implemented once for each
/// attribute of the group. The constant type parameter `ID` maps the
/// implementation to a specific `Attribute`. `ID` must be passed when declaring
/// attributes via the [`kernel::configfs_attrs`] macro, to tie
/// `AttributeOperations` implementations to concrete named attributes.
#[vtable]
pub trait AttributeOperations<const ID: u64 = 0> {
    /// The type of the object that contains the field that is backing the
    /// attribute for this operation.
    type Data;

    /// Renders the value of an attribute.
    ///
    /// This function is called by the kernel to read the value of an attribute.
    ///
    /// Implementations should write the rendering of the attribute to `page`
    /// and return the number of bytes written.
    fn show(data: &Self::Data, page: &mut [u8; PAGE_SIZE]) -> Result<usize>;

    /// Stores the value of an attribute.
    ///
    /// This function is called by the kernel to update the value of an attribute.
    ///
    /// Implementations should parse the value from `page` and update internal
    /// state to reflect the parsed value.
    fn store(_data: &Self::Data, _page: &[u8]) -> Result {
        kernel::build_error!(kernel::error::VTABLE_DEFAULT_ERROR)
    }
}

/// A list of attributes.
///
/// This type is used to construct a new [`ItemType`]. It represents a list of
/// [`Attribute`] that will appear in the directory representing a [`Group`].
/// Users should not directly instantiate this type, rather they should use the
/// [`kernel::configfs_attrs`] macro to declare a static set of attributes for a
/// group.
///
/// # Note
///
/// Instances of this type are constructed statically at compile by the
/// [`kernel::configfs_attrs`] macro.
#[repr(transparent)]
pub struct AttributeList<const N: usize, Data>(
    /// Null terminated Array of pointers to [`Attribute`]. The type is [`c_void`]
    /// to conform to the C API.
    UnsafeCell<[*mut kernel::ffi::c_void; N]>,
    PhantomData<Data>,
);

// SAFETY: Ownership of `AttributeList` can safely be transferred to other threads.
unsafe impl<const N: usize, Data> Send for AttributeList<N, Data> {}

// SAFETY: We do not provide any operations on `AttributeList` that need synchronization.
unsafe impl<const N: usize, Data> Sync for AttributeList<N, Data> {}

impl<const N: usize, Data> AttributeList<N, Data> {
    /// # Safety
    ///
    /// This function must only be called by the [`kernel::configfs_attrs`]
    /// macro.
    #[doc(hidden)]
    pub const unsafe fn new() -> Self {
        Self(UnsafeCell::new([core::ptr::null_mut(); N]), PhantomData)
    }

    /// # Safety
    ///
    /// The caller must ensure that there are no other concurrent accesses to
    /// `self`. That is, the caller has exclusive access to `self.`
    #[doc(hidden)]
    pub const unsafe fn add<const I: usize, const ID: u64, O>(
        &'static self,
        attribute: &'static Attribute<ID, O, Data>,
    ) where
        O: AttributeOperations<ID, Data = Data>,
    {
        // We need a space at the end of our list for a null terminator.
        const { assert!(I < N - 1, "Invalid attribute index") };

        // SAFETY: By function safety requirements, we have exclusive access to
        // `self` and the reference created below will be exclusive.
        unsafe { (&mut *self.0.get())[I] = core::ptr::from_ref(attribute).cast_mut().cast() };
    }
}

/// A representation of the attributes that will appear in a [`Group`] or
/// [`Subsystem`].
///
/// Users should not directly instantiate objects of this type. Rather, they
/// should use the [`kernel::configfs_attrs`] macro to statically declare the
/// shape of a [`Group`] or [`Subsystem`].
#[pin_data]
pub struct ItemType<Container, Data> {
    #[pin]
    item_type: Opaque<bindings::config_item_type>,
    _p: PhantomData<(Container, Data)>,
}

// SAFETY: We do not provide any operations on `ItemType` that need synchronization.
unsafe impl<Container, Data> Sync for ItemType<Container, Data> {}

// SAFETY: Ownership of `ItemType` can safely be transferred to other threads.
unsafe impl<Container, Data> Send for ItemType<Container, Data> {}

macro_rules! impl_item_type {
    ($tpe:ty) => {
        impl<Data> ItemType<$tpe, Data> {
            #[doc(hidden)]
            pub const fn new_with_child_ctor<const N: usize, Child>(
                owner: &'static ThisModule,
                attributes: &'static AttributeList<N, Data>,
            ) -> Self
            where
                Data: GroupOperations<Child = Child>,
                Child: 'static,
            {
                Self {
                    item_type: Opaque::new(bindings::config_item_type {
                        ct_owner: owner.as_ptr(),
                        ct_group_ops: GroupOperationsVTable::<Data, Child>::vtable_ptr().cast_mut(),
                        ct_item_ops: ItemOperationsVTable::<$tpe, Data>::vtable_ptr().cast_mut(),
                        ct_attrs: core::ptr::from_ref(attributes).cast_mut().cast(),
                        ct_bin_attrs: core::ptr::null_mut(),
                    }),
                    _p: PhantomData,
                }
            }

            #[doc(hidden)]
            pub const fn new<const N: usize>(
                owner: &'static ThisModule,
                attributes: &'static AttributeList<N, Data>,
            ) -> Self {
                Self {
                    item_type: Opaque::new(bindings::config_item_type {
                        ct_owner: owner.as_ptr(),
                        ct_group_ops: core::ptr::null_mut(),
                        ct_item_ops: ItemOperationsVTable::<$tpe, Data>::vtable_ptr().cast_mut(),
                        ct_attrs: core::ptr::from_ref(attributes).cast_mut().cast(),
                        ct_bin_attrs: core::ptr::null_mut(),
                    }),
                    _p: PhantomData,
                }
            }
        }
    };
}

impl_item_type!(Subsystem<Data>);
impl_item_type!(Group<Data>);

impl<Container, Data> ItemType<Container, Data> {
    fn as_ptr(&self) -> *const bindings::config_item_type {
        self.item_type.get()
    }
}

/// Define a list of configfs attributes statically.
///
/// Invoking the macro in the following manner:
///
/// ```ignore
/// let item_type = configfs_attrs! {
///     container: configfs::Subsystem<Configuration>,
///     data: Configuration,
///     child: Child,
///     attributes: [
///         message: 0,
///         bar: 1,
///     ],
/// };
/// ```
///
/// Expands the following output:
///
/// ```ignore
/// let item_type = {
///     static CONFIGURATION_MESSAGE_ATTR: kernel::configfs::Attribute<
///         0,
///         Configuration,
///         Configuration,
///     > = unsafe {
///         kernel::configfs::Attribute::new({
///             const S: &str = "message\u{0}";
///             const C: &kernel::str::CStr = match kernel::str::CStr::from_bytes_with_nul(
///                 S.as_bytes()
///             ) {
///                 Ok(v) => v,
///                 Err(_) => {
///                     core::panicking::panic_fmt(core::const_format_args!(
///                         "string contains interior NUL"
///                     ));
///                 }
///             };
///             C
///         })
///     };
///
///     static CONFIGURATION_BAR_ATTR: kernel::configfs::Attribute<
///             1,
///             Configuration,
///             Configuration
///     > = unsafe {
///         kernel::configfs::Attribute::new({
///             const S: &str = "bar\u{0}";
///             const C: &kernel::str::CStr = match kernel::str::CStr::from_bytes_with_nul(
///                 S.as_bytes()
///             ) {
///                 Ok(v) => v,
///                 Err(_) => {
///                     core::panicking::panic_fmt(core::const_format_args!(
///                         "string contains interior NUL"
///                     ));
///                 }
///             };
///             C
///         })
///     };
///
///     const N: usize = (1usize + (1usize + 0usize)) + 1usize;
///
///     static CONFIGURATION_ATTRS: kernel::configfs::AttributeList<N, Configuration> =
///         unsafe { kernel::configfs::AttributeList::new() };
///
///     {
///         const N: usize = 0usize;
///         unsafe { CONFIGURATION_ATTRS.add::<N, 0, _>(&CONFIGURATION_MESSAGE_ATTR) };
///     }
///
///     {
///         const N: usize = (1usize + 0usize);
///         unsafe { CONFIGURATION_ATTRS.add::<N, 1, _>(&CONFIGURATION_BAR_ATTR) };
///     }
///
///     static CONFIGURATION_TPE:
///       kernel::configfs::ItemType<configfs::Subsystem<Configuration> ,Configuration>
///         = kernel::configfs::ItemType::<
///                 configfs::Subsystem<Configuration>,
///                 Configuration
///                 >::new_with_child_ctor::<N,Child>(
///             &THIS_MODULE,
///             &CONFIGURATION_ATTRS
///         );
///
///     &CONFIGURATION_TPE
/// }
/// ```
#[macro_export]
macro_rules! configfs_attrs {
    (
        container: $container:ty,
        data: $data:ty,
        attributes: [
            $($name:ident: $attr:literal),* $(,)?
        ] $(,)?
    ) => {
        $crate::configfs_attrs!(
            count:
            @container($container),
            @data($data),
            @child(),
            @no_child(x),
            @attrs($($name $attr)*),
            @eat($($name $attr,)*),
            @assign(),
            @cnt(0usize),
        )
    };
    (
        container: $container:ty,
        data: $data:ty,
        child: $child:ty,
        attributes: [
            $($name:ident: $attr:literal),* $(,)?
        ] $(,)?
    ) => {
        $crate::configfs_attrs!(
            count:
            @container($container),
            @data($data),
            @child($child),
            @no_child(),
            @attrs($($name $attr)*),
            @eat($($name $attr,)*),
            @assign(),
            @cnt(0usize),
        )
    };
    (count:
     @container($container:ty),
     @data($data:ty),
     @child($($child:ty)?),
     @no_child($($no_child:ident)?),
     @attrs($($aname:ident $aattr:literal)*),
     @eat($name:ident $attr:literal, $($rname:ident $rattr:literal,)*),
     @assign($($assign:block)*),
     @cnt($cnt:expr),
    ) => {
        $crate::configfs_attrs!(
            count:
            @container($container),
            @data($data),
            @child($($child)?),
            @no_child($($no_child)?),
            @attrs($($aname $aattr)*),
            @eat($($rname $rattr,)*),
            @assign($($assign)* {
                const N: usize = $cnt;
                // The following macro text expands to a call to `Attribute::add`.

                // SAFETY: By design of this macro, the name of the variable we
                // invoke the `add` method on below, is not visible outside of
                // the macro expansion. The macro does not operate concurrently
                // on this variable, and thus we have exclusive access to the
                // variable.
                unsafe {
                    $crate::macros::paste!(
                        [< $data:upper _ATTRS >]
                            .add::<N, $attr, _>(&[< $data:upper _ $name:upper _ATTR >])
                    )
                };
            }),
            @cnt(1usize + $cnt),
        )
    };
    (count:
     @container($container:ty),
     @data($data:ty),
     @child($($child:ty)?),
     @no_child($($no_child:ident)?),
     @attrs($($aname:ident $aattr:literal)*),
     @eat(),
     @assign($($assign:block)*),
     @cnt($cnt:expr),
    ) =>
    {
        $crate::configfs_attrs!(
            final:
            @container($container),
            @data($data),
            @child($($child)?),
            @no_child($($no_child)?),
            @attrs($($aname $aattr)*),
            @assign($($assign)*),
            @cnt($cnt),
        )
    };
    (final:
     @container($container:ty),
     @data($data:ty),
     @child($($child:ty)?),
     @no_child($($no_child:ident)?),
     @attrs($($name:ident $attr:literal)*),
     @assign($($assign:block)*),
     @cnt($cnt:expr),
    ) =>
    {
        $crate::macros::paste!{
            {
                $(
                    // SAFETY: We are expanding `configfs_attrs`.
                    static [< $data:upper _ $name:upper _ATTR >]:
                        $crate::configfs::Attribute<$attr, $data, $data> =
                            unsafe {
                                $crate::configfs::Attribute::new(c_str!(::core::stringify!($name)))
                            };
                )*


                // We need space for a null terminator.
                const N: usize = $cnt + 1usize;

                // SAFETY: We are expanding `configfs_attrs`.
                static [< $data:upper _ATTRS >]:
                $crate::configfs::AttributeList<N, $data> =
                    unsafe { $crate::configfs::AttributeList::new() };

                $($assign)*

                $(
                    const [<$no_child:upper>]: bool = true;

                    static [< $data:upper _TPE >] : $crate::configfs::ItemType<$container, $data>  =
                        $crate::configfs::ItemType::<$container, $data>::new::<N>(
                            &THIS_MODULE, &[<$ data:upper _ATTRS >]
                        );
                )?

                $(
                    static [< $data:upper _TPE >]:
                        $crate::configfs::ItemType<$container, $data>  =
                            $crate::configfs::ItemType::<$container, $data>::
                            new_with_child_ctor::<N, $child>(
                                &THIS_MODULE, &[<$ data:upper _ATTRS >]
                            );
                )?

                & [< $data:upper _TPE >]
            }
        }
    };

}

pub use crate::configfs_attrs;
