// SPDX-License-Identifier: GPL-2.0

//! CPU frequency scaling.
//!
//! This module provides rust abstractions for interacting with the cpufreq subsystem.
//!
//! C header: [`include/linux/cpufreq.h`](srctree/include/linux/cpufreq.h)
//!
//! Reference: <https://docs.kernel.org/admin-guide/pm/cpufreq.html>

use crate::{
    clk::Hertz,
    error::{code::*, to_result, Result},
    ffi::c_ulong,
    prelude::*,
    types::Opaque,
};

use core::{ops::Deref, pin::Pin};

/// Default transition latency value in nanoseconds.
pub const ETERNAL_LATENCY_NS: u32 = bindings::CPUFREQ_ETERNAL as u32;

/// CPU frequency driver flags.
pub mod flags {
    /// Driver needs to update internal limits even if frequency remains unchanged.
    pub const NEED_UPDATE_LIMITS: u16 = 1 << 0;

    /// Platform where constants like `loops_per_jiffy` are unaffected by frequency changes.
    pub const CONST_LOOPS: u16 = 1 << 1;

    /// Register driver as a thermal cooling device automatically.
    pub const IS_COOLING_DEV: u16 = 1 << 2;

    /// Supports multiple clock domains with per-policy governors in `cpu/cpuN/cpufreq/`.
    pub const HAVE_GOVERNOR_PER_POLICY: u16 = 1 << 3;

    /// Allows post-change notifications outside of the `target()` routine.
    pub const ASYNC_NOTIFICATION: u16 = 1 << 4;

    /// Ensure CPU starts at a valid frequency from the driver's freq-table.
    pub const NEED_INITIAL_FREQ_CHECK: u16 = 1 << 5;

    /// Disallow governors with `dynamic_switching` capability.
    pub const NO_AUTO_DYNAMIC_SWITCHING: u16 = 1 << 6;
}

/// Relations from the C code.
const CPUFREQ_RELATION_L: u32 = 0;
const CPUFREQ_RELATION_H: u32 = 1;
const CPUFREQ_RELATION_C: u32 = 2;

/// Can be used with any of the above values.
const CPUFREQ_RELATION_E: u32 = 1 << 2;

/// CPU frequency selection relations.
///
/// CPU frequency selection relations, each optionally marked as "efficient".
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Relation {
    /// Select the lowest frequency at or above target.
    Low(bool),
    /// Select the highest frequency below or at target.
    High(bool),
    /// Select the closest frequency to the target.
    Close(bool),
}

impl Relation {
    // Construct from a C-compatible `u32` value.
    fn new(val: u32) -> Result<Self> {
        let efficient = val & CPUFREQ_RELATION_E != 0;

        Ok(match val & !CPUFREQ_RELATION_E {
            CPUFREQ_RELATION_L => Self::Low(efficient),
            CPUFREQ_RELATION_H => Self::High(efficient),
            CPUFREQ_RELATION_C => Self::Close(efficient),
            _ => return Err(EINVAL),
        })
    }
}

impl From<Relation> for u32 {
    // Convert to a C-compatible `u32` value.
    fn from(rel: Relation) -> Self {
        let (mut val, efficient) = match rel {
            Relation::Low(e) => (CPUFREQ_RELATION_L, e),
            Relation::High(e) => (CPUFREQ_RELATION_H, e),
            Relation::Close(e) => (CPUFREQ_RELATION_C, e),
        };

        if efficient {
            val |= CPUFREQ_RELATION_E;
        }

        val
    }
}

/// Policy data.
///
/// Rust abstraction for the C `struct cpufreq_policy_data`.
///
/// # Invariants
///
/// A [`PolicyData`] instance always corresponds to a valid C `struct cpufreq_policy_data`.
///
/// The callers must ensure that the `struct cpufreq_policy_data` is valid for access and remains
/// valid for the lifetime of the returned reference.
#[repr(transparent)]
pub struct PolicyData(Opaque<bindings::cpufreq_policy_data>);

impl PolicyData {
    /// Creates a mutable reference to an existing `struct cpufreq_policy_data` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for writing and remains valid for the lifetime
    /// of the returned reference.
    #[inline]
    pub unsafe fn from_raw_mut<'a>(ptr: *mut bindings::cpufreq_policy_data) -> &'a mut Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for writing and remains valid for the
        // lifetime of the returned reference.
        unsafe { &mut *ptr.cast() }
    }

    /// Returns a raw pointer to the underlying C `cpufreq_policy_data`.
    #[inline]
    pub fn as_raw(&self) -> *mut bindings::cpufreq_policy_data {
        let this: *const Self = self;
        this.cast_mut().cast()
    }

    /// Wrapper for `cpufreq_generic_frequency_table_verify`.
    #[inline]
    pub fn generic_verify(&self) -> Result {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid.
        to_result(unsafe { bindings::cpufreq_generic_frequency_table_verify(self.as_raw()) })
    }
}

/// The frequency table index.
///
/// Represents index with a frequency table.
///
/// # Invariants
///
/// The index must correspond to a valid entry in the [`Table`] it is used for.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct TableIndex(usize);

impl TableIndex {
    /// Creates an instance of [`TableIndex`].
    ///
    /// # Safety
    ///
    /// The caller must ensure that `index` correspond to a valid entry in the [`Table`] it is used
    /// for.
    pub unsafe fn new(index: usize) -> Self {
        // INVARIANT: The caller ensures that `index` correspond to a valid entry in the [`Table`].
        Self(index)
    }
}

impl From<TableIndex> for usize {
    #[inline]
    fn from(index: TableIndex) -> Self {
        index.0
    }
}

/// CPU frequency table.
///
/// Rust abstraction for the C `struct cpufreq_frequency_table`.
///
/// # Invariants
///
/// A [`Table`] instance always corresponds to a valid C `struct cpufreq_frequency_table`.
///
/// The callers must ensure that the `struct cpufreq_frequency_table` is valid for access and
/// remains valid for the lifetime of the returned reference.
///
/// ## Examples
///
/// The following example demonstrates how to read a frequency value from [`Table`].
///
/// ```
/// use kernel::cpufreq::{Policy, TableIndex};
///
/// fn show_freq(policy: &Policy) -> Result {
///     let table = policy.freq_table()?;
///
///     // SAFETY: Index is a valid entry in the table.
///     let index = unsafe { TableIndex::new(0) };
///
///     pr_info!("The frequency at index 0 is: {:?}\n", table.freq(index)?);
///     pr_info!("The flags at index 0 is: {}\n", table.flags(index));
///     pr_info!("The data at index 0 is: {}\n", table.data(index));
///     Ok(())
/// }
/// ```
#[repr(transparent)]
pub struct Table(Opaque<bindings::cpufreq_frequency_table>);

impl Table {
    /// Creates a reference to an existing C `struct cpufreq_frequency_table` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for reading and remains valid for the lifetime
    /// of the returned reference.
    #[inline]
    pub unsafe fn from_raw<'a>(ptr: *const bindings::cpufreq_frequency_table) -> &'a Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for reading and remains valid for the
        // lifetime of the returned reference.
        unsafe { &*ptr.cast() }
    }

    /// Returns the raw mutable pointer to the C `struct cpufreq_frequency_table`.
    #[inline]
    pub fn as_raw(&self) -> *mut bindings::cpufreq_frequency_table {
        let this: *const Self = self;
        this.cast_mut().cast()
    }

    /// Returns frequency at `index` in the [`Table`].
    #[inline]
    pub fn freq(&self, index: TableIndex) -> Result<Hertz> {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid and `index` is
        // guaranteed to be valid by its safety requirements.
        Ok(Hertz::from_khz(unsafe {
            (*self.as_raw().add(index.into())).frequency.try_into()?
        }))
    }

    /// Returns flags at `index` in the [`Table`].
    #[inline]
    pub fn flags(&self, index: TableIndex) -> u32 {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid and `index` is
        // guaranteed to be valid by its safety requirements.
        unsafe { (*self.as_raw().add(index.into())).flags }
    }

    /// Returns data at `index` in the [`Table`].
    #[inline]
    pub fn data(&self, index: TableIndex) -> u32 {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid and `index` is
        // guaranteed to be valid by its safety requirements.
        unsafe { (*self.as_raw().add(index.into())).driver_data }
    }
}

/// CPU frequency table owned and pinned in memory, created from a [`TableBuilder`].
pub struct TableBox {
    entries: Pin<KVec<bindings::cpufreq_frequency_table>>,
}

impl TableBox {
    /// Constructs a new [`TableBox`] from a [`KVec`] of entries.
    ///
    /// # Errors
    ///
    /// Returns `EINVAL` if the entries list is empty.
    #[inline]
    fn new(entries: KVec<bindings::cpufreq_frequency_table>) -> Result<Self> {
        if entries.is_empty() {
            return Err(EINVAL);
        }

        Ok(Self {
            // Pin the entries to memory, since we are passing its pointer to the C code.
            entries: Pin::new(entries),
        })
    }

    /// Returns a raw pointer to the underlying C `cpufreq_frequency_table`.
    #[inline]
    fn as_raw(&self) -> *const bindings::cpufreq_frequency_table {
        // The pointer is valid until the table gets dropped.
        self.entries.as_ptr()
    }
}

impl Deref for TableBox {
    type Target = Table;

    fn deref(&self) -> &Self::Target {
        // SAFETY: The caller owns TableBox, it is safe to deref.
        unsafe { Self::Target::from_raw(self.as_raw()) }
    }
}

/// CPU frequency table builder.
///
/// This is used by the CPU frequency drivers to build a frequency table dynamically.
///
/// ## Examples
///
/// The following example demonstrates how to create a CPU frequency table.
///
/// ```
/// use kernel::cpufreq::{TableBuilder, TableIndex};
/// use kernel::clk::Hertz;
///
/// let mut builder = TableBuilder::new();
///
/// // Adds few entries to the table.
/// builder.add(Hertz::from_mhz(700), 0, 1).unwrap();
/// builder.add(Hertz::from_mhz(800), 2, 3).unwrap();
/// builder.add(Hertz::from_mhz(900), 4, 5).unwrap();
/// builder.add(Hertz::from_ghz(1), 6, 7).unwrap();
///
/// let table = builder.to_table().unwrap();
///
/// // SAFETY: Index values correspond to valid entries in the table.
/// let (index0, index2) = unsafe { (TableIndex::new(0), TableIndex::new(2)) };
///
/// assert_eq!(table.freq(index0), Ok(Hertz::from_mhz(700)));
/// assert_eq!(table.flags(index0), 0);
/// assert_eq!(table.data(index0), 1);
///
/// assert_eq!(table.freq(index2), Ok(Hertz::from_mhz(900)));
/// assert_eq!(table.flags(index2), 4);
/// assert_eq!(table.data(index2), 5);
/// ```
#[derive(Default)]
#[repr(transparent)]
pub struct TableBuilder {
    entries: KVec<bindings::cpufreq_frequency_table>,
}

impl TableBuilder {
    /// Creates a new instance of [`TableBuilder`].
    #[inline]
    pub fn new() -> Self {
        Self {
            entries: KVec::new(),
        }
    }

    /// Adds a new entry to the table.
    pub fn add(&mut self, freq: Hertz, flags: u32, driver_data: u32) -> Result {
        // Adds the new entry at the end of the vector.
        Ok(self.entries.push(
            bindings::cpufreq_frequency_table {
                flags,
                driver_data,
                frequency: freq.as_khz() as u32,
            },
            GFP_KERNEL,
        )?)
    }

    /// Consumes the [`TableBuilder`] and returns [`TableBox`].
    pub fn to_table(mut self) -> Result<TableBox> {
        // Add last entry to the table.
        self.add(Hertz(c_ulong::MAX), 0, 0)?;

        TableBox::new(self.entries)
    }
}
