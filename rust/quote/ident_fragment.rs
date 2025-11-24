use alloc::borrow::Cow;
use core::fmt;
use proc_macro2::{Ident, Span};

/// Specialized formatting trait used by `format_ident!`.
///
/// [`Ident`] arguments formatted using this trait will have their `r#` prefix
/// stripped, if present.
///
/// See [`format_ident!`] for more information.
///
/// [`format_ident!`]: crate::format_ident
pub trait IdentFragment {
    /// Format this value as an identifier fragment.
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result;

    /// Span associated with this `IdentFragment`.
    ///
    /// If non-`None`, may be inherited by formatted identifiers.
    fn span(&self) -> Option<Span> {
        None
    }
}

impl<T: IdentFragment + ?Sized> IdentFragment for &T {
    fn span(&self) -> Option<Span> {
        <T as IdentFragment>::span(*self)
    }

    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        IdentFragment::fmt(*self, f)
    }
}

impl<T: IdentFragment + ?Sized> IdentFragment for &mut T {
    fn span(&self) -> Option<Span> {
        <T as IdentFragment>::span(*self)
    }

    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        IdentFragment::fmt(*self, f)
    }
}

impl IdentFragment for Ident {
    fn span(&self) -> Option<Span> {
        Some(self.span())
    }

    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let id = self.to_string();
        if let Some(id) = id.strip_prefix("r#") {
            fmt::Display::fmt(id, f)
        } else {
            fmt::Display::fmt(&id[..], f)
        }
    }
}

impl<T> IdentFragment for Cow<'_, T>
where
    T: IdentFragment + ToOwned + ?Sized,
{
    fn span(&self) -> Option<Span> {
        T::span(self)
    }

    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        T::fmt(self, f)
    }
}

// Limited set of types which this is implemented for, as we want to avoid types
// which will often include non-identifier characters in their `Display` impl.
macro_rules! ident_fragment_display {
    ($($T:ty),*) => {
        $(
            impl IdentFragment for $T {
                fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
                    fmt::Display::fmt(self, f)
                }
            }
        )*
    };
}

ident_fragment_display!(bool, str, String, char);
ident_fragment_display!(u8, u16, u32, u64, u128, usize);
