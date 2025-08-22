// SPDX-License-Identifier: GPL-2.0

//! Kernel file systems.
//!
//! C headers: [`include/linux/fs.h`](srctree/include/linux/fs.h)

pub mod file;
pub use self::file::{File, LocalFile};

mod kiocb;
pub use self::kiocb::Kiocb;
