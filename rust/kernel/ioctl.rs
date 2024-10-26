// SPDX-License-Identifier: GPL-2.0

//! `ioctl()` number definitions.
//!
//! C header: [`include/asm-generic/ioctl.h`](srctree/include/asm-generic/ioctl.h)
#![allow(non_snake_case)]
use crate::build_assert;
/// Type alias for IOCTL numbers.
type IoctlNumber = u32;

/// Type alias for sizes used in IOCTL operations.
type IoctlSize = usize;

/// Build an ioctl number, analogous to the C macro of the same name.
#[inline(always)]
const fn _IOC(dir: u32, ty: u32, nr: u32, size: IoctlSize) -> IoctlNumber {
    build_assert!(dir <= uapi::_IOC_DIRMASK, "Invalid direction value");
    build_assert!(ty <= uapi::_IOC_TYPEMASK, "Invalid type value");
    build_assert!(nr <= uapi::_IOC_NRMASK, "Invalid number value");
    build_assert!(size <= (uapi::_IOC_SIZEMASK as IoctlSize), "Invalid size value");

    (dir << uapi::_IOC_DIRSHIFT)
        | (ty << uapi::_IOC_TYPESHIFT)
        | (nr << uapi::_IOC_NRSHIFT)
        | ((size as IoctlNumber) << uapi::_IOC_SIZESHIFT)
}

/// Build an ioctl number for an argumentless ioctl.
#[inline(always)]
pub const fn _IO(ty: u32, nr: u32) -> IoctlNumber {
    _IOC(uapi::_IOC_NONE, ty, nr, 0)
}

/// Build an ioctl number for a read-only ioctl.
#[inline(always)]
pub const fn _IOR<T>(ty: u32, nr: u32) -> IoctlNumber {
    _IOC(uapi::_IOC_READ, ty, nr, core::mem::size_of::<T>())
}

/// Build an ioctl number for a write-only ioctl.
#[inline(always)]
pub const fn _IOW<T>(ty: u32, nr: u32) -> IoctlNumber {
    _IOC(uapi::_IOC_WRITE, ty, nr, core::mem::size_of::<T>())
}

/// Build an ioctl number for a read-write ioctl.
#[inline(always)]
pub const fn _IOWR<T>(ty: u32, nr: u32) -> IoctlNumber {
    _IOC(
        uapi::_IOC_READ | uapi::_IOC_WRITE,
        ty,
        nr,
        core::mem::size_of::<T>(),
    )
}

/// Get the ioctl direction from an ioctl number.
pub const fn _IOC_DIR(nr: IoctlNumber) -> u32 {
    (nr >> uapi::_IOC_DIRSHIFT) & uapi::_IOC_DIRMASK
}

/// Get the ioctl type from an ioctl number.
pub const fn _IOC_TYPE(nr: IoctlNumber) -> u32 {
    (nr >> uapi::_IOC_TYPESHIFT) & uapi::_IOC_TYPEMASK
}

/// Get the ioctl number from an ioctl number.
pub const fn _IOC_NR(nr: IoctlNumber) -> u32 {
    (nr >> uapi::_IOC_NRSHIFT) & uapi::_IOC_NRMASK
}

/// Get the ioctl size from an ioctl number.
pub const fn _IOC_SIZE(nr: IoctlNumber) -> IoctlSize {
    ((nr >> uapi::_IOC_SIZESHIFT) & uapi::_IOC_SIZEMASK) as IoctlSize
}

// Example usage
const MY_DEVICE_TYPE: u32 = 0x1234;
const MY_IOCTL_NUMBER: u32 = 0x01;

// Creating IOCTL numbers
const IOCTL_MY_DEVICE_READ: IoctlNumber = _IOR<MyDataType>(MY_DEVICE_TYPE, MY_IOCTL_NUMBER);
const IOCTL_MY_DEVICE_WRITE: IoctlNumber = _IOW<MyDataType>(MY_DEVICE_TYPE, MY_IOCTL_NUMBER);
