// SPDX-License-Identifier: GPL-2.0

//! Infrastructure for handling projections.

use core::{
    mem::MaybeUninit,
    ops::Deref, //
};

use crate::prelude::*;

/// Error raised when a projection is attempted on an array or slice out of bounds.
pub struct OutOfBound;

impl From<OutOfBound> for Error {
    #[inline(always)]
    fn from(_: OutOfBound) -> Self {
        ERANGE
    }
}

/// A helper trait to perform index projection.
///
/// This is similar to [`core::slice::SliceIndex`], but operates on raw pointers safely and
/// fallibly.
///
/// # Safety
///
/// The implementation of `index` and `get` (if [`Some`] is returned) must ensure that, if provided
/// input pointer `slice` and returned pointer `output`, then:
/// - `output` has the same provenance as `slice`;
/// - `output.byte_offset_from(slice)` is between 0 to
///   `KnownSize::size(slice) - KnownSize::size(output)`.
///
/// This means that if the input pointer is valid, then pointer returned by `get` or `index` is
/// also valid.
#[diagnostic::on_unimplemented(message = "`{Self}` cannot be used to index `{T}`")]
#[doc(hidden)]
pub unsafe trait ProjectIndex<T: ?Sized>: Sized {
    type Output: ?Sized;

    /// Returns an index-projected pointer, if in bounds.
    fn get(self, slice: *mut T) -> Option<*mut Self::Output>;

    /// Returns an index-projected pointer; fail the build if it cannot be proved to be in bounds.
    #[inline(always)]
    fn index(self, slice: *mut T) -> *mut Self::Output {
        Self::get(self, slice).unwrap_or_else(|| build_error!())
    }
}

// Forward array impl to slice impl.
//
// SAFETY: Safety requirement guaranteed by the forwarded impl.
unsafe impl<T, I, const N: usize> ProjectIndex<[T; N]> for I
where
    I: ProjectIndex<[T]>,
{
    type Output = <I as ProjectIndex<[T]>>::Output;

    #[inline(always)]
    fn get(self, slice: *mut [T; N]) -> Option<*mut Self::Output> {
        <I as ProjectIndex<[T]>>::get(self, slice)
    }

    #[inline(always)]
    fn index(self, slice: *mut [T; N]) -> *mut Self::Output {
        <I as ProjectIndex<[T]>>::index(self, slice)
    }
}

// SAFETY: `get`-returned pointer has the same provenance as `slice` and the offset is checked to
// not exceed the required bound.
unsafe impl<T> ProjectIndex<[T]> for usize {
    type Output = T;

    #[inline(always)]
    fn get(self, slice: *mut [T]) -> Option<*mut T> {
        if self >= slice.len() {
            None
        } else {
            Some(slice.cast::<T>().wrapping_add(self))
        }
    }
}

// SAFETY: `get`-returned pointer has the same provenance as `slice` and the offset is checked to
// not exceed the required bound.
unsafe impl<T> ProjectIndex<[T]> for core::ops::Range<usize> {
    type Output = [T];

    #[inline(always)]
    fn get(self, slice: *mut [T]) -> Option<*mut [T]> {
        let new_len = self.end.checked_sub(self.start)?;
        if self.end > slice.len() {
            return None;
        }
        Some(core::ptr::slice_from_raw_parts_mut(
            slice.cast::<T>().wrapping_add(self.start),
            new_len,
        ))
    }
}

// SAFETY: Safety requirement guaranteed by the forwarded impl.
unsafe impl<T> ProjectIndex<[T]> for core::ops::RangeTo<usize> {
    type Output = [T];

    #[inline(always)]
    fn get(self, slice: *mut [T]) -> Option<*mut [T]> {
        (0..self.end).get(slice)
    }
}

// SAFETY: Safety requirement guaranteed by the forwarded impl.
unsafe impl<T> ProjectIndex<[T]> for core::ops::RangeFrom<usize> {
    type Output = [T];

    #[inline(always)]
    fn get(self, slice: *mut [T]) -> Option<*mut [T]> {
        (self.start..slice.len()).get(slice)
    }
}

// SAFETY: `get` returned the pointer as is, so it always has the same provenance and offset of 0.
unsafe impl<T> ProjectIndex<[T]> for core::ops::RangeFull {
    type Output = [T];

    #[inline(always)]
    fn get(self, slice: *mut [T]) -> Option<*mut [T]> {
        Some(slice)
    }
}

/// A helper trait to perform field projection.
///
/// This trait has a `DEREF` generic parameter so it can be implemented twice for types that
/// implement [`Deref`]. This will cause an ambiguity error and thus block [`Deref`] types being
/// used as base of projection, as they can inject unsoundness. Users therefore must not specify
/// `DEREF` and should always leave it to be inferred.
///
/// # Safety
///
/// `proj` may only invoke `f` with a valid allocation, as the documentation of [`Self::proj`]
/// describes.
#[doc(hidden)]
pub unsafe trait ProjectField<const DEREF: bool> {
    /// Project a pointer to a type to a pointer of a field.
    ///
    /// `f` may only be invoked with a valid allocation so it can safely obtain raw pointers to
    /// fields using `&raw mut`.
    ///
    /// This is needed because `base` might not point to a valid allocation, while `&raw mut`
    /// requires pointers to be in bounds of a valid allocation.
    ///
    /// # Safety
    ///
    /// `f` must return a pointer in bounds of the provided pointer.
    unsafe fn proj<F>(base: *mut Self, f: impl FnOnce(*mut Self) -> *mut F) -> *mut F;
}

// NOTE: in theory, this API should work for `T: ?Sized` and `F: ?Sized`, too. However, we cannot
// currently support that as we need to obtain a valid allocation that `&raw const` can operate on.
//
// SAFETY: `proj` invokes `f` with valid allocation.
unsafe impl<T> ProjectField<false> for T {
    #[inline(always)]
    unsafe fn proj<F>(base: *mut Self, f: impl FnOnce(*mut Self) -> *mut F) -> *mut F {
        // Create a valid allocation to start projection, as `base` is not necessarily so. The
        // memory is never actually used so it will be optimized out, so it should work even for
        // very large `T` (`memoffset` crate also relies on this). To be extra certain, we also
        // annotate `f` closure with `#[inline(always)]` in the macro.
        let mut place = MaybeUninit::uninit();
        let place_base = place.as_mut_ptr();
        let field = f(place_base);
        // SAFETY: `field` is in bounds from `base` per safety requirement.
        let offset = unsafe { field.byte_offset_from(place_base) };
        // Use `wrapping_byte_offset` as `base` does not need to be of valid allocation.
        base.wrapping_byte_offset(offset).cast()
    }
}

// SAFETY: Vacuously satisfied.
unsafe impl<T: Deref> ProjectField<true> for T {
    #[inline(always)]
    unsafe fn proj<F>(_: *mut Self, _: impl FnOnce(*mut Self) -> *mut F) -> *mut F {
        build_error!("this function is a guard against `Deref` impl and is never invoked");
    }
}

/// Create a projection from a raw pointer.
///
/// The projected pointer is within the memory region marked by the input pointer. There is no
/// requirement that the input raw pointer needs to be valid, so this macro may be used for
/// projecting pointers outside normal address space, e.g. I/O pointers. However, if the input
/// pointer is valid, the projected pointer is also valid.
///
/// Supported projections include field projections and index projections.
/// It is not allowed to project into types that implement custom [`Deref`] or
/// [`Index`](core::ops::Index).
///
/// The macro has basic syntax of `kernel::ptr::project!(ptr, projection)`, where `ptr` is an
/// expression that evaluates to a raw pointer which serves as the base of projection. `projection`
/// can be a projection expression of form `.field` (normally identifier, or numeral in case of
/// tuple structs) or of form `[index]`.
///
/// If a mutable pointer is needed, the macro input can be prefixed with the `mut` keyword, i.e.
/// `kernel::ptr::project!(mut ptr, projection)`. By default, a const pointer is created.
///
/// `ptr::project!` macro can perform both fallible indexing and build-time checked indexing.
/// `[index]` form performs build-time bounds checking; if compiler fails to prove `[index]` is in
/// bounds, compilation will fail. `[index]?` can be used to perform runtime bounds checking;
/// `OutOfBound` error is raised via `?` if the index is out of bounds.
///
/// # Examples
///
/// Field projections are performed with `.field_name`:
///
/// ```
/// struct MyStruct { field: u32, }
/// let ptr: *const MyStruct = core::ptr::dangling();
/// let field_ptr: *const u32 = kernel::ptr::project!(ptr, .field);
///
/// struct MyTupleStruct(u32, u32);
///
/// fn proj(ptr: *const MyTupleStruct) {
///     let field_ptr: *const u32 = kernel::ptr::project!(ptr, .1);
/// }
/// ```
///
/// Index projections are performed with `[index]`:
///
/// ```
/// fn proj(ptr: *const [u8; 32]) -> Result {
///     let field_ptr: *const u8 = kernel::ptr::project!(ptr, [1]);
///     // The following invocation, if uncommented, would fail the build.
///     //
///     // kernel::ptr::project!(ptr, [128]);
///
///     // This will raise an `OutOfBound` error (which is convertible to `ERANGE`).
///     kernel::ptr::project!(ptr, [128]?);
///     Ok(())
/// }
/// ```
///
/// If you need to match on the error instead of propagate, put the invocation inside a closure:
///
/// ```
/// let ptr: *const [u8; 32] = core::ptr::dangling();
/// let field_ptr: Result<*const u8> = (|| -> Result<_> {
///     Ok(kernel::ptr::project!(ptr, [128]?))
/// })();
/// assert!(field_ptr.is_err());
/// ```
///
/// For mutable pointers, put `mut` as the first token in macro invocation.
///
/// ```
/// let ptr: *mut [(u8, u16); 32] = core::ptr::dangling_mut();
/// let field_ptr: *mut u16 = kernel::ptr::project!(mut ptr, [1].1);
/// ```
#[macro_export]
macro_rules! project_pointer {
    (@gen $ptr:ident, ) => {};
    // Field projection. `$field` needs to be `tt` to support tuple index like `.0`.
    (@gen $ptr:ident, .$field:tt $($rest:tt)*) => {
        // SAFETY: The provided closure always returns an in-bounds pointer.
        let $ptr = unsafe {
            $crate::ptr::projection::ProjectField::proj($ptr, #[inline(always)] |ptr| {
                // Check unaligned field. Not all users (e.g. DMA) can handle unaligned
                // projections.
                if false {
                    let _ = &(*ptr).$field;
                }
                // SAFETY: `$field` is in bounds, and no implicit `Deref` is possible (if the
                // type implements `Deref`, Rust cannot infer the generic parameter `DEREF`).
                &raw mut (*ptr).$field
            })
        };
        $crate::ptr::project!(@gen $ptr, $($rest)*)
    };
    // Fallible index projection.
    (@gen $ptr:ident, [$index:expr]? $($rest:tt)*) => {
        let $ptr = $crate::ptr::projection::ProjectIndex::get($index, $ptr)
            .ok_or($crate::ptr::projection::OutOfBound)?;
        $crate::ptr::project!(@gen $ptr, $($rest)*)
    };
    // Build-time checked index projection.
    (@gen $ptr:ident, [$index:expr] $($rest:tt)*) => {
        let $ptr = $crate::ptr::projection::ProjectIndex::index($index, $ptr);
        $crate::ptr::project!(@gen $ptr, $($rest)*)
    };
    (mut $ptr:expr, $($proj:tt)*) => {{
        let ptr: *mut _ = $ptr;
        $crate::ptr::project!(@gen ptr, $($proj)*);
        ptr
    }};
    ($ptr:expr, $($proj:tt)*) => {{
        let ptr = <*const _>::cast_mut($ptr);
        // We currently always project using mutable pointer, as it is not decided whether `&raw
        // const` allows the resulting pointer to be mutated (see documentation of `addr_of!`).
        $crate::ptr::project!(@gen ptr, $($proj)*);
        ptr.cast_const()
    }};
}
