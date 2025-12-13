// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright 2025 Collabora ltd.

use crate::bindings;
use crate::prelude::*;

/// Flags to be used when registering IRQ handlers.
///
/// Flags can be used to request specific behaviors when registering an IRQ
/// handler, and can be combined using the `|`, `&`, and `!` operators to
/// further control the system's behavior.
///
/// A common use case is to register a shared interrupt, as sharing the line
/// between devices is increasingly common in modern systems and is even
/// required for some buses. This requires setting [`Flags::SHARED`] when
/// requesting the interrupt. Other use cases include setting the trigger type
/// through `Flags::TRIGGER_*`, which determines when the interrupt fires, or
/// controlling whether the interrupt is masked after the handler runs by using
/// [`Flags::ONESHOT`].
///
/// If an invalid combination of flags is provided, the system will refuse to
/// register the handler, and lower layers will enforce certain flags when
/// necessary. This means, for example, that all the
/// [`crate::irq::Registration`] for a shared interrupt have to agree on
/// [`Flags::SHARED`] and on the same trigger type, if set.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Flags(c_ulong);

impl Flags {
    /// Use the interrupt line as already configured.
    pub const TRIGGER_NONE: Flags = Flags::new(bindings::IRQF_TRIGGER_NONE);

    /// The interrupt is triggered when the signal goes from low to high.
    pub const TRIGGER_RISING: Flags = Flags::new(bindings::IRQF_TRIGGER_RISING);

    /// The interrupt is triggered when the signal goes from high to low.
    pub const TRIGGER_FALLING: Flags = Flags::new(bindings::IRQF_TRIGGER_FALLING);

    /// The interrupt is triggered while the signal is held high.
    pub const TRIGGER_HIGH: Flags = Flags::new(bindings::IRQF_TRIGGER_HIGH);

    /// The interrupt is triggered while the signal is held low.
    pub const TRIGGER_LOW: Flags = Flags::new(bindings::IRQF_TRIGGER_LOW);

    /// Allow sharing the IRQ among several devices.
    pub const SHARED: Flags = Flags::new(bindings::IRQF_SHARED);

    /// Set by callers when they expect sharing mismatches to occur.
    pub const PROBE_SHARED: Flags = Flags::new(bindings::IRQF_PROBE_SHARED);

    /// Flag to mark this interrupt as timer interrupt.
    pub const TIMER: Flags = Flags::new(bindings::IRQF_TIMER);

    /// Interrupt is per CPU.
    pub const PERCPU: Flags = Flags::new(bindings::IRQF_PERCPU);

    /// Flag to exclude this interrupt from irq balancing.
    pub const NOBALANCING: Flags = Flags::new(bindings::IRQF_NOBALANCING);

    /// Interrupt is used for polling (only the interrupt that is registered
    /// first in a shared interrupt is considered for performance reasons).
    pub const IRQPOLL: Flags = Flags::new(bindings::IRQF_IRQPOLL);

    /// Interrupt is not re-enabled after the hardirq handler finished. Used by
    /// threaded interrupts which need to keep the irq line disabled until the
    /// threaded handler has been run.
    pub const ONESHOT: Flags = Flags::new(bindings::IRQF_ONESHOT);

    /// Do not disable this IRQ during suspend. Does not guarantee that this
    /// interrupt will wake the system from a suspended state.
    pub const NO_SUSPEND: Flags = Flags::new(bindings::IRQF_NO_SUSPEND);

    /// Force enable it on resume even if [`Flags::NO_SUSPEND`] is set.
    pub const FORCE_RESUME: Flags = Flags::new(bindings::IRQF_FORCE_RESUME);

    /// Interrupt cannot be threaded.
    pub const NO_THREAD: Flags = Flags::new(bindings::IRQF_NO_THREAD);

    /// Resume IRQ early during syscore instead of at device resume time.
    pub const EARLY_RESUME: Flags = Flags::new(bindings::IRQF_EARLY_RESUME);

    /// If the IRQ is shared with a [`Flags::NO_SUSPEND`] user, execute this
    /// interrupt handler after suspending interrupts. For system wakeup devices
    /// users need to implement wakeup detection in their interrupt handlers.
    pub const COND_SUSPEND: Flags = Flags::new(bindings::IRQF_COND_SUSPEND);

    /// Don't enable IRQ or NMI automatically when users request it. Users will
    /// enable it explicitly by `enable_irq` or `enable_nmi` later.
    pub const NO_AUTOEN: Flags = Flags::new(bindings::IRQF_NO_AUTOEN);

    /// Exclude from runnaway detection for IPI and similar handlers, depends on
    /// `PERCPU`.
    pub const NO_DEBUG: Flags = Flags::new(bindings::IRQF_NO_DEBUG);

    pub(crate) fn into_inner(self) -> c_ulong {
        self.0
    }

    const fn new(value: u32) -> Self {
        build_assert!(value as u64 <= c_ulong::MAX as u64);
        Self(value as c_ulong)
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
