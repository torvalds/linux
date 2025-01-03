// SPDX-License-Identifier: GPL-2.0

//! Traits for transmuting types.

/// Types for which any bit pattern is valid.
///
/// Not all types are valid for all values. For example, a `bool` must be either zero or one, so
/// reading arbitrary bytes into something that contains a `bool` is not okay.
///
/// It's okay for the type to have padding, as initializing those bytes has no effect.
///
/// # Safety
///
/// All bit-patterns must be valid for this type. This type must not have interior mutability.
pub unsafe trait FromBytes {}

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
pub unsafe trait AsBytes {}

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
