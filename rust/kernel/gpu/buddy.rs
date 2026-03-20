// SPDX-License-Identifier: GPL-2.0

//! GPU buddy allocator bindings.
//!
//! C header: [`include/linux/gpu_buddy.h`](srctree/include/linux/gpu_buddy.h)
//!
//! This module provides Rust abstractions over the Linux kernel's GPU buddy
//! allocator, which implements a binary buddy memory allocator.
//!
//! The buddy allocator manages a contiguous address space and allocates blocks
//! in power-of-two sizes, useful for GPU physical memory management.
//!
//! # Examples
//!
//! Create a buddy allocator and perform a basic range allocation:
//!
//! ```
//! use kernel::{
//!     gpu::buddy::{
//!         GpuBuddy,
//!         GpuBuddyAllocFlags,
//!         GpuBuddyAllocMode,
//!         GpuBuddyParams, //
//!     },
//!     prelude::*,
//!     ptr::Alignment,
//!     sizes::*, //
//! };
//!
//! // Create a 1GB buddy allocator with 4KB minimum chunk size.
//! let buddy = GpuBuddy::new(GpuBuddyParams {
//!     base_offset: 0,
//!     size: SZ_1G as u64,
//!     chunk_size: Alignment::new::<SZ_4K>(),
//! })?;
//!
//! assert_eq!(buddy.size(), SZ_1G as u64);
//! assert_eq!(buddy.chunk_size(), Alignment::new::<SZ_4K>());
//! let initial_free = buddy.avail();
//!
//! // Allocate 16MB. Block lands at the top of the address range.
//! let allocated = KBox::pin_init(
//!     buddy.alloc_blocks(
//!         GpuBuddyAllocMode::Simple,
//!         SZ_16M as u64,
//!         Alignment::new::<SZ_16M>(),
//!         GpuBuddyAllocFlags::default(),
//!     ),
//!     GFP_KERNEL,
//! )?;
//! assert_eq!(buddy.avail(), initial_free - SZ_16M as u64);
//!
//! let block = allocated.iter().next().expect("expected one block");
//! assert_eq!(block.offset(), (SZ_1G - SZ_16M) as u64);
//! assert_eq!(block.order(), 12); // 2^12 pages = 16MB
//! assert_eq!(block.size(), SZ_16M as u64);
//! assert_eq!(allocated.iter().count(), 1);
//!
//! // Dropping the allocation returns the range to the buddy allocator.
//! drop(allocated);
//! assert_eq!(buddy.avail(), initial_free);
//! # Ok::<(), Error>(())
//! ```
//!
//! Top-down allocation allocates from the highest addresses:
//!
//! ```
//! # use kernel::{
//! #     gpu::buddy::{GpuBuddy, GpuBuddyAllocMode, GpuBuddyAllocFlags, GpuBuddyParams},
//! #     prelude::*,
//! #     ptr::Alignment,
//! #     sizes::*, //
//! # };
//! # let buddy = GpuBuddy::new(GpuBuddyParams {
//! #     base_offset: 0,
//! #     size: SZ_1G as u64,
//! #     chunk_size: Alignment::new::<SZ_4K>(),
//! # })?;
//! # let initial_free = buddy.avail();
//! let topdown = KBox::pin_init(
//!     buddy.alloc_blocks(
//!         GpuBuddyAllocMode::TopDown,
//!         SZ_16M as u64,
//!         Alignment::new::<SZ_16M>(),
//!         GpuBuddyAllocFlags::default(),
//!     ),
//!     GFP_KERNEL,
//! )?;
//! assert_eq!(buddy.avail(), initial_free - SZ_16M as u64);
//!
//! let block = topdown.iter().next().expect("expected one block");
//! assert_eq!(block.offset(), (SZ_1G - SZ_16M) as u64);
//! assert_eq!(block.order(), 12);
//! assert_eq!(block.size(), SZ_16M as u64);
//!
//! // Dropping the allocation returns the range to the buddy allocator.
//! drop(topdown);
//! assert_eq!(buddy.avail(), initial_free);
//! # Ok::<(), Error>(())
//! ```
//!
//! Non-contiguous allocation can fill fragmented memory by returning multiple
//! blocks:
//!
//! ```
//! # use kernel::{
//! #     gpu::buddy::{
//! #         GpuBuddy, GpuBuddyAllocFlags, GpuBuddyAllocMode, GpuBuddyParams,
//! #     },
//! #     prelude::*,
//! #     ptr::Alignment,
//! #     sizes::*, //
//! # };
//! # let buddy = GpuBuddy::new(GpuBuddyParams {
//! #     base_offset: 0,
//! #     size: SZ_1G as u64,
//! #     chunk_size: Alignment::new::<SZ_4K>(),
//! # })?;
//! # let initial_free = buddy.avail();
//! // Create fragmentation by allocating 4MB blocks at [0,4M) and [8M,12M).
//! let frag1 = KBox::pin_init(
//!     buddy.alloc_blocks(
//!         GpuBuddyAllocMode::Range(0..SZ_4M as u64),
//!         SZ_4M as u64,
//!         Alignment::new::<SZ_4M>(),
//!         GpuBuddyAllocFlags::default(),
//!     ),
//!     GFP_KERNEL,
//! )?;
//! assert_eq!(buddy.avail(), initial_free - SZ_4M as u64);
//!
//! let frag2 = KBox::pin_init(
//!     buddy.alloc_blocks(
//!         GpuBuddyAllocMode::Range(SZ_8M as u64..(SZ_8M + SZ_4M) as u64),
//!         SZ_4M as u64,
//!         Alignment::new::<SZ_4M>(),
//!         GpuBuddyAllocFlags::default(),
//!     ),
//!     GFP_KERNEL,
//! )?;
//! assert_eq!(buddy.avail(), initial_free - SZ_8M as u64);
//!
//! // Allocate 8MB, this returns 2 blocks from the holes.
//! let fragmented = KBox::pin_init(
//!     buddy.alloc_blocks(
//!         GpuBuddyAllocMode::Range(0..SZ_16M as u64),
//!         SZ_8M as u64,
//!         Alignment::new::<SZ_4M>(),
//!         GpuBuddyAllocFlags::default(),
//!     ),
//!     GFP_KERNEL,
//! )?;
//! assert_eq!(buddy.avail(), initial_free - SZ_16M as u64);
//!
//! let (mut count, mut total) = (0u32, 0u64);
//! for block in fragmented.iter() {
//!     assert_eq!(block.size(), SZ_4M as u64);
//!     total += block.size();
//!     count += 1;
//! }
//! assert_eq!(total, SZ_8M as u64);
//! assert_eq!(count, 2);
//! # Ok::<(), Error>(())
//! ```
//!
//! Contiguous allocation fails when only fragmented space is available:
//!
//! ```
//! # use kernel::{
//! #     gpu::buddy::{
//! #         GpuBuddy, GpuBuddyAllocFlag, GpuBuddyAllocFlags, GpuBuddyAllocMode, GpuBuddyParams,
//! #     },
//! #     prelude::*,
//! #     ptr::Alignment,
//! #     sizes::*, //
//! # };
//! // Create a small 16MB buddy allocator with fragmented memory.
//! let small = GpuBuddy::new(GpuBuddyParams {
//!     base_offset: 0,
//!     size: SZ_16M as u64,
//!     chunk_size: Alignment::new::<SZ_4K>(),
//! })?;
//!
//! let _hole1 = KBox::pin_init(
//!     small.alloc_blocks(
//!         GpuBuddyAllocMode::Range(0..SZ_4M as u64),
//!         SZ_4M as u64,
//!         Alignment::new::<SZ_4M>(),
//!         GpuBuddyAllocFlags::default(),
//!     ),
//!     GFP_KERNEL,
//! )?;
//!
//! let _hole2 = KBox::pin_init(
//!     small.alloc_blocks(
//!         GpuBuddyAllocMode::Range(SZ_8M as u64..(SZ_8M + SZ_4M) as u64),
//!         SZ_4M as u64,
//!         Alignment::new::<SZ_4M>(),
//!         GpuBuddyAllocFlags::default(),
//!     ),
//!     GFP_KERNEL,
//! )?;
//!
//! // 8MB contiguous should fail, only two non-contiguous 4MB holes exist.
//! let result = KBox::pin_init(
//!     small.alloc_blocks(
//!         GpuBuddyAllocMode::Simple,
//!         SZ_8M as u64,
//!         Alignment::new::<SZ_4M>(),
//!         GpuBuddyAllocFlag::Contiguous,
//!     ),
//!     GFP_KERNEL,
//! );
//! assert!(result.is_err());
//! # Ok::<(), Error>(())
//! ```

use core::ops::Range;

use crate::{
    bindings,
    clist_create,
    error::to_result,
    interop::list::CListHead,
    new_mutex,
    prelude::*,
    ptr::Alignment,
    sync::{
        lock::mutex::MutexGuard,
        Arc,
        Mutex, //
    },
    types::Opaque, //
};

/// Allocation mode for the GPU buddy allocator.
///
/// The mode determines the primary allocation strategy. Modes are mutually
/// exclusive: an allocation is either simple, range-constrained, or top-down.
///
/// Orthogonal modifier flags (e.g., contiguous, clear) are specified separately
/// via [`GpuBuddyAllocFlags`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum GpuBuddyAllocMode {
    /// Simple allocation without constraints.
    Simple,
    /// Range-based allocation within the given address range.
    Range(Range<u64>),
    /// Allocate from top of address space downward.
    TopDown,
}

impl GpuBuddyAllocMode {
    /// Returns the C flags corresponding to the allocation mode.
    fn as_flags(&self) -> usize {
        match self {
            Self::Simple => 0,
            Self::Range(_) => bindings::GPU_BUDDY_RANGE_ALLOCATION,
            Self::TopDown => bindings::GPU_BUDDY_TOPDOWN_ALLOCATION,
        }
    }

    /// Extracts the range start/end, defaulting to `(0, 0)` for non-range modes.
    fn range(&self) -> (u64, u64) {
        match self {
            Self::Range(range) => (range.start, range.end),
            _ => (0, 0),
        }
    }
}

crate::impl_flags!(
    /// Modifier flags for GPU buddy allocation.
    ///
    /// These flags can be combined with any [`GpuBuddyAllocMode`] to control
    /// additional allocation behavior.
    #[derive(Clone, Copy, Default, PartialEq, Eq)]
    pub struct GpuBuddyAllocFlags(usize);

    /// Individual modifier flag for GPU buddy allocation.
    #[derive(Clone, Copy, PartialEq, Eq)]
    pub enum GpuBuddyAllocFlag {
        /// Allocate physically contiguous blocks.
        Contiguous = bindings::GPU_BUDDY_CONTIGUOUS_ALLOCATION,

        /// Request allocation from cleared (zeroed) memory.
        Clear = bindings::GPU_BUDDY_CLEAR_ALLOCATION,

        /// Disable trimming of partially used blocks.
        TrimDisable = bindings::GPU_BUDDY_TRIM_DISABLE,
    }
);

/// Parameters for creating a GPU buddy allocator.
pub struct GpuBuddyParams {
    /// Base offset (in bytes) where the managed memory region starts.
    /// Allocations will be offset by this value.
    pub base_offset: u64,
    /// Total size (in bytes) of the address space managed by the allocator.
    pub size: u64,
    /// Minimum allocation unit / chunk size; must be >= 4KB.
    pub chunk_size: Alignment,
}

/// Inner structure holding the actual buddy allocator.
///
/// # Synchronization
///
/// The C `gpu_buddy` API requires synchronization (see `include/linux/gpu_buddy.h`).
/// Internal locking ensures all allocator and free operations are properly
/// synchronized, preventing races between concurrent allocations and the
/// freeing that occurs when [`AllocatedBlocks`] is dropped.
///
/// # Invariants
///
/// The inner [`Opaque`] contains an initialized buddy allocator.
#[pin_data(PinnedDrop)]
struct GpuBuddyInner {
    #[pin]
    inner: Opaque<bindings::gpu_buddy>,

    // TODO: Replace `Mutex<()>` with `Mutex<Opaque<..>>` once `Mutex::new()`
    // accepts `impl PinInit<T>`.
    #[pin]
    lock: Mutex<()>,
    /// Cached creation parameters (do not change after init).
    params: GpuBuddyParams,
}

impl GpuBuddyInner {
    /// Create a pin-initializer for the buddy allocator.
    fn new(params: GpuBuddyParams) -> impl PinInit<Self, Error> {
        let size = params.size;
        let chunk_size = params.chunk_size;

        // INVARIANT: `gpu_buddy_init` returns 0 on success, at which point the
        // `gpu_buddy` structure is initialized and ready for use with all
        // `gpu_buddy_*` APIs. `try_pin_init!` only completes if all fields succeed,
        // so the invariant holds when construction finishes.
        try_pin_init!(Self {
            inner <- Opaque::try_ffi_init(|ptr| {
                // SAFETY: `ptr` points to valid uninitialized memory from the pin-init
                // infrastructure. `gpu_buddy_init` will initialize the structure.
                to_result(unsafe {
                    bindings::gpu_buddy_init(ptr, size, chunk_size.as_usize() as u64)
                })
            }),
            lock <- new_mutex!(()),
            params,
        })
    }

    /// Lock the mutex and return a guard for accessing the allocator.
    fn lock(&self) -> GpuBuddyGuard<'_> {
        GpuBuddyGuard {
            inner: self,
            _guard: self.lock.lock(),
        }
    }
}

#[pinned_drop]
impl PinnedDrop for GpuBuddyInner {
    fn drop(self: Pin<&mut Self>) {
        let guard = self.lock();

        // SAFETY: Per the type invariant, `inner` contains an initialized
        // allocator. `guard` provides exclusive access.
        unsafe { bindings::gpu_buddy_fini(guard.as_raw()) };
    }
}

// SAFETY: `GpuBuddyInner` can be sent between threads.
unsafe impl Send for GpuBuddyInner {}

// SAFETY: `GpuBuddyInner` is `Sync` because `GpuBuddyInner::lock`
// serializes all access to the C allocator, preventing data races.
unsafe impl Sync for GpuBuddyInner {}

/// Guard that proves the lock is held, enabling access to the allocator.
///
/// The `_guard` holds the lock for the duration of this guard's lifetime.
struct GpuBuddyGuard<'a> {
    inner: &'a GpuBuddyInner,
    _guard: MutexGuard<'a, ()>,
}

impl GpuBuddyGuard<'_> {
    /// Get a raw pointer to the underlying C `gpu_buddy` structure.
    fn as_raw(&self) -> *mut bindings::gpu_buddy {
        self.inner.inner.get()
    }
}

/// GPU buddy allocator instance.
///
/// This structure wraps the C `gpu_buddy` allocator using reference counting.
/// The allocator is automatically cleaned up when all references are dropped.
///
/// Refer to the module-level documentation for usage examples.
pub struct GpuBuddy(Arc<GpuBuddyInner>);

impl GpuBuddy {
    /// Create a new buddy allocator.
    ///
    /// The allocator manages a contiguous address space of the given size, with the
    /// specified minimum allocation unit (chunk_size must be at least 4KB).
    pub fn new(params: GpuBuddyParams) -> Result<Self> {
        Arc::pin_init(GpuBuddyInner::new(params), GFP_KERNEL).map(Self)
    }

    /// Get the base offset for allocations.
    pub fn base_offset(&self) -> u64 {
        self.0.params.base_offset
    }

    /// Get the chunk size (minimum allocation unit).
    pub fn chunk_size(&self) -> Alignment {
        self.0.params.chunk_size
    }

    /// Get the total managed size.
    pub fn size(&self) -> u64 {
        self.0.params.size
    }

    /// Get the available (free) memory in bytes.
    pub fn avail(&self) -> u64 {
        let guard = self.0.lock();

        // SAFETY: Per the type invariant, `inner` contains an initialized allocator.
        // `guard` provides exclusive access.
        unsafe { (*guard.as_raw()).avail }
    }

    /// Allocate blocks from the buddy allocator.
    ///
    /// Returns a pin-initializer for [`AllocatedBlocks`].
    pub fn alloc_blocks(
        &self,
        mode: GpuBuddyAllocMode,
        size: u64,
        min_block_size: Alignment,
        flags: impl Into<GpuBuddyAllocFlags>,
    ) -> impl PinInit<AllocatedBlocks, Error> {
        let buddy_arc = Arc::clone(&self.0);
        let (start, end) = mode.range();
        let mode_flags = mode.as_flags();
        let modifier_flags = flags.into();

        // Create pin-initializer that initializes list and allocates blocks.
        try_pin_init!(AllocatedBlocks {
            buddy: buddy_arc,
            list <- CListHead::new(),
            _: {
                // Reject zero-sized or inverted ranges.
                if let GpuBuddyAllocMode::Range(range) = &mode {
                    if range.is_empty() {
                        Err::<(), Error>(EINVAL)?;
                    }
                }

                // Lock while allocating to serialize with concurrent frees.
                let guard = buddy.lock();

                // SAFETY: Per the type invariant, `inner` contains an initialized
                // allocator. `guard` provides exclusive access.
                to_result(unsafe {
                    bindings::gpu_buddy_alloc_blocks(
                        guard.as_raw(),
                        start,
                        end,
                        size,
                        min_block_size.as_usize() as u64,
                        list.as_raw(),
                        mode_flags | usize::from(modifier_flags),
                    )
                })?
            }
        })
    }
}

/// Allocated blocks from the buddy allocator with automatic cleanup.
///
/// This structure owns a list of allocated blocks and ensures they are
/// automatically freed when dropped. Use `iter()` to iterate over all
/// allocated blocks.
///
/// # Invariants
///
/// - `list` is an initialized, valid list head containing allocated blocks.
#[pin_data(PinnedDrop)]
pub struct AllocatedBlocks {
    #[pin]
    list: CListHead,
    buddy: Arc<GpuBuddyInner>,
}

impl AllocatedBlocks {
    /// Check if the block list is empty.
    pub fn is_empty(&self) -> bool {
        // An empty list head points to itself.
        !self.list.is_linked()
    }

    /// Iterate over allocated blocks.
    ///
    /// Returns an iterator yielding [`AllocatedBlock`] values. Each [`AllocatedBlock`]
    /// borrows `self` and is only valid for the duration of that borrow.
    pub fn iter(&self) -> impl Iterator<Item = AllocatedBlock<'_>> + '_ {
        let head = self.list.as_raw();
        // SAFETY: Per the type invariant, `list` is an initialized sentinel `list_head`
        // and is not concurrently modified (we hold a `&self` borrow). The list contains
        // `gpu_buddy_block` items linked via `__bindgen_anon_1.link`. `Block` is
        // `#[repr(transparent)]` over `gpu_buddy_block`.
        let clist = unsafe {
            clist_create!(
                head,
                Block,
                bindings::gpu_buddy_block,
                __bindgen_anon_1.link
            )
        };

        clist
            .iter()
            .map(|this| AllocatedBlock { this, blocks: self })
    }
}

#[pinned_drop]
impl PinnedDrop for AllocatedBlocks {
    fn drop(self: Pin<&mut Self>) {
        let guard = self.buddy.lock();

        // SAFETY:
        // - list is valid per the type's invariants.
        // - guard provides exclusive access to the allocator.
        unsafe {
            bindings::gpu_buddy_free_list(guard.as_raw(), self.list.as_raw(), 0);
        }
    }
}

/// A GPU buddy block.
///
/// Transparent wrapper over C `gpu_buddy_block` structure. This type is returned
/// as references during iteration over [`AllocatedBlocks`].
///
/// # Invariants
///
/// The inner [`Opaque`] contains a valid, allocated `gpu_buddy_block`.
#[repr(transparent)]
struct Block(Opaque<bindings::gpu_buddy_block>);

impl Block {
    /// Get a raw pointer to the underlying C block.
    fn as_raw(&self) -> *mut bindings::gpu_buddy_block {
        self.0.get()
    }

    /// Get the block's raw offset in the buddy address space (without base offset).
    fn offset(&self) -> u64 {
        // SAFETY: `self.as_raw()` is valid per the type's invariants.
        unsafe { bindings::gpu_buddy_block_offset(self.as_raw()) }
    }

    /// Get the block order.
    fn order(&self) -> u32 {
        // SAFETY: `self.as_raw()` is valid per the type's invariants.
        unsafe { bindings::gpu_buddy_block_order(self.as_raw()) }
    }
}

// SAFETY: `Block` is a wrapper around `gpu_buddy_block` which can be
// sent across threads safely.
unsafe impl Send for Block {}

// SAFETY: `Block` is only accessed through shared references after
// allocation, and thus safe to access concurrently across threads.
unsafe impl Sync for Block {}

/// A buddy block paired with its owning [`AllocatedBlocks`] context.
///
/// Unlike a raw block, which only knows its offset within the buddy address
/// space, an [`AllocatedBlock`] also has access to the allocator's `base_offset`
/// and `chunk_size`, enabling it to compute absolute offsets and byte sizes.
///
/// Returned by [`AllocatedBlocks::iter()`].
pub struct AllocatedBlock<'a> {
    this: &'a Block,
    blocks: &'a AllocatedBlocks,
}

impl AllocatedBlock<'_> {
    /// Get the block's offset in the address space.
    ///
    /// Returns the absolute offset including the allocator's base offset.
    /// This is the actual address to use for accessing the allocated memory.
    pub fn offset(&self) -> u64 {
        self.blocks.buddy.params.base_offset + self.this.offset()
    }

    /// Get the block order (size = chunk_size << order).
    pub fn order(&self) -> u32 {
        self.this.order()
    }

    /// Get the block's size in bytes.
    pub fn size(&self) -> u64 {
        (self.blocks.buddy.params.chunk_size.as_usize() as u64) << self.this.order()
    }
}
