// SPDX-License-Identifier: GPL-2.0

//! Implementation of [`Bounded`], a wrapper around integer types limiting the number of bits
//! usable for value representation.

use core::{
    cmp,
    fmt,
    ops::{
        self,
        Deref, //
    }, //,
};

use kernel::{
    num::Integer,
    prelude::*, //
};

/// Evaluates to `true` if `$value` can be represented using at most `$n` bits in a `$type`.
///
/// `expr` must be of type `type`, or the result will be incorrect.
///
/// Can be used in const context.
macro_rules! fits_within {
    ($value:expr, $type:ty, $n:expr) => {{
        let shift: u32 = <$type>::BITS - $n;

        // `value` fits within `$n` bits if shifting it left by the number of unused bits, then
        // right by the same number, doesn't change it.
        //
        // This method has the benefit of working for both unsigned and signed values.
        ($value << shift) >> shift == $value
    }};
}

/// Returns `true` if `value` can be represented with at most `N` bits in a `T`.
#[inline(always)]
fn fits_within<T: Integer>(value: T, num_bits: u32) -> bool {
    fits_within!(value, T, num_bits)
}

/// An integer value that requires only the `N` least significant bits of the wrapped type to be
/// encoded.
///
/// This limits the number of usable bits in the wrapped integer type, and thus the stored value to
/// a narrower range, which provides guarantees that can be useful when working within e.g.
/// bitfields.
///
/// # Invariants
///
/// - `N` is greater than `0`.
/// - `N` is less than or equal to `T::BITS`.
/// - Stored values can be represented with at most `N` bits.
///
/// # Examples
///
/// The preferred way to create values is through constants and the [`Bounded::new`] family of
/// constructors, as they trigger a build error if the type invariants cannot be upheld.
///
/// ```
/// use kernel::num::Bounded;
///
/// // An unsigned 8-bit integer, of which only the 4 LSBs are used.
/// // The value `15` is statically validated to fit that constraint at build time.
/// let v = Bounded::<u8, 4>::new::<15>();
/// assert_eq!(v.get(), 15);
///
/// // Same using signed values.
/// let v = Bounded::<i8, 4>::new::<-8>();
/// assert_eq!(v.get(), -8);
///
/// // This doesn't build: a `u8` is smaller than the requested 9 bits.
/// // let _ = Bounded::<u8, 9>::new::<10>();
///
/// // This also doesn't build: the requested value doesn't fit within 4 signed bits.
/// // let _ = Bounded::<i8, 4>::new::<8>();
/// ```
///
/// Values can also be validated at runtime with [`Bounded::try_new`].
///
/// ```
/// use kernel::num::Bounded;
///
/// // This succeeds because `15` can be represented with 4 unsigned bits.
/// assert!(Bounded::<u8, 4>::try_new(15).is_some());
///
/// // This fails because `16` cannot be represented with 4 unsigned bits.
/// assert!(Bounded::<u8, 4>::try_new(16).is_none());
/// ```
///
/// Non-constant expressions can be validated at build-time thanks to compiler optimizations. This
/// should be used with caution, on simple expressions only.
///
/// ```
/// use kernel::num::Bounded;
/// # fn some_number() -> u32 { 0xffffffff }
///
/// // Here the compiler can infer from the mask that the type invariants are not violated, even
/// // though the value returned by `some_number` is not statically known.
/// let v = Bounded::<u32, 4>::from_expr(some_number() & 0xf);
/// ```
///
/// Comparison and arithmetic operations are supported on [`Bounded`]s with a compatible backing
/// type, regardless of their number of valid bits.
///
/// ```
/// use kernel::num::Bounded;
///
/// let v1 = Bounded::<u32, 8>::new::<4>();
/// let v2 = Bounded::<u32, 4>::new::<15>();
///
/// assert!(v1 != v2);
/// assert!(v1 < v2);
/// assert_eq!(v1 + v2, 19);
/// assert_eq!(v2 % v1, 3);
/// ```
///
/// These operations are also supported between a [`Bounded`] and its backing type.
///
/// ```
/// use kernel::num::Bounded;
///
/// let v = Bounded::<u8, 4>::new::<15>();
///
/// assert!(v == 15);
/// assert!(v > 12);
/// assert_eq!(v + 5, 20);
/// assert_eq!(v / 3, 5);
/// ```
///
/// A change of backing types is possible using [`Bounded::cast`], and the number of valid bits can
/// be extended or reduced with [`Bounded::extend`] and [`Bounded::try_shrink`].
///
/// ```
/// use kernel::num::Bounded;
///
/// let v = Bounded::<u32, 12>::new::<127>();
///
/// // Changes backing type from `u32` to `u16`.
/// let _: Bounded<u16, 12> = v.cast();
///
/// // This does not build, as `u8` is smaller than 12 bits.
/// // let _: Bounded<u8, 12> = v.cast();
///
/// // We can safely extend the number of bits...
/// let _ = v.extend::<15>();
///
/// // ... to the limits of the backing type. This doesn't build as a `u32` cannot contain 33 bits.
/// // let _ = v.extend::<33>();
///
/// // Reducing the number of bits is validated at runtime. This works because `127` can be
/// // represented with 8 bits.
/// assert!(v.try_shrink::<8>().is_some());
///
/// // ... but not with 6, so this fails.
/// assert!(v.try_shrink::<6>().is_none());
/// ```
///
/// Infallible conversions from a primitive integer to a large-enough [`Bounded`] are supported.
///
/// ```
/// use kernel::num::Bounded;
///
/// // This unsigned `Bounded` has 8 bits, so it can represent any `u8`.
/// let v = Bounded::<u32, 8>::from(128u8);
/// assert_eq!(v.get(), 128);
///
/// // This signed `Bounded` has 8 bits, so it can represent any `i8`.
/// let v = Bounded::<i32, 8>::from(-128i8);
/// assert_eq!(v.get(), -128);
///
/// // This doesn't build, as this 6-bit `Bounded` does not have enough capacity to represent a
/// // `u8` (regardless of the passed value).
/// // let _ = Bounded::<u32, 6>::from(10u8);
///
/// // Booleans can be converted into single-bit `Bounded`s.
///
/// let v = Bounded::<u64, 1>::from(false);
/// assert_eq!(v.get(), 0);
///
/// let v = Bounded::<u64, 1>::from(true);
/// assert_eq!(v.get(), 1);
/// ```
///
/// Infallible conversions from a [`Bounded`] to a primitive integer are also supported, and
/// dependent on the number of bits used for value representation, not on the backing type.
///
/// ```
/// use kernel::num::Bounded;
///
/// // Even though its backing type is `u32`, this `Bounded` only uses 6 bits and thus can safely
/// // be converted to a `u8`.
/// let v = Bounded::<u32, 6>::new::<63>();
/// assert_eq!(u8::from(v), 63);
///
/// // Same using signed values.
/// let v = Bounded::<i32, 8>::new::<-128>();
/// assert_eq!(i8::from(v), -128);
///
/// // This however does not build, as 10 bits won't fit into a `u8` (regardless of the actually
/// // contained value).
/// let _v = Bounded::<u32, 10>::new::<10>();
/// // assert_eq!(u8::from(_v), 10);
///
/// // Single-bit `Bounded`s can be converted into a boolean.
/// let v = Bounded::<u8, 1>::new::<1>();
/// assert_eq!(bool::from(v), true);
///
/// let v = Bounded::<u8, 1>::new::<0>();
/// assert_eq!(bool::from(v), false);
/// ```
///
/// Fallible conversions from any primitive integer to any [`Bounded`] are also supported using the
/// [`TryIntoBounded`] trait.
///
/// ```
/// use kernel::num::{Bounded, TryIntoBounded};
///
/// // Succeeds because `128` fits into 8 bits.
/// let v: Option<Bounded<u16, 8>> = 128u32.try_into_bounded();
/// assert_eq!(v.as_deref().copied(), Some(128));
///
/// // Fails because `128` doesn't fit into 6 bits.
/// let v: Option<Bounded<u16, 6>> = 128u32.try_into_bounded();
/// assert_eq!(v, None);
/// ```
#[repr(transparent)]
#[derive(Clone, Copy, Debug, Default, Hash)]
pub struct Bounded<T: Integer, const N: u32>(T);

/// Validating the value as a const expression cannot be done as a regular method, as the
/// arithmetic operations we rely on to check the bounds are not const. Thus, implement
/// [`Bounded::new`] using a macro.
macro_rules! impl_const_new {
    ($($type:ty)*) => {
        $(
        impl<const N: u32> Bounded<$type, N> {
            /// Creates a [`Bounded`] for the constant `VALUE`.
            ///
            /// Fails at build time if `VALUE` cannot be represented with `N` bits.
            ///
            /// This method should be preferred to [`Self::from_expr`] whenever possible.
            ///
            /// # Examples
            ///
            /// ```
            /// use kernel::num::Bounded;
            ///
            #[doc = ::core::concat!(
                "let v = Bounded::<",
                ::core::stringify!($type),
                ", 4>::new::<7>();")]
            /// assert_eq!(v.get(), 7);
            /// ```
            pub const fn new<const VALUE: $type>() -> Self {
                // Statically assert that `VALUE` fits within the set number of bits.
                const {
                    assert!(fits_within!(VALUE, $type, N));
                }

                // SAFETY: `fits_within` confirmed that `VALUE` can be represented within
                // `N` bits.
                unsafe { Self::__new(VALUE) }
            }
        }
        )*
    };
}

impl_const_new!(
    u8 u16 u32 u64 usize
    i8 i16 i32 i64 isize
);

impl<T, const N: u32> Bounded<T, N>
where
    T: Integer,
{
    /// Private constructor enforcing the type invariants.
    ///
    /// All instances of [`Bounded`] must be created through this method as it enforces most of the
    /// type invariants.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `value` can be represented within `N` bits.
    const unsafe fn __new(value: T) -> Self {
        // Enforce the type invariants.
        const {
            // `N` cannot be zero.
            assert!(N != 0);
            // The backing type is at least as large as `N` bits.
            assert!(N <= T::BITS);
        }

        // INVARIANT: The caller ensures `value` fits within `N` bits.
        Self(value)
    }

    /// Attempts to turn `value` into a `Bounded` using `N` bits.
    ///
    /// Returns [`None`] if `value` doesn't fit within `N` bits.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::num::Bounded;
    ///
    /// let v = Bounded::<u8, 1>::try_new(1);
    /// assert_eq!(v.as_deref().copied(), Some(1));
    ///
    /// let v = Bounded::<i8, 4>::try_new(-2);
    /// assert_eq!(v.as_deref().copied(), Some(-2));
    ///
    /// // `0x1ff` doesn't fit into 8 unsigned bits.
    /// let v = Bounded::<u32, 8>::try_new(0x1ff);
    /// assert_eq!(v, None);
    ///
    /// // The range of values representable with 4 bits is `[-8..=7]`. The following tests these
    /// // limits.
    /// let v = Bounded::<i8, 4>::try_new(-8);
    /// assert_eq!(v.map(Bounded::get), Some(-8));
    /// let v = Bounded::<i8, 4>::try_new(-9);
    /// assert_eq!(v, None);
    /// let v = Bounded::<i8, 4>::try_new(7);
    /// assert_eq!(v.map(Bounded::get), Some(7));
    /// let v = Bounded::<i8, 4>::try_new(8);
    /// assert_eq!(v, None);
    /// ```
    pub fn try_new(value: T) -> Option<Self> {
        fits_within(value, N).then(|| {
            // SAFETY: `fits_within` confirmed that `value` can be represented within `N` bits.
            unsafe { Self::__new(value) }
        })
    }

    /// Checks that `expr` is valid for this type at compile-time and build a new value.
    ///
    /// This relies on [`build_assert!`] and guaranteed optimization to perform validation at
    /// compile-time. If `expr` cannot be proved to be within the requested bounds at compile-time,
    /// use the fallible [`Self::try_new`] instead.
    ///
    /// Limit this to simple, easily provable expressions, and prefer one of the [`Self::new`]
    /// constructors whenever possible as they statically validate the value instead of relying on
    /// compiler optimizations.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::num::Bounded;
    /// # fn some_number() -> u32 { 0xffffffff }
    ///
    /// // Some undefined number.
    /// let v: u32 = some_number();
    ///
    /// // Triggers a build error as `v` cannot be asserted to fit within 4 bits...
    /// // let _ = Bounded::<u32, 4>::from_expr(v);
    ///
    /// // ... but this works as the compiler can assert the range from the mask.
    /// let _ = Bounded::<u32, 4>::from_expr(v & 0xf);
    ///
    /// // These expressions are simple enough to be proven correct, but since they are static the
    /// // `new` constructor should be preferred.
    /// assert_eq!(Bounded::<u8, 1>::from_expr(1).get(), 1);
    /// assert_eq!(Bounded::<u16, 8>::from_expr(0xff).get(), 0xff);
    /// ```
    // Always inline to optimize out error path of `build_assert`.
    #[inline(always)]
    pub fn from_expr(expr: T) -> Self {
        crate::build_assert!(
            fits_within(expr, N),
            "Requested value larger than maximal representable value."
        );

        // SAFETY: `fits_within` confirmed that `expr` can be represented within `N` bits.
        unsafe { Self::__new(expr) }
    }

    /// Returns the wrapped value as the backing type.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::num::Bounded;
    ///
    /// let v = Bounded::<u32, 4>::new::<7>();
    /// assert_eq!(v.get(), 7u32);
    /// ```
    pub fn get(self) -> T {
        *self.deref()
    }

    /// Increases the number of bits usable for `self`.
    ///
    /// This operation cannot fail.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::num::Bounded;
    ///
    /// let v = Bounded::<u32, 4>::new::<7>();
    /// let larger_v = v.extend::<12>();
    /// // The contained values are equal even though `larger_v` has a bigger capacity.
    /// assert_eq!(larger_v, v);
    /// ```
    pub const fn extend<const M: u32>(self) -> Bounded<T, M> {
        const {
            assert!(
                M >= N,
                "Requested number of bits is less than the current representation."
            );
        }

        // SAFETY: The value did fit within `N` bits, so it will all the more fit within
        // the larger `M` bits.
        unsafe { Bounded::__new(self.0) }
    }

    /// Attempts to shrink the number of bits usable for `self`.
    ///
    /// Returns [`None`] if the value of `self` cannot be represented within `M` bits.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::num::Bounded;
    ///
    /// let v = Bounded::<u32, 12>::new::<7>();
    ///
    /// // `7` can be represented using 3 unsigned bits...
    /// let smaller_v = v.try_shrink::<3>();
    /// assert_eq!(smaller_v.as_deref().copied(), Some(7));
    ///
    /// // ... but doesn't fit within `2` bits.
    /// assert_eq!(v.try_shrink::<2>(), None);
    /// ```
    pub fn try_shrink<const M: u32>(self) -> Option<Bounded<T, M>> {
        Bounded::<T, M>::try_new(self.get())
    }

    /// Casts `self` into a [`Bounded`] backed by a different storage type, but using the same
    /// number of valid bits.
    ///
    /// Both `T` and `U` must be of same signedness, and `U` must be at least as large as
    /// `N` bits, or a build error will occur.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::num::Bounded;
    ///
    /// let v = Bounded::<u32, 12>::new::<127>();
    ///
    /// let u16_v: Bounded<u16, 12> = v.cast();
    /// assert_eq!(u16_v.get(), 127);
    ///
    /// // This won't build: a `u8` is smaller than the required 12 bits.
    /// // let _: Bounded<u8, 12> = v.cast();
    /// ```
    pub fn cast<U>(self) -> Bounded<U, N>
    where
        U: TryFrom<T> + Integer,
        T: Integer,
        U: Integer<Signedness = T::Signedness>,
    {
        // SAFETY: The converted value is represented using `N` bits, `U` can contain `N` bits, and
        // `U` and `T` have the same sign, hence this conversion cannot fail.
        let value = unsafe { U::try_from(self.get()).unwrap_unchecked() };

        // SAFETY: Although the backing type has changed, the value is still represented within
        // `N` bits, and with the same signedness.
        unsafe { Bounded::__new(value) }
    }
}

impl<T, const N: u32> Deref for Bounded<T, N>
where
    T: Integer,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // Enforce the invariant to inform the compiler of the bounds of the value.
        if !fits_within(self.0, N) {
            // SAFETY: Per the `Bounded` invariants, `fits_within` can never return `false` on the
            // value of a valid instance.
            unsafe { core::hint::unreachable_unchecked() }
        }

        &self.0
    }
}

/// Trait similar to [`TryInto`] but for [`Bounded`], to avoid conflicting implementations.
///
/// # Examples
///
/// ```
/// use kernel::num::{Bounded, TryIntoBounded};
///
/// // Succeeds because `128` fits into 8 bits.
/// let v: Option<Bounded<u16, 8>> = 128u32.try_into_bounded();
/// assert_eq!(v.as_deref().copied(), Some(128));
///
/// // Fails because `128` doesn't fit into 6 bits.
/// let v: Option<Bounded<u16, 6>> = 128u32.try_into_bounded();
/// assert_eq!(v, None);
/// ```
pub trait TryIntoBounded<T: Integer, const N: u32> {
    /// Attempts to convert `self` into a [`Bounded`] using `N` bits.
    ///
    /// Returns [`None`] if `self` does not fit into the target type.
    fn try_into_bounded(self) -> Option<Bounded<T, N>>;
}

/// Any integer value can be attempted to be converted into a [`Bounded`] of any size.
impl<T, U, const N: u32> TryIntoBounded<T, N> for U
where
    T: Integer,
    U: TryInto<T>,
{
    fn try_into_bounded(self) -> Option<Bounded<T, N>> {
        self.try_into().ok().and_then(Bounded::try_new)
    }
}

// Comparisons between `Bounded`s.

impl<T, U, const N: u32, const M: u32> PartialEq<Bounded<U, M>> for Bounded<T, N>
where
    T: Integer,
    U: Integer,
    T: PartialEq<U>,
{
    fn eq(&self, other: &Bounded<U, M>) -> bool {
        self.get() == other.get()
    }
}

impl<T, const N: u32> Eq for Bounded<T, N> where T: Integer {}

impl<T, U, const N: u32, const M: u32> PartialOrd<Bounded<U, M>> for Bounded<T, N>
where
    T: Integer,
    U: Integer,
    T: PartialOrd<U>,
{
    fn partial_cmp(&self, other: &Bounded<U, M>) -> Option<cmp::Ordering> {
        self.get().partial_cmp(&other.get())
    }
}

impl<T, const N: u32> Ord for Bounded<T, N>
where
    T: Integer,
    T: Ord,
{
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.get().cmp(&other.get())
    }
}

// Comparisons between a `Bounded` and its backing type.

impl<T, const N: u32> PartialEq<T> for Bounded<T, N>
where
    T: Integer,
    T: PartialEq,
{
    fn eq(&self, other: &T) -> bool {
        self.get() == *other
    }
}

impl<T, const N: u32> PartialOrd<T> for Bounded<T, N>
where
    T: Integer,
    T: PartialOrd,
{
    fn partial_cmp(&self, other: &T) -> Option<cmp::Ordering> {
        self.get().partial_cmp(other)
    }
}

// Implementations of `core::ops` for two `Bounded` with the same backing type.

impl<T, const N: u32, const M: u32> ops::Add<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::Add<Output = T>,
{
    type Output = T;

    fn add(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() + rhs.get()
    }
}

impl<T, const N: u32, const M: u32> ops::BitAnd<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::BitAnd<Output = T>,
{
    type Output = T;

    fn bitand(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() & rhs.get()
    }
}

impl<T, const N: u32, const M: u32> ops::BitOr<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::BitOr<Output = T>,
{
    type Output = T;

    fn bitor(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() | rhs.get()
    }
}

impl<T, const N: u32, const M: u32> ops::BitXor<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::BitXor<Output = T>,
{
    type Output = T;

    fn bitxor(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() ^ rhs.get()
    }
}

impl<T, const N: u32, const M: u32> ops::Div<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::Div<Output = T>,
{
    type Output = T;

    fn div(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() / rhs.get()
    }
}

impl<T, const N: u32, const M: u32> ops::Mul<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::Mul<Output = T>,
{
    type Output = T;

    fn mul(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() * rhs.get()
    }
}

impl<T, const N: u32, const M: u32> ops::Rem<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::Rem<Output = T>,
{
    type Output = T;

    fn rem(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() % rhs.get()
    }
}

impl<T, const N: u32, const M: u32> ops::Sub<Bounded<T, M>> for Bounded<T, N>
where
    T: Integer,
    T: ops::Sub<Output = T>,
{
    type Output = T;

    fn sub(self, rhs: Bounded<T, M>) -> Self::Output {
        self.get() - rhs.get()
    }
}

// Implementations of `core::ops` between a `Bounded` and its backing type.

impl<T, const N: u32> ops::Add<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::Add<Output = T>,
{
    type Output = T;

    fn add(self, rhs: T) -> Self::Output {
        self.get() + rhs
    }
}

impl<T, const N: u32> ops::BitAnd<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::BitAnd<Output = T>,
{
    type Output = T;

    fn bitand(self, rhs: T) -> Self::Output {
        self.get() & rhs
    }
}

impl<T, const N: u32> ops::BitOr<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::BitOr<Output = T>,
{
    type Output = T;

    fn bitor(self, rhs: T) -> Self::Output {
        self.get() | rhs
    }
}

impl<T, const N: u32> ops::BitXor<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::BitXor<Output = T>,
{
    type Output = T;

    fn bitxor(self, rhs: T) -> Self::Output {
        self.get() ^ rhs
    }
}

impl<T, const N: u32> ops::Div<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::Div<Output = T>,
{
    type Output = T;

    fn div(self, rhs: T) -> Self::Output {
        self.get() / rhs
    }
}

impl<T, const N: u32> ops::Mul<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::Mul<Output = T>,
{
    type Output = T;

    fn mul(self, rhs: T) -> Self::Output {
        self.get() * rhs
    }
}

impl<T, const N: u32> ops::Neg for Bounded<T, N>
where
    T: Integer,
    T: ops::Neg<Output = T>,
{
    type Output = T;

    fn neg(self) -> Self::Output {
        -self.get()
    }
}

impl<T, const N: u32> ops::Not for Bounded<T, N>
where
    T: Integer,
    T: ops::Not<Output = T>,
{
    type Output = T;

    fn not(self) -> Self::Output {
        !self.get()
    }
}

impl<T, const N: u32> ops::Rem<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::Rem<Output = T>,
{
    type Output = T;

    fn rem(self, rhs: T) -> Self::Output {
        self.get() % rhs
    }
}

impl<T, const N: u32> ops::Sub<T> for Bounded<T, N>
where
    T: Integer,
    T: ops::Sub<Output = T>,
{
    type Output = T;

    fn sub(self, rhs: T) -> Self::Output {
        self.get() - rhs
    }
}

// Proxy implementations of `core::fmt`.

impl<T, const N: u32> fmt::Display for Bounded<T, N>
where
    T: Integer,
    T: fmt::Display,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T, const N: u32> fmt::Binary for Bounded<T, N>
where
    T: Integer,
    T: fmt::Binary,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T, const N: u32> fmt::LowerExp for Bounded<T, N>
where
    T: Integer,
    T: fmt::LowerExp,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T, const N: u32> fmt::LowerHex for Bounded<T, N>
where
    T: Integer,
    T: fmt::LowerHex,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T, const N: u32> fmt::Octal for Bounded<T, N>
where
    T: Integer,
    T: fmt::Octal,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T, const N: u32> fmt::UpperExp for Bounded<T, N>
where
    T: Integer,
    T: fmt::UpperExp,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T, const N: u32> fmt::UpperHex for Bounded<T, N>
where
    T: Integer,
    T: fmt::UpperHex,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

/// Implements `$trait` for all [`Bounded`] types represented using `$num_bits`.
///
/// This is used to declare size properties as traits that we can constrain against in impl blocks.
macro_rules! impl_size_rule {
    ($trait:ty, $($num_bits:literal)*) => {
        $(
        impl<T> $trait for Bounded<T, $num_bits> where T: Integer {}
        )*
    };
}

/// Local trait expressing the fact that a given [`Bounded`] has at least `N` bits used for value
/// representation.
trait AtLeastXBits<const N: usize> {}

/// Implementations for infallibly converting a primitive type into a [`Bounded`] that can contain
/// it.
///
/// Put into their own module for readability, and to avoid cluttering the rustdoc of the parent
/// module.
mod atleast_impls {
    use super::*;

    // Number of bits at least as large as 64.
    impl_size_rule!(AtLeastXBits<64>, 64);

    // Anything 64 bits or more is also larger than 32.
    impl<T> AtLeastXBits<32> for T where T: AtLeastXBits<64> {}
    // Other numbers of bits at least as large as 32.
    impl_size_rule!(AtLeastXBits<32>,
        32 33 34 35 36 37 38 39
        40 41 42 43 44 45 46 47
        48 49 50 51 52 53 54 55
        56 57 58 59 60 61 62 63
    );

    // Anything 32 bits or more is also larger than 16.
    impl<T> AtLeastXBits<16> for T where T: AtLeastXBits<32> {}
    // Other numbers of bits at least as large as 16.
    impl_size_rule!(AtLeastXBits<16>,
        16 17 18 19 20 21 22 23
        24 25 26 27 28 29 30 31
    );

    // Anything 16 bits or more is also larger than 8.
    impl<T> AtLeastXBits<8> for T where T: AtLeastXBits<16> {}
    // Other numbers of bits at least as large as 8.
    impl_size_rule!(AtLeastXBits<8>, 8 9 10 11 12 13 14 15);
}

/// Generates `From` implementations from a primitive type into a [`Bounded`] with
/// enough bits to store any value of that type.
///
/// Note: The only reason for having this macro is that if we pass `$type` as a generic
/// parameter, we cannot use it in the const context of [`AtLeastXBits`]'s generic parameter. This
/// can be fixed once the `generic_const_exprs` feature is usable, and this macro replaced by a
/// regular `impl` block.
macro_rules! impl_from_primitive {
    ($($type:ty)*) => {
        $(
        #[doc = ::core::concat!(
            "Conversion from a [`",
            ::core::stringify!($type),
            "`] into a [`Bounded`] of same signedness with enough bits to store it.")]
        impl<T, const N: u32> From<$type> for Bounded<T, N>
        where
            $type: Integer,
            T: Integer<Signedness = <$type as Integer>::Signedness> + From<$type>,
            Self: AtLeastXBits<{ <$type as Integer>::BITS as usize }>,
        {
            fn from(value: $type) -> Self {
                // SAFETY: The trait bound on `Self` guarantees that `N` bits is
                // enough to hold any value of the source type.
                unsafe { Self::__new(T::from(value)) }
            }
        }
        )*
    }
}

impl_from_primitive!(
    u8 u16 u32 u64 usize
    i8 i16 i32 i64 isize
);

/// Local trait expressing the fact that a given [`Bounded`] fits into a primitive type of `N` bits,
/// provided they have the same signedness.
trait FitsInXBits<const N: usize> {}

/// Implementations for infallibly converting a [`Bounded`] into a primitive type that can contain
/// it.
///
/// Put into their own module for readability, and to avoid cluttering the rustdoc of the parent
/// module.
mod fits_impls {
    use super::*;

    // Number of bits that fit into a 8-bits primitive.
    impl_size_rule!(FitsInXBits<8>, 1 2 3 4 5 6 7 8);

    // Anything that fits into 8 bits also fits into 16.
    impl<T> FitsInXBits<16> for T where T: FitsInXBits<8> {}
    // Other number of bits that fit into a 16-bits primitive.
    impl_size_rule!(FitsInXBits<16>, 9 10 11 12 13 14 15 16);

    // Anything that fits into 16 bits also fits into 32.
    impl<T> FitsInXBits<32> for T where T: FitsInXBits<16> {}
    // Other number of bits that fit into a 32-bits primitive.
    impl_size_rule!(FitsInXBits<32>,
        17 18 19 20 21 22 23 24
        25 26 27 28 29 30 31 32
    );

    // Anything that fits into 32 bits also fits into 64.
    impl<T> FitsInXBits<64> for T where T: FitsInXBits<32> {}
    // Other number of bits that fit into a 64-bits primitive.
    impl_size_rule!(FitsInXBits<64>,
        33 34 35 36 37 38 39 40
        41 42 43 44 45 46 47 48
        49 50 51 52 53 54 55 56
        57 58 59 60 61 62 63 64
    );
}

/// Generates [`From`] implementations from a [`Bounded`] into a primitive type that is
/// guaranteed to contain it.
///
/// Note: The only reason for having this macro is that if we pass `$type` as a generic
/// parameter, we cannot use it in the const context of `AtLeastXBits`'s generic parameter. This
/// can be fixed once the `generic_const_exprs` feature is usable, and this macro replaced by a
/// regular `impl` block.
macro_rules! impl_into_primitive {
    ($($type:ty)*) => {
        $(
        #[doc = ::core::concat!(
            "Conversion from a [`Bounded`] with no more bits than a [`",
            ::core::stringify!($type),
            "`] and of same signedness into [`",
            ::core::stringify!($type),
            "`]")]
        impl<T, const N: u32> From<Bounded<T, N>> for $type
        where
            $type: Integer + TryFrom<T>,
            T: Integer<Signedness = <$type as Integer>::Signedness>,
            Bounded<T, N>: FitsInXBits<{ <$type as Integer>::BITS as usize }>,
        {
            fn from(value: Bounded<T, N>) -> $type {
                // SAFETY: The trait bound on `Bounded` ensures that any value it holds (which
                // is constrained to `N` bits) can fit into the destination type, so this
                // conversion cannot fail.
                unsafe { <$type>::try_from(value.get()).unwrap_unchecked() }
            }
        }
        )*
    }
}

impl_into_primitive!(
    u8 u16 u32 u64 usize
    i8 i16 i32 i64 isize
);

// Single-bit `Bounded`s can be converted from/to a boolean.

impl<T> From<Bounded<T, 1>> for bool
where
    T: Integer + Zeroable,
{
    fn from(value: Bounded<T, 1>) -> Self {
        value.get() != Zeroable::zeroed()
    }
}

impl<T, const N: u32> From<bool> for Bounded<T, N>
where
    T: Integer + From<bool>,
{
    fn from(value: bool) -> Self {
        // SAFETY: A boolean can be represented using a single bit, and thus fits within any
        // integer type for any `N` > 0.
        unsafe { Self::__new(T::from(value)) }
    }
}
