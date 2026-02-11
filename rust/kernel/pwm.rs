// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Samsung Electronics Co., Ltd.
// Author: Michal Wilczynski <m.wilczynski@samsung.com>

//! PWM subsystem abstractions.
//!
//! C header: [`include/linux/pwm.h`](srctree/include/linux/pwm.h).

use crate::{
    bindings,
    container_of,
    device::{self, Bound},
    devres,
    error::{self, to_result},
    prelude::*,
    sync::aref::{ARef, AlwaysRefCounted},
    types::Opaque, //
};
use core::{
    marker::PhantomData,
    ops::Deref,
    ptr::NonNull, //
};

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

/// Describes the outcome of a `round_waveform` operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RoundingOutcome {
    /// The requested waveform was achievable exactly or by rounding values down.
    ExactOrRoundedDown,

    /// The requested waveform could only be achieved by rounding up.
    RoundedUp,
}

/// Wrapper for a PWM device [`struct pwm_device`](srctree/include/linux/pwm.h).
#[repr(transparent)]
pub struct Device(Opaque<bindings::pwm_device>);

impl Device {
    /// Creates a reference to a [`Device`] from a valid C pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and remains valid for the lifetime of the
    /// returned [`Device`] reference.
    pub(crate) unsafe fn from_raw<'a>(ptr: *mut bindings::pwm_device) -> &'a Self {
        // SAFETY: The safety requirements guarantee the validity of the dereference, while the
        // `Device` type being transparent makes the cast ok.
        unsafe { &*ptr.cast::<Self>() }
    }

    /// Returns a raw pointer to the underlying `pwm_device`.
    fn as_raw(&self) -> *mut bindings::pwm_device {
        self.0.get()
    }

    /// Gets the hardware PWM index for this device within its chip.
    pub fn hwpwm(&self) -> u32 {
        // SAFETY: `self.as_raw()` provides a valid pointer for `self`'s lifetime.
        unsafe { (*self.as_raw()).hwpwm }
    }

    /// Gets a reference to the parent `Chip` that this device belongs to.
    pub fn chip<T: PwmOps>(&self) -> &Chip<T> {
        // SAFETY: `self.as_raw()` provides a valid pointer. (*self.as_raw()).chip
        // is assumed to be a valid pointer to `pwm_chip` managed by the kernel.
        // Chip::from_raw's safety conditions must be met.
        unsafe { Chip::<T>::from_raw((*self.as_raw()).chip) }
    }

    /// Gets the label for this PWM device, if any.
    pub fn label(&self) -> Option<&CStr> {
        // SAFETY: self.as_raw() provides a valid pointer.
        let label_ptr = unsafe { (*self.as_raw()).label };
        if label_ptr.is_null() {
            return None;
        }

        // SAFETY: label_ptr is non-null and points to a C string
        // managed by the kernel, valid for the lifetime of the PWM device.
        Some(unsafe { CStr::from_char_ptr(label_ptr) })
    }

    /// Sets the PWM waveform configuration and enables the PWM signal.
    pub fn set_waveform(&self, wf: &Waveform, exact: bool) -> Result {
        let c_wf = bindings::pwm_waveform::from(*wf);

        // SAFETY: `self.as_raw()` provides a valid `*mut pwm_device` pointer.
        // `&c_wf` is a valid pointer to a `pwm_waveform` struct. The C function
        // handles all necessary internal locking.
        to_result(unsafe { bindings::pwm_set_waveform_might_sleep(self.as_raw(), &c_wf, exact) })
    }

    /// Queries the hardware for the configuration it would apply for a given
    /// request.
    pub fn round_waveform(&self, wf: &mut Waveform) -> Result<RoundingOutcome> {
        let mut c_wf = bindings::pwm_waveform::from(*wf);

        // SAFETY: `self.as_raw()` provides a valid `*mut pwm_device` pointer.
        // `&mut c_wf` is a valid pointer to a mutable `pwm_waveform` struct that
        // the C function will update.
        let ret = unsafe { bindings::pwm_round_waveform_might_sleep(self.as_raw(), &mut c_wf) };

        to_result(ret)?;

        *wf = Waveform::from(c_wf);

        if ret == 1 {
            Ok(RoundingOutcome::RoundedUp)
        } else {
            Ok(RoundingOutcome::ExactOrRoundedDown)
        }
    }

    /// Reads the current waveform configuration directly from the hardware.
    pub fn get_waveform(&self) -> Result<Waveform> {
        let mut c_wf = bindings::pwm_waveform::default();

        // SAFETY: `self.as_raw()` is a valid pointer. We provide a valid pointer
        // to a stack-allocated `pwm_waveform` struct for the kernel to fill.
        to_result(unsafe { bindings::pwm_get_waveform_might_sleep(self.as_raw(), &mut c_wf) })?;

        Ok(Waveform::from(c_wf))
    }
}

/// The result of a `round_waveform_tohw` operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RoundedWaveform<WfHw> {
    /// A status code, 0 for success or 1 if values were rounded up.
    pub status: c_int,
    /// The driver-specific hardware representation of the waveform.
    pub hardware_waveform: WfHw,
}

/// Trait defining the operations for a PWM driver.
pub trait PwmOps: 'static + Send + Sync + Sized {
    /// The driver-specific hardware representation of a waveform.
    ///
    /// This type must be [`Copy`], [`Default`], and fit within `PWM_WFHWSIZE`.
    type WfHw: Copy + Default;

    /// Optional hook for when a PWM device is requested.
    fn request(_chip: &Chip<Self>, _pwm: &Device, _parent_dev: &device::Device<Bound>) -> Result {
        Ok(())
    }

    /// Optional hook for capturing a PWM signal.
    fn capture(
        _chip: &Chip<Self>,
        _pwm: &Device,
        _result: &mut bindings::pwm_capture,
        _timeout: usize,
        _parent_dev: &device::Device<Bound>,
    ) -> Result {
        Err(ENOTSUPP)
    }

    /// Convert a generic waveform to the hardware-specific representation.
    /// This is typically a pure calculation and does not perform I/O.
    fn round_waveform_tohw(
        _chip: &Chip<Self>,
        _pwm: &Device,
        _wf: &Waveform,
    ) -> Result<RoundedWaveform<Self::WfHw>> {
        Err(ENOTSUPP)
    }

    /// Convert a hardware-specific representation back to a generic waveform.
    /// This is typically a pure calculation and does not perform I/O.
    fn round_waveform_fromhw(
        _chip: &Chip<Self>,
        _pwm: &Device,
        _wfhw: &Self::WfHw,
        _wf: &mut Waveform,
    ) -> Result {
        Err(ENOTSUPP)
    }

    /// Read the current hardware configuration into the hardware-specific representation.
    fn read_waveform(
        _chip: &Chip<Self>,
        _pwm: &Device,
        _parent_dev: &device::Device<Bound>,
    ) -> Result<Self::WfHw> {
        Err(ENOTSUPP)
    }

    /// Write a hardware-specific waveform configuration to the hardware.
    fn write_waveform(
        _chip: &Chip<Self>,
        _pwm: &Device,
        _wfhw: &Self::WfHw,
        _parent_dev: &device::Device<Bound>,
    ) -> Result {
        Err(ENOTSUPP)
    }
}

/// Bridges Rust `PwmOps` to the C `pwm_ops` vtable.
struct Adapter<T: PwmOps> {
    _p: PhantomData<T>,
}

impl<T: PwmOps> Adapter<T> {
    const VTABLE: PwmOpsVTable = create_pwm_ops::<T>();

    /// # Safety
    ///
    /// `wfhw_ptr` must be valid for writes of `size_of::<T::WfHw>()` bytes.
    unsafe fn serialize_wfhw(wfhw: &T::WfHw, wfhw_ptr: *mut c_void) -> Result {
        let size = core::mem::size_of::<T::WfHw>();

        build_assert!(size <= bindings::PWM_WFHWSIZE as usize);

        // SAFETY: The caller ensures `wfhw_ptr` is valid for `size` bytes.
        unsafe {
            core::ptr::copy_nonoverlapping(
                core::ptr::from_ref::<T::WfHw>(wfhw).cast::<u8>(),
                wfhw_ptr.cast::<u8>(),
                size,
            )
        };

        Ok(())
    }

    /// # Safety
    ///
    /// `wfhw_ptr` must be valid for reads of `size_of::<T::WfHw>()` bytes.
    unsafe fn deserialize_wfhw(wfhw_ptr: *const c_void) -> Result<T::WfHw> {
        let size = core::mem::size_of::<T::WfHw>();

        build_assert!(size <= bindings::PWM_WFHWSIZE as usize);

        let mut wfhw = T::WfHw::default();
        // SAFETY: The caller ensures `wfhw_ptr` is valid for `size` bytes.
        unsafe {
            core::ptr::copy_nonoverlapping(
                wfhw_ptr.cast::<u8>(),
                core::ptr::from_mut::<T::WfHw>(&mut wfhw).cast::<u8>(),
                size,
            )
        };

        Ok(wfhw)
    }

    /// # Safety
    ///
    /// `dev` must be a valid pointer to a `bindings::device` embedded within a
    /// `bindings::pwm_chip`. This function is called by the device core when the
    /// last reference to the device is dropped.
    unsafe extern "C" fn release_callback(dev: *mut bindings::device) {
        // SAFETY: The function's contract guarantees that `dev` points to a `device`
        // field embedded within a valid `pwm_chip`. `container_of!` can therefore
        // safely calculate the address of the containing struct.
        let c_chip_ptr = unsafe { container_of!(dev, bindings::pwm_chip, dev) };

        // SAFETY: `c_chip_ptr` is a valid pointer to a `pwm_chip` as established
        // above. Calling this FFI function is safe.
        let drvdata_ptr = unsafe { bindings::pwmchip_get_drvdata(c_chip_ptr) };

        // SAFETY: The driver data was initialized in `new`. We run its destructor here.
        unsafe { core::ptr::drop_in_place(drvdata_ptr.cast::<T>()) };

        // Now, call the original release function to free the `pwm_chip` itself.
        // SAFETY: `dev` is the valid pointer passed into this callback, which is
        // the expected argument for `pwmchip_release`.
        unsafe { bindings::pwmchip_release(dev) };
    }

    /// # Safety
    ///
    /// Pointers from C must be valid.
    unsafe extern "C" fn request_callback(
        chip_ptr: *mut bindings::pwm_chip,
        pwm_ptr: *mut bindings::pwm_device,
    ) -> c_int {
        // SAFETY: PWM core guarentees `chip_ptr` and `pwm_ptr` are valid pointers.
        let (chip, pwm) = unsafe { (Chip::<T>::from_raw(chip_ptr), Device::from_raw(pwm_ptr)) };

        // SAFETY: The PWM core guarantees the parent device exists and is bound during callbacks.
        let bound_parent = unsafe { chip.bound_parent_device() };
        match T::request(chip, pwm, bound_parent) {
            Ok(()) => 0,
            Err(e) => e.to_errno(),
        }
    }

    /// # Safety
    ///
    /// Pointers from C must be valid.
    unsafe extern "C" fn capture_callback(
        chip_ptr: *mut bindings::pwm_chip,
        pwm_ptr: *mut bindings::pwm_device,
        res: *mut bindings::pwm_capture,
        timeout: usize,
    ) -> c_int {
        // SAFETY: Relies on the function's contract that `chip_ptr` and `pwm_ptr` are valid
        // pointers.
        let (chip, pwm, result) = unsafe {
            (
                Chip::<T>::from_raw(chip_ptr),
                Device::from_raw(pwm_ptr),
                &mut *res,
            )
        };

        // SAFETY: The PWM core guarantees the parent device exists and is bound during callbacks.
        let bound_parent = unsafe { chip.bound_parent_device() };
        match T::capture(chip, pwm, result, timeout, bound_parent) {
            Ok(()) => 0,
            Err(e) => e.to_errno(),
        }
    }

    /// # Safety
    ///
    /// Pointers from C must be valid.
    unsafe extern "C" fn round_waveform_tohw_callback(
        chip_ptr: *mut bindings::pwm_chip,
        pwm_ptr: *mut bindings::pwm_device,
        wf_ptr: *const bindings::pwm_waveform,
        wfhw_ptr: *mut c_void,
    ) -> c_int {
        // SAFETY: Relies on the function's contract that `chip_ptr` and `pwm_ptr` are valid
        // pointers.
        let (chip, pwm, wf) = unsafe {
            (
                Chip::<T>::from_raw(chip_ptr),
                Device::from_raw(pwm_ptr),
                Waveform::from(*wf_ptr),
            )
        };
        match T::round_waveform_tohw(chip, pwm, &wf) {
            Ok(rounded) => {
                // SAFETY: `wfhw_ptr` is valid per this function's safety contract.
                if unsafe { Self::serialize_wfhw(&rounded.hardware_waveform, wfhw_ptr) }.is_err() {
                    return EINVAL.to_errno();
                }
                rounded.status
            }
            Err(e) => e.to_errno(),
        }
    }

    /// # Safety
    ///
    /// Pointers from C must be valid.
    unsafe extern "C" fn round_waveform_fromhw_callback(
        chip_ptr: *mut bindings::pwm_chip,
        pwm_ptr: *mut bindings::pwm_device,
        wfhw_ptr: *const c_void,
        wf_ptr: *mut bindings::pwm_waveform,
    ) -> c_int {
        // SAFETY: Relies on the function's contract that `chip_ptr` and `pwm_ptr` are valid
        // pointers.
        let (chip, pwm) = unsafe { (Chip::<T>::from_raw(chip_ptr), Device::from_raw(pwm_ptr)) };
        // SAFETY: `deserialize_wfhw`'s safety contract is met by this function's contract.
        let wfhw = match unsafe { Self::deserialize_wfhw(wfhw_ptr) } {
            Ok(v) => v,
            Err(e) => return e.to_errno(),
        };

        let mut rust_wf = Waveform::default();
        match T::round_waveform_fromhw(chip, pwm, &wfhw, &mut rust_wf) {
            Ok(()) => {
                // SAFETY: `wf_ptr` is guaranteed valid by the C caller.
                unsafe { *wf_ptr = rust_wf.into() };
                0
            }
            Err(e) => e.to_errno(),
        }
    }

    /// # Safety
    ///
    /// Pointers from C must be valid.
    unsafe extern "C" fn read_waveform_callback(
        chip_ptr: *mut bindings::pwm_chip,
        pwm_ptr: *mut bindings::pwm_device,
        wfhw_ptr: *mut c_void,
    ) -> c_int {
        // SAFETY: Relies on the function's contract that `chip_ptr` and `pwm_ptr` are valid
        // pointers.
        let (chip, pwm) = unsafe { (Chip::<T>::from_raw(chip_ptr), Device::from_raw(pwm_ptr)) };

        // SAFETY: The PWM core guarantees the parent device exists and is bound during callbacks.
        let bound_parent = unsafe { chip.bound_parent_device() };
        match T::read_waveform(chip, pwm, bound_parent) {
            // SAFETY: `wfhw_ptr` is valid per this function's safety contract.
            Ok(wfhw) => match unsafe { Self::serialize_wfhw(&wfhw, wfhw_ptr) } {
                Ok(()) => 0,
                Err(e) => e.to_errno(),
            },
            Err(e) => e.to_errno(),
        }
    }

    /// # Safety
    ///
    /// Pointers from C must be valid.
    unsafe extern "C" fn write_waveform_callback(
        chip_ptr: *mut bindings::pwm_chip,
        pwm_ptr: *mut bindings::pwm_device,
        wfhw_ptr: *const c_void,
    ) -> c_int {
        // SAFETY: Relies on the function's contract that `chip_ptr` and `pwm_ptr` are valid
        // pointers.
        let (chip, pwm) = unsafe { (Chip::<T>::from_raw(chip_ptr), Device::from_raw(pwm_ptr)) };

        // SAFETY: The PWM core guarantees the parent device exists and is bound during callbacks.
        let bound_parent = unsafe { chip.bound_parent_device() };

        // SAFETY: `wfhw_ptr` is valid per this function's safety contract.
        let wfhw = match unsafe { Self::deserialize_wfhw(wfhw_ptr) } {
            Ok(v) => v,
            Err(e) => return e.to_errno(),
        };
        match T::write_waveform(chip, pwm, &wfhw, bound_parent) {
            Ok(()) => 0,
            Err(e) => e.to_errno(),
        }
    }
}

/// VTable structure wrapper for PWM operations.
/// Mirrors [`struct pwm_ops`](srctree/include/linux/pwm.h).
#[repr(transparent)]
pub struct PwmOpsVTable(bindings::pwm_ops);

// SAFETY: PwmOpsVTable is Send. The vtable contains only function pointers
// and a size, which are simple data types that can be safely moved across
// threads. The thread-safety of calling these functions is handled by the
// kernel's locking mechanisms.
unsafe impl Send for PwmOpsVTable {}

// SAFETY: PwmOpsVTable is Sync. The vtable is immutable after it is created,
// so it can be safely referenced and accessed concurrently by multiple threads
// e.g. to read the function pointers.
unsafe impl Sync for PwmOpsVTable {}

impl PwmOpsVTable {
    /// Returns a raw pointer to the underlying `pwm_ops` struct.
    pub(crate) fn as_raw(&self) -> *const bindings::pwm_ops {
        &self.0
    }
}

/// Creates a PWM operations vtable for a type `T` that implements `PwmOps`.
///
/// This is used to bridge Rust trait implementations to the C `struct pwm_ops`
/// expected by the kernel.
pub const fn create_pwm_ops<T: PwmOps>() -> PwmOpsVTable {
    // SAFETY: `core::mem::zeroed()` is unsafe. For `pwm_ops`, all fields are
    // `Option<extern "C" fn(...)>` or data, so a zeroed pattern (None/0) is valid initially.
    let mut ops: bindings::pwm_ops = unsafe { core::mem::zeroed() };

    ops.request = Some(Adapter::<T>::request_callback);
    ops.capture = Some(Adapter::<T>::capture_callback);

    ops.round_waveform_tohw = Some(Adapter::<T>::round_waveform_tohw_callback);
    ops.round_waveform_fromhw = Some(Adapter::<T>::round_waveform_fromhw_callback);
    ops.read_waveform = Some(Adapter::<T>::read_waveform_callback);
    ops.write_waveform = Some(Adapter::<T>::write_waveform_callback);
    ops.sizeof_wfhw = core::mem::size_of::<T::WfHw>();

    PwmOpsVTable(ops)
}

/// Wrapper for a PWM chip/controller ([`struct pwm_chip`](srctree/include/linux/pwm.h)).
#[repr(transparent)]
pub struct Chip<T: PwmOps>(Opaque<bindings::pwm_chip>, PhantomData<T>);

impl<T: PwmOps> Chip<T> {
    /// Creates a reference to a [`Chip`] from a valid pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and remains valid for the lifetime of the
    /// returned [`Chip`] reference.
    pub(crate) unsafe fn from_raw<'a>(ptr: *mut bindings::pwm_chip) -> &'a Self {
        // SAFETY: The safety requirements guarantee the validity of the dereference, while the
        // `Chip` type being transparent makes the cast ok.
        unsafe { &*ptr.cast::<Self>() }
    }

    /// Returns a raw pointer to the underlying `pwm_chip`.
    pub(crate) fn as_raw(&self) -> *mut bindings::pwm_chip {
        self.0.get()
    }

    /// Gets the number of PWM channels (hardware PWMs) on this chip.
    pub fn num_channels(&self) -> u32 {
        // SAFETY: `self.as_raw()` provides a valid pointer for `self`'s lifetime.
        unsafe { (*self.as_raw()).npwm }
    }

    /// Returns `true` if the chip supports atomic operations for configuration.
    pub fn is_atomic(&self) -> bool {
        // SAFETY: `self.as_raw()` provides a valid pointer for `self`'s lifetime.
        unsafe { (*self.as_raw()).atomic }
    }

    /// Returns a reference to the embedded `struct device` abstraction.
    pub fn device(&self) -> &device::Device {
        // SAFETY:
        // - `self.as_raw()` provides a valid pointer to `bindings::pwm_chip`.
        // - The `dev` field is an instance of `bindings::device` embedded
        //   within `pwm_chip`.
        // - Taking a pointer to this embedded field is valid.
        // - `device::Device` is `#[repr(transparent)]`.
        // - The lifetime of the returned reference is tied to `self`.
        unsafe { device::Device::from_raw(&raw mut (*self.as_raw()).dev) }
    }

    /// Gets the typed driver specific data associated with this chip's embedded device.
    pub fn drvdata(&self) -> &T {
        // SAFETY: `pwmchip_get_drvdata` returns the pointer to the private data area,
        // which we know holds our `T`. The pointer is valid for the lifetime of `self`.
        unsafe { &*bindings::pwmchip_get_drvdata(self.as_raw()).cast::<T>() }
    }

    /// Returns a reference to the parent device of this PWM chip's device.
    ///
    /// # Safety
    ///
    /// The caller must guarantee that the parent device exists and is bound.
    /// This is guaranteed by the PWM core during `PwmOps` callbacks.
    unsafe fn bound_parent_device(&self) -> &device::Device<Bound> {
        // SAFETY: Per the function's safety contract, the parent device exists.
        let parent = unsafe { self.device().parent().unwrap_unchecked() };

        // SAFETY: Per the function's safety contract, the parent device is bound.
        // This is guaranteed by the PWM core during `PwmOps` callbacks.
        unsafe { parent.as_bound() }
    }

    /// Allocates and wraps a PWM chip using `bindings::pwmchip_alloc`.
    ///
    /// Returns an [`ARef<Chip>`] managing the chip's lifetime via refcounting
    /// on its embedded `struct device`.
    #[allow(clippy::new_ret_no_self)]
    pub fn new<'a>(
        parent_dev: &'a device::Device<Bound>,
        num_channels: u32,
        data: impl pin_init::PinInit<T, Error>,
    ) -> Result<UnregisteredChip<'a, T>> {
        let sizeof_priv = core::mem::size_of::<T>();
        // SAFETY: `pwmchip_alloc` allocates memory for the C struct and our private data.
        let c_chip_ptr_raw =
            unsafe { bindings::pwmchip_alloc(parent_dev.as_raw(), num_channels, sizeof_priv) };

        let c_chip_ptr: *mut bindings::pwm_chip = error::from_err_ptr(c_chip_ptr_raw)?;

        // SAFETY: The `drvdata` pointer is the start of the private area, which is where
        // we will construct our `T` object.
        let drvdata_ptr = unsafe { bindings::pwmchip_get_drvdata(c_chip_ptr) };

        // SAFETY: We construct the `T` object in-place in the allocated private memory.
        unsafe { data.__pinned_init(drvdata_ptr.cast()) }.inspect_err(|_| {
            // SAFETY: It is safe to call `pwmchip_put()` with a valid pointer obtained
            // from `pwmchip_alloc()`. We will not use pointer after this.
            unsafe { bindings::pwmchip_put(c_chip_ptr) }
        })?;

        // SAFETY: `c_chip_ptr` points to a valid chip.
        unsafe { (*c_chip_ptr).dev.release = Some(Adapter::<T>::release_callback) };

        // SAFETY: `c_chip_ptr` points to a valid chip.
        // The `Adapter`'s `VTABLE` has a 'static lifetime, so the pointer
        // returned by `as_raw()` is always valid.
        unsafe { (*c_chip_ptr).ops = Adapter::<T>::VTABLE.as_raw() };

        // Cast the `*mut bindings::pwm_chip` to `*mut Chip`. This is valid because
        // `Chip` is `repr(transparent)` over `Opaque<bindings::pwm_chip>`, and
        // `Opaque<T>` is `repr(transparent)` over `T`.
        let chip_ptr_as_self = c_chip_ptr.cast::<Self>();

        // SAFETY: `chip_ptr_as_self` points to a valid `Chip` (layout-compatible with
        // `bindings::pwm_chip`) whose embedded device has refcount 1.
        // `ARef::from_raw` takes this pointer and manages it via `AlwaysRefCounted`.
        let chip = unsafe { ARef::from_raw(NonNull::new_unchecked(chip_ptr_as_self)) };

        Ok(UnregisteredChip { chip, parent_dev })
    }
}

// SAFETY: Implements refcounting for `Chip` using the embedded `struct device`.
unsafe impl<T: PwmOps> AlwaysRefCounted for Chip<T> {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: `self.0.get()` points to a valid `pwm_chip` because `self` exists.
        // The embedded `dev` is valid. `get_device` increments its refcount.
        unsafe { bindings::get_device(&raw mut (*self.0.get()).dev) };
    }

    #[inline]
    unsafe fn dec_ref(obj: NonNull<Chip<T>>) {
        let c_chip_ptr = obj.cast::<bindings::pwm_chip>().as_ptr();

        // SAFETY: `obj` is a valid pointer to a `Chip` (and thus `bindings::pwm_chip`)
        // with a non-zero refcount. `put_device` handles decrement and final release.
        unsafe { bindings::put_device(&raw mut (*c_chip_ptr).dev) };
    }
}

// SAFETY: `Chip` is a wrapper around `*mut bindings::pwm_chip`. The underlying C
// structure's state is managed and synchronized by the kernel's device model
// and PWM core locking mechanisms. Therefore, it is safe to move the `Chip`
// wrapper (and the pointer it contains) across threads.
unsafe impl<T: PwmOps> Send for Chip<T> {}

// SAFETY: It is safe for multiple threads to have shared access (`&Chip`) because
// the `Chip` data is immutable from the Rust side without holding the appropriate
// kernel locks, which the C core is responsible for. Any interior mutability is
// handled and synchronized by the C kernel code.
unsafe impl<T: PwmOps> Sync for Chip<T> {}

/// A wrapper around `ARef<Chip<T>>` that ensures that `register` can only be called once.
pub struct UnregisteredChip<'a, T: PwmOps> {
    chip: ARef<Chip<T>>,
    parent_dev: &'a device::Device<Bound>,
}

impl<T: PwmOps> UnregisteredChip<'_, T> {
    /// Registers a PWM chip with the PWM subsystem.
    ///
    /// Transfers its ownership to the `devres` framework, which ties its lifetime
    /// to the parent device.
    /// On unbind of the parent device, the `devres` entry will be dropped, automatically
    /// calling `pwmchip_remove`. This function should be called from the driver's `probe`.
    pub fn register(self) -> Result<ARef<Chip<T>>> {
        let c_chip_ptr = self.chip.as_raw();

        // SAFETY: `c_chip_ptr` points to a valid chip with its ops initialized.
        // `__pwmchip_add` is the C function to register the chip with the PWM core.
        to_result(unsafe { bindings::__pwmchip_add(c_chip_ptr, core::ptr::null_mut()) })?;

        let registration = Registration {
            chip: ARef::clone(&self.chip),
        };

        devres::register(self.parent_dev, registration, GFP_KERNEL)?;

        Ok(self.chip)
    }
}

impl<T: PwmOps> Deref for UnregisteredChip<'_, T> {
    type Target = Chip<T>;

    fn deref(&self) -> &Self::Target {
        &self.chip
    }
}

/// A resource guard that ensures `pwmchip_remove` is called on drop.
///
/// This struct is intended to be managed by the `devres` framework by transferring its ownership
/// via [`devres::register`]. This ties the lifetime of the PWM chip registration
/// to the lifetime of the underlying device.
struct Registration<T: PwmOps> {
    chip: ARef<Chip<T>>,
}

impl<T: PwmOps> Drop for Registration<T> {
    fn drop(&mut self) {
        let chip_raw = self.chip.as_raw();

        // SAFETY: `chip_raw` points to a chip that was successfully registered.
        // `bindings::pwmchip_remove` is the correct C function to unregister it.
        // This `drop` implementation is called automatically by `devres` on driver unbind.
        unsafe { bindings::pwmchip_remove(chip_raw) };
    }
}

/// Declares a kernel module that exposes a single PWM driver.
///
/// # Examples
///
///```ignore
/// kernel::module_pwm_platform_driver! {
///     type: MyDriver,
///     name: "Module name",
///     authors: ["Author name"],
///     description: "Description",
///     license: "GPL v2",
/// }
///```
#[macro_export]
macro_rules! module_pwm_platform_driver {
    ($($user_args:tt)*) => {
        $crate::module_platform_driver! {
            $($user_args)*
            imports_ns: ["PWM"],
        }
    };
}
