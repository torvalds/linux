// SPDX-License-Identifier: GPL-2.0

//! DRM GEM shmem helper objects
//!
//! C header: [`include/linux/drm/drm_gem_shmem_helper.h`](srctree/include/drm/drm_gem_shmem_helper.h)

// TODO:
// - There are a number of spots here that manually acquire/release the DMA reservation lock using
//   dma_resv_(un)lock(). In the future we should add support for ww mutex, expose a method to
//   acquire a reference to the WwMutex, and then use that directly instead of the C functions here.

use crate::{
    container_of,
    device::{
        self,
        Bound, //
    },
    devres::*,
    drm::{
        driver,
        gem,
        private::Sealed,
        Device, //
    },
    error::{
        from_err_ptr,
        to_result, //
    },
    io::{
        IoBase,
        Region,
        SysMem,
        SysMemBackend, //
    },
    prelude::*,
    scatterlist,
    sync::{
        aref::ARef,
        new_mutex,
        Mutex,
        SetOnce, //
    },
    types::{
        NotThreadSafe,
        Opaque, //
    },
};
use core::{
    ffi::c_void,
    mem::{
        ManuallyDrop,
        MaybeUninit, //
    },
    ops::{
        Deref,
        DerefMut, //
    },
    ptr::{
        self,
        NonNull, //
    },
};
use gem::{
    BaseObject,
    BaseObjectPrivate,
    DriverObject,
    IntoGEMObject, //
};

/// A struct for controlling the creation of shmem-backed GEM objects.
///
/// This is used with [`Object::new()`] to control various properties that can only be set when
/// initially creating a shmem-backed GEM object.
pub struct ObjectConfig<'a, T: DriverObject> {
    /// Whether to set the write-combine map flag.
    pub map_wc: bool,

    /// Reuse the DMA reservation from another GEM object.
    ///
    /// The newly created [`Object`] will hold an owned refcount to `parent_resv_obj` if specified.
    pub parent_resv_obj: Option<&'a Object<T>>,
}

impl<'a, T: DriverObject> Default for ObjectConfig<'a, T> {
    #[inline(always)]
    fn default() -> Self {
        Self {
            map_wc: false,
            parent_resv_obj: None,
        }
    }
}

/// A shmem-backed GEM object.
///
/// # Invariants
///
/// - `obj` contains a valid initialized `struct drm_gem_shmem_object` for the lifetime of this
///   object.
#[repr(C)]
#[pin_data]
pub struct Object<T: DriverObject> {
    #[pin]
    obj: Opaque<bindings::drm_gem_shmem_object>,
    /// Parent object that owns this object's DMA reservation object.
    parent_resv_obj: Option<ARef<Object<T>>>,
    /// Devres object for unmapping any SGTable on driver-unbind.
    sgt_res: ManuallyDrop<SetOnce<Devres<SGTableMap<T>>>>,
    #[pin]
    /// Lock for protecting initialization of `sgt_res`.
    sgt_lock: Mutex<()>,
    #[pin]
    inner: T,
}

super::impl_aref_for_gem_obj! {
    impl<T> for Object<T>
    where
        T: DriverObject
}

// SAFETY: All GEM objects are thread-safe.
unsafe impl<T: DriverObject> Send for Object<T> {}

// SAFETY: All GEM objects are thread-safe.
unsafe impl<T: DriverObject> Sync for Object<T> {}

impl<T: DriverObject> Object<T> {
    /// `drm_gem_object_funcs` vtable suitable for GEM shmem objects.
    const VTABLE: bindings::drm_gem_object_funcs = bindings::drm_gem_object_funcs {
        free: Some(Self::free_callback),
        open: Some(super::open_callback::<T>),
        close: Some(super::close_callback::<T>),
        print_info: Some(bindings::drm_gem_shmem_object_print_info),
        export: None,
        pin: Some(bindings::drm_gem_shmem_object_pin),
        unpin: Some(bindings::drm_gem_shmem_object_unpin),
        get_sg_table: Some(bindings::drm_gem_shmem_object_get_sg_table),
        vmap: Some(bindings::drm_gem_shmem_object_vmap),
        vunmap: Some(bindings::drm_gem_shmem_object_vunmap),
        mmap: Some(bindings::drm_gem_shmem_object_mmap),
        status: None,
        rss: None,
        #[allow(unused_unsafe, reason = "Safe since Rust 1.82.0")]
        // SAFETY: `drm_gem_shmem_vm_ops` is a valid, static const on the C side.
        vm_ops: unsafe { &raw const bindings::drm_gem_shmem_vm_ops },
        evict: None,
    };

    /// Return a raw pointer to the embedded drm_gem_shmem_object.
    fn as_raw_shmem(&self) -> *mut bindings::drm_gem_shmem_object {
        self.obj.get()
    }

    /// Returns the `Device` that owns this GEM object.
    pub fn dev(&self) -> &Device<T::Driver> {
        // SAFETY: `dev` will have been initialized in `Self::new()` by `drm_gem_shmem_init()`.
        unsafe { Device::from_raw((*self.as_raw()).dev) }
    }

    extern "C" fn free_callback(obj: *mut bindings::drm_gem_object) {
        // SAFETY:
        // - DRM always passes a valid gem object here
        // - We used drm_gem_shmem_create() in our create_gem_object callback, so we know that
        //   `obj` is contained within a drm_gem_shmem_object
        let base = unsafe { container_of!(obj, bindings::drm_gem_shmem_object, base) };

        // SAFETY:
        // - We verified above that `obj` is valid, which makes `this` valid
        // - This function is set in AllocOps, so we know that `this` is contained within an
        //   `Object<T>`
        let this = unsafe { container_of!(Opaque::cast_from(base), Self, obj) }.cast_mut();

        // We need to drop `sgt_res` first, since doing so requires that the GEM object is still
        // alive.
        // SAFETY:
        // - We verified above that `this` is valid.
        // - We are in free_callback, guaranteeing we have exclusive access to `this` and that
        //   `sgt_res` will not be used after dropping it here.
        unsafe { ManuallyDrop::drop(&mut (*this).sgt_res) };

        // SAFETY:
        // - We're in free_callback - so this function is safe to call.
        // - We won't be using the gem resources on `this` after this call.
        unsafe { bindings::drm_gem_shmem_release(base) };

        // SAFETY: We're recovering the Kbox<> we created in gem_create_object()
        let _ = unsafe { KBox::from_raw(this) };
    }

    /// Attempt to create a vmap from the gem object, and confirm the size of said vmap.
    fn make_vmap<'a, R, const SIZE: usize>(&'a self) -> Result<VMap<T, R, SIZE>>
    where
        R: Deref<Target = Self> + From<&'a Self>,
    {
        // INVARIANT: We check here that the gem object is at least as large as `SIZE`.
        if self.size() < SIZE {
            return Err(ENOSPC);
        }

        let mut map: MaybeUninit<bindings::iosys_map> = MaybeUninit::uninit();
        let guard = DmaResvGuard::new(self);

        // SAFETY: `drm_gem_shmem_vmap()` can be called with the DMA reservation lock held.
        to_result(unsafe {
            bindings::drm_gem_shmem_vmap_locked(self.as_raw_shmem(), map.as_mut_ptr())
        })?;

        // Drop the guard explicitly here, since we may need to call `raw_vunmap()` (which
        // re-acquires the lock).
        drop(guard);

        // SAFETY: The call to `drm_gem_shmem_vmap_locked()` succeeded above, so we are guaranteed
        // that map is properly initialized.
        let map = unsafe { map.assume_init() };

        // XXX: We don't currently support iomem allocations
        if map.is_iomem {
            // SAFETY: The vmap operation above succeeded, guaranteeing that `map` points to a valid
            // memory mapping.
            unsafe { self.raw_vunmap(map) };

            Err(ENOTSUPP)
        } else {
            Ok(VMap {
                // INVARIANT: `addr` remains valid for as long as `owner` does, which extends to the
                // lifetime of `VMap` itself.
                // SAFETY: We checked that this is not an iomem allocation, making it safe to read
                // vaddr.
                addr: unsafe { map.__bindgen_anon_1.vaddr },
                owner: self.into(),
            })
        }
    }

    /// Unmap a vmap from the gem object.
    ///
    /// # Safety
    ///
    /// - The caller promises that `map` is a valid vmap on this gem object.
    /// - The caller promises that the memory pointed to by map will no longer be accesed through
    ///   this instance.
    unsafe fn raw_vunmap(&self, mut map: bindings::iosys_map) {
        let _guard = DmaResvGuard::new(self);

        // SAFETY:
        // - This function is safe to call with the DMA reservation lock held.
        // - The caller promises that `map` is a valid vmap on this gem object.
        unsafe { bindings::drm_gem_shmem_vunmap_locked(self.as_raw_shmem(), &mut map) };
    }

    /// Creates and returns a virtual kernel memory mapping for this object.
    #[inline]
    pub fn vmap<const SIZE: usize>(&self) -> Result<VMapRef<'_, T, SIZE>> {
        self.make_vmap()
    }

    /// Creates (if necessary) and returns an immutable reference to a scatter-gather table of DMA
    /// pages for this object.
    ///
    /// This will pin the object in memory. It is expected that `dev` should be a pointer to the
    /// same [`device::Device`] which `self` belongs to, otherwise this function will return
    /// `Err(EINVAL)`.
    pub fn sg_table<'a>(
        &'a self,
        dev: &'a device::Device<Bound>,
    ) -> Result<&'a scatterlist::SGTable> {
        let parent = self.dev().as_ref();
        if dev.as_raw() != parent.as_ref().as_raw() {
            return Err(EINVAL);
        }

        let sgt_res = 'out: {
            // Fast path: sgt_res is already initialized
            if let Some(sgt_res) = self.sgt_res.as_ref() {
                break 'out sgt_res;
            }

            // Slow path: Grab the lock and see if we need to initialize sgt_res.
            let _guard = self.sgt_lock.lock();

            // If someone initialized it while we were waiting, we can exit early.
            if let Some(sgt_res) = self.sgt_res.as_ref() {
                break 'out sgt_res;
            }

            // If not, finish initializing and return. `populate()` cannot return false, as
            // `sgt_res` must be unpopulated, and we must hold `sgt_lock` to reach this point.
            self.sgt_res
                .populate(Devres::new(dev, SGTableMap::new(self))?);

            // SAFETY: We just populated sgt_res above.
            unsafe { self.sgt_res.as_ref().unwrap_unchecked() }
        };

        Ok(sgt_res.access(dev)?)
    }

    /// Create a new shmem-backed DRM object of the given size.
    ///
    /// Additional config options can be specified using `config`.
    pub fn new(
        dev: &Device<T::Driver>,
        size: usize,
        config: ObjectConfig<'_, T>,
        args: T::Args,
    ) -> Result<ARef<Self>> {
        let new: Pin<KBox<Self>> = KBox::try_pin_init(
            try_pin_init!(Self {
                obj <- Opaque::init_zeroed(),
                parent_resv_obj: config.parent_resv_obj.map(|p| p.into()),
                sgt_res: ManuallyDrop::new(SetOnce::new()),
                sgt_lock <- new_mutex!(()),
                inner <- T::new(dev, size, args),
            }),
            GFP_KERNEL,
        )?;

        // SAFETY: `obj.as_raw()` is guaranteed to be valid by the initialization above.
        unsafe { (*new.as_raw()).funcs = &Self::VTABLE };

        // SAFETY: The arguments are all valid via the type invariants.
        to_result(unsafe { bindings::drm_gem_shmem_init(dev.as_raw(), new.as_raw_shmem(), size) })?;

        // SAFETY: We never move out of `self`.
        let new = KBox::into_raw(unsafe { Pin::into_inner_unchecked(new) });

        // SAFETY: We're taking over the owned refcount from `drm_gem_shmem_init`.
        let obj = unsafe { ARef::from_raw(NonNull::new_unchecked(new)) };

        // Start filling out values from `config`
        if let Some(parent_resv) = config.parent_resv_obj {
            // SAFETY: We have yet to expose the new gem object outside of this function, so it is
            // safe to modify this field.
            unsafe { (*obj.obj.get()).base.resv = parent_resv.raw_dma_resv() };
        }

        // SAFETY: We have yet to expose this object outside of this function, so we're guaranteed
        // to have exclusive access - thus making this safe to hold a mutable reference to.
        let shmem = unsafe { &mut *obj.as_raw_shmem() };
        shmem.set_map_wc(config.map_wc);

        Ok(obj)
    }

    /// Creates and returns an owned reference to a virtual kernel memory mapping for this object.
    #[inline]
    pub fn owned_vmap<const SIZE: usize>(&self) -> Result<VMapOwned<T, SIZE>> {
        self.make_vmap()
    }
}

impl<T: DriverObject> Deref for Object<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<T: DriverObject> DerefMut for Object<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

impl<T: DriverObject> Sealed for Object<T> {}

impl<T: DriverObject> gem::IntoGEMObject for Object<T> {
    fn as_raw(&self) -> *mut bindings::drm_gem_object {
        // SAFETY:
        // - Our immutable reference is proof that this is safe to dereference.
        // - `obj` is always a valid drm_gem_shmem_object via our type invariants.
        unsafe { &raw mut (*self.obj.get()).base }
    }

    unsafe fn from_raw<'a>(obj: *mut bindings::drm_gem_object) -> &'a Self {
        // SAFETY: The safety contract of from_gem_obj() guarantees that `obj` is contained within
        // `Self`
        unsafe {
            let obj = Opaque::cast_from(container_of!(obj, bindings::drm_gem_shmem_object, base));

            &*container_of!(obj, Self, obj)
        }
    }
}

impl<T: DriverObject> driver::AllocImpl for Object<T> {
    type Driver = T::Driver;

    const ALLOC_OPS: driver::AllocOps = driver::AllocOps {
        gem_create_object: None,
        prime_handle_to_fd: None,
        prime_fd_to_handle: None,
        gem_prime_import: None,
        gem_prime_import_sg_table: Some(bindings::drm_gem_shmem_prime_import_sg_table),
        dumb_create: Some(bindings::drm_gem_shmem_dumb_create),
        dumb_map_offset: None,
    };
}

/// Private helper-type for holding the `dma_resv` object for a GEM shmem object.
///
/// When this is dropped, the `dma_resv` lock is dropped as well.
///
// TODO: This should be replace with a WwMutex equivalent once we have such bindings in the kernel.
struct DmaResvGuard<'a, T: DriverObject>(&'a Object<T>, NotThreadSafe);

impl<'a, T: DriverObject> DmaResvGuard<'a, T> {
    #[inline]
    fn new(obj: &'a Object<T>) -> Self {
        // SAFETY: This lock is initialized throughout the lifetime of `object`.
        unsafe { bindings::dma_resv_lock(obj.raw_dma_resv(), ptr::null_mut()) };

        Self(obj, NotThreadSafe)
    }
}

impl<'a, T: DriverObject> Drop for DmaResvGuard<'a, T> {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: We are releasing the lock grabbed during the creation of this object.
        unsafe { bindings::dma_resv_unlock(self.0.raw_dma_resv()) };
    }
}

/// A reference to a virtual mapping for an shmem-based GEM object in kernel address space.
///
/// # Invariants
///
/// - The size of `owner` is >= SIZE.
/// - The memory pointed to by `addr` remains valid at least until this object is dropped.
pub struct VMap<D, R, const SIZE: usize = 0>
where
    D: DriverObject,
    R: Deref<Target = Object<D>>,
{
    addr: *mut c_void,
    owner: R,
}

/// An alias type for a reference to a shmem-based GEM object's VMap.
pub type VMapRef<'a, D, const SIZE: usize = 0> = VMap<D, &'a Object<D>, SIZE>;

/// An alias type for an owned reference to a shmem-based GEM object's VMap.
pub type VMapOwned<D, const SIZE: usize = 0> = VMap<D, ARef<Object<D>>, SIZE>;

impl<D, R, const SIZE: usize> VMap<D, R, SIZE>
where
    D: DriverObject,
    R: Deref<Target = Object<D>>,
{
    /// Borrows a reference to the object that owns this virtual mapping.
    #[inline]
    pub fn owner(&self) -> &Object<D> {
        &self.owner
    }
}

impl<'a, D, R, const SIZE: usize> IoBase<'a> for &'a VMap<D, R, SIZE>
where
    D: DriverObject,
    R: Deref<Target = Object<D>>,
{
    type Backend = SysMemBackend;
    type Target = Region<SIZE>;

    #[inline]
    fn as_view(self) -> SysMem<'a, Region<SIZE>> {
        let ptr = Region::ptr_from_raw_parts_mut(self.addr.cast(), self.owner.size());

        // SAFETY: Per type invariants of `VMap`:
        // - `addr .. addr + owner.size()` is a valid kernel accessible memory region.
        // - `addr` is page-aligned, which satisfies `Region`'s 4-byte alignment requirement.
        // - The memory remains valid until this `VMap` is dropped; since `self` is `&'a VMap`,
        //   the borrow prevents the `VMap` from being dropped for the lifetime `'a`.
        unsafe { SysMem::new(ptr) }
    }
}

impl<D, R, const SIZE: usize> Drop for VMap<D, R, SIZE>
where
    D: DriverObject,
    R: Deref<Target = Object<D>>,
{
    #[inline]
    fn drop(&mut self) {
        // SAFETY:
        // - Our existence is proof that this map was previously created using self.owner.
        // - Since we are in Drop, we are guaranteed that no one will access the memory
        //   through this mapping after calling this.
        unsafe {
            self.owner.raw_vunmap(bindings::iosys_map {
                is_iomem: false,
                __bindgen_anon_1: bindings::iosys_map__bindgen_ty_1 { vaddr: self.addr },
            })
        };
    }
}

// SAFETY: `addr` points to a valid memory address for as long as `owner` exists, meaning that so
// long as `owner` is `Send` so is `VMap`.
unsafe impl<D, R, const SIZE: usize> Send for VMap<D, R, SIZE>
where
    D: DriverObject,
    R: Deref<Target = Object<D>> + Send,
{
}

// SAFETY: `addr` points to a valid memory address for as long as `owner` exists, meaning that so
// long as `owner` is `Sync` so is `VMap`.
unsafe impl<D, R, const SIZE: usize> Sync for VMap<D, R, SIZE>
where
    D: DriverObject,
    R: Deref<Target = Object<D>> + Sync,
{
}

/// A reference to a GEM object that is known to have a mapped [`SGTable`].
///
/// This is used by the Rust bindings with [`Devres`] in order to ensure that mappings for SGTables
/// on GEM shmem objects are revoked on driver-unbind.
///
/// # Invariants
///
/// - `self.obj` always points to a valid GEM object.
/// - This object is proof that `self.obj.owner.sgt_res` has an initialized and valid pointer to an
///   [`SGTable`].
///
/// [`SGTable`]: scatterlist::SGTable
pub struct SGTableMap<T: DriverObject> {
    obj: NonNull<Object<T>>,
}

impl<T: DriverObject> Deref for SGTableMap<T> {
    type Target = scatterlist::SGTable;

    fn deref(&self) -> &Self::Target {
        // SAFETY:
        // - The NonNull is guaranteed to be valid via our type invariants.
        // - The sgt field is guaranteed to be initialized and valid via our type invariants.
        unsafe { scatterlist::SGTable::from_raw((*self.obj.as_ref().as_raw_shmem()).sgt) }
    }
}

impl<T: DriverObject> Drop for SGTableMap<T> {
    fn drop(&mut self) {
        // SAFETY: `obj` is always valid via our type invariants
        let obj = unsafe { self.obj.as_ref() };
        let _lock = DmaResvGuard::new(obj);

        // SAFETY: We acquired the lock needed for calling this function above
        unsafe { bindings::__drm_gem_shmem_free_sgt_locked(obj.as_raw_shmem()) };
    }
}

impl<T: DriverObject> SGTableMap<T> {
    fn new(obj: &Object<T>) -> impl Init<Self, Error> {
        // INVARIANT:
        // - We call drm_gem_shmem_get_pages_sgt below and check whether or not it succeeds,
        //   fulfilling the invariant of SGTableMap that the object's `sgt` field is initialized.
        // SAFETY:
        // - `obj` is fully initialized, making this function safe to call.
        from_err_ptr(unsafe { bindings::drm_gem_shmem_get_pages_sgt(obj.as_raw_shmem()) })?;

        Ok(Self { obj: obj.into() })
    }
}

// SAFETY: The NonNull in SGTableMap is guaranteed valid by our type invariants, and the GEM object
// it points to is guaranteed to be thread-safe.
unsafe impl<T: DriverObject> Send for SGTableMap<T> {}
// SAFETY: The NonNull in SGTableMap is guaranteed valid by our type invariants, and the GEM object
// it points to is guaranteed to be thread-safe.
unsafe impl<T: DriverObject> Sync for SGTableMap<T> {}

#[kunit_tests(rust_drm_gem_shmem)]
mod tests {
    use super::*;
    use crate::{
        drm::{
            self,
            UnregisteredDevice, //
        },
        faux,
        io::Io,
        page::PAGE_SIZE, //
    };

    // The bare minimum needed to create a fake drm driver for kunit

    #[pin_data]
    struct KunitData {}
    struct KunitDriver;
    struct KunitFile;
    #[pin_data]
    struct KunitObject {}

    const INFO: drm::DriverInfo = drm::DriverInfo {
        major: 0,
        minor: 0,
        patchlevel: 0,
        name: c"kunit",
        desc: c"Kunit",
    };

    impl drm::file::DriverFile for KunitFile {
        type Driver = KunitDriver;

        fn open(_dev: &drm::Device<KunitDriver>) -> Result<Pin<KBox<Self>>> {
            Ok(KBox::new(Self, GFP_KERNEL)?.into())
        }
    }

    impl gem::DriverObject for KunitObject {
        type Driver = KunitDriver;
        type Args = ();

        fn new(
            _dev: &drm::Device<KunitDriver>,
            _size: usize,
            _args: Self::Args,
        ) -> impl PinInit<Self, Error> {
            try_pin_init!(KunitObject {})
        }
    }

    #[vtable]
    impl drm::Driver for KunitDriver {
        type Data = KunitData;
        type RegistrationData<'a> = ();
        type File = KunitFile;
        type Object = Object<KunitObject>;
        type ParentDevice<Ctx: device::DeviceContext> = faux::Device<Ctx>;

        const INFO: drm::DriverInfo = INFO;
        const IOCTLS: &'static [drm::ioctl::DrmIoctlDescriptor] = &[];
    }

    fn create_drm_dev() -> Result<(faux::Registration, UnregisteredDevice<KunitDriver>)> {
        // Create a faux DRM device so we can test gem object creation.
        let data = try_pin_init!(KunitData {});
        let reg = faux::Registration::new(c"Kunit", None)?;
        let fdev = reg.as_ref();
        let drm = UnregisteredDevice::new(fdev, data)?;

        Ok((reg, drm))
    }

    #[test]
    fn compile_time_vmap_sizes() -> Result {
        let (_dev, drm) = create_drm_dev()?;

        let obj = Object::<KunitObject>::new(&drm, PAGE_SIZE, ObjectConfig::default(), ())?;

        // Try creating a normal vmap
        obj.vmap::<PAGE_SIZE>()?;

        // Try creating a vmap that's smaller then the size we specified
        let vmap = obj.vmap::<{ PAGE_SIZE - 100 }>()?;

        // Verify the owner matches
        assert!(ptr::eq(vmap.owner(), obj.deref()));

        // Verify the size matches the actual object size
        assert_eq!(vmap.size(), PAGE_SIZE);

        // Make sure creating a vmap that's too large fails
        assert!(obj.vmap::<{ PAGE_SIZE + 200 }>().is_err());

        Ok(())
    }

    #[test]
    fn vmap_io() -> Result {
        let (_dev, drm) = create_drm_dev()?;

        let obj = Object::<KunitObject>::new(&drm, PAGE_SIZE, ObjectConfig::default(), ())?;

        let vmap = obj.vmap::<PAGE_SIZE>()?;

        vmap.write8(0xDE, 0x0);
        assert_eq!(vmap.read8(0x0), 0xDE);
        vmap.write32(0xFEDCBA98, 0x20);

        assert_eq!(vmap.read32(0x20), 0xFEDCBA98);

        // Ensure the ordering in memory is correct
        let expected = 0xFEDCBA98_u32.to_ne_bytes().into_iter();
        for (offset, expected) in (0x20..=0x23).zip(expected) {
            assert_eq!(vmap.try_read8(offset).unwrap(), expected);
        }

        Ok(())
    }

    // TODO: I would love to actually test the success paths of sg_table(), but that would require
    // also implementing dummy dma_ops so that trying to create a mapping doesn't explode. So, leave
    // that for someone else.

    // Ensures that passing the wrong device to sg_table() fails as we expect, and also ensure it
    // skips initializing `sgt_res` since we could otherwise create `sgt_res` with the wrong device
    // bound to it.
    #[test]
    fn fail_sg_table_on_wrong_dev() -> Result {
        let (_dev, drm) = create_drm_dev()?;
        let reg = faux::Registration::new(c"EvilKunit", None)?;
        let wrong_dev = reg.as_ref();

        let obj = Object::<KunitObject>::new(&drm, PAGE_SIZE, ObjectConfig::default(), ())?;

        assert_eq!(obj.sg_table(wrong_dev.as_ref()).err().unwrap(), EINVAL);

        // If sgt_res was not initialized mistakenly with the wrong device, this should still fail.
        assert_eq!(obj.sg_table(wrong_dev.as_ref()).err().unwrap(), EINVAL);

        // TODO: Someday, we should test that creating an sg_table here still succeeds.

        Ok(())
    }
}
