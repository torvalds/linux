// SPDX-License-Identifier: GPL-2.0 OR MIT

use super::*;

/// Represents that a range of a GEM object is mapped in this [`GpuVm`] instance.
///
/// Does not assume that GEM lock is held.
///
/// # Invariants
///
/// * This is a valid `drm_gpuva` object that is resident in a [`GpuVm<T>`] instance.
/// * It is associated with a [`GpuVmBo<T>`]. Or in other words, it's not an
///   `gpuvm->kernel_alloc_node` and `DRM_GPUVA_SPARSE` is not set.
/// * The associated [`GpuVmBo<T>`] is part of the GEM list.
#[repr(C)]
#[pin_data]
pub struct GpuVa<T: DriverGpuVm> {
    #[pin]
    inner: Opaque<bindings::drm_gpuva>,
    #[pin]
    data: T::VaData,
}

impl<T: DriverGpuVm> PartialEq for GpuVa<T> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        core::ptr::eq(self.as_raw(), other.as_raw())
    }
}
impl<T: DriverGpuVm> Eq for GpuVa<T> {}

impl<T: DriverGpuVm> GpuVa<T> {
    /// Access this [`GpuVa`] from a raw pointer.
    ///
    /// # Safety
    ///
    /// * For the duration of `'a`, the pointer must reference a valid `drm_gpuva` associated with
    ///   a [`GpuVm<T>`].
    /// * It must be associated with a [`GpuVmBo<T>`].
    /// * The associated [`GpuVmBo<T>`] is part of the GEM list.
    #[inline]
    pub unsafe fn from_raw<'a>(ptr: *mut bindings::drm_gpuva) -> &'a Self {
        // CAST: `drm_gpuva` is first field and `repr(C)`.
        // SAFETY: The safety requirements match the invariants of `GpuVa`.
        unsafe { &*ptr.cast() }
    }

    /// Returns a raw pointer to underlying C value.
    #[inline]
    pub fn as_raw(&self) -> *mut bindings::drm_gpuva {
        self.inner.get()
    }

    /// Returns the address of this mapping in the GPU virtual address space.
    #[inline]
    pub fn addr(&self) -> u64 {
        // SAFETY: The `va.addr` field of `drm_gpuva` is immutable.
        unsafe { (*self.as_raw()).va.addr }
    }

    /// Returns the length of this mapping.
    #[inline]
    pub fn length(&self) -> u64 {
        // SAFETY: The `va.range` field of `drm_gpuva` is immutable.
        unsafe { (*self.as_raw()).va.range }
    }

    /// Returns `addr..addr+length`.
    #[inline]
    pub fn range(&self) -> Range<u64> {
        let addr = self.addr();
        addr..addr + self.length()
    }

    /// Returns the offset within the GEM object.
    #[inline]
    pub fn gem_offset(&self) -> u64 {
        // SAFETY: The `gem.offset` field of `drm_gpuva` is immutable.
        unsafe { (*self.as_raw()).gem.offset }
    }

    /// Returns the GEM object.
    #[inline]
    pub fn obj(&self) -> &T::Object {
        // SAFETY: The `gem.obj` field of `drm_gpuva` is immutable. We know that it's not null
        // because this VA is associated with a `GpuVmBo<T>`.
        unsafe { <T::Object as IntoGEMObject>::from_raw((*self.as_raw()).gem.obj) }
    }

    /// Returns the underlying [`GpuVmBo`] object that backs this [`GpuVa`].
    #[inline]
    pub fn vm_bo(&self) -> &GpuVmBo<T> {
        // SAFETY: The `vm_bo` field of `drm_gpuva` is immutable. We know that it's not null
        // because this VA is associated with a `GpuVmBo<T>`. The BO is in the GEM list by the type
        // invariants.
        unsafe { GpuVmBo::from_raw((*self.as_raw()).vm_bo) }
    }
}

/// A pre-allocated [`GpuVa`] object.
///
/// # Invariants
///
/// The memory is zeroed.
pub struct GpuVaAlloc<T: DriverGpuVm>(KBox<MaybeUninit<GpuVa<T>>>);

impl<T: DriverGpuVm> GpuVaAlloc<T> {
    /// Pre-allocate a [`GpuVa`] object.
    pub fn new(flags: AllocFlags) -> Result<GpuVaAlloc<T>, AllocError> {
        // INVARIANTS: Memory allocated with __GFP_ZERO.
        Ok(GpuVaAlloc(KBox::new_uninit(flags | __GFP_ZERO)?))
    }

    /// Prepare this `drm_gpuva` for insertion into the GPUVM.
    #[must_use]
    pub(super) fn prepare(mut self, va_data: impl PinInit<T::VaData>) -> *mut bindings::drm_gpuva {
        let va_ptr = MaybeUninit::as_mut_ptr(&mut self.0);
        // SAFETY: The `data` field is pinned.
        let Ok(()) = unsafe { va_data.__pinned_init(&raw mut (*va_ptr).data) };
        KBox::into_raw(self.0).cast()
    }
}

/// A [`GpuVa`] object that has been removed.
///
/// # Invariants
///
/// The `drm_gpuva` is not resident in the [`GpuVm`].
pub struct GpuVaRemoved<T: DriverGpuVm>(KBox<GpuVa<T>>);

impl<T: DriverGpuVm> GpuVaRemoved<T> {
    /// Convert a raw pointer into a [`GpuVaRemoved`].
    ///
    /// # Safety
    ///
    /// * Must have been removed from a [`GpuVm<T>`].
    /// * It must not be a `gpuvm->kernel_alloc_node` va.
    pub(super) unsafe fn from_raw(ptr: *mut bindings::drm_gpuva) -> Self {
        // SAFETY: Since it used to be a VA in a `GpuVm<T>` and it's not a kernel_alloc_node, this
        // pointer references a `GpuVa<T>` with a valid `T::VaData`. Since it has been removed, we
        // can take ownership of the allocation.
        GpuVaRemoved(unsafe { KBox::from_raw(ptr.cast()) })
    }

    /// Take ownership of the VA data.
    pub fn into_inner(self) -> T::VaData
    where
        T::VaData: Unpin,
    {
        KBox::into_inner(self.0).data
    }
}

impl<T: DriverGpuVm> Deref for GpuVaRemoved<T> {
    type Target = T::VaData;
    fn deref(&self) -> &T::VaData {
        &self.0.data
    }
}

impl<T: DriverGpuVm> DerefMut for GpuVaRemoved<T>
where
    T::VaData: Unpin,
{
    fn deref_mut(&mut self) -> &mut T::VaData {
        &mut self.0.data
    }
}
