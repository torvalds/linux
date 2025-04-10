// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM subsystem abstractions.

pub mod driver;
pub mod ioctl;

pub use self::driver::Driver;
pub use self::driver::DriverInfo;

pub(crate) mod private {
    pub trait Sealed {}
}
