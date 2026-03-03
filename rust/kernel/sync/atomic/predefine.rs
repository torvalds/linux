// SPDX-License-Identifier: GPL-2.0

//! Pre-defined atomic types

use crate::static_assert;
use core::mem::{align_of, size_of};
use ffi::c_void;

// Ensure size and alignment requirements are checked.
static_assert!(size_of::<bool>() == size_of::<i8>());
static_assert!(align_of::<bool>() == align_of::<i8>());

// SAFETY: `bool` has the same size and alignment as `i8`, and Rust guarantees that `bool` has
// only two valid bit patterns: 0 (false) and 1 (true). Those are valid `i8` values, so `bool` is
// round-trip transmutable to `i8`.
unsafe impl super::AtomicType for bool {
    type Repr = i8;
}

// SAFETY: `i8` has the same size and alignment with itself, and is round-trip transmutable to
// itself.
unsafe impl super::AtomicType for i8 {
    type Repr = i8;
}

// SAFETY: `i16` has the same size and alignment with itself, and is round-trip transmutable to
// itself.
unsafe impl super::AtomicType for i16 {
    type Repr = i16;
}

// SAFETY:
//
// - `*mut T` has the same size and alignment with `*const c_void`, and is round-trip
//   transmutable to `*const c_void`.
// - `*mut T` is safe to transfer between execution contexts. See the safety requirement of
//   [`AtomicType`].
unsafe impl<T: Sized> super::AtomicType for *mut T {
    type Repr = *const c_void;
}

// SAFETY:
//
// - `*const T` has the same size and alignment with `*const c_void`, and is round-trip
//   transmutable to `*const c_void`.
// - `*const T` is safe to transfer between execution contexts. See the safety requirement of
//   [`AtomicType`].
unsafe impl<T: Sized> super::AtomicType for *const T {
    type Repr = *const c_void;
}

// SAFETY: `i32` has the same size and alignment with itself, and is round-trip transmutable to
// itself.
unsafe impl super::AtomicType for i32 {
    type Repr = i32;
}

// SAFETY: The wrapping add result of two `i32`s is a valid `i32`.
unsafe impl super::AtomicAdd<i32> for i32 {
    fn rhs_into_delta(rhs: i32) -> i32 {
        rhs
    }
}

// SAFETY: `i64` has the same size and alignment with itself, and is round-trip transmutable to
// itself.
unsafe impl super::AtomicType for i64 {
    type Repr = i64;
}

// SAFETY: The wrapping add result of two `i64`s is a valid `i64`.
unsafe impl super::AtomicAdd<i64> for i64 {
    fn rhs_into_delta(rhs: i64) -> i64 {
        rhs
    }
}

// Defines an internal type that always maps to the integer type which has the same size alignment
// as `isize` and `usize`, and `isize` and `usize` are always bi-directional transmutable to
// `isize_atomic_repr`, which also always implements `AtomicImpl`.
#[allow(non_camel_case_types)]
#[cfg(not(testlib))]
#[cfg(not(CONFIG_64BIT))]
type isize_atomic_repr = i32;
#[allow(non_camel_case_types)]
#[cfg(not(testlib))]
#[cfg(CONFIG_64BIT)]
type isize_atomic_repr = i64;

#[allow(non_camel_case_types)]
#[cfg(testlib)]
#[cfg(target_pointer_width = "32")]
type isize_atomic_repr = i32;
#[allow(non_camel_case_types)]
#[cfg(testlib)]
#[cfg(target_pointer_width = "64")]
type isize_atomic_repr = i64;

// Ensure size and alignment requirements are checked.
static_assert!(size_of::<isize>() == size_of::<isize_atomic_repr>());
static_assert!(align_of::<isize>() == align_of::<isize_atomic_repr>());
static_assert!(size_of::<usize>() == size_of::<isize_atomic_repr>());
static_assert!(align_of::<usize>() == align_of::<isize_atomic_repr>());

// SAFETY: `isize` has the same size and alignment with `isize_atomic_repr`, and is round-trip
// transmutable to `isize_atomic_repr`.
unsafe impl super::AtomicType for isize {
    type Repr = isize_atomic_repr;
}

// SAFETY: The wrapping add result of two `isize_atomic_repr`s is a valid `usize`.
unsafe impl super::AtomicAdd<isize> for isize {
    fn rhs_into_delta(rhs: isize) -> isize_atomic_repr {
        rhs as isize_atomic_repr
    }
}

// SAFETY: `u32` and `i32` has the same size and alignment, and `u32` is round-trip transmutable to
// `i32`.
unsafe impl super::AtomicType for u32 {
    type Repr = i32;
}

// SAFETY: The wrapping add result of two `i32`s is a valid `u32`.
unsafe impl super::AtomicAdd<u32> for u32 {
    fn rhs_into_delta(rhs: u32) -> i32 {
        rhs as i32
    }
}

// SAFETY: `u64` and `i64` has the same size and alignment, and `u64` is round-trip transmutable to
// `i64`.
unsafe impl super::AtomicType for u64 {
    type Repr = i64;
}

// SAFETY: The wrapping add result of two `i64`s is a valid `u64`.
unsafe impl super::AtomicAdd<u64> for u64 {
    fn rhs_into_delta(rhs: u64) -> i64 {
        rhs as i64
    }
}

// SAFETY: `usize` has the same size and alignment with `isize_atomic_repr`, and is round-trip
// transmutable to `isize_atomic_repr`.
unsafe impl super::AtomicType for usize {
    type Repr = isize_atomic_repr;
}

// SAFETY: The wrapping add result of two `isize_atomic_repr`s is a valid `usize`.
unsafe impl super::AtomicAdd<usize> for usize {
    fn rhs_into_delta(rhs: usize) -> isize_atomic_repr {
        rhs as isize_atomic_repr
    }
}

use crate::macros::kunit_tests;

#[kunit_tests(rust_atomics)]
mod tests {
    use super::super::*;

    // Call $fn($val) with each $type of $val.
    macro_rules! for_each_type {
        ($val:literal in [$($type:ty),*] $fn:expr) => {
            $({
                let v: $type = $val;

                $fn(v);
            })*
        }
    }

    #[test]
    fn atomic_basic_tests() {
        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(v);

            assert_eq!(v, x.load(Relaxed));
        });

        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(v);
            let ptr = x.as_ptr();

            // SAFETY: `ptr` is a valid pointer and no concurrent access.
            assert_eq!(v, unsafe { atomic_load(ptr, Relaxed) });
        });
    }

    #[test]
    fn atomic_acquire_release_tests() {
        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(0);

            x.store(v, Release);
            assert_eq!(v, x.load(Acquire));
        });

        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(0);
            let ptr = x.as_ptr();

            // SAFETY: `ptr` is a valid pointer and no concurrent access.
            unsafe { atomic_store(ptr, v, Release) };

            // SAFETY: `ptr` is a valid pointer and no concurrent access.
            assert_eq!(v, unsafe { atomic_load(ptr, Acquire) });
        });
    }

    #[test]
    fn atomic_xchg_tests() {
        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(v);

            let old = v;
            let new = v + 1;

            assert_eq!(old, x.xchg(new, Full));
            assert_eq!(new, x.load(Relaxed));
        });

        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(v);
            let ptr = x.as_ptr();

            let old = v;
            let new = v + 1;

            // SAFETY: `ptr` is a valid pointer and no concurrent access.
            assert_eq!(old, unsafe { xchg(ptr, new, Full) });
            assert_eq!(new, x.load(Relaxed));
        });
    }

    #[test]
    fn atomic_cmpxchg_tests() {
        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(v);

            let old = v;
            let new = v + 1;

            assert_eq!(Err(old), x.cmpxchg(new, new, Full));
            assert_eq!(old, x.load(Relaxed));
            assert_eq!(Ok(old), x.cmpxchg(old, new, Relaxed));
            assert_eq!(new, x.load(Relaxed));
        });

        for_each_type!(42 in [i8, i16, i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(v);
            let ptr = x.as_ptr();

            let old = v;
            let new = v + 1;

            // SAFETY: `ptr` is a valid pointer and no concurrent access.
            assert_eq!(Err(old), unsafe { cmpxchg(ptr, new, new, Full) });
            assert_eq!(old, x.load(Relaxed));
            // SAFETY: `ptr` is a valid pointer and no concurrent access.
            assert_eq!(Ok(old), unsafe { cmpxchg(ptr, old, new, Relaxed) });
            assert_eq!(new, x.load(Relaxed));
        });
    }

    #[test]
    fn atomic_arithmetic_tests() {
        for_each_type!(42 in [i32, i64, u32, u64, isize, usize] |v| {
            let x = Atomic::new(v);

            assert_eq!(v, x.fetch_add(12, Full));
            assert_eq!(v + 12, x.load(Relaxed));

            x.add(13, Relaxed);

            assert_eq!(v + 25, x.load(Relaxed));
        });
    }

    #[test]
    fn atomic_bool_tests() {
        let x = Atomic::new(false);

        assert_eq!(false, x.load(Relaxed));
        x.store(true, Relaxed);
        assert_eq!(true, x.load(Relaxed));

        assert_eq!(true, x.xchg(false, Relaxed));
        assert_eq!(false, x.load(Relaxed));

        assert_eq!(Err(false), x.cmpxchg(true, true, Relaxed));
        assert_eq!(false, x.load(Relaxed));
        assert_eq!(Ok(false), x.cmpxchg(false, true, Full));
    }

    #[test]
    fn atomic_ptr_tests() {
        let mut v = 42;
        let mut u = 43;
        let x = Atomic::new(&raw mut v);

        assert_eq!(x.load(Acquire), &raw mut v);
        assert_eq!(x.cmpxchg(&raw mut u, &raw mut u, Relaxed), Err(&raw mut v));
        assert_eq!(x.cmpxchg(&raw mut v, &raw mut u, Relaxed), Ok(&raw mut v));
        assert_eq!(x.load(Relaxed), &raw mut u);

        let x = Atomic::new(&raw const v);

        assert_eq!(x.load(Acquire), &raw const v);
        assert_eq!(
            x.cmpxchg(&raw const u, &raw const u, Relaxed),
            Err(&raw const v)
        );
        assert_eq!(
            x.cmpxchg(&raw const v, &raw const u, Relaxed),
            Ok(&raw const v)
        );
        assert_eq!(x.load(Relaxed), &raw const u);
    }

    #[test]
    fn atomic_flag_tests() {
        let mut flag = AtomicFlag::new(false);

        assert_eq!(false, flag.load(Relaxed));

        *flag.get_mut() = true;
        assert_eq!(true, flag.load(Relaxed));

        assert_eq!(true, flag.xchg(false, Relaxed));
        assert_eq!(false, flag.load(Relaxed));

        *flag.get_mut() = true;
        assert_eq!(Ok(true), flag.cmpxchg(true, false, Full));
        assert_eq!(false, flag.load(Relaxed));
    }
}
