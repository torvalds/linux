// SPDX-License-Identifier: GPL-2.0

//! Basic utilities for dealing with memory, values, and types.

// TODO: `Alignment` should be moved here as well, Rust moved it upstream in 1.96.
pub use crate::ptr::Alignment;

/// Trait indicating that [`Align<N>`] is a valid alignment.
pub trait ValidAlign {
    #[doc(hidden)]
    type Repr: Copy;
}

/// Zero-sized type that provides the alignment specified via the generic parameter.
///
/// # Examples
///
/// ```
/// # use kernel::mem::Align;
/// const ALIGN: usize = 128;
/// struct MyStruct {
///     _align: Align<ALIGN>,
/// }
/// assert_eq!(align_of::<MyStruct>(), ALIGN);
/// ```
#[repr(transparent)]
#[derive(Default, Clone, Copy)]
pub struct Align<const N: usize>([<Self as ValidAlign>::Repr; 0])
where
    Self: ValidAlign;

impl<const N: usize> Align<N>
where
    Self: ValidAlign,
{
    /// Create a new [`Align<N>`].
    #[inline]
    pub const fn new() -> Self {
        Align([])
    }
}

macro_rules! impl_align {
    () => {};
    ($a:literal $($rest:literal)*) => {
        const _: () = {
            #[repr(align($a))]
            #[derive(Clone, Copy)]
            pub struct Repr;

            impl ValidAlign for Align<$a> {
                type Repr = Repr;
            }
        };

        impl_align!($($rest)*);
    }
}

impl_align!(1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536);

impl<const N: usize> core::fmt::Debug for Align<N>
where
    Self: ValidAlign,
{
    #[inline]
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "Align<{N}>")
    }
}
