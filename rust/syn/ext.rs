//! Extension traits to provide parsing methods on foreign types.

use crate::buffer::Cursor;
use crate::error::Result;
use crate::parse::ParseStream;
use crate::parse::Peek;
use crate::sealed::lookahead;
use crate::token::CustomToken;
use proc_macro2::Ident;

/// Additional methods for `Ident` not provided by proc-macro2 or libproc_macro.
///
/// This trait is sealed and cannot be implemented for types outside of Syn. It
/// is implemented only for `proc_macro2::Ident`.
pub trait IdentExt: Sized + private::Sealed {
    /// Parses any identifier including keywords.
    ///
    /// This is useful when parsing macro input which allows Rust keywords as
    /// identifiers.
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{Error, Ident, Result, Token};
    /// use syn::ext::IdentExt;
    /// use syn::parse::ParseStream;
    ///
    /// mod kw {
    ///     syn::custom_keyword!(name);
    /// }
    ///
    /// // Parses input that looks like `name = NAME` where `NAME` can be
    /// // any identifier.
    /// //
    /// // Examples:
    /// //
    /// //     name = anything
    /// //     name = impl
    /// fn parse_dsl(input: ParseStream) -> Result<Ident> {
    ///     input.parse::<kw::name>()?;
    ///     input.parse::<Token![=]>()?;
    ///     let name = input.call(Ident::parse_any)?;
    ///     Ok(name)
    /// }
    /// ```
    fn parse_any(input: ParseStream) -> Result<Self>;

    /// Peeks any identifier including keywords. Usage:
    /// `input.peek(Ident::peek_any)`
    ///
    /// This is different from `input.peek(Ident)` which only returns true in
    /// the case of an ident which is not a Rust keyword.
    #[allow(non_upper_case_globals)]
    const peek_any: private::PeekFn = private::PeekFn;

    /// Strips the raw marker `r#`, if any, from the beginning of an ident.
    ///
    ///   - unraw(`x`) = `x`
    ///   - unraw(`move`) = `move`
    ///   - unraw(`r#move`) = `move`
    ///
    /// # Example
    ///
    /// In the case of interop with other languages like Python that have a
    /// different set of keywords than Rust, we might come across macro input
    /// that involves raw identifiers to refer to ordinary variables in the
    /// other language with a name that happens to be a Rust keyword.
    ///
    /// The function below appends an identifier from the caller's input onto a
    /// fixed prefix. Without using `unraw()`, this would tend to produce
    /// invalid identifiers like `__pyo3_get_r#move`.
    ///
    /// ```
    /// use proc_macro2::Span;
    /// use syn::Ident;
    /// use syn::ext::IdentExt;
    ///
    /// fn ident_for_getter(variable: &Ident) -> Ident {
    ///     let getter = format!("__pyo3_get_{}", variable.unraw());
    ///     Ident::new(&getter, Span::call_site())
    /// }
    /// ```
    fn unraw(&self) -> Ident;
}

impl IdentExt for Ident {
    fn parse_any(input: ParseStream) -> Result<Self> {
        input.step(|cursor| match cursor.ident() {
            Some((ident, rest)) => Ok((ident, rest)),
            None => Err(cursor.error("expected ident")),
        })
    }

    fn unraw(&self) -> Ident {
        let string = self.to_string();
        if let Some(string) = string.strip_prefix("r#") {
            Ident::new(string, self.span())
        } else {
            self.clone()
        }
    }
}

impl Peek for private::PeekFn {
    type Token = private::IdentAny;
}

impl CustomToken for private::IdentAny {
    fn peek(cursor: Cursor) -> bool {
        cursor.ident().is_some()
    }

    fn display() -> &'static str {
        "identifier"
    }
}

impl lookahead::Sealed for private::PeekFn {}

mod private {
    use proc_macro2::Ident;

    pub trait Sealed {}

    impl Sealed for Ident {}

    pub struct PeekFn;
    pub struct IdentAny;

    impl Copy for PeekFn {}
    impl Clone for PeekFn {
        fn clone(&self) -> Self {
            *self
        }
    }
}
