// SPDX-License-Identifier: GPL-2.0

//! Abstractions for [system
//! resources](https://docs.kernel.org/core-api/kernel-api.html#resources-management).
//!
//! C header: [`include/linux/ioport.h`](srctree/include/linux/ioport.h)

use core::ops::Deref;
use core::ptr::NonNull;

use crate::prelude::*;
use crate::str::{CStr, CString};
use crate::types::Opaque;

/// Resource Size type.
///
/// This is a type alias to either `u32` or `u64` depending on the config option
/// `CONFIG_PHYS_ADDR_T_64BIT`, and it can be a u64 even on 32-bit architectures.
pub type ResourceSize = bindings::phys_addr_t;

/// A region allocated from a parent [`Resource`].
///
/// # Invariants
///
/// - `self.0` points to a valid `bindings::resource` that was obtained through
///   `bindings::__request_region`.
pub struct Region {
    /// The resource returned when the region was requested.
    resource: NonNull<bindings::resource>,
    /// The name that was passed in when the region was requested. We need to
    /// store it for ownership reasons.
    _name: CString,
}

impl Deref for Region {
    type Target = Resource;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Safe as per the invariant of `Region`.
        unsafe { Resource::from_raw(self.resource.as_ptr()) }
    }
}

impl Drop for Region {
    fn drop(&mut self) {
        let (flags, start, size) = {
            let res = &**self;
            (res.flags(), res.start(), res.size())
        };

        let release_fn = if flags.contains(Flags::IORESOURCE_MEM) {
            bindings::release_mem_region
        } else {
            bindings::release_region
        };

        // SAFETY: Safe as per the invariant of `Region`.
        unsafe { release_fn(start, size) };
    }
}

// SAFETY: `Region` only holds a pointer to a C `struct resource`, which is safe to be used from
// any thread.
unsafe impl Send for Region {}

// SAFETY: `Region` only holds a pointer to a C `struct resource`, references to which are
// safe to be used from any thread.
unsafe impl Sync for Region {}

/// A resource abstraction.
///
/// # Invariants
///
/// [`Resource`] is a transparent wrapper around a valid `bindings::resource`.
#[repr(transparent)]
pub struct Resource(Opaque<bindings::resource>);

impl Resource {
    /// Creates a reference to a [`Resource`] from a valid pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that for the duration of 'a, the pointer will
    /// point at a valid `bindings::resource`.
    ///
    /// The caller must also ensure that the [`Resource`] is only accessed via the
    /// returned reference for the duration of 'a.
    pub(crate) const unsafe fn from_raw<'a>(ptr: *mut bindings::resource) -> &'a Self {
        // SAFETY: Self is a transparent wrapper around `Opaque<bindings::resource>`.
        unsafe { &*ptr.cast() }
    }

    /// Requests a resource region.
    ///
    /// Exclusive access will be given and the region will be marked as busy.
    /// Further calls to [`Self::request_region`] will return [`None`] if
    /// the region, or a part of it, is already in use.
    pub fn request_region(
        &self,
        start: ResourceSize,
        size: ResourceSize,
        name: CString,
        flags: Flags,
    ) -> Option<Region> {
        // SAFETY:
        // - Safe as per the invariant of `Resource`.
        // - `__request_region` will store a reference to the name, but that is
        // safe as we own it and it will not be dropped until the `Region` is
        // dropped.
        let region = unsafe {
            bindings::__request_region(
                self.0.get(),
                start,
                size,
                name.as_char_ptr(),
                flags.0 as c_int,
            )
        };

        Some(Region {
            resource: NonNull::new(region)?,
            _name: name,
        })
    }

    /// Returns the size of the resource.
    pub fn size(&self) -> ResourceSize {
        let inner = self.0.get();
        // SAFETY: Safe as per the invariants of `Resource`.
        unsafe { bindings::resource_size(inner) }
    }

    /// Returns the start address of the resource.
    pub fn start(&self) -> ResourceSize {
        let inner = self.0.get();
        // SAFETY: Safe as per the invariants of `Resource`.
        unsafe { (*inner).start }
    }

    /// Returns the name of the resource.
    pub fn name(&self) -> Option<&CStr> {
        let inner = self.0.get();

        // SAFETY: Safe as per the invariants of `Resource`.
        let name = unsafe { (*inner).name };

        if name.is_null() {
            return None;
        }

        // SAFETY: In the C code, `resource::name` either contains a null
        // pointer or points to a valid NUL-terminated C string, and at this
        // point we know it is not null, so we can safely convert it to a
        // `CStr`.
        Some(unsafe { CStr::from_char_ptr(name) })
    }

    /// Returns the flags associated with the resource.
    pub fn flags(&self) -> Flags {
        let inner = self.0.get();
        // SAFETY: Safe as per the invariants of `Resource`.
        let flags = unsafe { (*inner).flags };

        Flags(flags)
    }
}

// SAFETY: `Resource` only holds a pointer to a C `struct resource`, which is
// safe to be used from any thread.
unsafe impl Send for Resource {}

// SAFETY: `Resource` only holds a pointer to a C `struct resource`, references
// to which are safe to be used from any thread.
unsafe impl Sync for Resource {}

/// Resource flags as stored in the C `struct resource::flags` field.
///
/// They can be combined with the operators `|`, `&`, and `!`.
///
/// Values can be used from the associated constants such as
/// [`Flags::IORESOURCE_IO`].
#[derive(Clone, Copy, PartialEq)]
pub struct Flags(c_ulong);

impl Flags {
    /// Check whether `flags` is contained in `self`.
    pub fn contains(self, flags: Flags) -> bool {
        (self & flags) == flags
    }
}

impl core::ops::BitOr for Flags {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl core::ops::BitAnd for Flags {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
}

impl core::ops::Not for Flags {
    type Output = Self;
    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}

impl Flags {
    /// PCI/ISA I/O ports.
    pub const IORESOURCE_IO: Flags = Flags::new(bindings::IORESOURCE_IO);

    /// Resource is software muxed.
    pub const IORESOURCE_MUXED: Flags = Flags::new(bindings::IORESOURCE_MUXED);

    /// Resource represents a memory region.
    pub const IORESOURCE_MEM: Flags = Flags::new(bindings::IORESOURCE_MEM);

    /// Resource represents a memory region that must be ioremaped using `ioremap_np`.
    pub const IORESOURCE_MEM_NONPOSTED: Flags = Flags::new(bindings::IORESOURCE_MEM_NONPOSTED);

    const fn new(value: u32) -> Self {
        crate::build_assert!(value as u64 <= c_ulong::MAX as u64);
        Flags(value as c_ulong)
    }
}
