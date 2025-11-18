// SPDX-License-Identifier: GPL-2.0

//! Operating performance points.
//!
//! This module provides rust abstractions for interacting with the OPP subsystem.
//!
//! C header: [`include/linux/pm_opp.h`](srctree/include/linux/pm_opp.h)
//!
//! Reference: <https://docs.kernel.org/power/opp.html>

use crate::{
    clk::Hertz,
    cpumask::{Cpumask, CpumaskVar},
    device::Device,
    error::{code::*, from_err_ptr, from_result, to_result, Result, VTABLE_DEFAULT_ERROR},
    ffi::c_ulong,
    prelude::*,
    str::CString,
    sync::aref::{ARef, AlwaysRefCounted},
    types::Opaque,
};

#[cfg(CONFIG_CPU_FREQ)]
/// Frequency table implementation.
mod freq {
    use super::*;
    use crate::cpufreq;
    use core::ops::Deref;

    /// OPP frequency table.
    ///
    /// A [`cpufreq::Table`] created from [`Table`].
    pub struct FreqTable {
        dev: ARef<Device>,
        ptr: *mut bindings::cpufreq_frequency_table,
    }

    impl FreqTable {
        /// Creates a new instance of [`FreqTable`] from [`Table`].
        pub(crate) fn new(table: &Table) -> Result<Self> {
            let mut ptr: *mut bindings::cpufreq_frequency_table = ptr::null_mut();

            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements.
            to_result(unsafe {
                bindings::dev_pm_opp_init_cpufreq_table(table.dev.as_raw(), &mut ptr)
            })?;

            Ok(Self {
                dev: table.dev.clone(),
                ptr,
            })
        }

        /// Returns a reference to the underlying [`cpufreq::Table`].
        #[inline]
        fn table(&self) -> &cpufreq::Table {
            // SAFETY: The `ptr` is guaranteed by the C code to be valid.
            unsafe { cpufreq::Table::from_raw(self.ptr) }
        }
    }

    impl Deref for FreqTable {
        type Target = cpufreq::Table;

        #[inline]
        fn deref(&self) -> &Self::Target {
            self.table()
        }
    }

    impl Drop for FreqTable {
        fn drop(&mut self) {
            // SAFETY: The pointer was created via `dev_pm_opp_init_cpufreq_table`, and is only
            // freed here.
            unsafe {
                bindings::dev_pm_opp_free_cpufreq_table(self.dev.as_raw(), &mut self.as_raw())
            };
        }
    }
}

#[cfg(CONFIG_CPU_FREQ)]
pub use freq::FreqTable;

use core::{marker::PhantomData, ptr};

use macros::vtable;

/// Creates a null-terminated slice of pointers to [`Cstring`]s.
fn to_c_str_array(names: &[CString]) -> Result<KVec<*const u8>> {
    // Allocated a null-terminated vector of pointers.
    let mut list = KVec::with_capacity(names.len() + 1, GFP_KERNEL)?;

    for name in names.iter() {
        list.push(name.as_ptr().cast(), GFP_KERNEL)?;
    }

    list.push(ptr::null(), GFP_KERNEL)?;
    Ok(list)
}

/// The voltage unit.
///
/// Represents voltage in microvolts, wrapping a [`c_ulong`] value.
///
/// # Examples
///
/// ```
/// use kernel::opp::MicroVolt;
///
/// let raw = 90500;
/// let volt = MicroVolt(raw);
///
/// assert_eq!(usize::from(volt), raw);
/// assert_eq!(volt, MicroVolt(raw));
/// ```
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct MicroVolt(pub c_ulong);

impl From<MicroVolt> for c_ulong {
    #[inline]
    fn from(volt: MicroVolt) -> Self {
        volt.0
    }
}

/// The power unit.
///
/// Represents power in microwatts, wrapping a [`c_ulong`] value.
///
/// # Examples
///
/// ```
/// use kernel::opp::MicroWatt;
///
/// let raw = 1000000;
/// let power = MicroWatt(raw);
///
/// assert_eq!(usize::from(power), raw);
/// assert_eq!(power, MicroWatt(raw));
/// ```
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct MicroWatt(pub c_ulong);

impl From<MicroWatt> for c_ulong {
    #[inline]
    fn from(power: MicroWatt) -> Self {
        power.0
    }
}

/// Handle for a dynamically created [`OPP`].
///
/// The associated [`OPP`] is automatically removed when the [`Token`] is dropped.
///
/// # Examples
///
/// The following example demonstrates how to create an [`OPP`] dynamically.
///
/// ```
/// use kernel::clk::Hertz;
/// use kernel::device::Device;
/// use kernel::error::Result;
/// use kernel::opp::{Data, MicroVolt, Token};
/// use kernel::sync::aref::ARef;
///
/// fn create_opp(dev: &ARef<Device>, freq: Hertz, volt: MicroVolt, level: u32) -> Result<Token> {
///     let data = Data::new(freq, volt, level, false);
///
///     // OPP is removed once token goes out of scope.
///     data.add_opp(dev)
/// }
/// ```
pub struct Token {
    dev: ARef<Device>,
    freq: Hertz,
}

impl Token {
    /// Dynamically adds an [`OPP`] and returns a [`Token`] that removes it on drop.
    fn new(dev: &ARef<Device>, mut data: Data) -> Result<Self> {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_add_dynamic(dev.as_raw(), &mut data.0) })?;
        Ok(Self {
            dev: dev.clone(),
            freq: data.freq(),
        })
    }
}

impl Drop for Token {
    fn drop(&mut self) {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        unsafe { bindings::dev_pm_opp_remove(self.dev.as_raw(), self.freq.into()) };
    }
}

/// OPP data.
///
/// Rust abstraction for the C `struct dev_pm_opp_data`, used to define operating performance
/// points (OPPs) dynamically.
///
/// # Examples
///
/// The following example demonstrates how to create an [`OPP`] with [`Data`].
///
/// ```
/// use kernel::clk::Hertz;
/// use kernel::device::Device;
/// use kernel::error::Result;
/// use kernel::opp::{Data, MicroVolt, Token};
/// use kernel::sync::aref::ARef;
///
/// fn create_opp(dev: &ARef<Device>, freq: Hertz, volt: MicroVolt, level: u32) -> Result<Token> {
///     let data = Data::new(freq, volt, level, false);
///
///     // OPP is removed once token goes out of scope.
///     data.add_opp(dev)
/// }
/// ```
#[repr(transparent)]
pub struct Data(bindings::dev_pm_opp_data);

impl Data {
    /// Creates a new instance of [`Data`].
    ///
    /// This can be used to define a dynamic OPP to be added to a device.
    pub fn new(freq: Hertz, volt: MicroVolt, level: u32, turbo: bool) -> Self {
        Self(bindings::dev_pm_opp_data {
            turbo,
            freq: freq.into(),
            u_volt: volt.into(),
            level,
        })
    }

    /// Adds an [`OPP`] dynamically.
    ///
    /// Returns a [`Token`] that ensures the OPP is automatically removed
    /// when it goes out of scope.
    #[inline]
    pub fn add_opp(self, dev: &ARef<Device>) -> Result<Token> {
        Token::new(dev, self)
    }

    /// Returns the frequency associated with this OPP data.
    #[inline]
    fn freq(&self) -> Hertz {
        Hertz(self.0.freq)
    }
}

/// [`OPP`] search options.
///
/// # Examples
///
/// Defines how to search for an [`OPP`] in a [`Table`] relative to a frequency.
///
/// ```
/// use kernel::clk::Hertz;
/// use kernel::error::Result;
/// use kernel::opp::{OPP, SearchType, Table};
/// use kernel::sync::aref::ARef;
///
/// fn find_opp(table: &Table, freq: Hertz) -> Result<ARef<OPP>> {
///     let opp = table.opp_from_freq(freq, Some(true), None, SearchType::Exact)?;
///
///     pr_info!("OPP frequency is: {:?}\n", opp.freq(None));
///     pr_info!("OPP voltage is: {:?}\n", opp.voltage());
///     pr_info!("OPP level is: {}\n", opp.level());
///     pr_info!("OPP power is: {:?}\n", opp.power());
///
///     Ok(opp)
/// }
/// ```
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum SearchType {
    /// Match the exact frequency.
    Exact,
    /// Find the highest frequency less than or equal to the given value.
    Floor,
    /// Find the lowest frequency greater than or equal to the given value.
    Ceil,
}

/// OPP configuration callbacks.
///
/// Implement this trait to customize OPP clock and regulator setup for your device.
#[vtable]
pub trait ConfigOps {
    /// This is typically used to scale clocks when transitioning between OPPs.
    #[inline]
    fn config_clks(_dev: &Device, _table: &Table, _opp: &OPP, _scaling_down: bool) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// This provides access to the old and new OPPs, allowing for safe regulator adjustments.
    #[inline]
    fn config_regulators(
        _dev: &Device,
        _opp_old: &OPP,
        _opp_new: &OPP,
        _data: *mut *mut bindings::regulator,
        _count: u32,
    ) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }
}

/// OPP configuration token.
///
/// Returned by the OPP core when configuration is applied to a [`Device`]. The associated
/// configuration is automatically cleared when the token is dropped.
pub struct ConfigToken(i32);

impl Drop for ConfigToken {
    fn drop(&mut self) {
        // SAFETY: This is the same token value returned by the C code via `dev_pm_opp_set_config`.
        unsafe { bindings::dev_pm_opp_clear_config(self.0) };
    }
}

/// OPP configurations.
///
/// Rust abstraction for the C `struct dev_pm_opp_config`.
///
/// # Examples
///
/// The following example demonstrates how to set OPP property-name configuration for a [`Device`].
///
/// ```
/// use kernel::device::Device;
/// use kernel::error::Result;
/// use kernel::opp::{Config, ConfigOps, ConfigToken};
/// use kernel::str::CString;
/// use kernel::sync::aref::ARef;
/// use kernel::macros::vtable;
///
/// #[derive(Default)]
/// struct Driver;
///
/// #[vtable]
/// impl ConfigOps for Driver {}
///
/// fn configure(dev: &ARef<Device>) -> Result<ConfigToken> {
///     let name = CString::try_from_fmt(fmt!("slow"))?;
///
///     // The OPP configuration is cleared once the [`ConfigToken`] goes out of scope.
///     Config::<Driver>::new()
///         .set_prop_name(name)?
///         .set(dev)
/// }
/// ```
#[derive(Default)]
pub struct Config<T: ConfigOps>
where
    T: Default,
{
    clk_names: Option<KVec<CString>>,
    prop_name: Option<CString>,
    regulator_names: Option<KVec<CString>>,
    supported_hw: Option<KVec<u32>>,

    // Tuple containing (required device, index)
    required_dev: Option<(ARef<Device>, u32)>,
    _data: PhantomData<T>,
}

impl<T: ConfigOps + Default> Config<T> {
    /// Creates a new instance of [`Config`].
    #[inline]
    pub fn new() -> Self {
        Self::default()
    }

    /// Initializes clock names.
    pub fn set_clk_names(mut self, names: KVec<CString>) -> Result<Self> {
        if self.clk_names.is_some() {
            return Err(EBUSY);
        }

        if names.is_empty() {
            return Err(EINVAL);
        }

        self.clk_names = Some(names);
        Ok(self)
    }

    /// Initializes property name.
    pub fn set_prop_name(mut self, name: CString) -> Result<Self> {
        if self.prop_name.is_some() {
            return Err(EBUSY);
        }

        self.prop_name = Some(name);
        Ok(self)
    }

    /// Initializes regulator names.
    pub fn set_regulator_names(mut self, names: KVec<CString>) -> Result<Self> {
        if self.regulator_names.is_some() {
            return Err(EBUSY);
        }

        if names.is_empty() {
            return Err(EINVAL);
        }

        self.regulator_names = Some(names);

        Ok(self)
    }

    /// Initializes required devices.
    pub fn set_required_dev(mut self, dev: ARef<Device>, index: u32) -> Result<Self> {
        if self.required_dev.is_some() {
            return Err(EBUSY);
        }

        self.required_dev = Some((dev, index));
        Ok(self)
    }

    /// Initializes supported hardware.
    pub fn set_supported_hw(mut self, hw: KVec<u32>) -> Result<Self> {
        if self.supported_hw.is_some() {
            return Err(EBUSY);
        }

        if hw.is_empty() {
            return Err(EINVAL);
        }

        self.supported_hw = Some(hw);
        Ok(self)
    }

    /// Sets the configuration with the OPP core.
    ///
    /// The returned [`ConfigToken`] will remove the configuration when dropped.
    pub fn set(self, dev: &Device) -> Result<ConfigToken> {
        let (_clk_list, clk_names) = match &self.clk_names {
            Some(x) => {
                let list = to_c_str_array(x)?;
                let ptr = list.as_ptr();
                (Some(list), ptr)
            }
            None => (None, ptr::null()),
        };

        let (_regulator_list, regulator_names) = match &self.regulator_names {
            Some(x) => {
                let list = to_c_str_array(x)?;
                let ptr = list.as_ptr();
                (Some(list), ptr)
            }
            None => (None, ptr::null()),
        };

        let prop_name = self
            .prop_name
            .as_ref()
            .map_or(ptr::null(), |p| p.as_char_ptr());

        let (supported_hw, supported_hw_count) = self
            .supported_hw
            .as_ref()
            .map_or((ptr::null(), 0), |hw| (hw.as_ptr(), hw.len() as u32));

        let (required_dev, required_dev_index) = self
            .required_dev
            .as_ref()
            .map_or((ptr::null_mut(), 0), |(dev, idx)| (dev.as_raw(), *idx));

        let mut config = bindings::dev_pm_opp_config {
            clk_names,
            config_clks: if T::HAS_CONFIG_CLKS {
                Some(Self::config_clks)
            } else {
                None
            },
            prop_name,
            regulator_names,
            config_regulators: if T::HAS_CONFIG_REGULATORS {
                Some(Self::config_regulators)
            } else {
                None
            },
            supported_hw,
            supported_hw_count,

            required_dev,
            required_dev_index,
        };

        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements. The OPP core guarantees not to access fields of [`Config`] after this call
        // and so we don't need to save a copy of them for future use.
        let ret = unsafe { bindings::dev_pm_opp_set_config(dev.as_raw(), &mut config) };

        to_result(ret).map(|()| ConfigToken(ret))
    }

    /// Config's clk callback.
    ///
    /// SAFETY: Called from C. Inputs must be valid pointers.
    extern "C" fn config_clks(
        dev: *mut bindings::device,
        opp_table: *mut bindings::opp_table,
        opp: *mut bindings::dev_pm_opp,
        _data: *mut c_void,
        scaling_down: bool,
    ) -> c_int {
        from_result(|| {
            // SAFETY: 'dev' is guaranteed by the C code to be valid.
            let dev = unsafe { Device::get_device(dev) };
            T::config_clks(
                &dev,
                // SAFETY: 'opp_table' is guaranteed by the C code to be valid.
                &unsafe { Table::from_raw_table(opp_table, &dev) },
                // SAFETY: 'opp' is guaranteed by the C code to be valid.
                unsafe { OPP::from_raw_opp(opp)? },
                scaling_down,
            )
            .map(|()| 0)
        })
    }

    /// Config's regulator callback.
    ///
    /// SAFETY: Called from C. Inputs must be valid pointers.
    extern "C" fn config_regulators(
        dev: *mut bindings::device,
        old_opp: *mut bindings::dev_pm_opp,
        new_opp: *mut bindings::dev_pm_opp,
        regulators: *mut *mut bindings::regulator,
        count: c_uint,
    ) -> c_int {
        from_result(|| {
            // SAFETY: 'dev' is guaranteed by the C code to be valid.
            let dev = unsafe { Device::get_device(dev) };
            T::config_regulators(
                &dev,
                // SAFETY: 'old_opp' is guaranteed by the C code to be valid.
                unsafe { OPP::from_raw_opp(old_opp)? },
                // SAFETY: 'new_opp' is guaranteed by the C code to be valid.
                unsafe { OPP::from_raw_opp(new_opp)? },
                regulators,
                count,
            )
            .map(|()| 0)
        })
    }
}

/// A reference-counted OPP table.
///
/// Rust abstraction for the C `struct opp_table`.
///
/// # Invariants
///
/// The pointer stored in `Self` is non-null and valid for the lifetime of the [`Table`].
///
/// Instances of this type are reference-counted.
///
/// # Examples
///
/// The following example demonstrates how to get OPP [`Table`] for a [`Cpumask`] and set its
/// frequency.
///
/// ```
/// # #![cfg(CONFIG_OF)]
/// use kernel::clk::Hertz;
/// use kernel::cpumask::Cpumask;
/// use kernel::device::Device;
/// use kernel::error::Result;
/// use kernel::opp::Table;
/// use kernel::sync::aref::ARef;
///
/// fn get_table(dev: &ARef<Device>, mask: &mut Cpumask, freq: Hertz) -> Result<Table> {
///     let mut opp_table = Table::from_of_cpumask(dev, mask)?;
///
///     if opp_table.opp_count()? == 0 {
///         return Err(EINVAL);
///     }
///
///     pr_info!("Max transition latency is: {} ns\n", opp_table.max_transition_latency_ns());
///     pr_info!("Suspend frequency is: {:?}\n", opp_table.suspend_freq());
///
///     opp_table.set_rate(freq)?;
///     Ok(opp_table)
/// }
/// ```
pub struct Table {
    ptr: *mut bindings::opp_table,
    dev: ARef<Device>,
    #[allow(dead_code)]
    em: bool,
    #[allow(dead_code)]
    of: bool,
    cpus: Option<CpumaskVar>,
}

/// SAFETY: It is okay to send ownership of [`Table`] across thread boundaries.
unsafe impl Send for Table {}

/// SAFETY: It is okay to access [`Table`] through shared references from other threads because
/// we're either accessing properties that don't change or that are properly synchronised by C code.
unsafe impl Sync for Table {}

impl Table {
    /// Creates a new reference-counted [`Table`] from a raw pointer.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `ptr` is valid and non-null.
    unsafe fn from_raw_table(ptr: *mut bindings::opp_table, dev: &ARef<Device>) -> Self {
        // SAFETY: By the safety requirements, ptr is valid and its refcount will be incremented.
        //
        // INVARIANT: The reference-count is decremented when [`Table`] goes out of scope.
        unsafe { bindings::dev_pm_opp_get_opp_table_ref(ptr) };

        Self {
            ptr,
            dev: dev.clone(),
            em: false,
            of: false,
            cpus: None,
        }
    }

    /// Creates a new reference-counted [`Table`] instance for a [`Device`].
    pub fn from_dev(dev: &Device) -> Result<Self> {
        // SAFETY: The requirements are satisfied by the existence of the [`Device`] and its safety
        // requirements.
        //
        // INVARIANT: The reference-count is incremented by the C code and is decremented when
        // [`Table`] goes out of scope.
        let ptr = from_err_ptr(unsafe { bindings::dev_pm_opp_get_opp_table(dev.as_raw()) })?;

        Ok(Self {
            ptr,
            dev: dev.into(),
            em: false,
            of: false,
            cpus: None,
        })
    }

    /// Creates a new reference-counted [`Table`] instance for a [`Device`] based on device tree
    /// entries.
    #[cfg(CONFIG_OF)]
    pub fn from_of(dev: &ARef<Device>, index: i32) -> Result<Self> {
        // SAFETY: The requirements are satisfied by the existence of the [`Device`] and its safety
        // requirements.
        //
        // INVARIANT: The reference-count is incremented by the C code and is decremented when
        // [`Table`] goes out of scope.
        to_result(unsafe { bindings::dev_pm_opp_of_add_table_indexed(dev.as_raw(), index) })?;

        // Get the newly created [`Table`].
        let mut table = Self::from_dev(dev)?;
        table.of = true;

        Ok(table)
    }

    /// Remove device tree based [`Table`].
    #[cfg(CONFIG_OF)]
    #[inline]
    fn remove_of(&self) {
        // SAFETY: The requirements are satisfied by the existence of the [`Device`] and its safety
        // requirements. We took the reference from [`from_of`] earlier, it is safe to drop the
        // same now.
        unsafe { bindings::dev_pm_opp_of_remove_table(self.dev.as_raw()) };
    }

    /// Creates a new reference-counted [`Table`] instance for a [`Cpumask`] based on device tree
    /// entries.
    #[cfg(CONFIG_OF)]
    pub fn from_of_cpumask(dev: &Device, cpumask: &mut Cpumask) -> Result<Self> {
        // SAFETY: The cpumask is valid and the returned pointer will be owned by the [`Table`]
        // instance.
        //
        // INVARIANT: The reference-count is incremented by the C code and is decremented when
        // [`Table`] goes out of scope.
        to_result(unsafe { bindings::dev_pm_opp_of_cpumask_add_table(cpumask.as_raw()) })?;

        // Fetch the newly created table.
        let mut table = Self::from_dev(dev)?;
        table.cpus = Some(CpumaskVar::try_clone(cpumask)?);

        Ok(table)
    }

    /// Remove device tree based [`Table`] for a [`Cpumask`].
    #[cfg(CONFIG_OF)]
    #[inline]
    fn remove_of_cpumask(&self, cpumask: &Cpumask) {
        // SAFETY: The cpumask is valid and we took the reference from [`from_of_cpumask`] earlier,
        // it is safe to drop the same now.
        unsafe { bindings::dev_pm_opp_of_cpumask_remove_table(cpumask.as_raw()) };
    }

    /// Returns the number of [`OPP`]s in the [`Table`].
    pub fn opp_count(&self) -> Result<u32> {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        let ret = unsafe { bindings::dev_pm_opp_get_opp_count(self.dev.as_raw()) };

        to_result(ret).map(|()| ret as u32)
    }

    /// Returns max clock latency (in nanoseconds) of the [`OPP`]s in the [`Table`].
    #[inline]
    pub fn max_clock_latency_ns(&self) -> usize {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        unsafe { bindings::dev_pm_opp_get_max_clock_latency(self.dev.as_raw()) }
    }

    /// Returns max volt latency (in nanoseconds) of the [`OPP`]s in the [`Table`].
    #[inline]
    pub fn max_volt_latency_ns(&self) -> usize {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        unsafe { bindings::dev_pm_opp_get_max_volt_latency(self.dev.as_raw()) }
    }

    /// Returns max transition latency (in nanoseconds) of the [`OPP`]s in the [`Table`].
    #[inline]
    pub fn max_transition_latency_ns(&self) -> usize {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        unsafe { bindings::dev_pm_opp_get_max_transition_latency(self.dev.as_raw()) }
    }

    /// Returns the suspend [`OPP`]'s frequency.
    #[inline]
    pub fn suspend_freq(&self) -> Hertz {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        Hertz(unsafe { bindings::dev_pm_opp_get_suspend_opp_freq(self.dev.as_raw()) })
    }

    /// Synchronizes regulators used by the [`Table`].
    #[inline]
    pub fn sync_regulators(&self) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_sync_regulators(self.dev.as_raw()) })
    }

    /// Gets sharing CPUs.
    #[inline]
    pub fn sharing_cpus(dev: &Device, cpumask: &mut Cpumask) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_get_sharing_cpus(dev.as_raw(), cpumask.as_raw()) })
    }

    /// Sets sharing CPUs.
    pub fn set_sharing_cpus(&mut self, cpumask: &mut Cpumask) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe {
            bindings::dev_pm_opp_set_sharing_cpus(self.dev.as_raw(), cpumask.as_raw())
        })?;

        if let Some(mask) = self.cpus.as_mut() {
            // Update the cpumask as this will be used while removing the table.
            cpumask.copy(mask);
        }

        Ok(())
    }

    /// Gets sharing CPUs from device tree.
    #[cfg(CONFIG_OF)]
    #[inline]
    pub fn of_sharing_cpus(dev: &Device, cpumask: &mut Cpumask) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe {
            bindings::dev_pm_opp_of_get_sharing_cpus(dev.as_raw(), cpumask.as_raw())
        })
    }

    /// Updates the voltage value for an [`OPP`].
    #[inline]
    pub fn adjust_voltage(
        &self,
        freq: Hertz,
        volt: MicroVolt,
        volt_min: MicroVolt,
        volt_max: MicroVolt,
    ) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe {
            bindings::dev_pm_opp_adjust_voltage(
                self.dev.as_raw(),
                freq.into(),
                volt.into(),
                volt_min.into(),
                volt_max.into(),
            )
        })
    }

    /// Creates [`FreqTable`] from [`Table`].
    #[cfg(CONFIG_CPU_FREQ)]
    #[inline]
    pub fn cpufreq_table(&mut self) -> Result<FreqTable> {
        FreqTable::new(self)
    }

    /// Configures device with [`OPP`] matching the frequency value.
    #[inline]
    pub fn set_rate(&self, freq: Hertz) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_set_rate(self.dev.as_raw(), freq.into()) })
    }

    /// Configures device with [`OPP`].
    #[inline]
    pub fn set_opp(&self, opp: &OPP) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_set_opp(self.dev.as_raw(), opp.as_raw()) })
    }

    /// Finds [`OPP`] based on frequency.
    pub fn opp_from_freq(
        &self,
        freq: Hertz,
        available: Option<bool>,
        index: Option<u32>,
        stype: SearchType,
    ) -> Result<ARef<OPP>> {
        let raw_dev = self.dev.as_raw();
        let index = index.unwrap_or(0);
        let mut rate = freq.into();

        let ptr = from_err_ptr(match stype {
            SearchType::Exact => {
                if let Some(available) = available {
                    // SAFETY: The requirements are satisfied by the existence of [`Device`] and
                    // its safety requirements. The returned pointer will be owned by the new
                    // [`OPP`] instance.
                    unsafe {
                        bindings::dev_pm_opp_find_freq_exact_indexed(
                            raw_dev, rate, index, available,
                        )
                    }
                } else {
                    return Err(EINVAL);
                }
            }

            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements. The returned pointer will be owned by the new [`OPP`] instance.
            SearchType::Ceil => unsafe {
                bindings::dev_pm_opp_find_freq_ceil_indexed(raw_dev, &mut rate, index)
            },

            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements. The returned pointer will be owned by the new [`OPP`] instance.
            SearchType::Floor => unsafe {
                bindings::dev_pm_opp_find_freq_floor_indexed(raw_dev, &mut rate, index)
            },
        })?;

        // SAFETY: The `ptr` is guaranteed by the C code to be valid.
        unsafe { OPP::from_raw_opp_owned(ptr) }
    }

    /// Finds [`OPP`] based on level.
    pub fn opp_from_level(&self, mut level: u32, stype: SearchType) -> Result<ARef<OPP>> {
        let raw_dev = self.dev.as_raw();

        let ptr = from_err_ptr(match stype {
            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements. The returned pointer will be owned by the new [`OPP`] instance.
            SearchType::Exact => unsafe { bindings::dev_pm_opp_find_level_exact(raw_dev, level) },

            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements. The returned pointer will be owned by the new [`OPP`] instance.
            SearchType::Ceil => unsafe {
                bindings::dev_pm_opp_find_level_ceil(raw_dev, &mut level)
            },

            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements. The returned pointer will be owned by the new [`OPP`] instance.
            SearchType::Floor => unsafe {
                bindings::dev_pm_opp_find_level_floor(raw_dev, &mut level)
            },
        })?;

        // SAFETY: The `ptr` is guaranteed by the C code to be valid.
        unsafe { OPP::from_raw_opp_owned(ptr) }
    }

    /// Finds [`OPP`] based on bandwidth.
    pub fn opp_from_bw(&self, mut bw: u32, index: i32, stype: SearchType) -> Result<ARef<OPP>> {
        let raw_dev = self.dev.as_raw();

        let ptr = from_err_ptr(match stype {
            // The OPP core doesn't support this yet.
            SearchType::Exact => return Err(EINVAL),

            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements. The returned pointer will be owned by the new [`OPP`] instance.
            SearchType::Ceil => unsafe {
                bindings::dev_pm_opp_find_bw_ceil(raw_dev, &mut bw, index)
            },

            // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
            // requirements. The returned pointer will be owned by the new [`OPP`] instance.
            SearchType::Floor => unsafe {
                bindings::dev_pm_opp_find_bw_floor(raw_dev, &mut bw, index)
            },
        })?;

        // SAFETY: The `ptr` is guaranteed by the C code to be valid.
        unsafe { OPP::from_raw_opp_owned(ptr) }
    }

    /// Enables the [`OPP`].
    #[inline]
    pub fn enable_opp(&self, freq: Hertz) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_enable(self.dev.as_raw(), freq.into()) })
    }

    /// Disables the [`OPP`].
    #[inline]
    pub fn disable_opp(&self, freq: Hertz) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_disable(self.dev.as_raw(), freq.into()) })
    }

    /// Registers with the Energy model.
    #[cfg(CONFIG_OF)]
    pub fn of_register_em(&mut self, cpumask: &mut Cpumask) -> Result {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe {
            bindings::dev_pm_opp_of_register_em(self.dev.as_raw(), cpumask.as_raw())
        })?;

        self.em = true;
        Ok(())
    }

    /// Unregisters with the Energy model.
    #[cfg(all(CONFIG_OF, CONFIG_ENERGY_MODEL))]
    #[inline]
    fn of_unregister_em(&self) {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements. We registered with the EM framework earlier, it is safe to unregister now.
        unsafe { bindings::em_dev_unregister_perf_domain(self.dev.as_raw()) };
    }
}

impl Drop for Table {
    fn drop(&mut self) {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe
        // to relinquish it now.
        unsafe { bindings::dev_pm_opp_put_opp_table(self.ptr) };

        #[cfg(CONFIG_OF)]
        {
            #[cfg(CONFIG_ENERGY_MODEL)]
            if self.em {
                self.of_unregister_em();
            }

            if self.of {
                self.remove_of();
            } else if let Some(cpumask) = self.cpus.take() {
                self.remove_of_cpumask(&cpumask);
            }
        }
    }
}

/// A reference-counted Operating performance point (OPP).
///
/// Rust abstraction for the C `struct dev_pm_opp`.
///
/// # Invariants
///
/// The pointer stored in `Self` is non-null and valid for the lifetime of the [`OPP`].
///
/// Instances of this type are reference-counted. The reference count is incremented by the
/// `dev_pm_opp_get` function and decremented by `dev_pm_opp_put`. The Rust type `ARef<OPP>`
/// represents a pointer that owns a reference count on the [`OPP`].
///
/// A reference to the [`OPP`], &[`OPP`], isn't refcounted by the Rust code.
///
/// # Examples
///
/// The following example demonstrates how to get [`OPP`] corresponding to a frequency value and
/// configure the device with it.
///
/// ```
/// use kernel::clk::Hertz;
/// use kernel::error::Result;
/// use kernel::opp::{SearchType, Table};
///
/// fn configure_opp(table: &Table, freq: Hertz) -> Result {
///     let opp = table.opp_from_freq(freq, Some(true), None, SearchType::Exact)?;
///
///     if opp.freq(None) != freq {
///         return Err(EINVAL);
///     }
///
///     table.set_opp(&opp)
/// }
/// ```
#[repr(transparent)]
pub struct OPP(Opaque<bindings::dev_pm_opp>);

/// SAFETY: It is okay to send the ownership of [`OPP`] across thread boundaries.
unsafe impl Send for OPP {}

/// SAFETY: It is okay to access [`OPP`] through shared references from other threads because we're
/// either accessing properties that don't change or that are properly synchronised by C code.
unsafe impl Sync for OPP {}

/// SAFETY: The type invariants guarantee that [`OPP`] is always refcounted.
unsafe impl AlwaysRefCounted for OPP {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::dev_pm_opp_get(self.0.get()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is nonzero.
        unsafe { bindings::dev_pm_opp_put(obj.cast().as_ptr()) }
    }
}

impl OPP {
    /// Creates an owned reference to a [`OPP`] from a valid pointer.
    ///
    /// The refcount is incremented by the C code and will be decremented by `dec_ref` when the
    /// [`ARef`] object is dropped.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and the refcount of the [`OPP`] is incremented.
    /// The caller must also ensure that it doesn't explicitly drop the refcount of the [`OPP`], as
    /// the returned [`ARef`] object takes over the refcount increment on the underlying object and
    /// the same will be dropped along with it.
    pub unsafe fn from_raw_opp_owned(ptr: *mut bindings::dev_pm_opp) -> Result<ARef<Self>> {
        let ptr = ptr::NonNull::new(ptr).ok_or(ENODEV)?;

        // SAFETY: The safety requirements guarantee the validity of the pointer.
        //
        // INVARIANT: The reference-count is decremented when [`OPP`] goes out of scope.
        Ok(unsafe { ARef::from_raw(ptr.cast()) })
    }

    /// Creates a reference to a [`OPP`] from a valid pointer.
    ///
    /// The refcount is not updated by the Rust API unless the returned reference is converted to
    /// an [`ARef`] object.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and remains valid for the duration of `'a`.
    #[inline]
    pub unsafe fn from_raw_opp<'a>(ptr: *mut bindings::dev_pm_opp) -> Result<&'a Self> {
        // SAFETY: The caller guarantees that the pointer is not dangling and stays valid for the
        // duration of 'a. The cast is okay because [`OPP`] is `repr(transparent)`.
        Ok(unsafe { &*ptr.cast() })
    }

    #[inline]
    fn as_raw(&self) -> *mut bindings::dev_pm_opp {
        self.0.get()
    }

    /// Returns the frequency of an [`OPP`].
    pub fn freq(&self, index: Option<u32>) -> Hertz {
        let index = index.unwrap_or(0);

        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        Hertz(unsafe { bindings::dev_pm_opp_get_freq_indexed(self.as_raw(), index) })
    }

    /// Returns the voltage of an [`OPP`].
    #[inline]
    pub fn voltage(&self) -> MicroVolt {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        MicroVolt(unsafe { bindings::dev_pm_opp_get_voltage(self.as_raw()) })
    }

    /// Returns the level of an [`OPP`].
    #[inline]
    pub fn level(&self) -> u32 {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        unsafe { bindings::dev_pm_opp_get_level(self.as_raw()) }
    }

    /// Returns the power of an [`OPP`].
    #[inline]
    pub fn power(&self) -> MicroWatt {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        MicroWatt(unsafe { bindings::dev_pm_opp_get_power(self.as_raw()) })
    }

    /// Returns the required pstate of an [`OPP`].
    #[inline]
    pub fn required_pstate(&self, index: u32) -> u32 {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        unsafe { bindings::dev_pm_opp_get_required_pstate(self.as_raw(), index) }
    }

    /// Returns true if the [`OPP`] is turbo.
    #[inline]
    pub fn is_turbo(&self) -> bool {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        unsafe { bindings::dev_pm_opp_is_turbo(self.as_raw()) }
    }
}
