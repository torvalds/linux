// SPDX-License-Identifier: GPL-2.0

//! Bitflag type generator.

/// Common helper for declaring bitflag and bitmask types.
///
/// This macro takes as input:
/// - A struct declaration representing a bitmask type
///   (e.g., `pub struct Permissions(u32)`).
/// - An enumeration declaration representing individual bit flags
///   (e.g., `pub enum Permission { ... }`).
///
/// And generates:
/// - The struct and enum types with appropriate `#[repr]` attributes.
/// - Implementations of common bitflag operators
///   ([`::core::ops::BitOr`], [`::core::ops::BitAnd`], etc.).
/// - Utility methods such as `.contains()` to check flags.
///
/// # Examples
///
/// ```
/// use kernel::impl_flags;
///
/// impl_flags!(
///     /// Represents multiple permissions.
///     #[derive(Debug, Clone, Default, Copy, PartialEq, Eq)]
///     pub struct Permissions(u32);
///
///     /// Represents a single permission.
///     #[derive(Debug, Clone, Copy, PartialEq, Eq)]
///     pub enum Permission {
///         /// Read permission.
///         Read = 1 << 0,
///
///         /// Write permission.
///         Write = 1 << 1,
///
///         /// Execute permission.
///         Execute = 1 << 2,
///     }
/// );
///
/// // Combine multiple permissions using the bitwise OR (`|`) operator.
/// let mut read_write: Permissions = Permission::Read | Permission::Write;
/// assert!(read_write.contains(Permission::Read));
/// assert!(read_write.contains(Permission::Write));
/// assert!(!read_write.contains(Permission::Execute));
/// assert!(read_write.contains_any(Permission::Read | Permission::Execute));
/// assert!(read_write.contains_all(Permission::Read | Permission::Write));
///
/// // Using the bitwise OR assignment (`|=`) operator.
/// read_write |= Permission::Execute;
/// assert!(read_write.contains(Permission::Execute));
///
/// // Masking a permission with the bitwise AND (`&`) operator.
/// let read_only: Permissions = read_write & Permission::Read;
/// assert!(read_only.contains(Permission::Read));
/// assert!(!read_only.contains(Permission::Write));
///
/// // Toggling permissions with the bitwise XOR (`^`) operator.
/// let toggled: Permissions = read_only ^ Permission::Read;
/// assert!(!toggled.contains(Permission::Read));
///
/// // Inverting permissions with the bitwise NOT (`!`) operator.
/// let negated = !read_only;
/// assert!(negated.contains(Permission::Write));
/// assert!(!negated.contains(Permission::Read));
/// ```
#[macro_export]
macro_rules! impl_flags {
    (
        $(#[$outer_flags:meta])*
        $vis_flags:vis struct $flags:ident($ty:ty);

        $(#[$outer_flag:meta])*
        $vis_flag:vis enum $flag:ident {
            $(
                $(#[$inner_flag:meta])*
                $name:ident = $value:expr
            ),+ $( , )?
        }
    ) => {
        $(#[$outer_flags])*
        #[repr(transparent)]
        $vis_flags struct $flags($ty);

        $(#[$outer_flag])*
        #[repr($ty)]
        $vis_flag enum $flag {
            $(
                $(#[$inner_flag])*
                $name = $value
            ),+
        }

        impl ::core::convert::From<$flag> for $flags {
            #[inline]
            fn from(value: $flag) -> Self {
                Self(value as $ty)
            }
        }

        impl ::core::convert::From<$flags> for $ty {
            #[inline]
            fn from(value: $flags) -> Self {
                value.0
            }
        }

        impl ::core::ops::BitOr for $flags {
            type Output = Self;
            #[inline]
            fn bitor(self, rhs: Self) -> Self::Output {
                Self(self.0 | rhs.0)
            }
        }

        impl ::core::ops::BitOrAssign for $flags {
            #[inline]
            fn bitor_assign(&mut self, rhs: Self) {
                *self = *self | rhs;
            }
        }

        impl ::core::ops::BitOr<$flag> for $flags {
            type Output = Self;
            #[inline]
            fn bitor(self, rhs: $flag) -> Self::Output {
                self | Self::from(rhs)
            }
        }

        impl ::core::ops::BitOrAssign<$flag> for $flags {
            #[inline]
            fn bitor_assign(&mut self, rhs: $flag) {
                *self = *self | rhs;
            }
        }

        impl ::core::ops::BitAnd for $flags {
            type Output = Self;
            #[inline]
            fn bitand(self, rhs: Self) -> Self::Output {
                Self(self.0 & rhs.0)
            }
        }

        impl ::core::ops::BitAndAssign for $flags {
            #[inline]
            fn bitand_assign(&mut self, rhs: Self) {
                *self = *self & rhs;
            }
        }

        impl ::core::ops::BitAnd<$flag> for $flags {
            type Output = Self;
            #[inline]
            fn bitand(self, rhs: $flag) -> Self::Output {
                self & Self::from(rhs)
            }
        }

        impl ::core::ops::BitAndAssign<$flag> for $flags {
            #[inline]
            fn bitand_assign(&mut self, rhs: $flag) {
                *self = *self & rhs;
            }
        }

        impl ::core::ops::BitXor for $flags {
            type Output = Self;
            #[inline]
            fn bitxor(self, rhs: Self) -> Self::Output {
                Self((self.0 ^ rhs.0) & Self::all_bits())
            }
        }

        impl ::core::ops::BitXorAssign for $flags {
            #[inline]
            fn bitxor_assign(&mut self, rhs: Self) {
                *self = *self ^ rhs;
            }
        }

        impl ::core::ops::BitXor<$flag> for $flags {
            type Output = Self;
            #[inline]
            fn bitxor(self, rhs: $flag) -> Self::Output {
                self ^ Self::from(rhs)
            }
        }

        impl ::core::ops::BitXorAssign<$flag> for $flags {
            #[inline]
            fn bitxor_assign(&mut self, rhs: $flag) {
                *self = *self ^ rhs;
            }
        }

        impl ::core::ops::Not for $flags {
            type Output = Self;
            #[inline]
            fn not(self) -> Self::Output {
                Self((!self.0) & Self::all_bits())
            }
        }

        impl ::core::ops::BitOr for $flag {
            type Output = $flags;
            #[inline]
            fn bitor(self, rhs: Self) -> Self::Output {
                $flags(self as $ty | rhs as $ty)
            }
        }

        impl ::core::ops::BitAnd for $flag {
            type Output = $flags;
            #[inline]
            fn bitand(self, rhs: Self) -> Self::Output {
                $flags(self as $ty & rhs as $ty)
            }
        }

        impl ::core::ops::BitXor for $flag {
            type Output = $flags;
            #[inline]
            fn bitxor(self, rhs: Self) -> Self::Output {
                $flags((self as $ty ^ rhs as $ty) & $flags::all_bits())
            }
        }

        impl ::core::ops::Not for $flag {
            type Output = $flags;
            #[inline]
            fn not(self) -> Self::Output {
                $flags((!(self as $ty)) & $flags::all_bits())
            }
        }

        impl $flags {
            /// Returns an empty instance where no flags are set.
            #[inline]
            pub const fn empty() -> Self {
                Self(0)
            }

            /// Returns a mask containing all valid flag bits.
            #[inline]
            pub const fn all_bits() -> $ty {
                0 $( | $value )+
            }

            /// Checks if a specific flag is set.
            #[inline]
            pub fn contains(self, flag: $flag) -> bool {
                (self.0 & flag as $ty) == flag as $ty
            }

            /// Checks if at least one of the provided flags is set.
            #[inline]
            pub fn contains_any(self, flags: $flags) -> bool {
                (self.0 & flags.0) != 0
            }

            /// Checks if all of the provided flags are set.
            #[inline]
            pub fn contains_all(self, flags: $flags) -> bool {
                (self.0 & flags.0) == flags.0
            }
        }
    };
}
