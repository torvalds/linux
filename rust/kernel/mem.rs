// SPDX-License-Identifier: GPL-2.0

//! Basic utilities for dealing with memory, values, and types.

use crate::prelude::*;

/// Transmute between two types.
///
/// Use this instead of [`core::mem::transmute`] when it is known that sizes are identical but this
/// cannot be proven by the compiler.
///
/// This is equivalent to Rust's `transmute_unchecked` intrinsics.
///
/// # Safety
///
/// All safety requirements of [`core::mem::transmute`] apply, plus that the size `Src` and `Dst`
/// must match.
///
/// # Examples
///
/// This can be used when types are known to have the same size, but only at runtime.
///
/// ```no_run
/// # use core::any::TypeId;
/// fn to_u32<T: 'static>(v: T) -> Option<u32> {
///     if TypeId::of::<T>() != TypeId::of::<u32>() {
///         return None;
///     }
///
///     // `core::mem::transmute` won't work here.
///     // SAFETY: We've checked that `T` is `u32`!
///     Some(unsafe { kernel::mem::transmute_unchecked(v) })
/// }
///
/// to_u32(1u32);
/// ```
#[inline(always)]
pub const unsafe fn transmute_unchecked<Src, Dst>(val: Src) -> Dst {
    // SAFETY: This is identical to `transmute` except that we bypassed the size check; which is
    // true per safety requirement.
    unsafe { core::mem::transmute_copy(&core::mem::ManuallyDrop::new(val)) }
}

/// Version of `transmute` that performs size check at monomorphization-time.
///
/// Use this instead of [`core::mem::transmute`] when it is known that sizes are identical but this
/// cannot be proven by the compiler during type checking and can be proven during monomorphization.
///
/// The signature is equivalent to Rust standard library's unstable `transmute_neo` and that of
/// [RFC 3844](https://github.com/rust-lang/rfcs/pull/3844).
///
/// # Safety
///
/// Same as [`core::mem::transmute`].
///
/// # Examples
///
/// This is typically used in generic code where it's known that type will have the same size, but
/// the compiler cannot prove it generically.
///
/// ```no_run
/// trait IsU32 {}
/// impl IsU32 for u32 {}
///
/// fn to_u32<T: IsU32>(v: T) -> u32 {
///     // `core::mem::transmute` won't work here.
///     // SAFETY: We know that `v` is u32!
///     unsafe { kernel::mem::transmute(v) }
/// }
///
/// to_u32(1u32);
/// ```
#[inline(always)]
pub const unsafe fn transmute<Src, Dst>(val: Src) -> Dst {
    const_assert!(size_of::<Src>() == size_of::<Dst>());

    // SAFETY: Size is checked above. Other safety requirements follow those of the function.
    unsafe { transmute_unchecked(val) }
}

/// Safely transmutes a value of one type to a value of another type of the same size.
///
/// The sizes are checked during monomorphization.
///
/// This can be considered as generic version of [`zerocopy::transmute!`] macro that defers the size
/// check and thus can be used in more cases.
///
/// # Examples
///
/// ```no_run
/// fn to_u32<T: FromBytes + IntoBytes>(v: T) -> u32 {
///     // `zerocopy::transmute!` won't work here.
///     kernel::mem::safe_transmute(v)
/// }
///
/// to_u32(1i32);
/// ```
#[inline(always)]
pub const fn safe_transmute<Src: IntoBytes, Dst: FromBytes>(val: Src) -> Dst {
    // SAFETY: `transmute` is safe with `IntoBytes` and `FromBytes` bounds.
    unsafe { transmute(val) }
}

/// Type that is layout-compatible with a primitive representation.
///
/// # Safety
///
/// - [`Self`] must have the same size and alignment as [`Self::Repr`].
/// - [`Self`] must be [transmutable] to [`Self::Repr`].
/// - Neither [`Self`] nor [`Self::Repr`] contains interior mutability.
///
/// The above basically says that `&Self` can be transmuted to `&Self::Repr`.
///
/// [transmutable]: core::mem::transmute
pub unsafe trait AsRepr: Sized {
    /// Primitive representation of this type.
    type Repr;

    /// Convert from [`&Self`](Self) to [`&Self::Repr`](AsRepr::Repr).
    #[inline(always)]
    fn as_repr(this: &Self) -> &Self::Repr {
        // SAFETY: Per safety requirement of the trait.
        unsafe { core::mem::transmute(this) }
    }

    /// Convert from [`Self`] to [`Self::Repr`].
    #[inline(always)]
    fn into_repr(this: Self) -> Self::Repr {
        // SAFETY: Per safety requirement of the trait.
        unsafe { transmute(this) }
    }

    /// Convert from [`Self::Repr`] to [`Self`].
    ///
    /// # Safety
    ///
    /// `repr` must be a valid bit pattern of [`Self`] and satisfy type-specific invariants of it.
    ///
    /// Alternatively, if `repr` is previously obtained using [`Self::into_repr`], and each
    /// `from_repr_unchecked` should correspond to a unique `into_repr` call, then it is safe to
    /// call as well (this means that we're undoing a `into_repr` call getting the exact bytes
    /// back).
    ///
    /// No guarantee is made if the result of a `into_repr` is passed to multiple
    /// `from_repr_unchecked` (i.e. copies are made), to allow for cases where `Repr` is a pointer
    /// and the user of the API wants ownership transfer. Users that want the ability to call
    /// `from_repr_unchecked` after copying can require `Copy` bound explicitly.
    #[inline(always)]
    unsafe fn from_repr_unchecked(repr: Self::Repr) -> Self {
        // SAFETY: Per safety requirement, `repr` is valid repr of `Self`, or it is previously from
        // `into_repr`, in which case we're undoing the transmute so it is also safe.
        unsafe { transmute(repr) }
    }
}

/// Type that is bi-directionally transmutable with a primitive representation.
///
/// # Safety
///
/// - [`Self`] must be [transmutable] from [`Self::Repr`].
///
/// [transmutable]: core::mem::transmute
/// [`Self::Repr`]: AsRepr::Repr
pub unsafe trait AsReprMut: AsRepr {
    /// Convert from `&mut Self` to [`&mut Self::Repr`](AsRepr::Repr).
    #[inline(always)]
    fn as_repr_mut(this: &mut Self) -> &mut Self::Repr {
        // SAFETY: Per safety requirement of the trait.
        unsafe { core::mem::transmute(this) }
    }

    /// Convert from [`Self::Repr`](AsRepr::Repr) to `Self`.
    #[inline(always)]
    fn from_repr(repr: Self::Repr) -> Self {
        // SAFETY: Per safety requirement of the trait.
        unsafe { transmute(repr) }
    }
}

// SAFETY: `bool` has the same size and alignment as `u8`, and Rust guarantees that `bool` has
// only two valid bit patterns: 0 (`false`) and 1 (`true`). Thus `bool` can be transmuted to `u8`.
// Neither types contain interior mutability.
unsafe impl AsRepr for bool {
    type Repr = u8;
}

// SAFETY: `*mut T` has the same size and alignment with `*const c_void`, and thus `*mut T` is
// transmutable to `*const c_void`. Neither types contain interior mutability.
unsafe impl<T> AsRepr for *mut T {
    type Repr = *const c_void;
}

// SAFETY: `*mut T` is transmutable from `*const c_void`.
unsafe impl<T> AsReprMut for *mut T {}

// SAFETY: `*const T` has the same size and alignment with `*const c_void`, and is transmutable to
// `*const c_void`. Neither types contain interior mutability.
unsafe impl<T> AsRepr for *const T {
    type Repr = *const c_void;
}

// SAFETY: `*const T` is transmutable from `*const c_void`.
unsafe impl<T> AsReprMut for *const T {}

macro_rules! int_impl {
    ($($unsigned:ident $signed:ident ,)*) => {$(
        // SAFETY: `$unsigned` has the same size and alignment with itself, and is transmutable to
        // itself. It does not contain interior mutability.
        unsafe impl AsRepr for $unsigned {
            type Repr = $unsigned;
        }

        // SAFETY: `$unsigned` is transmutable from itself.
        unsafe impl AsReprMut for $unsigned {}

        // SAFETY: `$signed` has the same size and alignment with `$unsigned`, and is transmutable
        // to it Neither types contain interior mutability.
        unsafe impl AsRepr for $signed {
            type Repr = $unsigned;
        }

        // SAFETY: `$signed` is transmutable from `$unsigned`.
        unsafe impl AsReprMut for $signed {}
    )*};
}

int_impl! {
    u8 i8,
    u16 i16,
    u32 i32,
    u64 i64,
    // `usize` is not normalized to particular integer for portability.
    usize isize,
}
