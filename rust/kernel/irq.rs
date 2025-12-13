// SPDX-License-Identifier: GPL-2.0

//! IRQ abstractions.
//!
//! An IRQ is an interrupt request from a device. It is used to get the CPU's
//! attention so it can service a hardware event in a timely manner.
//!
//! The current abstractions handle IRQ requests and handlers, i.e.: it allows
//! drivers to register a handler for a given IRQ line.
//!
//! C header: [`include/linux/device.h`](srctree/include/linux/interrupt.h)

/// Flags to be used when registering IRQ handlers.
mod flags;

/// IRQ allocation and handling.
mod request;

pub use flags::Flags;

pub use request::{
    Handler, IrqRequest, IrqReturn, Registration, ThreadedHandler, ThreadedIrqReturn,
    ThreadedRegistration,
};
