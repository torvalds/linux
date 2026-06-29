// SPDX-License-Identifier: GPL-2.0

//! Generic implementation of device IDs.
//!
//! Each bus / subsystem that matches device and driver through a bus / subsystem specific ID is
//! expected to implement [`RawDeviceId`].

use core::{
    marker::PhantomData,
    mem::MaybeUninit, //
};

/// Marker trait to indicate a Rust device ID type represents a corresponding C device ID type.
///
/// This is meant to be implemented by buses/subsystems so that they can use [`IdTable`] to
/// guarantee (at compile-time) zero-termination of device id tables provided by drivers.
///
/// # Safety
///
/// Implementers must ensure that `Self` is layout-compatible with [`RawDeviceId::RawType`];
/// i.e. it's safe to transmute to `RawType`.
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

    /// Obtain the data pointer stored inside the device ID.
    ///
    /// # Safety
    ///
    /// `&Self` must be stored inside a `IdArray<Self, U>`.
    unsafe fn info_unchecked<U>(&self) -> &'static U {
        // SAFETY: By safety requirement of the trait, this is `self.driver_data as *const U` and by
        // the safety requirement of the function, this is stored in `IdArray<Self, U>` so is
        // convertible to `&'static U`.
        unsafe {
            core::ptr::from_ref(self)
                .byte_add(Self::DRIVER_DATA_OFFSET)
                .cast::<&U>()
                .read()
        }
    }

    /// Obtain the data pointer stored inside the device ID.
    ///
    /// # Safety
    ///
    /// `&Self` must be stored inside a `IdArray<Self, U>`, or has NULL (or 0) as driver data.
    unsafe fn info_unchecked_opt<U>(&self) -> Option<&'static U> {
        // SAFETY: By safety requirement of the trait, this is `self.driver_data as *const U` and by
        // the safety requirement of the function, if this is stored in `IdArray<Self, U>`, this is
        // convertible to `Option<&'static U>`. Otherwise it is NULL which is `None` as
        // `Option<&U>`.
        unsafe {
            core::ptr::from_ref(self)
                .byte_add(Self::DRIVER_DATA_OFFSET)
                .cast::<Option<&U>>()
                .read()
        }
    }
}

/// A zero-terminated device id array, followed by context data.
#[repr(C)]
pub struct IdArray<T: RawDeviceId, U: 'static, const N: usize> {
    // This is `MaybeUninit<T::RawType>` so any bytes inside it can carry provenance in CTFE.
    // If this were `T::RawType`, integer fields would not be able to contain pointers.
    ids: [MaybeUninit<T::RawType>; N],
    sentinel: MaybeUninit<T::RawType>,
    phantom: PhantomData<&'static U>,
}

// SAFETY: device ID is plain data plus a `&'static U` and can thus be sent between threads safely
// if `&U` can.
unsafe impl<T: RawDeviceId, U: Sync + 'static, const N: usize> Send for IdArray<T, U, N> {}

// SAFETY: device ID is plain data plus a `&'static U` and can thus be shared between threads safely
// if `&U` can.
unsafe impl<T: RawDeviceId, U: Sync + 'static, const N: usize> Sync for IdArray<T, U, N> {}

impl<T: RawDeviceId + RawDeviceIdIndex, U: 'static, const N: usize> IdArray<T, U, N> {
    /// Creates a new instance of the array.
    ///
    /// The contents are derived from the given identifiers and context information.
    pub const fn new(ids: [(T, &'static U); N]) -> Self {
        let mut raw_ids = [const { MaybeUninit::<T::RawType>::uninit() }; N];

        let mut i = 0usize;
        while i < N {
            // SAFETY: by the safety requirement of `RawDeviceId`, we're guaranteed that `T` is
            // layout-wise compatible with `RawType`.
            raw_ids[i] = unsafe { core::mem::transmute_copy(&ids[i].0) };
            // SAFETY: by the safety requirement of `RawDeviceIdIndex`, this would be effectively
            // `raw_ids[i].driver_data = ids[i].1;`.
            unsafe {
                raw_ids[i]
                    .as_mut_ptr()
                    .byte_add(T::DRIVER_DATA_OFFSET)
                    .cast::<&U>()
                    .write(ids[i].1);
            }

            i += 1;
        }

        core::mem::forget(ids);

        Self {
            ids: raw_ids,
            sentinel: MaybeUninit::zeroed(),
            phantom: PhantomData,
        }
    }
}

impl<T: RawDeviceId, const N: usize> IdArray<T, (), N> {
    /// Creates a new instance of the array without writing index values.
    ///
    /// The contents are derived from the given identifiers and context information.
    /// If the device implements [`RawDeviceIdIndex`], consider using [`IdArray::new`] instead.
    pub const fn new_without_index(ids: [T; N]) -> Self {
        // SAFETY: `T` is layout-wise compatible with `T::RawType`, so is the array of them.
        let raw_ids: [MaybeUninit<T::RawType>; N] = unsafe { core::mem::transmute_copy(&ids) };
        core::mem::forget(ids);

        Self {
            ids: raw_ids,
            sentinel: MaybeUninit::zeroed(),
            phantom: PhantomData,
        }
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
}

impl<T: RawDeviceId, U, const N: usize> IdTable<T, U> for IdArray<T, U, N> {
    fn as_ptr(&self) -> *const T::RawType {
        // This cannot be `self.ids.as_ptr()`, as the return pointer must have correct provenance
        // to access the sentinel.
        core::ptr::from_ref(self).cast()
    }
}

/// Create device table alias for modpost.
#[macro_export]
macro_rules! module_device_table {
    (
        $table_type: literal, $device_id_ty: ty,
        $table_name: ident, $id_info_type: ty,
        [$(($id: expr, $info:expr $(,)?)),* $(,)?]
    ) => {
        #[export_name =
            concat!("__mod_device_table__", ::core::line!(),
                    "__kmod_", module_path!(),
                    "__", $table_type,
                    "__", stringify!($table_name))
        ]
        static $table_name: $crate::device_id::IdArray<
            $device_id_ty,
            $id_info_type,
            { <[$device_id_ty]>::len(&[$($id,)*]) },
        > = $crate::device_id::IdArray::new([$(($id, &$info),)*]);
    };

    // Case for no ID info.
    (
        $table_type: literal, $device_id_ty: ty,
        $table_name: ident, @none,
        [$($id: expr),* $(,)?]
    ) => {
        #[export_name =
            concat!("__mod_device_table__", ::core::line!(),
                    "__kmod_", module_path!(),
                    "__", $table_type,
                    "__", stringify!($table_name))
        ]
        static $table_name: $crate::device_id::IdArray<
            $device_id_ty,
            (),
            { <[$device_id_ty]>::len(&[$($id,)*]) },
        > = $crate::device_id::IdArray::new_without_index([$($id),*]);
    };
}
