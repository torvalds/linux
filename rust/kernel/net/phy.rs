// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2023 FUJITA Tomonori <fujita.tomonori@gmail.com>

//! Network PHY device.
//!
//! C headers: [`include/linux/phy.h`](srctree/include/linux/phy.h).

use crate::{error::*, prelude::*, types::Opaque};
use core::{marker::PhantomData, ptr::addr_of_mut};

pub mod reg;

/// PHY state machine states.
///
/// Corresponds to the kernel's [`enum phy_state`].
///
/// Some of PHY drivers access to the state of PHY's software state machine.
///
/// [`enum phy_state`]: srctree/include/linux/phy.h
#[derive(PartialEq, Eq)]
pub enum DeviceState {
    /// PHY device and driver are not ready for anything.
    Down,
    /// PHY is ready to send and receive packets.
    Ready,
    /// PHY is up, but no polling or interrupts are done.
    Halted,
    /// PHY is up, but is in an error state.
    Error,
    /// PHY and attached device are ready to do work.
    Up,
    /// PHY is currently running.
    Running,
    /// PHY is up, but not currently plugged in.
    NoLink,
    /// PHY is performing a cable test.
    CableTest,
}

/// A mode of Ethernet communication.
///
/// PHY drivers get duplex information from hardware and update the current state.
pub enum DuplexMode {
    /// PHY is in full-duplex mode.
    Full,
    /// PHY is in half-duplex mode.
    Half,
    /// PHY is in unknown duplex mode.
    Unknown,
}

/// An instance of a PHY device.
///
/// Wraps the kernel's [`struct phy_device`].
///
/// A [`Device`] instance is created when a callback in [`Driver`] is executed. A PHY driver
/// executes [`Driver`]'s methods during the callback.
///
/// # Invariants
///
/// - Referencing a `phy_device` using this struct asserts that you are in
///   a context where all methods defined on this struct are safe to call.
/// - This struct always has a valid `self.0.mdio.dev`.
///
/// [`struct phy_device`]: srctree/include/linux/phy.h
// During the calls to most functions in [`Driver`], the C side (`PHYLIB`) holds a lock that is
// unique for every instance of [`Device`]. `PHYLIB` uses a different serialization technique for
// [`Driver::resume`] and [`Driver::suspend`]: `PHYLIB` updates `phy_device`'s state with
// the lock held, thus guaranteeing that [`Driver::resume`] has exclusive access to the instance.
// [`Driver::resume`] and [`Driver::suspend`] also are called where only one thread can access
// to the instance.
#[repr(transparent)]
pub struct Device(Opaque<bindings::phy_device>);

impl Device {
    /// Creates a new [`Device`] instance from a raw pointer.
    ///
    /// # Safety
    ///
    /// For the duration of `'a`,
    /// - the pointer must point at a valid `phy_device`, and the caller
    ///   must be in a context where all methods defined on this struct
    ///   are safe to call.
    /// - `(*ptr).mdio.dev` must be a valid.
    unsafe fn from_raw<'a>(ptr: *mut bindings::phy_device) -> &'a mut Self {
        // CAST: `Self` is a `repr(transparent)` wrapper around `bindings::phy_device`.
        let ptr = ptr.cast::<Self>();
        // SAFETY: by the function requirements the pointer is valid and we have unique access for
        // the duration of `'a`.
        unsafe { &mut *ptr }
    }

    /// Gets the id of the PHY.
    pub fn phy_id(&self) -> u32 {
        let phydev = self.0.get();
        // SAFETY: The struct invariant ensures that we may access
        // this field without additional synchronization.
        unsafe { (*phydev).phy_id }
    }

    /// Gets the state of PHY state machine states.
    pub fn state(&self) -> DeviceState {
        let phydev = self.0.get();
        // SAFETY: The struct invariant ensures that we may access
        // this field without additional synchronization.
        let state = unsafe { (*phydev).state };
        // TODO: this conversion code will be replaced with automatically generated code by bindgen
        // when it becomes possible.
        match state {
            bindings::phy_state_PHY_DOWN => DeviceState::Down,
            bindings::phy_state_PHY_READY => DeviceState::Ready,
            bindings::phy_state_PHY_HALTED => DeviceState::Halted,
            bindings::phy_state_PHY_ERROR => DeviceState::Error,
            bindings::phy_state_PHY_UP => DeviceState::Up,
            bindings::phy_state_PHY_RUNNING => DeviceState::Running,
            bindings::phy_state_PHY_NOLINK => DeviceState::NoLink,
            bindings::phy_state_PHY_CABLETEST => DeviceState::CableTest,
            _ => DeviceState::Error,
        }
    }

    /// Gets the current link state.
    ///
    /// It returns true if the link is up.
    pub fn is_link_up(&self) -> bool {
        const LINK_IS_UP: u64 = 1;
        // TODO: the code to access to the bit field will be replaced with automatically
        // generated code by bindgen when it becomes possible.
        // SAFETY: The struct invariant ensures that we may access
        // this field without additional synchronization.
        let bit_field = unsafe { &(*self.0.get())._bitfield_1 };
        bit_field.get(14, 1) == LINK_IS_UP
    }

    /// Gets the current auto-negotiation configuration.
    ///
    /// It returns true if auto-negotiation is enabled.
    pub fn is_autoneg_enabled(&self) -> bool {
        // TODO: the code to access to the bit field will be replaced with automatically
        // generated code by bindgen when it becomes possible.
        // SAFETY: The struct invariant ensures that we may access
        // this field without additional synchronization.
        let bit_field = unsafe { &(*self.0.get())._bitfield_1 };
        bit_field.get(13, 1) == bindings::AUTONEG_ENABLE as u64
    }

    /// Gets the current auto-negotiation state.
    ///
    /// It returns true if auto-negotiation is completed.
    pub fn is_autoneg_completed(&self) -> bool {
        const AUTONEG_COMPLETED: u64 = 1;
        // TODO: the code to access to the bit field will be replaced with automatically
        // generated code by bindgen when it becomes possible.
        // SAFETY: The struct invariant ensures that we may access
        // this field without additional synchronization.
        let bit_field = unsafe { &(*self.0.get())._bitfield_1 };
        bit_field.get(15, 1) == AUTONEG_COMPLETED
    }

    /// Sets the speed of the PHY.
    pub fn set_speed(&mut self, speed: u32) {
        let phydev = self.0.get();
        // SAFETY: The struct invariant ensures that we may access
        // this field without additional synchronization.
        unsafe { (*phydev).speed = speed as i32 };
    }

    /// Sets duplex mode.
    pub fn set_duplex(&mut self, mode: DuplexMode) {
        let phydev = self.0.get();
        let v = match mode {
            DuplexMode::Full => bindings::DUPLEX_FULL as i32,
            DuplexMode::Half => bindings::DUPLEX_HALF as i32,
            DuplexMode::Unknown => bindings::DUPLEX_UNKNOWN as i32,
        };
        // SAFETY: The struct invariant ensures that we may access
        // this field without additional synchronization.
        unsafe { (*phydev).duplex = v };
    }

    /// Reads a PHY register.
    // This function reads a hardware register and updates the stats so takes `&mut self`.
    pub fn read<R: reg::Register>(&mut self, reg: R) -> Result<u16> {
        reg.read(self)
    }

    /// Writes a PHY register.
    pub fn write<R: reg::Register>(&mut self, reg: R, val: u16) -> Result {
        reg.write(self, val)
    }

    /// Reads a paged register.
    pub fn read_paged(&mut self, page: u16, regnum: u16) -> Result<u16> {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        let ret = unsafe { bindings::phy_read_paged(phydev, page.into(), regnum.into()) };
        if ret < 0 {
            Err(Error::from_errno(ret))
        } else {
            Ok(ret as u16)
        }
    }

    /// Resolves the advertisements into PHY settings.
    pub fn resolve_aneg_linkmode(&mut self) {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        unsafe { bindings::phy_resolve_aneg_linkmode(phydev) };
    }

    /// Executes software reset the PHY via `BMCR_RESET` bit.
    pub fn genphy_soft_reset(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::genphy_soft_reset(phydev) })
    }

    /// Initializes the PHY.
    pub fn init_hw(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::phy_init_hw(phydev) })
    }

    /// Starts auto-negotiation.
    pub fn start_aneg(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::_phy_start_aneg(phydev) })
    }

    /// Resumes the PHY via `BMCR_PDOWN` bit.
    pub fn genphy_resume(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::genphy_resume(phydev) })
    }

    /// Suspends the PHY via `BMCR_PDOWN` bit.
    pub fn genphy_suspend(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::genphy_suspend(phydev) })
    }

    /// Checks the link status and updates current link state.
    pub fn genphy_read_status<R: reg::Register>(&mut self) -> Result<u16> {
        R::read_status(self)
    }

    /// Updates the link status.
    pub fn genphy_update_link(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::genphy_update_link(phydev) })
    }

    /// Reads link partner ability.
    pub fn genphy_read_lpa(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::genphy_read_lpa(phydev) })
    }

    /// Reads PHY abilities.
    pub fn genphy_read_abilities(&mut self) -> Result {
        let phydev = self.0.get();
        // SAFETY: `phydev` is pointing to a valid object by the type invariant of `Self`.
        // So it's just an FFI call.
        to_result(unsafe { bindings::genphy_read_abilities(phydev) })
    }
}

impl AsRef<kernel::device::Device> for Device {
    fn as_ref(&self) -> &kernel::device::Device {
        let phydev = self.0.get();
        // SAFETY: The struct invariant ensures that `mdio.dev` is valid.
        unsafe { kernel::device::Device::as_ref(addr_of_mut!((*phydev).mdio.dev)) }
    }
}

/// Defines certain other features this PHY supports (like interrupts).
///
/// These flag values are used in [`Driver::FLAGS`].
pub mod flags {
    /// PHY is internal.
    pub const IS_INTERNAL: u32 = bindings::PHY_IS_INTERNAL;
    /// PHY needs to be reset after the refclk is enabled.
    pub const RST_AFTER_CLK_EN: u32 = bindings::PHY_RST_AFTER_CLK_EN;
    /// Polling is used to detect PHY status changes.
    pub const POLL_CABLE_TEST: u32 = bindings::PHY_POLL_CABLE_TEST;
    /// Don't suspend.
    pub const ALWAYS_CALL_SUSPEND: u32 = bindings::PHY_ALWAYS_CALL_SUSPEND;
}

/// An adapter for the registration of a PHY driver.
struct Adapter<T: Driver> {
    _p: PhantomData<T>,
}

impl<T: Driver> Adapter<T> {
    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn soft_reset_callback(
        phydev: *mut bindings::phy_device,
    ) -> crate::ffi::c_int {
        from_result(|| {
            // SAFETY: This callback is called only in contexts
            // where we hold `phy_device->lock`, so the accessors on
            // `Device` are okay to call.
            let dev = unsafe { Device::from_raw(phydev) };
            T::soft_reset(dev)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn probe_callback(phydev: *mut bindings::phy_device) -> crate::ffi::c_int {
        from_result(|| {
            // SAFETY: This callback is called only in contexts
            // where we can exclusively access `phy_device` because
            // it's not published yet, so the accessors on `Device` are okay
            // to call.
            let dev = unsafe { Device::from_raw(phydev) };
            T::probe(dev)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn get_features_callback(
        phydev: *mut bindings::phy_device,
    ) -> crate::ffi::c_int {
        from_result(|| {
            // SAFETY: This callback is called only in contexts
            // where we hold `phy_device->lock`, so the accessors on
            // `Device` are okay to call.
            let dev = unsafe { Device::from_raw(phydev) };
            T::get_features(dev)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn suspend_callback(phydev: *mut bindings::phy_device) -> crate::ffi::c_int {
        from_result(|| {
            // SAFETY: The C core code ensures that the accessors on
            // `Device` are okay to call even though `phy_device->lock`
            // might not be held.
            let dev = unsafe { Device::from_raw(phydev) };
            T::suspend(dev)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn resume_callback(phydev: *mut bindings::phy_device) -> crate::ffi::c_int {
        from_result(|| {
            // SAFETY: The C core code ensures that the accessors on
            // `Device` are okay to call even though `phy_device->lock`
            // might not be held.
            let dev = unsafe { Device::from_raw(phydev) };
            T::resume(dev)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn config_aneg_callback(
        phydev: *mut bindings::phy_device,
    ) -> crate::ffi::c_int {
        from_result(|| {
            // SAFETY: This callback is called only in contexts
            // where we hold `phy_device->lock`, so the accessors on
            // `Device` are okay to call.
            let dev = unsafe { Device::from_raw(phydev) };
            T::config_aneg(dev)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn read_status_callback(
        phydev: *mut bindings::phy_device,
    ) -> crate::ffi::c_int {
        from_result(|| {
            // SAFETY: This callback is called only in contexts
            // where we hold `phy_device->lock`, so the accessors on
            // `Device` are okay to call.
            let dev = unsafe { Device::from_raw(phydev) };
            T::read_status(dev)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn match_phy_device_callback(
        phydev: *mut bindings::phy_device,
    ) -> crate::ffi::c_int {
        // SAFETY: This callback is called only in contexts
        // where we hold `phy_device->lock`, so the accessors on
        // `Device` are okay to call.
        let dev = unsafe { Device::from_raw(phydev) };
        T::match_phy_device(dev) as i32
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn read_mmd_callback(
        phydev: *mut bindings::phy_device,
        devnum: i32,
        regnum: u16,
    ) -> i32 {
        from_result(|| {
            // SAFETY: This callback is called only in contexts
            // where we hold `phy_device->lock`, so the accessors on
            // `Device` are okay to call.
            let dev = unsafe { Device::from_raw(phydev) };
            // CAST: the C side verifies devnum < 32.
            let ret = T::read_mmd(dev, devnum as u8, regnum)?;
            Ok(ret.into())
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn write_mmd_callback(
        phydev: *mut bindings::phy_device,
        devnum: i32,
        regnum: u16,
        val: u16,
    ) -> i32 {
        from_result(|| {
            // SAFETY: This callback is called only in contexts
            // where we hold `phy_device->lock`, so the accessors on
            // `Device` are okay to call.
            let dev = unsafe { Device::from_raw(phydev) };
            T::write_mmd(dev, devnum as u8, regnum, val)?;
            Ok(0)
        })
    }

    /// # Safety
    ///
    /// `phydev` must be passed by the corresponding callback in `phy_driver`.
    unsafe extern "C" fn link_change_notify_callback(phydev: *mut bindings::phy_device) {
        // SAFETY: This callback is called only in contexts
        // where we hold `phy_device->lock`, so the accessors on
        // `Device` are okay to call.
        let dev = unsafe { Device::from_raw(phydev) };
        T::link_change_notify(dev);
    }
}

/// Driver structure for a particular PHY type.
///
/// Wraps the kernel's [`struct phy_driver`].
/// This is used to register a driver for a particular PHY type with the kernel.
///
/// # Invariants
///
/// `self.0` is always in a valid state.
///
/// [`struct phy_driver`]: srctree/include/linux/phy.h
#[repr(transparent)]
pub struct DriverVTable(Opaque<bindings::phy_driver>);

// SAFETY: `DriverVTable` doesn't expose any &self method to access internal data, so it's safe to
// share `&DriverVTable` across execution context boundaries.
unsafe impl Sync for DriverVTable {}

/// Creates a [`DriverVTable`] instance from [`Driver`].
///
/// This is used by [`module_phy_driver`] macro to create a static array of `phy_driver`.
///
/// [`module_phy_driver`]: crate::module_phy_driver
pub const fn create_phy_driver<T: Driver>() -> DriverVTable {
    // INVARIANT: All the fields of `struct phy_driver` are initialized properly.
    DriverVTable(Opaque::new(bindings::phy_driver {
        name: T::NAME.as_char_ptr().cast_mut(),
        flags: T::FLAGS,
        phy_id: T::PHY_DEVICE_ID.id,
        phy_id_mask: T::PHY_DEVICE_ID.mask_as_int(),
        soft_reset: if T::HAS_SOFT_RESET {
            Some(Adapter::<T>::soft_reset_callback)
        } else {
            None
        },
        probe: if T::HAS_PROBE {
            Some(Adapter::<T>::probe_callback)
        } else {
            None
        },
        get_features: if T::HAS_GET_FEATURES {
            Some(Adapter::<T>::get_features_callback)
        } else {
            None
        },
        match_phy_device: if T::HAS_MATCH_PHY_DEVICE {
            Some(Adapter::<T>::match_phy_device_callback)
        } else {
            None
        },
        suspend: if T::HAS_SUSPEND {
            Some(Adapter::<T>::suspend_callback)
        } else {
            None
        },
        resume: if T::HAS_RESUME {
            Some(Adapter::<T>::resume_callback)
        } else {
            None
        },
        config_aneg: if T::HAS_CONFIG_ANEG {
            Some(Adapter::<T>::config_aneg_callback)
        } else {
            None
        },
        read_status: if T::HAS_READ_STATUS {
            Some(Adapter::<T>::read_status_callback)
        } else {
            None
        },
        read_mmd: if T::HAS_READ_MMD {
            Some(Adapter::<T>::read_mmd_callback)
        } else {
            None
        },
        write_mmd: if T::HAS_WRITE_MMD {
            Some(Adapter::<T>::write_mmd_callback)
        } else {
            None
        },
        link_change_notify: if T::HAS_LINK_CHANGE_NOTIFY {
            Some(Adapter::<T>::link_change_notify_callback)
        } else {
            None
        },
        // SAFETY: The rest is zeroed out to initialize `struct phy_driver`,
        // sets `Option<&F>` to be `None`.
        ..unsafe { core::mem::MaybeUninit::<bindings::phy_driver>::zeroed().assume_init() }
    }))
}

/// Driver implementation for a particular PHY type.
///
/// This trait is used to create a [`DriverVTable`].
#[vtable]
pub trait Driver {
    /// Defines certain other features this PHY supports.
    /// It is a combination of the flags in the [`flags`] module.
    const FLAGS: u32 = 0;

    /// The friendly name of this PHY type.
    const NAME: &'static CStr;

    /// This driver only works for PHYs with IDs which match this field.
    /// The default id and mask are zero.
    const PHY_DEVICE_ID: DeviceId = DeviceId::new_with_custom_mask(0, 0);

    /// Issues a PHY software reset.
    fn soft_reset(_dev: &mut Device) -> Result {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Sets up device-specific structures during discovery.
    fn probe(_dev: &mut Device) -> Result {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Probes the hardware to determine what abilities it has.
    fn get_features(_dev: &mut Device) -> Result {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Returns true if this is a suitable driver for the given phydev.
    /// If not implemented, matching is based on [`Driver::PHY_DEVICE_ID`].
    fn match_phy_device(_dev: &Device) -> bool {
        false
    }

    /// Configures the advertisement and resets auto-negotiation
    /// if auto-negotiation is enabled.
    fn config_aneg(_dev: &mut Device) -> Result {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Determines the negotiated speed and duplex.
    fn read_status(_dev: &mut Device) -> Result<u16> {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Suspends the hardware, saving state if needed.
    fn suspend(_dev: &mut Device) -> Result {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Resumes the hardware, restoring state if needed.
    fn resume(_dev: &mut Device) -> Result {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Overrides the default MMD read function for reading a MMD register.
    fn read_mmd(_dev: &mut Device, _devnum: u8, _regnum: u16) -> Result<u16> {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Overrides the default MMD write function for writing a MMD register.
    fn write_mmd(_dev: &mut Device, _devnum: u8, _regnum: u16, _val: u16) -> Result {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    /// Callback for notification of link change.
    fn link_change_notify(_dev: &mut Device) {}
}

/// Registration structure for PHY drivers.
///
/// Registers [`DriverVTable`] instances with the kernel. They will be unregistered when dropped.
///
/// # Invariants
///
/// The `drivers` slice are currently registered to the kernel via `phy_drivers_register`.
pub struct Registration {
    drivers: Pin<&'static mut [DriverVTable]>,
}

// SAFETY: The only action allowed in a `Registration` instance is dropping it, which is safe to do
// from any thread because `phy_drivers_unregister` can be called from any thread context.
unsafe impl Send for Registration {}

impl Registration {
    /// Registers a PHY driver.
    pub fn register(
        module: &'static crate::ThisModule,
        drivers: Pin<&'static mut [DriverVTable]>,
    ) -> Result<Self> {
        if drivers.is_empty() {
            return Err(code::EINVAL);
        }
        // SAFETY: The type invariants of [`DriverVTable`] ensure that all elements of
        // the `drivers` slice are initialized properly. `drivers` will not be moved.
        // So it's just an FFI call.
        to_result(unsafe {
            bindings::phy_drivers_register(drivers[0].0.get(), drivers.len().try_into()?, module.0)
        })?;
        // INVARIANT: The `drivers` slice is successfully registered to the kernel via `phy_drivers_register`.
        Ok(Registration { drivers })
    }
}

impl Drop for Registration {
    fn drop(&mut self) {
        // SAFETY: The type invariants guarantee that `self.drivers` is valid.
        // So it's just an FFI call.
        unsafe {
            bindings::phy_drivers_unregister(self.drivers[0].0.get(), self.drivers.len() as i32)
        };
    }
}

/// An identifier for PHY devices on an MDIO/MII bus.
///
/// Represents the kernel's `struct mdio_device_id`. This is used to find an appropriate
/// PHY driver.
pub struct DeviceId {
    id: u32,
    mask: DeviceMask,
}

impl DeviceId {
    /// Creates a new instance with the exact match mask.
    pub const fn new_with_exact_mask(id: u32) -> Self {
        DeviceId {
            id,
            mask: DeviceMask::Exact,
        }
    }

    /// Creates a new instance with the model match mask.
    pub const fn new_with_model_mask(id: u32) -> Self {
        DeviceId {
            id,
            mask: DeviceMask::Model,
        }
    }

    /// Creates a new instance with the vendor match mask.
    pub const fn new_with_vendor_mask(id: u32) -> Self {
        DeviceId {
            id,
            mask: DeviceMask::Vendor,
        }
    }

    /// Creates a new instance with a custom match mask.
    pub const fn new_with_custom_mask(id: u32, mask: u32) -> Self {
        DeviceId {
            id,
            mask: DeviceMask::Custom(mask),
        }
    }

    /// Creates a new instance from [`Driver`].
    pub const fn new_with_driver<T: Driver>() -> Self {
        T::PHY_DEVICE_ID
    }

    /// Get a `mask` as u32.
    pub const fn mask_as_int(&self) -> u32 {
        self.mask.as_int()
    }

    // macro use only
    #[doc(hidden)]
    pub const fn mdio_device_id(&self) -> bindings::mdio_device_id {
        bindings::mdio_device_id {
            phy_id: self.id,
            phy_id_mask: self.mask.as_int(),
        }
    }
}

enum DeviceMask {
    Exact,
    Model,
    Vendor,
    Custom(u32),
}

impl DeviceMask {
    const MASK_EXACT: u32 = !0;
    const MASK_MODEL: u32 = !0 << 4;
    const MASK_VENDOR: u32 = !0 << 10;

    const fn as_int(&self) -> u32 {
        match self {
            DeviceMask::Exact => Self::MASK_EXACT,
            DeviceMask::Model => Self::MASK_MODEL,
            DeviceMask::Vendor => Self::MASK_VENDOR,
            DeviceMask::Custom(mask) => *mask,
        }
    }
}

/// Declares a kernel module for PHYs drivers.
///
/// This creates a static array of kernel's `struct phy_driver` and registers it.
/// This also corresponds to the kernel's `MODULE_DEVICE_TABLE` macro, which embeds the information
/// for module loading into the module binary file. Every driver needs an entry in `device_table`.
///
/// # Examples
///
/// ```
/// # mod module_phy_driver_sample {
/// use kernel::c_str;
/// use kernel::net::phy::{self, DeviceId};
/// use kernel::prelude::*;
///
/// kernel::module_phy_driver! {
///     drivers: [PhySample],
///     device_table: [
///         DeviceId::new_with_driver::<PhySample>()
///     ],
///     name: "rust_sample_phy",
///     author: "Rust for Linux Contributors",
///     description: "Rust sample PHYs driver",
///     license: "GPL",
/// }
///
/// struct PhySample;
///
/// #[vtable]
/// impl phy::Driver for PhySample {
///     const NAME: &'static CStr = c_str!("PhySample");
///     const PHY_DEVICE_ID: phy::DeviceId = phy::DeviceId::new_with_exact_mask(0x00000001);
/// }
/// # }
/// ```
///
/// This expands to the following code:
///
/// ```ignore
/// use kernel::c_str;
/// use kernel::net::phy::{self, DeviceId};
/// use kernel::prelude::*;
///
/// struct Module {
///     _reg: ::kernel::net::phy::Registration,
/// }
///
/// module! {
///     type: Module,
///     name: "rust_sample_phy",
///     author: "Rust for Linux Contributors",
///     description: "Rust sample PHYs driver",
///     license: "GPL",
/// }
///
/// struct PhySample;
///
/// #[vtable]
/// impl phy::Driver for PhySample {
///     const NAME: &'static CStr = c_str!("PhySample");
///     const PHY_DEVICE_ID: phy::DeviceId = phy::DeviceId::new_with_exact_mask(0x00000001);
/// }
///
/// const _: () = {
///     static mut DRIVERS: [::kernel::net::phy::DriverVTable; 1] =
///         [::kernel::net::phy::create_phy_driver::<PhySample>()];
///
///     impl ::kernel::Module for Module {
///         fn init(module: &'static ::kernel::ThisModule) -> Result<Self> {
///             let drivers = unsafe { &mut DRIVERS };
///             let mut reg = ::kernel::net::phy::Registration::register(
///                 module,
///                 ::core::pin::Pin::static_mut(drivers),
///             )?;
///             Ok(Module { _reg: reg })
///         }
///     }
/// };
///
/// const _DEVICE_TABLE: [::kernel::bindings::mdio_device_id; 2] = [
///     ::kernel::bindings::mdio_device_id {
///         phy_id: 0x00000001,
///         phy_id_mask: 0xffffffff,
///     },
///     ::kernel::bindings::mdio_device_id {
///         phy_id: 0,
///         phy_id_mask: 0,
///     },
/// ];
/// #[cfg(MODULE)]
/// #[no_mangle]
/// static __mod_device_table__mdio__phydev: [::kernel::bindings::mdio_device_id; 2] = _DEVICE_TABLE;
/// ```
#[macro_export]
macro_rules! module_phy_driver {
    (@replace_expr $_t:tt $sub:expr) => {$sub};

    (@count_devices $($x:expr),*) => {
        0usize $(+ $crate::module_phy_driver!(@replace_expr $x 1usize))*
    };

    (@device_table [$($dev:expr),+]) => {
        // SAFETY: C will not read off the end of this constant since the last element is zero.
        const _DEVICE_TABLE: [$crate::bindings::mdio_device_id;
            $crate::module_phy_driver!(@count_devices $($dev),+) + 1] = [
            $($dev.mdio_device_id()),+,
            $crate::bindings::mdio_device_id {
                phy_id: 0,
                phy_id_mask: 0
            }
        ];

        #[cfg(MODULE)]
        #[no_mangle]
        static __mod_device_table__mdio__phydev: [$crate::bindings::mdio_device_id;
            $crate::module_phy_driver!(@count_devices $($dev),+) + 1] = _DEVICE_TABLE;
    };

    (drivers: [$($driver:ident),+ $(,)?], device_table: [$($dev:expr),+ $(,)?], $($f:tt)*) => {
        struct Module {
            _reg: $crate::net::phy::Registration,
        }

        $crate::prelude::module! {
            type: Module,
            $($f)*
        }

        const _: () = {
            static mut DRIVERS: [$crate::net::phy::DriverVTable;
                $crate::module_phy_driver!(@count_devices $($driver),+)] =
                [$($crate::net::phy::create_phy_driver::<$driver>()),+];

            impl $crate::Module for Module {
                fn init(module: &'static $crate::ThisModule) -> Result<Self> {
                    // SAFETY: The anonymous constant guarantees that nobody else can access
                    // the `DRIVERS` static. The array is used only in the C side.
                    let drivers = unsafe { &mut DRIVERS };
                    let mut reg = $crate::net::phy::Registration::register(
                        module,
                        ::core::pin::Pin::static_mut(drivers),
                    )?;
                    Ok(Module { _reg: reg })
                }
            }
        };

        $crate::module_phy_driver!(@device_table [$($dev),+]);
    }
}
