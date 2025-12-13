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
    cpu::CpuId,
    cpumask,
    device::{Bound, Device},
    devres,
    error::{code::*, from_err_ptr, from_result, to_result, Result, VTABLE_DEFAULT_ERROR},
    ffi::{c_char, c_ulong},
    prelude::*,
    types::ForeignOwnable,
    types::Opaque,
};

#[cfg(CONFIG_COMMON_CLK)]
use crate::clk::Clk;

use core::{
    cell::UnsafeCell,
    marker::PhantomData,
    ops::{Deref, DerefMut},
    pin::Pin,
    ptr,
};

use macros::vtable;

/// Maximum length of CPU frequency driver's name.
const CPUFREQ_NAME_LEN: usize = bindings::CPUFREQ_NAME_LEN as usize;

/// Default transition latency value in nanoseconds.
pub const DEFAULT_TRANSITION_LATENCY_NS: u32 = bindings::CPUFREQ_DEFAULT_TRANSITION_LATENCY_NS;

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
/// # Examples
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
/// # Examples
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

/// CPU frequency policy.
///
/// Rust abstraction for the C `struct cpufreq_policy`.
///
/// # Invariants
///
/// A [`Policy`] instance always corresponds to a valid C `struct cpufreq_policy`.
///
/// The callers must ensure that the `struct cpufreq_policy` is valid for access and remains valid
/// for the lifetime of the returned reference.
///
/// # Examples
///
/// The following example demonstrates how to create a CPU frequency table.
///
/// ```
/// use kernel::cpufreq::{DEFAULT_TRANSITION_LATENCY_NS, Policy};
///
/// fn update_policy(policy: &mut Policy) {
///     policy
///         .set_dvfs_possible_from_any_cpu(true)
///         .set_fast_switch_possible(true)
///         .set_transition_latency_ns(DEFAULT_TRANSITION_LATENCY_NS);
///
///     pr_info!("The policy details are: {:?}\n", (policy.cpu(), policy.cur()));
/// }
/// ```
#[repr(transparent)]
pub struct Policy(Opaque<bindings::cpufreq_policy>);

impl Policy {
    /// Creates a reference to an existing `struct cpufreq_policy` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for reading and remains valid for the lifetime
    /// of the returned reference.
    #[inline]
    pub unsafe fn from_raw<'a>(ptr: *const bindings::cpufreq_policy) -> &'a Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for reading and remains valid for the
        // lifetime of the returned reference.
        unsafe { &*ptr.cast() }
    }

    /// Creates a mutable reference to an existing `struct cpufreq_policy` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for writing and remains valid for the lifetime
    /// of the returned reference.
    #[inline]
    pub unsafe fn from_raw_mut<'a>(ptr: *mut bindings::cpufreq_policy) -> &'a mut Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for writing and remains valid for the
        // lifetime of the returned reference.
        unsafe { &mut *ptr.cast() }
    }

    /// Returns a raw mutable pointer to the C `struct cpufreq_policy`.
    #[inline]
    fn as_raw(&self) -> *mut bindings::cpufreq_policy {
        let this: *const Self = self;
        this.cast_mut().cast()
    }

    #[inline]
    fn as_ref(&self) -> &bindings::cpufreq_policy {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid.
        unsafe { &*self.as_raw() }
    }

    #[inline]
    fn as_mut_ref(&mut self) -> &mut bindings::cpufreq_policy {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid.
        unsafe { &mut *self.as_raw() }
    }

    /// Returns the primary CPU for the [`Policy`].
    #[inline]
    pub fn cpu(&self) -> CpuId {
        // SAFETY: The C API guarantees that `cpu` refers to a valid CPU number.
        unsafe { CpuId::from_u32_unchecked(self.as_ref().cpu) }
    }

    /// Returns the minimum frequency for the [`Policy`].
    #[inline]
    pub fn min(&self) -> Hertz {
        Hertz::from_khz(self.as_ref().min as usize)
    }

    /// Set the minimum frequency for the [`Policy`].
    #[inline]
    pub fn set_min(&mut self, min: Hertz) -> &mut Self {
        self.as_mut_ref().min = min.as_khz() as u32;
        self
    }

    /// Returns the maximum frequency for the [`Policy`].
    #[inline]
    pub fn max(&self) -> Hertz {
        Hertz::from_khz(self.as_ref().max as usize)
    }

    /// Set the maximum frequency for the [`Policy`].
    #[inline]
    pub fn set_max(&mut self, max: Hertz) -> &mut Self {
        self.as_mut_ref().max = max.as_khz() as u32;
        self
    }

    /// Returns the current frequency for the [`Policy`].
    #[inline]
    pub fn cur(&self) -> Hertz {
        Hertz::from_khz(self.as_ref().cur as usize)
    }

    /// Returns the suspend frequency for the [`Policy`].
    #[inline]
    pub fn suspend_freq(&self) -> Hertz {
        Hertz::from_khz(self.as_ref().suspend_freq as usize)
    }

    /// Sets the suspend frequency for the [`Policy`].
    #[inline]
    pub fn set_suspend_freq(&mut self, freq: Hertz) -> &mut Self {
        self.as_mut_ref().suspend_freq = freq.as_khz() as u32;
        self
    }

    /// Provides a wrapper to the generic suspend routine.
    #[inline]
    pub fn generic_suspend(&mut self) -> Result {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid.
        to_result(unsafe { bindings::cpufreq_generic_suspend(self.as_mut_ref()) })
    }

    /// Provides a wrapper to the generic get routine.
    #[inline]
    pub fn generic_get(&self) -> Result<u32> {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid.
        Ok(unsafe { bindings::cpufreq_generic_get(u32::from(self.cpu())) })
    }

    /// Provides a wrapper to the register with energy model using the OPP core.
    #[cfg(CONFIG_PM_OPP)]
    #[inline]
    pub fn register_em_opp(&mut self) {
        // SAFETY: By the type invariant, the pointer stored in `self` is valid.
        unsafe { bindings::cpufreq_register_em_with_opp(self.as_mut_ref()) };
    }

    /// Gets [`cpumask::Cpumask`] for a cpufreq [`Policy`].
    #[inline]
    pub fn cpus(&mut self) -> &mut cpumask::Cpumask {
        // SAFETY: The pointer to `cpus` is valid for writing and remains valid for the lifetime of
        // the returned reference.
        unsafe { cpumask::CpumaskVar::from_raw_mut(&mut self.as_mut_ref().cpus) }
    }

    /// Sets clock for the [`Policy`].
    ///
    /// # Safety
    ///
    /// The caller must guarantee that the returned [`Clk`] is not dropped while it is getting used
    /// by the C code.
    #[cfg(CONFIG_COMMON_CLK)]
    pub unsafe fn set_clk(&mut self, dev: &Device, name: Option<&CStr>) -> Result<Clk> {
        let clk = Clk::get(dev, name)?;
        self.as_mut_ref().clk = clk.as_raw();
        Ok(clk)
    }

    /// Allows / disallows frequency switching code to run on any CPU.
    #[inline]
    pub fn set_dvfs_possible_from_any_cpu(&mut self, val: bool) -> &mut Self {
        self.as_mut_ref().dvfs_possible_from_any_cpu = val;
        self
    }

    /// Returns if fast switching of frequencies is possible or not.
    #[inline]
    pub fn fast_switch_possible(&self) -> bool {
        self.as_ref().fast_switch_possible
    }

    /// Enables / disables fast frequency switching.
    #[inline]
    pub fn set_fast_switch_possible(&mut self, val: bool) -> &mut Self {
        self.as_mut_ref().fast_switch_possible = val;
        self
    }

    /// Sets transition latency (in nanoseconds) for the [`Policy`].
    #[inline]
    pub fn set_transition_latency_ns(&mut self, latency_ns: u32) -> &mut Self {
        self.as_mut_ref().cpuinfo.transition_latency = latency_ns;
        self
    }

    /// Sets cpuinfo `min_freq`.
    #[inline]
    pub fn set_cpuinfo_min_freq(&mut self, min_freq: Hertz) -> &mut Self {
        self.as_mut_ref().cpuinfo.min_freq = min_freq.as_khz() as u32;
        self
    }

    /// Sets cpuinfo `max_freq`.
    #[inline]
    pub fn set_cpuinfo_max_freq(&mut self, max_freq: Hertz) -> &mut Self {
        self.as_mut_ref().cpuinfo.max_freq = max_freq.as_khz() as u32;
        self
    }

    /// Set `transition_delay_us`, i.e. the minimum time between successive frequency change
    /// requests.
    #[inline]
    pub fn set_transition_delay_us(&mut self, transition_delay_us: u32) -> &mut Self {
        self.as_mut_ref().transition_delay_us = transition_delay_us;
        self
    }

    /// Returns reference to the CPU frequency [`Table`] for the [`Policy`].
    pub fn freq_table(&self) -> Result<&Table> {
        if self.as_ref().freq_table.is_null() {
            return Err(EINVAL);
        }

        // SAFETY: The `freq_table` is guaranteed to be valid for reading and remains valid for the
        // lifetime of the returned reference.
        Ok(unsafe { Table::from_raw(self.as_ref().freq_table) })
    }

    /// Sets the CPU frequency [`Table`] for the [`Policy`].
    ///
    /// # Safety
    ///
    /// The caller must guarantee that the [`Table`] is not dropped while it is getting used by the
    /// C code.
    #[inline]
    pub unsafe fn set_freq_table(&mut self, table: &Table) -> &mut Self {
        self.as_mut_ref().freq_table = table.as_raw();
        self
    }

    /// Returns the [`Policy`]'s private data.
    pub fn data<T: ForeignOwnable>(&mut self) -> Option<<T>::Borrowed<'_>> {
        if self.as_ref().driver_data.is_null() {
            None
        } else {
            // SAFETY: The data is earlier set from [`set_data`].
            Some(unsafe { T::borrow(self.as_ref().driver_data.cast()) })
        }
    }

    /// Sets the private data of the [`Policy`] using a foreign-ownable wrapper.
    ///
    /// # Errors
    ///
    /// Returns `EBUSY` if private data is already set.
    fn set_data<T: ForeignOwnable>(&mut self, data: T) -> Result {
        if self.as_ref().driver_data.is_null() {
            // Transfer the ownership of the data to the foreign interface.
            self.as_mut_ref().driver_data = <T as ForeignOwnable>::into_foreign(data).cast();
            Ok(())
        } else {
            Err(EBUSY)
        }
    }

    /// Clears and returns ownership of the private data.
    fn clear_data<T: ForeignOwnable>(&mut self) -> Option<T> {
        if self.as_ref().driver_data.is_null() {
            None
        } else {
            let data = Some(
                // SAFETY: The data is earlier set by us from [`set_data`]. It is safe to take
                // back the ownership of the data from the foreign interface.
                unsafe { <T as ForeignOwnable>::from_foreign(self.as_ref().driver_data.cast()) },
            );
            self.as_mut_ref().driver_data = ptr::null_mut();
            data
        }
    }
}

/// CPU frequency policy created from a CPU number.
///
/// This struct represents the CPU frequency policy obtained for a specific CPU, providing safe
/// access to the underlying `cpufreq_policy` and ensuring proper cleanup when the `PolicyCpu` is
/// dropped.
struct PolicyCpu<'a>(&'a mut Policy);

impl<'a> PolicyCpu<'a> {
    fn from_cpu(cpu: CpuId) -> Result<Self> {
        // SAFETY: It is safe to call `cpufreq_cpu_get` for any valid CPU.
        let ptr = from_err_ptr(unsafe { bindings::cpufreq_cpu_get(u32::from(cpu)) })?;

        Ok(Self(
            // SAFETY: The `ptr` is guaranteed to be valid and remains valid for the lifetime of
            // the returned reference.
            unsafe { Policy::from_raw_mut(ptr) },
        ))
    }
}

impl<'a> Deref for PolicyCpu<'a> {
    type Target = Policy;

    fn deref(&self) -> &Self::Target {
        self.0
    }
}

impl<'a> DerefMut for PolicyCpu<'a> {
    fn deref_mut(&mut self) -> &mut Policy {
        self.0
    }
}

impl<'a> Drop for PolicyCpu<'a> {
    fn drop(&mut self) {
        // SAFETY: The underlying pointer is guaranteed to be valid for the lifetime of `self`.
        unsafe { bindings::cpufreq_cpu_put(self.0.as_raw()) };
    }
}

/// CPU frequency driver.
///
/// Implement this trait to provide a CPU frequency driver and its callbacks.
///
/// Reference: <https://docs.kernel.org/cpu-freq/cpu-drivers.html>
#[vtable]
pub trait Driver {
    /// Driver's name.
    const NAME: &'static CStr;

    /// Driver's flags.
    const FLAGS: u16;

    /// Boost support.
    const BOOST_ENABLED: bool;

    /// Policy specific data.
    ///
    /// Require that `PData` implements `ForeignOwnable`. We guarantee to never move the underlying
    /// wrapped data structure.
    type PData: ForeignOwnable;

    /// Driver's `init` callback.
    fn init(policy: &mut Policy) -> Result<Self::PData>;

    /// Driver's `exit` callback.
    fn exit(_policy: &mut Policy, _data: Option<Self::PData>) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `online` callback.
    fn online(_policy: &mut Policy) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `offline` callback.
    fn offline(_policy: &mut Policy) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `suspend` callback.
    fn suspend(_policy: &mut Policy) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `resume` callback.
    fn resume(_policy: &mut Policy) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `ready` callback.
    fn ready(_policy: &mut Policy) {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `verify` callback.
    fn verify(data: &mut PolicyData) -> Result;

    /// Driver's `setpolicy` callback.
    fn setpolicy(_policy: &mut Policy) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `target` callback.
    fn target(_policy: &mut Policy, _target_freq: u32, _relation: Relation) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `target_index` callback.
    fn target_index(_policy: &mut Policy, _index: TableIndex) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `fast_switch` callback.
    fn fast_switch(_policy: &mut Policy, _target_freq: u32) -> u32 {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `adjust_perf` callback.
    fn adjust_perf(_policy: &mut Policy, _min_perf: usize, _target_perf: usize, _capacity: usize) {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `get_intermediate` callback.
    fn get_intermediate(_policy: &mut Policy, _index: TableIndex) -> u32 {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `target_intermediate` callback.
    fn target_intermediate(_policy: &mut Policy, _index: TableIndex) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `get` callback.
    fn get(_policy: &mut Policy) -> Result<u32> {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `update_limits` callback.
    fn update_limits(_policy: &mut Policy) {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `bios_limit` callback.
    fn bios_limit(_policy: &mut Policy, _limit: &mut u32) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `set_boost` callback.
    fn set_boost(_policy: &mut Policy, _state: i32) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Driver's `register_em` callback.
    fn register_em(_policy: &mut Policy) {
        build_error!(VTABLE_DEFAULT_ERROR)
    }
}

/// CPU frequency driver Registration.
///
/// # Examples
///
/// The following example demonstrates how to register a cpufreq driver.
///
/// ```
/// use kernel::{
///     cpufreq,
///     c_str,
///     device::{Core, Device},
///     macros::vtable,
///     of, platform,
///     sync::Arc,
/// };
/// struct SampleDevice;
///
/// #[derive(Default)]
/// struct SampleDriver;
///
/// #[vtable]
/// impl cpufreq::Driver for SampleDriver {
///     const NAME: &'static CStr = c_str!("cpufreq-sample");
///     const FLAGS: u16 = cpufreq::flags::NEED_INITIAL_FREQ_CHECK | cpufreq::flags::IS_COOLING_DEV;
///     const BOOST_ENABLED: bool = true;
///
///     type PData = Arc<SampleDevice>;
///
///     fn init(policy: &mut cpufreq::Policy) -> Result<Self::PData> {
///         // Initialize here
///         Ok(Arc::new(SampleDevice, GFP_KERNEL)?)
///     }
///
///     fn exit(_policy: &mut cpufreq::Policy, _data: Option<Self::PData>) -> Result {
///         Ok(())
///     }
///
///     fn suspend(policy: &mut cpufreq::Policy) -> Result {
///         policy.generic_suspend()
///     }
///
///     fn verify(data: &mut cpufreq::PolicyData) -> Result {
///         data.generic_verify()
///     }
///
///     fn target_index(policy: &mut cpufreq::Policy, index: cpufreq::TableIndex) -> Result {
///         // Update CPU frequency
///         Ok(())
///     }
///
///     fn get(policy: &mut cpufreq::Policy) -> Result<u32> {
///         policy.generic_get()
///     }
/// }
///
/// impl platform::Driver for SampleDriver {
///     type IdInfo = ();
///     const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = None;
///
///     fn probe(
///         pdev: &platform::Device<Core>,
///         _id_info: Option<&Self::IdInfo>,
///     ) -> Result<Pin<KBox<Self>>> {
///         cpufreq::Registration::<SampleDriver>::new_foreign_owned(pdev.as_ref())?;
///         Ok(KBox::new(Self {}, GFP_KERNEL)?.into())
///     }
/// }
/// ```
#[repr(transparent)]
pub struct Registration<T: Driver>(KBox<UnsafeCell<bindings::cpufreq_driver>>, PhantomData<T>);

/// SAFETY: `Registration` doesn't offer any methods or access to fields when shared between threads
/// or CPUs, so it is safe to share it.
unsafe impl<T: Driver> Sync for Registration<T> {}

#[allow(clippy::non_send_fields_in_send_ty)]
/// SAFETY: Registration with and unregistration from the cpufreq subsystem can happen from any
/// thread.
unsafe impl<T: Driver> Send for Registration<T> {}

impl<T: Driver> Registration<T> {
    const VTABLE: bindings::cpufreq_driver = bindings::cpufreq_driver {
        name: Self::copy_name(T::NAME),
        boost_enabled: T::BOOST_ENABLED,
        flags: T::FLAGS,

        // Initialize mandatory callbacks.
        init: Some(Self::init_callback),
        verify: Some(Self::verify_callback),

        // Initialize optional callbacks based on the traits of `T`.
        setpolicy: if T::HAS_SETPOLICY {
            Some(Self::setpolicy_callback)
        } else {
            None
        },
        target: if T::HAS_TARGET {
            Some(Self::target_callback)
        } else {
            None
        },
        target_index: if T::HAS_TARGET_INDEX {
            Some(Self::target_index_callback)
        } else {
            None
        },
        fast_switch: if T::HAS_FAST_SWITCH {
            Some(Self::fast_switch_callback)
        } else {
            None
        },
        adjust_perf: if T::HAS_ADJUST_PERF {
            Some(Self::adjust_perf_callback)
        } else {
            None
        },
        get_intermediate: if T::HAS_GET_INTERMEDIATE {
            Some(Self::get_intermediate_callback)
        } else {
            None
        },
        target_intermediate: if T::HAS_TARGET_INTERMEDIATE {
            Some(Self::target_intermediate_callback)
        } else {
            None
        },
        get: if T::HAS_GET {
            Some(Self::get_callback)
        } else {
            None
        },
        update_limits: if T::HAS_UPDATE_LIMITS {
            Some(Self::update_limits_callback)
        } else {
            None
        },
        bios_limit: if T::HAS_BIOS_LIMIT {
            Some(Self::bios_limit_callback)
        } else {
            None
        },
        online: if T::HAS_ONLINE {
            Some(Self::online_callback)
        } else {
            None
        },
        offline: if T::HAS_OFFLINE {
            Some(Self::offline_callback)
        } else {
            None
        },
        exit: if T::HAS_EXIT {
            Some(Self::exit_callback)
        } else {
            None
        },
        suspend: if T::HAS_SUSPEND {
            Some(Self::suspend_callback)
        } else {
            None
        },
        resume: if T::HAS_RESUME {
            Some(Self::resume_callback)
        } else {
            None
        },
        ready: if T::HAS_READY {
            Some(Self::ready_callback)
        } else {
            None
        },
        set_boost: if T::HAS_SET_BOOST {
            Some(Self::set_boost_callback)
        } else {
            None
        },
        register_em: if T::HAS_REGISTER_EM {
            Some(Self::register_em_callback)
        } else {
            None
        },
        ..pin_init::zeroed()
    };

    const fn copy_name(name: &'static CStr) -> [c_char; CPUFREQ_NAME_LEN] {
        let src = name.to_bytes_with_nul();
        let mut dst = [0; CPUFREQ_NAME_LEN];

        build_assert!(src.len() <= CPUFREQ_NAME_LEN);

        let mut i = 0;
        while i < src.len() {
            dst[i] = src[i];
            i += 1;
        }

        dst
    }

    /// Registers a CPU frequency driver with the cpufreq core.
    pub fn new() -> Result<Self> {
        // We can't use `&Self::VTABLE` directly because the cpufreq core modifies some fields in
        // the C `struct cpufreq_driver`, which requires a mutable reference.
        let mut drv = KBox::new(UnsafeCell::new(Self::VTABLE), GFP_KERNEL)?;

        // SAFETY: `drv` is guaranteed to be valid for the lifetime of `Registration`.
        to_result(unsafe { bindings::cpufreq_register_driver(drv.get_mut()) })?;

        Ok(Self(drv, PhantomData))
    }

    /// Same as [`Registration::new`], but does not return a [`Registration`] instance.
    ///
    /// Instead the [`Registration`] is owned by [`devres::register`] and will be dropped, once the
    /// device is detached.
    pub fn new_foreign_owned(dev: &Device<Bound>) -> Result
    where
        T: 'static,
    {
        devres::register(dev, Self::new()?, GFP_KERNEL)
    }
}

/// CPU frequency driver callbacks.
impl<T: Driver> Registration<T> {
    /// Driver's `init` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn init_callback(ptr: *mut bindings::cpufreq_policy) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };

            let data = T::init(policy)?;
            policy.set_data(data)?;
            Ok(0)
        })
    }

    /// Driver's `exit` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn exit_callback(ptr: *mut bindings::cpufreq_policy) {
        // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
        // lifetime of `policy`.
        let policy = unsafe { Policy::from_raw_mut(ptr) };

        let data = policy.clear_data();
        let _ = T::exit(policy, data);
    }

    /// Driver's `online` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn online_callback(ptr: *mut bindings::cpufreq_policy) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };
            T::online(policy).map(|()| 0)
        })
    }

    /// Driver's `offline` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn offline_callback(ptr: *mut bindings::cpufreq_policy) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };
            T::offline(policy).map(|()| 0)
        })
    }

    /// Driver's `suspend` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn suspend_callback(ptr: *mut bindings::cpufreq_policy) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };
            T::suspend(policy).map(|()| 0)
        })
    }

    /// Driver's `resume` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn resume_callback(ptr: *mut bindings::cpufreq_policy) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };
            T::resume(policy).map(|()| 0)
        })
    }

    /// Driver's `ready` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn ready_callback(ptr: *mut bindings::cpufreq_policy) {
        // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
        // lifetime of `policy`.
        let policy = unsafe { Policy::from_raw_mut(ptr) };
        T::ready(policy);
    }

    /// Driver's `verify` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn verify_callback(ptr: *mut bindings::cpufreq_policy_data) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let data = unsafe { PolicyData::from_raw_mut(ptr) };
            T::verify(data).map(|()| 0)
        })
    }

    /// Driver's `setpolicy` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn setpolicy_callback(ptr: *mut bindings::cpufreq_policy) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };
            T::setpolicy(policy).map(|()| 0)
        })
    }

    /// Driver's `target` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn target_callback(
        ptr: *mut bindings::cpufreq_policy,
        target_freq: c_uint,
        relation: c_uint,
    ) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };
            T::target(policy, target_freq, Relation::new(relation)?).map(|()| 0)
        })
    }

    /// Driver's `target_index` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn target_index_callback(
        ptr: *mut bindings::cpufreq_policy,
        index: c_uint,
    ) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };

            // SAFETY: The C code guarantees that `index` corresponds to a valid entry in the
            // frequency table.
            let index = unsafe { TableIndex::new(index as usize) };

            T::target_index(policy, index).map(|()| 0)
        })
    }

    /// Driver's `fast_switch` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn fast_switch_callback(
        ptr: *mut bindings::cpufreq_policy,
        target_freq: c_uint,
    ) -> c_uint {
        // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
        // lifetime of `policy`.
        let policy = unsafe { Policy::from_raw_mut(ptr) };
        T::fast_switch(policy, target_freq)
    }

    /// Driver's `adjust_perf` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    unsafe extern "C" fn adjust_perf_callback(
        cpu: c_uint,
        min_perf: c_ulong,
        target_perf: c_ulong,
        capacity: c_ulong,
    ) {
        // SAFETY: The C API guarantees that `cpu` refers to a valid CPU number.
        let cpu_id = unsafe { CpuId::from_u32_unchecked(cpu) };

        if let Ok(mut policy) = PolicyCpu::from_cpu(cpu_id) {
            T::adjust_perf(&mut policy, min_perf, target_perf, capacity);
        }
    }

    /// Driver's `get_intermediate` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn get_intermediate_callback(
        ptr: *mut bindings::cpufreq_policy,
        index: c_uint,
    ) -> c_uint {
        // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
        // lifetime of `policy`.
        let policy = unsafe { Policy::from_raw_mut(ptr) };

        // SAFETY: The C code guarantees that `index` corresponds to a valid entry in the
        // frequency table.
        let index = unsafe { TableIndex::new(index as usize) };

        T::get_intermediate(policy, index)
    }

    /// Driver's `target_intermediate` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn target_intermediate_callback(
        ptr: *mut bindings::cpufreq_policy,
        index: c_uint,
    ) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };

            // SAFETY: The C code guarantees that `index` corresponds to a valid entry in the
            // frequency table.
            let index = unsafe { TableIndex::new(index as usize) };

            T::target_intermediate(policy, index).map(|()| 0)
        })
    }

    /// Driver's `get` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    unsafe extern "C" fn get_callback(cpu: c_uint) -> c_uint {
        // SAFETY: The C API guarantees that `cpu` refers to a valid CPU number.
        let cpu_id = unsafe { CpuId::from_u32_unchecked(cpu) };

        PolicyCpu::from_cpu(cpu_id).map_or(0, |mut policy| T::get(&mut policy).map_or(0, |f| f))
    }

    /// Driver's `update_limit` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn update_limits_callback(ptr: *mut bindings::cpufreq_policy) {
        // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
        // lifetime of `policy`.
        let policy = unsafe { Policy::from_raw_mut(ptr) };
        T::update_limits(policy);
    }

    /// Driver's `bios_limit` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn bios_limit_callback(cpu: c_int, limit: *mut c_uint) -> c_int {
        // SAFETY: The C API guarantees that `cpu` refers to a valid CPU number.
        let cpu_id = unsafe { CpuId::from_i32_unchecked(cpu) };

        from_result(|| {
            let mut policy = PolicyCpu::from_cpu(cpu_id)?;

            // SAFETY: `limit` is guaranteed by the C code to be valid.
            T::bios_limit(&mut policy, &mut (unsafe { *limit })).map(|()| 0)
        })
    }

    /// Driver's `set_boost` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn set_boost_callback(
        ptr: *mut bindings::cpufreq_policy,
        state: c_int,
    ) -> c_int {
        from_result(|| {
            // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
            // lifetime of `policy`.
            let policy = unsafe { Policy::from_raw_mut(ptr) };
            T::set_boost(policy, state).map(|()| 0)
        })
    }

    /// Driver's `register_em` callback.
    ///
    /// # Safety
    ///
    /// - This function may only be called from the cpufreq C infrastructure.
    /// - The pointer arguments must be valid pointers.
    unsafe extern "C" fn register_em_callback(ptr: *mut bindings::cpufreq_policy) {
        // SAFETY: The `ptr` is guaranteed to be valid by the contract with the C code for the
        // lifetime of `policy`.
        let policy = unsafe { Policy::from_raw_mut(ptr) };
        T::register_em(policy);
    }
}

impl<T: Driver> Drop for Registration<T> {
    /// Unregisters with the cpufreq core.
    fn drop(&mut self) {
        // SAFETY: `self.0` is guaranteed to be valid for the lifetime of `Registration`.
        unsafe { bindings::cpufreq_unregister_driver(self.0.get_mut()) };
    }
}
