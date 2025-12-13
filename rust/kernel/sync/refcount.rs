// SPDX-License-Identifier: GPL-2.0

//! Atomic reference counting.
//!
//! C header: [`include/linux/refcount.h`](srctree/include/linux/refcount.h)

use crate::build_assert;
use crate::sync::atomic::Atomic;
use crate::types::Opaque;

/// Atomic reference counter.
///
/// This type is conceptually an atomic integer, but provides saturation semantics compared to
/// normal atomic integers. Values in the negative range when viewed as a signed integer are
/// saturation (bad) values. For details about the saturation semantics, please refer to top of
/// [`include/linux/refcount.h`](srctree/include/linux/refcount.h).
///
/// Wraps the kernel's C `refcount_t`.
#[repr(transparent)]
pub struct Refcount(Opaque<bindings::refcount_t>);

impl Refcount {
    /// Construct a new [`Refcount`] from an initial value.
    ///
    /// The initial value should be non-saturated.
    #[inline]
    pub fn new(value: i32) -> Self {
        build_assert!(value >= 0, "initial value saturated");
        // SAFETY: There are no safety requirements for this FFI call.
        Self(Opaque::new(unsafe { bindings::REFCOUNT_INIT(value) }))
    }

    #[inline]
    fn as_ptr(&self) -> *mut bindings::refcount_t {
        self.0.get()
    }

    /// Get the underlying atomic counter that backs the refcount.
    ///
    /// NOTE: Usage of this function is discouraged as it can circumvent the protections offered by
    /// `refcount.h`. If there is no way to achieve the result using APIs in `refcount.h`, then
    /// this function can be used. Otherwise consider adding a binding for the required API.
    #[inline]
    pub fn as_atomic(&self) -> &Atomic<i32> {
        let ptr = self.0.get().cast();
        // SAFETY: `refcount_t` is a transparent wrapper of `atomic_t`, which is an atomic 32-bit
        // integer that is layout-wise compatible with `Atomic<i32>`. All values are valid for
        // `refcount_t`, despite some of the values being considered saturated and "bad".
        unsafe { &*ptr }
    }

    /// Set a refcount's value.
    #[inline]
    pub fn set(&self, value: i32) {
        // SAFETY: `self.as_ptr()` is valid.
        unsafe { bindings::refcount_set(self.as_ptr(), value) }
    }

    /// Increment a refcount.
    ///
    /// It will saturate if overflows and `WARN`. It will also `WARN` if the refcount is 0, as this
    /// represents a possible use-after-free condition.
    ///
    /// Provides no memory ordering, it is assumed that caller already has a reference on the
    /// object.
    #[inline]
    pub fn inc(&self) {
        // SAFETY: self is valid.
        unsafe { bindings::refcount_inc(self.as_ptr()) }
    }

    /// Decrement a refcount.
    ///
    /// It will `WARN` on underflow and fail to decrement when saturated.
    ///
    /// Provides release memory ordering, such that prior loads and stores are done
    /// before.
    #[inline]
    pub fn dec(&self) {
        // SAFETY: `self.as_ptr()` is valid.
        unsafe { bindings::refcount_dec(self.as_ptr()) }
    }

    /// Decrement a refcount and test if it is 0.
    ///
    /// It will `WARN` on underflow and fail to decrement when saturated.
    ///
    /// Provides release memory ordering, such that prior loads and stores are done
    /// before, and provides an acquire ordering on success such that memory deallocation
    /// must come after.
    ///
    /// Returns true if the resulting refcount is 0, false otherwise.
    ///
    /// # Notes
    ///
    /// A common pattern of using `Refcount` is to free memory when the reference count reaches
    /// zero. This means that the reference to `Refcount` could become invalid after calling this
    /// function. This is fine as long as the reference to `Refcount` is no longer used when this
    /// function returns `false`. It is not necessary to use raw pointers in this scenario, see
    /// <https://github.com/rust-lang/rust/issues/55005>.
    #[inline]
    #[must_use = "use `dec` instead if you do not need to test if it is 0"]
    pub fn dec_and_test(&self) -> bool {
        // SAFETY: `self.as_ptr()` is valid.
        unsafe { bindings::refcount_dec_and_test(self.as_ptr()) }
    }
}

// SAFETY: `refcount_t` is thread-safe.
unsafe impl Send for Refcount {}

// SAFETY: `refcount_t` is thread-safe.
unsafe impl Sync for Refcount {}
