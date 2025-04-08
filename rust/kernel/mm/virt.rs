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

use crate::{bindings, mm::MmWithUser, types::Opaque};

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
}

/// The integer type used for vma flags.
#[doc(inline)]
pub use bindings::vm_flags_t;

/// All possible flags for [`VmaRef`].
pub mod flags {
    use super::vm_flags_t;
    use crate::bindings;

    /// No flags are set.
    pub const NONE: vm_flags_t = bindings::VM_NONE as _;

    /// Mapping allows reads.
    pub const READ: vm_flags_t = bindings::VM_READ as _;

    /// Mapping allows writes.
    pub const WRITE: vm_flags_t = bindings::VM_WRITE as _;

    /// Mapping allows execution.
    pub const EXEC: vm_flags_t = bindings::VM_EXEC as _;

    /// Mapping is shared.
    pub const SHARED: vm_flags_t = bindings::VM_SHARED as _;

    /// Mapping may be updated to allow reads.
    pub const MAYREAD: vm_flags_t = bindings::VM_MAYREAD as _;

    /// Mapping may be updated to allow writes.
    pub const MAYWRITE: vm_flags_t = bindings::VM_MAYWRITE as _;

    /// Mapping may be updated to allow execution.
    pub const MAYEXEC: vm_flags_t = bindings::VM_MAYEXEC as _;

    /// Mapping may be updated to be shared.
    pub const MAYSHARE: vm_flags_t = bindings::VM_MAYSHARE as _;

    /// Page-ranges managed without `struct page`, just pure PFN.
    pub const PFNMAP: vm_flags_t = bindings::VM_PFNMAP as _;

    /// Memory mapped I/O or similar.
    pub const IO: vm_flags_t = bindings::VM_IO as _;

    /// Do not copy this vma on fork.
    pub const DONTCOPY: vm_flags_t = bindings::VM_DONTCOPY as _;

    /// Cannot expand with mremap().
    pub const DONTEXPAND: vm_flags_t = bindings::VM_DONTEXPAND as _;

    /// Lock the pages covered when they are faulted in.
    pub const LOCKONFAULT: vm_flags_t = bindings::VM_LOCKONFAULT as _;

    /// Is a VM accounted object.
    pub const ACCOUNT: vm_flags_t = bindings::VM_ACCOUNT as _;

    /// Should the VM suppress accounting.
    pub const NORESERVE: vm_flags_t = bindings::VM_NORESERVE as _;

    /// Huge TLB Page VM.
    pub const HUGETLB: vm_flags_t = bindings::VM_HUGETLB as _;

    /// Synchronous page faults. (DAX-specific)
    pub const SYNC: vm_flags_t = bindings::VM_SYNC as _;

    /// Architecture-specific flag.
    pub const ARCH_1: vm_flags_t = bindings::VM_ARCH_1 as _;

    /// Wipe VMA contents in child on fork.
    pub const WIPEONFORK: vm_flags_t = bindings::VM_WIPEONFORK as _;

    /// Do not include in the core dump.
    pub const DONTDUMP: vm_flags_t = bindings::VM_DONTDUMP as _;

    /// Not soft dirty clean area.
    pub const SOFTDIRTY: vm_flags_t = bindings::VM_SOFTDIRTY as _;

    /// Can contain `struct page` and pure PFN pages.
    pub const MIXEDMAP: vm_flags_t = bindings::VM_MIXEDMAP as _;

    /// MADV_HUGEPAGE marked this vma.
    pub const HUGEPAGE: vm_flags_t = bindings::VM_HUGEPAGE as _;

    /// MADV_NOHUGEPAGE marked this vma.
    pub const NOHUGEPAGE: vm_flags_t = bindings::VM_NOHUGEPAGE as _;

    /// KSM may merge identical pages.
    pub const MERGEABLE: vm_flags_t = bindings::VM_MERGEABLE as _;
}
