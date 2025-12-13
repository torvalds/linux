// SPDX-License-Identifier: GPL-2.0

//! Advanced Configuration and Power Interface abstractions.

use crate::{
    bindings,
    device_id::{RawDeviceId, RawDeviceIdIndex},
    prelude::*,
};

/// IdTable type for ACPI drivers.
pub type IdTable<T> = &'static dyn kernel::device_id::IdTable<DeviceId, T>;

/// An ACPI device id.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct DeviceId(bindings::acpi_device_id);

// SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `acpi_device_id` and does not add
// additional invariants, so it's safe to transmute to `RawType`.
unsafe impl RawDeviceId for DeviceId {
    type RawType = bindings::acpi_device_id;
}

// SAFETY: `DRIVER_DATA_OFFSET` is the offset to the `driver_data` field.
unsafe impl RawDeviceIdIndex for DeviceId {
    const DRIVER_DATA_OFFSET: usize = core::mem::offset_of!(bindings::acpi_device_id, driver_data);

    fn index(&self) -> usize {
        self.0.driver_data
    }
}

impl DeviceId {
    const ACPI_ID_LEN: usize = 16;

    /// Create a new device id from an ACPI 'id' string.
    #[inline(always)]
    pub const fn new(id: &'static CStr) -> Self {
        let src = id.to_bytes_with_nul();
        build_assert!(src.len() <= Self::ACPI_ID_LEN, "ID exceeds 16 bytes");
        // Replace with `bindings::acpi_device_id::default()` once stabilized for `const`.
        // SAFETY: FFI type is valid to be zero-initialized.
        let mut acpi: bindings::acpi_device_id = unsafe { core::mem::zeroed() };
        let mut i = 0;
        while i < src.len() {
            acpi.id[i] = src[i];
            i += 1;
        }

        Self(acpi)
    }
}

/// Create an ACPI `IdTable` with an "alias" for modpost.
#[macro_export]
macro_rules! acpi_device_table {
    ($table_name:ident, $module_table_name:ident, $id_info_type: ty, $table_data: expr) => {
        const $table_name: $crate::device_id::IdArray<
            $crate::acpi::DeviceId,
            $id_info_type,
            { $table_data.len() },
        > = $crate::device_id::IdArray::new($table_data);

        $crate::module_device_table!("acpi", $module_table_name, $table_name);
    };
}
