// SPDX-License-Identifier: GPL-2.0

//! Generic device ID implementation for Rust kernel modules.
//!
//! Provides types and traits for creating zero-terminated device ID tables that are compatible
//! with the Linux kernel's device model. This is used by buses/subsystems to match devices with
//! their drivers.
//!
//! The implementation ensures type safety while maintaining compatibility with C kernel code.

use core::mem::MaybeUninit;

/// Trait marking a Rust device ID type as representing a corresponding C device ID type.
///
/// # Safety
///
/// Implementers must guarantee that:
/// 1. `Self` has identical memory layout to `RawType` (can be safely transmuted)
/// 2. `DRIVER_DATA_OFFSET` correctly points to the driver_data field in the raw type
/// 3. The field at `DRIVER_DATA_OFFSET` has space for a usize value
///
/// These requirements allow safe conversion between Rust and C device ID types while maintaining
/// the expected memory layout for the kernel.
pub unsafe trait RawDeviceId {
    /// The corresponding raw C type for this device ID
    type RawType: Copy;

    /// Byte offset to the driver_data field in the raw type
    const DRIVER_DATA_OFFSET: usize;

    /// Returns the index value to store in driver_data field
    fn index(&self) -> usize;
}

/// Zero-terminated array of raw device IDs for C compatibility
///
/// The array is followed by a zero sentinel value to mark the end, matching
/// the convention used in C kernel code for device ID tables.
#[repr(C)]
pub struct RawIdArray<T: RawDeviceId, const N: usize> {
    /// Array of device IDs
    ids: [T::RawType; N],
    /// Zero terminator
    sentinel: MaybeUninit<T::RawType>,
}

impl<T: RawDeviceId, const N: usize> RawIdArray<T, N> {
    /// Returns the size in bytes of the entire array including terminator
    #[doc(hidden)]
    pub const fn size(&self) -> usize {
        core::mem::size_of::<Self>()
    }
}

/// Complete device ID table with both raw IDs and driver context data
///
/// This combines:
/// 1. A zero-terminated array of raw device IDs (for C compatibility)
/// 2. An array of Rust context data for each ID
#[repr(C)]
pub struct IdArray<T: RawDeviceId, U, const N: usize> {
    /// Raw device IDs with zero terminator
    raw_ids: RawIdArray<T, N>,
    /// Driver-specific data for each ID
    id_infos: [U; N],
}

impl<T: RawDeviceId, U, const N: usize> IdArray<T, U, N> {
    /// Constructs a new device ID table from pairs of device IDs and context data
    ///
    /// # Safety
    ///
    /// The implementation relies on the safety guarantees from RawDeviceId:
    /// 1. Safe transmute between T and RawType
    /// 2. Correct driver_data field offset
    pub const fn new(ids: [(T, U); N]) -> Self {
        let mut raw_ids = [const { MaybeUninit::<T::RawType>::uninit() }; N];
        let mut infos = [const { MaybeUninit::uninit() }; N];

        // Initialize each element in the arrays
        let mut i = 0usize;
        while i < N {
            // Convert Rust ID to raw C ID
            raw_ids[i] = unsafe { core::mem::transmute_copy(&ids[i].0) };
            
            // Store the index in driver_data field
            unsafe {
                raw_ids[i]
                    .as_mut_ptr()
                    .byte_offset(T::DRIVER_DATA_OFFSET as _)
                    .cast::<usize>()
                    .write(i);
            }

            // Move context data to new array
            infos[i] = MaybeUninit::new(unsafe { core::ptr::read(&ids[i].1) });
            i += 1;
        }

        // Prevent double-free of original data since we've moved it
        core::mem::forget(ids);

        Self {
            raw_ids: RawIdArray {
                // SAFETY: All elements initialized above
                ids: unsafe { core::mem::transmute_copy(&raw_ids) },
                sentinel: MaybeUninit::zeroed(),
            },
            // SAFETY: All elements initialized above
            id_infos: unsafe { core::mem::transmute_copy(&infos) },
        }
    }

    /// Returns reference to the raw ID array portion
    pub const fn raw_ids(&self) -> &RawIdArray<T, N> {
        &self.raw_ids
    }
}

/// Trait representing a device ID table for type-erased usage
///
/// This allows working with device tables without knowing the const generic N parameter.
/// Implemented automatically for all IdArray types.
pub trait IdTable<T: RawDeviceId, U> {
    /// Returns pointer to start of the raw ID table
    fn as_ptr(&self) -> *const T::RawType;

    /// Returns reference to a specific raw device ID
    fn id(&self, index: usize) -> &T::RawType;

    /// Returns reference to driver-specific data for an ID
    fn info(&self, index: usize) -> &U;
}

impl<T: RawDeviceId, U, const N: usize> IdTable<T, U> for IdArray<T, U, N> {
    fn as_ptr(&self) -> *const T::RawType {
        // Cast entire struct to get pointer with correct provenance for sentinel
        (self as *const Self).cast()
    }

    fn id(&self, index: usize) -> &T::RawType {
        &self.raw_ids.ids[index]
    }

    fn info(&self, index: usize) -> &U {
        &self.id_infos[index]
    }
}

/// Creates a modpost device table alias for kernel module building
///
/// This generates a symbol that modpost can find when processing the module,
/// similar to MODULE_DEVICE_TABLE in C code.
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
