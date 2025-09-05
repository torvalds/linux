// SPDX-License-Identifier: GPL-2.0

//! Pre-defined atomic types

// SAFETY: `i32` has the same size and alignment with itself, and is round-trip transmutable to
// itself.
unsafe impl super::AtomicType for i32 {
    type Repr = i32;
}

// SAFETY: `i64` has the same size and alignment with itself, and is round-trip transmutable to
// itself.
unsafe impl super::AtomicType for i64 {
    type Repr = i64;
}
