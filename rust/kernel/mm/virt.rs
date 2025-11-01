// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Virtual memory.
//!
//! This module deals with managing a single VMA in the address space of a userspace process. Each
//! VMA corresponds to a region of memory that the userspace process can access, and the VMA lets
//! you control what happens when userspace reads or writes to that region of memory.
//!
//! The module has several different Rust types that all correspond to the C type called
//! `vm_area_struct`. The different structs represent what kind of access you have to the VMA, e.g.
//! [`VmaRef`] is used when you hold the mmap or vma read lock. Using the appropriate struct
//! ensures that you can't, for example, accidentally call a function that requires holding the
//! write lock when you only hold the read lock.

use crate::{
    bindings,
    error::{code::EINVAL, to_result, Result},
    mm::MmWithUser,
    page::Page,
    types::Opaque,
};

use core::ops::Deref;

/// A wrapper for the kernel's `struct vm_area_struct` with read access.
///
/// It represents an area of virtual memory.
///
/// # Invariants
///
/// The caller must hold the mmap read lock or the vma read lock.
#[repr(transparent)]
pub struct VmaRef {
    vma: Opaque<bindings::vm_area_struct>,
}

// Methods you can call when holding the mmap or vma read lock (or stronger). They must be usable
// no matter what the vma flags are.
impl VmaRef {
    /// Access a virtual memory area given a raw pointer.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `vma` is valid for the duration of 'a, and that the mmap or vma
    /// read lock (or stronger) is held for at least the duration of 'a.
    #[inline]
    pub unsafe fn from_raw<'a>(vma: *const bindings::vm_area_struct) -> &'a Self {
        // SAFETY: The caller ensures that the invariants are satisfied for the duration of 'a.
        unsafe { &*vma.cast() }
    }

    /// Returns a raw pointer to this area.
    #[inline]
    pub fn as_ptr(&self) -> *mut bindings::vm_area_struct {
        self.vma.get()
    }

    /// Access the underlying `mm_struct`.
    #[inline]
    pub fn mm(&self) -> &MmWithUser {
        // SAFETY: By the type invariants, this `vm_area_struct` is valid and we hold the mmap/vma
        // read lock or stronger. This implies that the underlying mm has a non-zero value of
        // `mm_users`.
        unsafe { MmWithUser::from_raw((*self.as_ptr()).vm_mm) }
    }

    /// Returns the flags associated with the virtual memory area.
    ///
    /// The possible flags are a combination of the constants in [`flags`].
    #[inline]
    pub fn flags(&self) -> vm_flags_t {
        // SAFETY: By the type invariants, the caller holds at least the mmap read lock, so this
        // access is not a data race.
        unsafe { (*self.as_ptr()).__bindgen_anon_2.vm_flags }
    }

    /// Returns the (inclusive) start address of the virtual memory area.
    #[inline]
    pub fn start(&self) -> usize {
        // SAFETY: By the type invariants, the caller holds at least the mmap read lock, so this
        // access is not a data race.
        unsafe { (*self.as_ptr()).__bindgen_anon_1.__bindgen_anon_1.vm_start }
    }

    /// Returns the (exclusive) end address of the virtual memory area.
    #[inline]
    pub fn end(&self) -> usize {
        // SAFETY: By the type invariants, the caller holds at least the mmap read lock, so this
        // access is not a data race.
        unsafe { (*self.as_ptr()).__bindgen_anon_1.__bindgen_anon_1.vm_end }
    }

    /// Zap pages in the given page range.
    ///
    /// This clears page table mappings for the range at the leaf level, leaving all other page
    /// tables intact, and freeing any memory referenced by the VMA in this range. That is,
    /// anonymous memory is completely freed, file-backed memory has its reference count on page
    /// cache folio's dropped, any dirty data will still be written back to disk as usual.
    ///
    /// It may seem odd that we clear at the leaf level, this is however a product of the page
    /// table structure used to map physical memory into a virtual address space - each virtual
    /// address actually consists of a bitmap of array indices into page tables, which form a
    /// hierarchical page table level structure.
    ///
    /// As a result, each page table level maps a multiple of page table levels below, and thus
    /// span ever increasing ranges of pages. At the leaf or PTE level, we map the actual physical
    /// memory.
    ///
    /// It is here where a zap operates, as it the only place we can be certain of clearing without
    /// impacting any other virtual mappings. It is an implementation detail as to whether the
    /// kernel goes further in freeing unused page tables, but for the purposes of this operation
    /// we must only assume that the leaf level is cleared.
    #[inline]
    pub fn zap_page_range_single(&self, address: usize, size: usize) {
        let (end, did_overflow) = address.overflowing_add(size);
        if did_overflow || address < self.start() || self.end() < end {
            // TODO: call WARN_ONCE once Rust version of it is added
            return;
        }

        // SAFETY: By the type invariants, the caller has read access to this VMA, which is
        // sufficient for this method call. This method has no requirements on the vma flags. The
        // address range is checked to be within the vma.
        unsafe {
            bindings::zap_page_range_single(self.as_ptr(), address, size, core::ptr::null_mut())
        };
    }

    /// If the [`VM_MIXEDMAP`] flag is set, returns a [`VmaMixedMap`] to this VMA, otherwise
    /// returns `None`.
    ///
    /// This can be used to access methods that require [`VM_MIXEDMAP`] to be set.
    ///
    /// [`VM_MIXEDMAP`]: flags::MIXEDMAP
    #[inline]
    pub fn as_mixedmap_vma(&self) -> Option<&VmaMixedMap> {
        if self.flags() & flags::MIXEDMAP != 0 {
            // SAFETY: We just checked that `VM_MIXEDMAP` is set. All other requirements are
            // satisfied by the type invariants of `VmaRef`.
            Some(unsafe { VmaMixedMap::from_raw(self.as_ptr()) })
        } else {
            None
        }
    }
}

/// A wrapper for the kernel's `struct vm_area_struct` with read access and [`VM_MIXEDMAP`] set.
///
/// It represents an area of virtual memory.
///
/// This struct is identical to [`VmaRef`] except that it must only be used when the
/// [`VM_MIXEDMAP`] flag is set on the vma.
///
/// # Invariants
///
/// The caller must hold the mmap read lock or the vma read lock. The `VM_MIXEDMAP` flag must be
/// set.
///
/// [`VM_MIXEDMAP`]: flags::MIXEDMAP
#[repr(transparent)]
pub struct VmaMixedMap {
    vma: VmaRef,
}

// Make all `VmaRef` methods available on `VmaMixedMap`.
impl Deref for VmaMixedMap {
    type Target = VmaRef;

    #[inline]
    fn deref(&self) -> &VmaRef {
        &self.vma
    }
}

impl VmaMixedMap {
    /// Access a virtual memory area given a raw pointer.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `vma` is valid for the duration of 'a, and that the mmap read lock
    /// (or stronger) is held for at least the duration of 'a. The `VM_MIXEDMAP` flag must be set.
    #[inline]
    pub unsafe fn from_raw<'a>(vma: *const bindings::vm_area_struct) -> &'a Self {
        // SAFETY: The caller ensures that the invariants are satisfied for the duration of 'a.
        unsafe { &*vma.cast() }
    }

    /// Maps a single page at the given address within the virtual memory area.
    ///
    /// This operation does not take ownership of the page.
    #[inline]
    pub fn vm_insert_page(&self, address: usize, page: &Page) -> Result {
        // SAFETY: By the type invariant of `Self` caller has read access and has verified that
        // `VM_MIXEDMAP` is set. By invariant on `Page` the page has order 0.
        to_result(unsafe { bindings::vm_insert_page(self.as_ptr(), address, page.as_ptr()) })
    }
}

/// A configuration object for setting up a VMA in an `f_ops->mmap()` hook.
///
/// The `f_ops->mmap()` hook is called when a new VMA is being created, and the hook is able to
/// configure the VMA in various ways to fit the driver that owns it. Using `VmaNew` indicates that
/// you are allowed to perform operations on the VMA that can only be performed before the VMA is
/// fully initialized.
///
/// # Invariants
///
/// For the duration of 'a, the referenced vma must be undergoing initialization in an
/// `f_ops->mmap()` hook.
#[repr(transparent)]
pub struct VmaNew {
    vma: VmaRef,
}

// Make all `VmaRef` methods available on `VmaNew`.
impl Deref for VmaNew {
    type Target = VmaRef;

    #[inline]
    fn deref(&self) -> &VmaRef {
        &self.vma
    }
}

impl VmaNew {
    /// Access a virtual memory area given a raw pointer.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `vma` is undergoing initial vma setup for the duration of 'a.
    #[inline]
    pub unsafe fn from_raw<'a>(vma: *mut bindings::vm_area_struct) -> &'a Self {
        // SAFETY: The caller ensures that the invariants are satisfied for the duration of 'a.
        unsafe { &*vma.cast() }
    }

    /// Internal method for updating the vma flags.
    ///
    /// # Safety
    ///
    /// This must not be used to set the flags to an invalid value.
    #[inline]
    unsafe fn update_flags(&self, set: vm_flags_t, unset: vm_flags_t) {
        let mut flags = self.flags();
        flags |= set;
        flags &= !unset;

        // SAFETY: This is not a data race: the vma is undergoing initial setup, so it's not yet
        // shared. Additionally, `VmaNew` is `!Sync`, so it cannot be used to write in parallel.
        // The caller promises that this does not set the flags to an invalid value.
        unsafe { (*self.as_ptr()).__bindgen_anon_2.__vm_flags = flags };
    }

    /// Set the `VM_MIXEDMAP` flag on this vma.
    ///
    /// This enables the vma to contain both `struct page` and pure PFN pages. Returns a reference
    /// that can be used to call `vm_insert_page` on the vma.
    #[inline]
    pub fn set_mixedmap(&self) -> &VmaMixedMap {
        // SAFETY: We don't yet provide a way to set VM_PFNMAP, so this cannot put the flags in an
        // invalid state.
        unsafe { self.update_flags(flags::MIXEDMAP, 0) };

        // SAFETY: We just set `VM_MIXEDMAP` on the vma.
        unsafe { VmaMixedMap::from_raw(self.vma.as_ptr()) }
    }

    /// Set the `VM_IO` flag on this vma.
    ///
    /// This is used for memory mapped IO and similar. The flag tells other parts of the kernel to
    /// avoid looking at the pages. For memory mapped IO this is useful as accesses to the pages
    /// could have side effects.
    #[inline]
    pub fn set_io(&self) {
        // SAFETY: Setting the VM_IO flag is always okay.
        unsafe { self.update_flags(flags::IO, 0) };
    }

    /// Set the `VM_DONTEXPAND` flag on this vma.
    ///
    /// This prevents the vma from being expanded with `mremap()`.
    #[inline]
    pub fn set_dontexpand(&self) {
        // SAFETY: Setting the VM_DONTEXPAND flag is always okay.
        unsafe { self.update_flags(flags::DONTEXPAND, 0) };
    }

    /// Set the `VM_DONTCOPY` flag on this vma.
    ///
    /// This prevents the vma from being copied on fork. This option is only permanent if `VM_IO`
    /// is set.
    #[inline]
    pub fn set_dontcopy(&self) {
        // SAFETY: Setting the VM_DONTCOPY flag is always okay.
        unsafe { self.update_flags(flags::DONTCOPY, 0) };
    }

    /// Set the `VM_DONTDUMP` flag on this vma.
    ///
    /// This prevents the vma from being included in core dumps. This option is only permanent if
    /// `VM_IO` is set.
    #[inline]
    pub fn set_dontdump(&self) {
        // SAFETY: Setting the VM_DONTDUMP flag is always okay.
        unsafe { self.update_flags(flags::DONTDUMP, 0) };
    }

    /// Returns whether `VM_READ` is set.
    ///
    /// This flag indicates whether userspace is mapping this vma as readable.
    #[inline]
    pub fn readable(&self) -> bool {
        (self.flags() & flags::READ) != 0
    }

    /// Try to clear the `VM_MAYREAD` flag, failing if `VM_READ` is set.
    ///
    /// This flag indicates whether userspace is allowed to make this vma readable with
    /// `mprotect()`.
    ///
    /// Note that this operation is irreversible. Once `VM_MAYREAD` has been cleared, it can never
    /// be set again.
    #[inline]
    pub fn try_clear_mayread(&self) -> Result {
        if self.readable() {
            return Err(EINVAL);
        }
        // SAFETY: Clearing `VM_MAYREAD` is okay when `VM_READ` is not set.
        unsafe { self.update_flags(0, flags::MAYREAD) };
        Ok(())
    }

    /// Returns whether `VM_WRITE` is set.
    ///
    /// This flag indicates whether userspace is mapping this vma as writable.
    #[inline]
    pub fn writable(&self) -> bool {
        (self.flags() & flags::WRITE) != 0
    }

    /// Try to clear the `VM_MAYWRITE` flag, failing if `VM_WRITE` is set.
    ///
    /// This flag indicates whether userspace is allowed to make this vma writable with
    /// `mprotect()`.
    ///
    /// Note that this operation is irreversible. Once `VM_MAYWRITE` has been cleared, it can never
    /// be set again.
    #[inline]
    pub fn try_clear_maywrite(&self) -> Result {
        if self.writable() {
            return Err(EINVAL);
        }
        // SAFETY: Clearing `VM_MAYWRITE` is okay when `VM_WRITE` is not set.
        unsafe { self.update_flags(0, flags::MAYWRITE) };
        Ok(())
    }

    /// Returns whether `VM_EXEC` is set.
    ///
    /// This flag indicates whether userspace is mapping this vma as executable.
    #[inline]
    pub fn executable(&self) -> bool {
        (self.flags() & flags::EXEC) != 0
    }

    /// Try to clear the `VM_MAYEXEC` flag, failing if `VM_EXEC` is set.
    ///
    /// This flag indicates whether userspace is allowed to make this vma executable with
    /// `mprotect()`.
    ///
    /// Note that this operation is irreversible. Once `VM_MAYEXEC` has been cleared, it can never
    /// be set again.
    #[inline]
    pub fn try_clear_mayexec(&self) -> Result {
        if self.executable() {
            return Err(EINVAL);
        }
        // SAFETY: Clearing `VM_MAYEXEC` is okay when `VM_EXEC` is not set.
        unsafe { self.update_flags(0, flags::MAYEXEC) };
        Ok(())
    }
}

/// The integer type used for vma flags.
#[doc(inline)]
pub use bindings::vm_flags_t;

/// All possible flags for [`VmaRef`].
pub mod flags {
    use super::vm_flags_t;
    use crate::bindings;

    /// No flags are set.
    pub const NONE: vm_flags_t = bindings::VM_NONE as vm_flags_t;

    /// Mapping allows reads.
    pub const READ: vm_flags_t = bindings::VM_READ as vm_flags_t;

    /// Mapping allows writes.
    pub const WRITE: vm_flags_t = bindings::VM_WRITE as vm_flags_t;

    /// Mapping allows execution.
    pub const EXEC: vm_flags_t = bindings::VM_EXEC as vm_flags_t;

    /// Mapping is shared.
    pub const SHARED: vm_flags_t = bindings::VM_SHARED as vm_flags_t;

    /// Mapping may be updated to allow reads.
    pub const MAYREAD: vm_flags_t = bindings::VM_MAYREAD as vm_flags_t;

    /// Mapping may be updated to allow writes.
    pub const MAYWRITE: vm_flags_t = bindings::VM_MAYWRITE as vm_flags_t;

    /// Mapping may be updated to allow execution.
    pub const MAYEXEC: vm_flags_t = bindings::VM_MAYEXEC as vm_flags_t;

    /// Mapping may be updated to be shared.
    pub const MAYSHARE: vm_flags_t = bindings::VM_MAYSHARE as vm_flags_t;

    /// Page-ranges managed without `struct page`, just pure PFN.
    pub const PFNMAP: vm_flags_t = bindings::VM_PFNMAP as vm_flags_t;

    /// Memory mapped I/O or similar.
    pub const IO: vm_flags_t = bindings::VM_IO as vm_flags_t;

    /// Do not copy this vma on fork.
    pub const DONTCOPY: vm_flags_t = bindings::VM_DONTCOPY as vm_flags_t;

    /// Cannot expand with mremap().
    pub const DONTEXPAND: vm_flags_t = bindings::VM_DONTEXPAND as vm_flags_t;

    /// Lock the pages covered when they are faulted in.
    pub const LOCKONFAULT: vm_flags_t = bindings::VM_LOCKONFAULT as vm_flags_t;

    /// Is a VM accounted object.
    pub const ACCOUNT: vm_flags_t = bindings::VM_ACCOUNT as vm_flags_t;

    /// Should the VM suppress accounting.
    pub const NORESERVE: vm_flags_t = bindings::VM_NORESERVE as vm_flags_t;

    /// Huge TLB Page VM.
    pub const HUGETLB: vm_flags_t = bindings::VM_HUGETLB as vm_flags_t;

    /// Synchronous page faults. (DAX-specific)
    pub const SYNC: vm_flags_t = bindings::VM_SYNC as vm_flags_t;

    /// Architecture-specific flag.
    pub const ARCH_1: vm_flags_t = bindings::VM_ARCH_1 as vm_flags_t;

    /// Wipe VMA contents in child on fork.
    pub const WIPEONFORK: vm_flags_t = bindings::VM_WIPEONFORK as vm_flags_t;

    /// Do not include in the core dump.
    pub const DONTDUMP: vm_flags_t = bindings::VM_DONTDUMP as vm_flags_t;

    /// Not soft dirty clean area.
    pub const SOFTDIRTY: vm_flags_t = bindings::VM_SOFTDIRTY as vm_flags_t;

    /// Can contain `struct page` and pure PFN pages.
    pub const MIXEDMAP: vm_flags_t = bindings::VM_MIXEDMAP as vm_flags_t;

    /// MADV_HUGEPAGE marked this vma.
    pub const HUGEPAGE: vm_flags_t = bindings::VM_HUGEPAGE as vm_flags_t;

    /// MADV_NOHUGEPAGE marked this vma.
    pub const NOHUGEPAGE: vm_flags_t = bindings::VM_NOHUGEPAGE as vm_flags_t;

    /// KSM may merge identical pages.
    pub const MERGEABLE: vm_flags_t = bindings::VM_MERGEABLE as vm_flags_t;
}
