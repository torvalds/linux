// SPDX-License-Identifier: GPL-2.0

//! CPU Mask abstractions.
//!
//! C header: [`include/linux/cpumask.h`](srctree/include/linux/cpumask.h)

use crate::{
    alloc::{AllocError, Flags},
    cpu::CpuId,
    prelude::*,
    types::Opaque,
};

#[cfg(CONFIG_CPUMASK_OFFSTACK)]
use core::ptr::{self, NonNull};

use core::ops::{Deref, DerefMut};

/// A CPU Mask.
///
/// Rust abstraction for the C `struct cpumask`.
///
/// # Invariants
///
/// A [`Cpumask`] instance always corresponds to a valid C `struct cpumask`.
///
/// The callers must ensure that the `struct cpumask` is valid for access and
/// remains valid for the lifetime of the returned reference.
///
/// # Examples
///
/// The following example demonstrates how to update a [`Cpumask`].
///
/// ```
/// use kernel::bindings;
/// use kernel::cpu::CpuId;
/// use kernel::cpumask::Cpumask;
///
/// fn set_clear_cpu(ptr: *mut bindings::cpumask, set_cpu: CpuId, clear_cpu: CpuId) {
///     // SAFETY: The `ptr` is valid for writing and remains valid for the lifetime of the
///     // returned reference.
///     let mask = unsafe { Cpumask::as_mut_ref(ptr) };
///
///     mask.set(set_cpu);
///     mask.clear(clear_cpu);
/// }
/// ```
#[repr(transparent)]
pub struct Cpumask(Opaque<bindings::cpumask>);

impl Cpumask {
    /// Creates a mutable reference to an existing `struct cpumask` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for writing and remains valid for the lifetime
    /// of the returned reference.
    pub unsafe fn as_mut_ref<'a>(ptr: *mut bindings::cpumask) -> &'a mut Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for writing and remains valid for the
        // lifetime of the returned reference.
        unsafe { &mut *ptr.cast() }
    }

    /// Creates a reference to an existing `struct cpumask` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for reading and remains valid for the lifetime
    /// of the returned reference.
    pub unsafe fn as_ref<'a>(ptr: *const bindings::cpumask) -> &'a Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for reading and remains valid for the
        // lifetime of the returned reference.
        unsafe { &*ptr.cast() }
    }

    /// Obtain the raw `struct cpumask` pointer.
    pub fn as_raw(&self) -> *mut bindings::cpumask {
        let this: *const Self = self;
        this.cast_mut().cast()
    }

    /// Set `cpu` in the cpumask.
    ///
    /// ATTENTION: Contrary to C, this Rust `set()` method is non-atomic.
    /// This mismatches kernel naming convention and corresponds to the C
    /// function `__cpumask_set_cpu()`.
    #[inline]
    pub fn set(&mut self, cpu: CpuId) {
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to `__cpumask_set_cpu`.
        unsafe { bindings::__cpumask_set_cpu(u32::from(cpu), self.as_raw()) };
    }

    /// Clear `cpu` in the cpumask.
    ///
    /// ATTENTION: Contrary to C, this Rust `clear()` method is non-atomic.
    /// This mismatches kernel naming convention and corresponds to the C
    /// function `__cpumask_clear_cpu()`.
    #[inline]
    pub fn clear(&mut self, cpu: CpuId) {
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to
        // `__cpumask_clear_cpu`.
        unsafe { bindings::__cpumask_clear_cpu(i32::from(cpu), self.as_raw()) };
    }

    /// Test `cpu` in the cpumask.
    ///
    /// Equivalent to the kernel's `cpumask_test_cpu` API.
    #[inline]
    pub fn test(&self, cpu: CpuId) -> bool {
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to `cpumask_test_cpu`.
        unsafe { bindings::cpumask_test_cpu(i32::from(cpu), self.as_raw()) }
    }

    /// Set all CPUs in the cpumask.
    ///
    /// Equivalent to the kernel's `cpumask_setall` API.
    #[inline]
    pub fn setall(&mut self) {
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to `cpumask_setall`.
        unsafe { bindings::cpumask_setall(self.as_raw()) };
    }

    /// Checks if cpumask is empty.
    ///
    /// Equivalent to the kernel's `cpumask_empty` API.
    #[inline]
    pub fn empty(&self) -> bool {
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to `cpumask_empty`.
        unsafe { bindings::cpumask_empty(self.as_raw()) }
    }

    /// Checks if cpumask is full.
    ///
    /// Equivalent to the kernel's `cpumask_full` API.
    #[inline]
    pub fn full(&self) -> bool {
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to `cpumask_full`.
        unsafe { bindings::cpumask_full(self.as_raw()) }
    }

    /// Get weight of the cpumask.
    ///
    /// Equivalent to the kernel's `cpumask_weight` API.
    #[inline]
    pub fn weight(&self) -> u32 {
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to `cpumask_weight`.
        unsafe { bindings::cpumask_weight(self.as_raw()) }
    }

    /// Copy cpumask.
    ///
    /// Equivalent to the kernel's `cpumask_copy` API.
    #[inline]
    pub fn copy(&self, dstp: &mut Self) {
        // SAFETY: By the type invariant, `Self::as_raw` is a valid argument to `cpumask_copy`.
        unsafe { bindings::cpumask_copy(dstp.as_raw(), self.as_raw()) };
    }
}

/// A CPU Mask pointer.
///
/// Rust abstraction for the C `struct cpumask_var_t`.
///
/// # Invariants
///
/// A [`CpumaskVar`] instance always corresponds to a valid C `struct cpumask_var_t`.
///
/// The callers must ensure that the `struct cpumask_var_t` is valid for access and remains valid
/// for the lifetime of [`CpumaskVar`].
///
/// # Examples
///
/// The following example demonstrates how to create and update a [`CpumaskVar`].
///
/// ```
/// use kernel::cpu::CpuId;
/// use kernel::cpumask::CpumaskVar;
///
/// let mut mask = CpumaskVar::new_zero(GFP_KERNEL).unwrap();
///
/// assert!(mask.empty());
/// let mut count = 0;
///
/// let cpu2 = CpuId::from_u32(2);
/// if let Some(cpu) = cpu2 {
///     mask.set(cpu);
///     assert!(mask.test(cpu));
///     count += 1;
/// }
///
/// let cpu3 = CpuId::from_u32(3);
/// if let Some(cpu) = cpu3 {
///     mask.set(cpu);
///     assert!(mask.test(cpu));
///     count += 1;
/// }
///
/// assert_eq!(mask.weight(), count);
///
/// let mask2 = CpumaskVar::try_clone(&mask).unwrap();
///
/// if let Some(cpu) = cpu2 {
///     assert!(mask2.test(cpu));
/// }
///
/// if let Some(cpu) = cpu3 {
///     assert!(mask2.test(cpu));
/// }
/// assert_eq!(mask2.weight(), count);
/// ```
#[repr(transparent)]
pub struct CpumaskVar {
    #[cfg(CONFIG_CPUMASK_OFFSTACK)]
    ptr: NonNull<Cpumask>,
    #[cfg(not(CONFIG_CPUMASK_OFFSTACK))]
    mask: Cpumask,
}

impl CpumaskVar {
    /// Creates a zero-initialized instance of the [`CpumaskVar`].
    pub fn new_zero(_flags: Flags) -> Result<Self, AllocError> {
        Ok(Self {
            #[cfg(CONFIG_CPUMASK_OFFSTACK)]
            ptr: {
                let mut ptr: *mut bindings::cpumask = ptr::null_mut();

                // SAFETY: It is safe to call this method as the reference to `ptr` is valid.
                //
                // INVARIANT: The associated memory is freed when the `CpumaskVar` goes out of
                // scope.
                unsafe { bindings::zalloc_cpumask_var(&mut ptr, _flags.as_raw()) };
                NonNull::new(ptr.cast()).ok_or(AllocError)?
            },

            #[cfg(not(CONFIG_CPUMASK_OFFSTACK))]
            mask: Cpumask(Opaque::zeroed()),
        })
    }

    /// Creates an instance of the [`CpumaskVar`].
    ///
    /// # Safety
    ///
    /// The caller must ensure that the returned [`CpumaskVar`] is properly initialized before
    /// getting used.
    pub unsafe fn new(_flags: Flags) -> Result<Self, AllocError> {
        Ok(Self {
            #[cfg(CONFIG_CPUMASK_OFFSTACK)]
            ptr: {
                let mut ptr: *mut bindings::cpumask = ptr::null_mut();

                // SAFETY: It is safe to call this method as the reference to `ptr` is valid.
                //
                // INVARIANT: The associated memory is freed when the `CpumaskVar` goes out of
                // scope.
                unsafe { bindings::alloc_cpumask_var(&mut ptr, _flags.as_raw()) };
                NonNull::new(ptr.cast()).ok_or(AllocError)?
            },
            #[cfg(not(CONFIG_CPUMASK_OFFSTACK))]
            mask: Cpumask(Opaque::uninit()),
        })
    }

    /// Creates a mutable reference to an existing `struct cpumask_var_t` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for writing and remains valid for the lifetime
    /// of the returned reference.
    pub unsafe fn from_raw_mut<'a>(ptr: *mut bindings::cpumask_var_t) -> &'a mut Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for writing and remains valid for the
        // lifetime of the returned reference.
        unsafe { &mut *ptr.cast() }
    }

    /// Creates a reference to an existing `struct cpumask_var_t` pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid for reading and remains valid for the lifetime
    /// of the returned reference.
    pub unsafe fn from_raw<'a>(ptr: *const bindings::cpumask_var_t) -> &'a Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        //
        // INVARIANT: The caller ensures that `ptr` is valid for reading and remains valid for the
        // lifetime of the returned reference.
        unsafe { &*ptr.cast() }
    }

    /// Clones cpumask.
    pub fn try_clone(cpumask: &Cpumask) -> Result<Self> {
        // SAFETY: The returned cpumask_var is initialized right after this call.
        let mut cpumask_var = unsafe { Self::new(GFP_KERNEL) }?;

        cpumask.copy(&mut cpumask_var);
        Ok(cpumask_var)
    }
}

// Make [`CpumaskVar`] behave like a pointer to [`Cpumask`].
impl Deref for CpumaskVar {
    type Target = Cpumask;

    #[cfg(CONFIG_CPUMASK_OFFSTACK)]
    fn deref(&self) -> &Self::Target {
        // SAFETY: The caller owns CpumaskVar, so it is safe to deref the cpumask.
        unsafe { &*self.ptr.as_ptr() }
    }

    #[cfg(not(CONFIG_CPUMASK_OFFSTACK))]
    fn deref(&self) -> &Self::Target {
        &self.mask
    }
}

impl DerefMut for CpumaskVar {
    #[cfg(CONFIG_CPUMASK_OFFSTACK)]
    fn deref_mut(&mut self) -> &mut Cpumask {
        // SAFETY: The caller owns CpumaskVar, so it is safe to deref the cpumask.
        unsafe { self.ptr.as_mut() }
    }

    #[cfg(not(CONFIG_CPUMASK_OFFSTACK))]
    fn deref_mut(&mut self) -> &mut Cpumask {
        &mut self.mask
    }
}

impl Drop for CpumaskVar {
    fn drop(&mut self) {
        #[cfg(CONFIG_CPUMASK_OFFSTACK)]
        // SAFETY: By the type invariant, `self.as_raw` is a valid argument to `free_cpumask_var`.
        unsafe {
            bindings::free_cpumask_var(self.as_raw())
        };
    }
}
