// SPDX-License-Identifier: GPL-2.0

//! Traits for transmuting types.

use core::mem::size_of;

/// Types for which any bit pattern is valid.
///
/// Not all types are valid for all values. For example, a `bool` must be either zero or one, so
/// reading arbitrary bytes into something that contains a `bool` is not okay.
///
/// It's okay for the type to have padding, as initializing those bytes has no effect.
///
/// # Examples
///
/// ```
/// use kernel::transmute::FromBytes;
///
/// # fn test() -> Option<()> {
/// let raw = [1, 2, 3, 4];
///
/// let result = u32::from_bytes(&raw)?;
///
/// #[cfg(target_endian = "little")]
/// assert_eq!(*result, 0x4030201);
///
/// #[cfg(target_endian = "big")]
/// assert_eq!(*result, 0x1020304);
///
/// # Some(()) }
/// # test().ok_or(EINVAL)?;
/// # Ok::<(), Error>(())
/// ```
///
/// # Safety
///
/// All bit-patterns must be valid for this type. This type must not have interior mutability.
pub unsafe trait FromBytes {
    /// Converts a slice of bytes to a reference to `Self`.
    ///
    /// Succeeds if the reference is properly aligned, and the size of `bytes` is equal to that of
    /// `T` and different from zero.
    ///
    /// Otherwise, returns [`None`].
    fn from_bytes(bytes: &[u8]) -> Option<&Self>
    where
        Self: Sized,
    {
        let slice_ptr = bytes.as_ptr().cast::<Self>();
        let size = size_of::<Self>();

        #[allow(clippy::incompatible_msrv)]
        if bytes.len() == size && slice_ptr.is_aligned() {
            // SAFETY: Size and alignment were just checked.
            unsafe { Some(&*slice_ptr) }
        } else {
            None
        }
    }

    /// Converts a mutable slice of bytes to a reference to `Self`.
    ///
    /// Succeeds if the reference is properly aligned, and the size of `bytes` is equal to that of
    /// `T` and different from zero.
    ///
    /// Otherwise, returns [`None`].
    fn from_bytes_mut(bytes: &mut [u8]) -> Option<&mut Self>
    where
        Self: AsBytes + Sized,
    {
        let slice_ptr = bytes.as_mut_ptr().cast::<Self>();
        let size = size_of::<Self>();

        #[allow(clippy::incompatible_msrv)]
        if bytes.len() == size && slice_ptr.is_aligned() {
            // SAFETY: Size and alignment were just checked.
            unsafe { Some(&mut *slice_ptr) }
        } else {
            None
        }
    }

    /// Creates an owned instance of `Self` by copying `bytes`.
    ///
    /// Unlike [`FromBytes::from_bytes`], which requires aligned input, this method can be used on
    /// non-aligned data at the cost of a copy.
    fn from_bytes_copy(bytes: &[u8]) -> Option<Self>
    where
        Self: Sized,
    {
        if bytes.len() == size_of::<Self>() {
            // SAFETY: we just verified that `bytes` has the same size as `Self`, and per the
            // invariants of `FromBytes`, any byte sequence of the correct length is a valid value
            // for `Self`.
            Some(unsafe { core::ptr::read_unaligned(bytes.as_ptr().cast::<Self>()) })
        } else {
            None
        }
    }
}

macro_rules! impl_frombytes {
    ($($({$($generics:tt)*})? $t:ty, )*) => {
        // SAFETY: Safety comments written in the macro invocation.
        $(unsafe impl$($($generics)*)? FromBytes for $t {})*
    };
}

impl_frombytes! {
    // SAFETY: All bit patterns are acceptable values of the types below.
    u8, u16, u32, u64, usize,
    i8, i16, i32, i64, isize,

    // SAFETY: If all bit patterns are acceptable for individual values in an array, then all bit
    // patterns are also acceptable for arrays of that type.
    {<T: FromBytes>} [T],
    {<T: FromBytes, const N: usize>} [T; N],
}

/// Types that can be viewed as an immutable slice of initialized bytes.
///
/// If a struct implements this trait, then it is okay to copy it byte-for-byte to userspace. This
/// means that it should not have any padding, as padding bytes are uninitialized. Reading
/// uninitialized memory is not just undefined behavior, it may even lead to leaking sensitive
/// information on the stack to userspace.
///
/// The struct should also not hold kernel pointers, as kernel pointer addresses are also considered
/// sensitive. However, leaking kernel pointers is not considered undefined behavior by Rust, so
/// this is a correctness requirement, but not a safety requirement.
///
/// # Safety
///
/// Values of this type may not contain any uninitialized bytes. This type must not have interior
/// mutability.
pub unsafe trait AsBytes {
    /// Returns `self` as a slice of bytes.
    fn as_bytes(&self) -> &[u8] {
        // CAST: `Self` implements `AsBytes` thus all bytes of `self` are initialized.
        let data = core::ptr::from_ref(self).cast::<u8>();
        let len = core::mem::size_of_val(self);

        // SAFETY: `data` is non-null and valid for reads of `len * sizeof::<u8>()` bytes.
        unsafe { core::slice::from_raw_parts(data, len) }
    }

    /// Returns `self` as a mutable slice of bytes.
    fn as_bytes_mut(&mut self) -> &mut [u8]
    where
        Self: FromBytes,
    {
        // CAST: `Self` implements both `AsBytes` and `FromBytes` thus making `Self`
        // bi-directionally transmutable to `[u8; size_of_val(self)]`.
        let data = core::ptr::from_mut(self).cast::<u8>();
        let len = core::mem::size_of_val(self);

        // SAFETY: `data` is non-null and valid for read and writes of `len * sizeof::<u8>()`
        // bytes.
        unsafe { core::slice::from_raw_parts_mut(data, len) }
    }
}

macro_rules! impl_asbytes {
    ($($({$($generics:tt)*})? $t:ty, )*) => {
        // SAFETY: Safety comments written in the macro invocation.
        $(unsafe impl$($($generics)*)? AsBytes for $t {})*
    };
}

impl_asbytes! {
    // SAFETY: Instances of the following types have no uninitialized portions.
    u8, u16, u32, u64, usize,
    i8, i16, i32, i64, isize,
    bool,
    char,
    str,

    // SAFETY: If individual values in an array have no uninitialized portions, then the array
    // itself does not have any uninitialized portions either.
    {<T: AsBytes>} [T],
    {<T: AsBytes, const N: usize>} [T; N],
}
