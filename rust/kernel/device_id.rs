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
/// Implementers must ensure that `Self` is layout-compatible with [`RawDeviceId::RawType`];
/// i.e. it's safe to transmute to `RawDeviceId`.
///
/// This requirement is needed so `IdArray::new` can convert `Self` to `RawType` when building
/// the ID table.
///
/// Ideally, this should be achieved using a const function that does conversion instead of
/// transmute; however, const trait functions relies on `const_trait_impl` unstable feature,
/// which is broken/gone in Rust 1.73.
pub unsafe trait RawDeviceId {
    /// The raw type that holds the device id.
    ///
    /// Id tables created from [`Self`] are going to hold this type in its zero-terminated array.
    type RawType: Copy;
}

/// Extension trait for [`RawDeviceId`] for devices that embed an index or context value.
///
/// This is typically used when the device ID struct includes a field like `driver_data`
/// that is used to store a pointer-sized value (e.g., an index or context pointer).
///
/// # Safety
///
/// Implementers must ensure that `DRIVER_DATA_OFFSET` is the correct offset (in bytes) to
/// the context/data field (e.g., the `driver_data` field) within the raw device ID structure.
/// This field must be correctly sized to hold a `usize`.
///
/// Ideally, the data should be added during `Self` to `RawType` conversion,
/// but there's currently no way to do it when using traits in const.
pub unsafe trait RawDeviceIdIndex: RawDeviceId {
    /// The offset (in bytes) to the context/data field in the raw device ID.
    const DRIVER_DATA_OFFSET: usize;

    /// The index stored at `DRIVER_DATA_OFFSET` of the implementor of the [`RawDeviceIdIndex`]
    /// trait.
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
    ///
    /// # Safety
    ///
    /// `data_offset` as `None` is always safe.
    /// If `data_offset` is `Some(data_offset)`, then:
    /// - `data_offset` must be the correct offset (in bytes) to the context/data field
    ///   (e.g., the `driver_data` field) within the raw device ID structure.
    /// - The field at `data_offset` must be correctly sized to hold a `usize`.
    const unsafe fn build(ids: [(T, U); N], data_offset: Option<usize>) -> Self {
        let mut raw_ids = [const { MaybeUninit::<T::RawType>::uninit() }; N];
        let mut infos = [const { MaybeUninit::uninit() }; N];

        let mut i = 0usize;
        while i < N {
            // SAFETY: by the safety requirement of `RawDeviceId`, we're guaranteed that `T` is
            // layout-wise compatible with `RawType`.
            raw_ids[i] = unsafe { core::mem::transmute_copy(&ids[i].0) };
            if let Some(data_offset) = data_offset {
                // SAFETY: by the safety requirement of this function, this would be effectively
                // `raw_ids[i].driver_data = i;`.
                unsafe {
                    raw_ids[i]
                        .as_mut_ptr()
                        .byte_add(data_offset)
                        .cast::<usize>()
                        .write(i);
                }
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

    /// Creates a new instance of the array without writing index values.
    ///
    /// The contents are derived from the given identifiers and context information.
    /// If the device implements [`RawDeviceIdIndex`], consider using [`IdArray::new`] instead.
    pub const fn new_without_index(ids: [(T, U); N]) -> Self {
        // SAFETY: Calling `Self::build` with `offset = None` is always safe,
        // because no raw memory writes are performed in this case.
        unsafe { Self::build(ids, None) }
    }

    /// Reference to the contained [`RawIdArray`].
    pub const fn raw_ids(&self) -> &RawIdArray<T, N> {
        &self.raw_ids
    }
}

impl<T: RawDeviceId + RawDeviceIdIndex, U, const N: usize> IdArray<T, U, N> {
    /// Creates a new instance of the array.
    ///
    /// The contents are derived from the given identifiers and context information.
    pub const fn new(ids: [(T, U); N]) -> Self {
        // SAFETY: by the safety requirement of `RawDeviceIdIndex`,
        // `T::DRIVER_DATA_OFFSET` is guaranteed to be the correct offset (in bytes) to
        // a field within `T::RawType`.
        unsafe { Self::build(ids, Some(T::DRIVER_DATA_OFFSET)) }
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
        core::ptr::from_ref(self).cast()
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
            concat!("__mod_device_table__", line!(),
                    "__kmod_", module_path!(),
                    "__", $table_type,
                    "__", stringify!($table_name))
        ]
        static $module_table_name: [::core::mem::MaybeUninit<u8>; $table_name.raw_ids().size()] =
            unsafe { ::core::mem::transmute_copy($table_name.raw_ids()) };
    };
}
