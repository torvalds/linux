// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Samsung Electronics Co., Ltd.
// Author: Michal Wilczynski <m.wilczynski@samsung.com>

//! PWM subsystem abstractions.
//!
//! C header: [`include/linux/pwm.h`](srctree/include/linux/pwm.h).

use crate::{
    bindings,
    prelude::*,
    types::Opaque,
};
use core::convert::TryFrom;

/// PWM polarity. Mirrors [`enum pwm_polarity`](srctree/include/linux/pwm.h).
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Polarity {
    /// Normal polarity (duty cycle defines the high period of the signal).
    Normal,

    /// Inversed polarity (duty cycle defines the low period of the signal).
    Inversed,
}

impl TryFrom<bindings::pwm_polarity> for Polarity {
    type Error = Error;

    fn try_from(polarity: bindings::pwm_polarity) -> Result<Self, Error> {
        match polarity {
            bindings::pwm_polarity_PWM_POLARITY_NORMAL => Ok(Polarity::Normal),
            bindings::pwm_polarity_PWM_POLARITY_INVERSED => Ok(Polarity::Inversed),
            _ => Err(EINVAL),
        }
    }
}

impl From<Polarity> for bindings::pwm_polarity {
    fn from(polarity: Polarity) -> Self {
        match polarity {
            Polarity::Normal => bindings::pwm_polarity_PWM_POLARITY_NORMAL,
            Polarity::Inversed => bindings::pwm_polarity_PWM_POLARITY_INVERSED,
        }
    }
}

/// Represents a PWM waveform configuration.
/// Mirrors struct [`struct pwm_waveform`](srctree/include/linux/pwm.h).
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct Waveform {
    /// Total duration of one complete PWM cycle, in nanoseconds.
    pub period_length_ns: u64,

    /// Duty-cycle active time, in nanoseconds.
    ///
    /// For a typical normal polarity configuration (active-high) this is the
    /// high time of the signal.
    pub duty_length_ns: u64,

    /// Duty-cycle start offset, in nanoseconds.
    ///
    /// Delay from the beginning of the period to the first active edge.
    /// In most simple PWM setups this is `0`, so the duty cycle starts
    /// immediately at each period’s start.
    pub duty_offset_ns: u64,
}

impl From<bindings::pwm_waveform> for Waveform {
    fn from(wf: bindings::pwm_waveform) -> Self {
        Waveform {
            period_length_ns: wf.period_length_ns,
            duty_length_ns: wf.duty_length_ns,
            duty_offset_ns: wf.duty_offset_ns,
        }
    }
}

impl From<Waveform> for bindings::pwm_waveform {
    fn from(wf: Waveform) -> Self {
        bindings::pwm_waveform {
            period_length_ns: wf.period_length_ns,
            duty_length_ns: wf.duty_length_ns,
            duty_offset_ns: wf.duty_offset_ns,
        }
    }
}

/// Wrapper for PWM state [`struct pwm_state`](srctree/include/linux/pwm.h).
#[repr(transparent)]
pub struct State(bindings::pwm_state);

impl State {
    /// Creates a `State` wrapper by taking ownership of a C `pwm_state` value.
    pub(crate) fn from_c(c_state: bindings::pwm_state) -> Self {
        State(c_state)
    }

    /// Returns `true` if the PWM signal is enabled.
    pub fn enabled(&self) -> bool {
        self.0.enabled
    }
}
