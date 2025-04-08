// SPDX-License-Identifier: GPL-2.0

//! Generic implementation of device IDs.
//!
//! Each bus / subsystem that matches device and driver through a bus / subsystem specific ID is
//! expected to implement [`RawDeviceId`].

use core::mem::MaybeUninit;

/// Marker trait to indicate a Rust device ID type represents a corresponding C device ID type.
///
/// This is meant to be implemented by buses/subsystems so that they can use [`IdTable`] to
/// guarantee (at compile-time) zero-termination of device id tables provided by drivers.
///
/// # Safety
///
/// Implementers must ensure that:
///   - `Self` is layout-compatible with [`RawDeviceId::RawType`]; i.e. it's safe to transmute to
///     `RawDeviceId`.
///
///     This requirement is needed so `IdArray::new` can convert `Self` to `RawType` when building
///     the ID table.
///
///     Ideally, this should be achieved using a const function that does conversion instead of
///     transmute; however, const trait functions relies on `const_trait_impl` unstable feature,
///     which is broken/gone in Rust 1.73.
///
///   - `DRIVER_DATA_OFFSET` is the offset of context/data field of the device ID (usually named
///     `driver_data`) of the device ID, the field is suitable sized to write a `usize` value.
///
///     Similar to the previous requirement, the data should ideally be added during `Self` to
///     `RawType` conversion, but there's currently no way to do it when using traits in const.
pub unsafe trait RawDeviceId {
    /// The raw type that holds the device id.
    ///
    /// Id tables created from [`Self`] are going to hold this type in its zero-terminated array.
    type RawType: Copy;

    /// The offset to the context/data field.
    const DRIVER_DATA_OFFSET: usize;

    /// The index stored at `DRIVER_DATA_OFFSET` of the implementor of the [`RawDeviceId`] trait.
    fn index(&self) -> usize;
}

/// A zero-terminated device id array.
#[repr(C)]
pub struct RawIdArray<T: RawDeviceId, const N: usize> {
    ids: [T::RawType; N],
    sentinel: MaybeUninit<T::RawType>,
}

impl<T: RawDeviceId, const N: usize> RawIdArray<T, N> {
    #[doc(hidden)]
    pub const fn size(&self) -> usize {
        core::mem::size_of::<Self>()
    }
}

/// A zero-terminated device id array, followed by context data.
#[repr(C)]
pub struct IdArray<T: RawDeviceId, U, const N: usize> {
    raw_ids: RawIdArray<T, N>,
    id_infos: [U; N],
}

impl<T: RawDeviceId, U, const N: usize> IdArray<T, U, N> {
    /// Creates a new instance of the array.
    ///
    /// The contents are derived from the given identifiers and context information.
    pub const fn new(ids: [(T, U); N]) -> Self {
        let mut raw_ids = [const { MaybeUninit::<T::RawType>::uninit() }; N];
        let mut infos = [const { MaybeUninit::uninit() }; N];

        let mut i = 0usize;
        while i < N {
            // SAFETY: by the safety requirement of `RawDeviceId`, we're guaranteed that `T` is
            // layout-wise compatible with `RawType`.
            raw_ids[i] = unsafe { core::mem::transmute_copy(&ids[i].0) };
            // SAFETY: by the safety requirement of `RawDeviceId`, this would be effectively
            // `raw_ids[i].driver_data = i;`.
            unsafe {
                raw_ids[i]
                    .as_mut_ptr()
                    .byte_offset(T::DRIVER_DATA_OFFSET as _)
                    .cast::<usize>()
                    .write(i);
            }

            // SAFETY: this is effectively a move: `infos[i] = ids[i].1`. We make a copy here but
            // later forget `ids`.
            infos[i] = MaybeUninit::new(unsafe { core::ptr::read(&ids[i].1) });
            i += 1;
        }

        core::mem::forget(ids);

        Self {
            raw_ids: RawIdArray {
                // SAFETY: this is effectively `array_assume_init`, which is unstable, so we use
                // `transmute_copy` instead. We have initialized all elements of `raw_ids` so this
                // `array_assume_init` is safe.
                ids: unsafe { core::mem::transmute_copy(&raw_ids) },
                sentinel: MaybeUninit::zeroed(),
            },
            // SAFETY: We have initialized all elements of `infos` so this `array_assume_init` is
            // safe.
            id_infos: unsafe { core::mem::transmute_copy(&infos) },
        }
    }

    /// Reference to the contained [`RawIdArray`].
    pub const fn raw_ids(&self) -> &RawIdArray<T, N> {
        &self.raw_ids
    }
}

/// A device id table.
///
/// This trait is only implemented by `IdArray`.
///
/// The purpose of this trait is to allow `&'static dyn IdArray<T, U>` to be in context when `N` in
/// `IdArray` doesn't matter.
pub trait IdTable<T: RawDeviceId, U> {
    /// Obtain the pointer to the ID table.
    fn as_ptr(&self) -> *const T::RawType;

    /// Obtain the pointer to the bus specific device ID from an index.
    fn id(&self, index: usize) -> &T::RawType;

    /// Obtain the pointer to the driver-specific information from an index.
    fn info(&self, index: usize) -> &U;
}

impl<T: RawDeviceId, U, const N: usize> IdTable<T, U> for IdArray<T, U, N> {
    fn as_ptr(&self) -> *const T::RawType {
        // This cannot be `self.ids.as_ptr()`, as the return pointer must have correct provenance
        // to access the sentinel.
        (self as *const Self).cast()
    }

    fn id(&self, index: usize) -> &T::RawType {
        &self.raw_ids.ids[index]
    }

    fn info(&self, index: usize) -> &U {
        &self.id_infos[index]
    }
}

/// Create device table alias for modpost.
#[macro_export]
macro_rules! module_device_table {
    ($table_type: literal, $module_table_name:ident, $table_name:ident) => {
        #[rustfmt::skip]
        #[export_name =
            concat!("__mod_device_table__", $table_type,
                    "__", module_path!(),
                    "_", line!(),
                    "_", stringify!($table_name))
        ]
        static $module_table_name: [core::mem::MaybeUninit<u8>; $table_name.raw_ids().size()] =
            unsafe { core::mem::transmute_copy($table_name.raw_ids()) };
    };
}
