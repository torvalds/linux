use super::TokenStreamExt;
use alloc::borrow::Cow;
use alloc::rc::Rc;
use core::iter;
use proc_macro2::{Group, Ident, Literal, Punct, Span, TokenStream, TokenTree};
use std::ffi::{CStr, CString};

/// Types that can be interpolated inside a `quote!` invocation.
pub trait ToTokens {
    /// Write `self` to the given `TokenStream`.
    ///
    /// The token append methods provided by the [`TokenStreamExt`] extension
    /// trait may be useful for implementing `ToTokens`.
    ///
    /// # Example
    ///
    /// Example implementation for a struct representing Rust paths like
    /// `std::cmp::PartialEq`:
    ///
    /// ```
    /// use proc_macro2::{TokenTree, Spacing, Span, Punct, TokenStream};
    /// use quote::{TokenStreamExt, ToTokens};
    ///
    /// pub struct Path {
    ///     pub global: bool,
    ///     pub segments: Vec<PathSegment>,
    /// }
    ///
    /// impl ToTokens for Path {
    ///     fn to_tokens(&self, tokens: &mut TokenStream) {
    ///         for (i, segment) in self.segments.iter().enumerate() {
    ///             if i > 0 || self.global {
    ///                 // Double colon `::`
    ///                 tokens.append(Punct::new(':', Spacing::Joint));
    ///                 tokens.append(Punct::new(':', Spacing::Alone));
    ///             }
    ///             segment.to_tokens(tokens);
    ///         }
    ///     }
    /// }
    /// #
    /// # pub struct PathSegment;
    /// #
    /// # impl ToTokens for PathSegment {
    /// #     fn to_tokens(&self, tokens: &mut TokenStream) {
    /// #         unimplemented!()
    /// #     }
    /// # }
    /// ```
    fn to_tokens(&self, tokens: &mut TokenStream);

    /// Convert `self` directly into a `TokenStream` object.
    ///
    /// This method is implicitly implemented using `to_tokens`, and acts as a
    /// convenience method for consumers of the `ToTokens` trait.
    fn to_token_stream(&self) -> TokenStream {
        let mut tokens = TokenStream::new();
        self.to_tokens(&mut tokens);
        tokens
    }

    /// Convert `self` directly into a `TokenStream` object.
    ///
    /// This method is implicitly implemented using `to_tokens`, and acts as a
    /// convenience method for consumers of the `ToTokens` trait.
    fn into_token_stream(self) -> TokenStream
    where
        Self: Sized,
    {
        self.to_token_stream()
    }
}

impl<T: ?Sized + ToTokens> ToTokens for &T {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        (**self).to_tokens(tokens);
    }
}

impl<T: ?Sized + ToTokens> ToTokens for &mut T {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        (**self).to_tokens(tokens);
    }
}

impl<'a, T: ?Sized + ToOwned + ToTokens> ToTokens for Cow<'a, T> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        (**self).to_tokens(tokens);
    }
}

impl<T: ?Sized + ToTokens> ToTokens for Box<T> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        (**self).to_tokens(tokens);
    }
}

impl<T: ?Sized + ToTokens> ToTokens for Rc<T> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        (**self).to_tokens(tokens);
    }
}

impl<T: ToTokens> ToTokens for Option<T> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        if let Some(t) = self {
            t.to_tokens(tokens);
        }
    }
}

impl ToTokens for str {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::string(self));
    }
}

impl ToTokens for String {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        self.as_str().to_tokens(tokens);
    }
}

impl ToTokens for i8 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::i8_suffixed(*self));
    }
}

impl ToTokens for i16 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::i16_suffixed(*self));
    }
}

impl ToTokens for i32 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::i32_suffixed(*self));
    }
}

impl ToTokens for i64 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::i64_suffixed(*self));
    }
}

impl ToTokens for i128 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::i128_suffixed(*self));
    }
}

impl ToTokens for isize {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::isize_suffixed(*self));
    }
}

impl ToTokens for u8 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::u8_suffixed(*self));
    }
}

impl ToTokens for u16 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::u16_suffixed(*self));
    }
}

impl ToTokens for u32 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::u32_suffixed(*self));
    }
}

impl ToTokens for u64 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::u64_suffixed(*self));
    }
}

impl ToTokens for u128 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::u128_suffixed(*self));
    }
}

impl ToTokens for usize {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::usize_suffixed(*self));
    }
}

impl ToTokens for f32 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::f32_suffixed(*self));
    }
}

impl ToTokens for f64 {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::f64_suffixed(*self));
    }
}

impl ToTokens for char {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::character(*self));
    }
}

impl ToTokens for bool {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let word = if *self { "true" } else { "false" };
        tokens.append(Ident::new(word, Span::call_site()));
    }
}

impl ToTokens for CStr {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::c_string(self));
    }
}

impl ToTokens for CString {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(Literal::c_string(self));
    }
}

impl ToTokens for Group {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(self.clone());
    }
}

impl ToTokens for Ident {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(self.clone());
    }
}

impl ToTokens for Punct {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(self.clone());
    }
}

impl ToTokens for Literal {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(self.clone());
    }
}

impl ToTokens for TokenTree {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.append(self.clone());
    }
}

impl ToTokens for TokenStream {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.extend(iter::once(self.clone()));
    }

    fn into_token_stream(self) -> TokenStream {
        self
    }
}
