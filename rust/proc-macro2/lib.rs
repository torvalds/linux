//! [![github]](https://github.com/dtolnay/proc-macro2)&ensp;[![crates-io]](https://crates.io/crates/proc-macro2)&ensp;[![docs-rs]](crate)
//!
//! [github]: https://img.shields.io/badge/github-8da0cb?style=for-the-badge&labelColor=555555&logo=github
//! [crates-io]: https://img.shields.io/badge/crates.io-fc8d62?style=for-the-badge&labelColor=555555&logo=rust
//! [docs-rs]: https://img.shields.io/badge/docs.rs-66c2a5?style=for-the-badge&labelColor=555555&logo=docs.rs
//!
//! <br>
//!
//! A wrapper around the procedural macro API of the compiler's [`proc_macro`]
//! crate. This library serves two purposes:
//!
//! - **Bring proc-macro-like functionality to other contexts like build.rs and
//!   main.rs.** Types from `proc_macro` are entirely specific to procedural
//!   macros and cannot ever exist in code outside of a procedural macro.
//!   Meanwhile `proc_macro2` types may exist anywhere including non-macro code.
//!   By developing foundational libraries like [syn] and [quote] against
//!   `proc_macro2` rather than `proc_macro`, the procedural macro ecosystem
//!   becomes easily applicable to many other use cases and we avoid
//!   reimplementing non-macro equivalents of those libraries.
//!
//! - **Make procedural macros unit testable.** As a consequence of being
//!   specific to procedural macros, nothing that uses `proc_macro` can be
//!   executed from a unit test. In order for helper libraries or components of
//!   a macro to be testable in isolation, they must be implemented using
//!   `proc_macro2`.
//!
//! [syn]: https://github.com/dtolnay/syn
//! [quote]: https://github.com/dtolnay/quote
//!
//! # Usage
//!
//! The skeleton of a typical procedural macro typically looks like this:
//!
//! ```
//! extern crate proc_macro;
//!
//! # const IGNORE: &str = stringify! {
//! #[proc_macro_derive(MyDerive)]
//! # };
//! # #[cfg(wrap_proc_macro)]
//! pub fn my_derive(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
//!     let input = proc_macro2::TokenStream::from(input);
//!
//!     let output: proc_macro2::TokenStream = {
//!         /* transform input */
//!         # input
//!     };
//!
//!     proc_macro::TokenStream::from(output)
//! }
//! ```
//!
//! If parsing with [Syn], you'll use [`parse_macro_input!`] instead to
//! propagate parse errors correctly back to the compiler when parsing fails.
//!
//! [`parse_macro_input!`]: https://docs.rs/syn/2.0/syn/macro.parse_macro_input.html
//!
//! # Unstable features
//!
//! The default feature set of proc-macro2 tracks the most recent stable
//! compiler API. Functionality in `proc_macro` that is not yet stable is not
//! exposed by proc-macro2 by default.
//!
//! To opt into the additional APIs available in the most recent nightly
//! compiler, the `procmacro2_semver_exempt` config flag must be passed to
//! rustc. We will polyfill those nightly-only APIs back to Rust 1.56.0. As
//! these are unstable APIs that track the nightly compiler, minor versions of
//! proc-macro2 may make breaking changes to them at any time.
//!
//! ```sh
//! RUSTFLAGS='--cfg procmacro2_semver_exempt' cargo build
//! ```
//!
//! Note that this must not only be done for your crate, but for any crate that
//! depends on your crate. This infectious nature is intentional, as it serves
//! as a reminder that you are outside of the normal semver guarantees.
//!
//! Semver exempt methods are marked as such in the proc-macro2 documentation.
//!
//! # Thread-Safety
//!
//! Most types in this crate are `!Sync` because the underlying compiler
//! types make use of thread-local memory, meaning they cannot be accessed from
//! a different thread.

// Proc-macro2 types in rustdoc of other crates get linked to here.
#![doc(html_root_url = "https://docs.rs/proc-macro2/1.0.101")]
#![cfg_attr(any(proc_macro_span, super_unstable), feature(proc_macro_span))]
#![cfg_attr(super_unstable, feature(proc_macro_def_site))]
#![cfg_attr(docsrs, feature(doc_cfg))]
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(
    clippy::cast_lossless,
    clippy::cast_possible_truncation,
    clippy::checked_conversions,
    clippy::doc_markdown,
    clippy::elidable_lifetime_names,
    clippy::incompatible_msrv,
    clippy::items_after_statements,
    clippy::iter_without_into_iter,
    clippy::let_underscore_untyped,
    clippy::manual_assert,
    clippy::manual_range_contains,
    clippy::missing_panics_doc,
    clippy::missing_safety_doc,
    clippy::must_use_candidate,
    clippy::needless_doctest_main,
    clippy::needless_lifetimes,
    clippy::new_without_default,
    clippy::return_self_not_must_use,
    clippy::shadow_unrelated,
    clippy::trivially_copy_pass_by_ref,
    clippy::unnecessary_wraps,
    clippy::unused_self,
    clippy::used_underscore_binding,
    clippy::vec_init_then_push
)]
#![allow(unknown_lints, mismatched_lifetime_syntaxes)]

#[cfg(all(procmacro2_semver_exempt, wrap_proc_macro, not(super_unstable)))]
compile_error! {"\
    Something is not right. If you've tried to turn on \
    procmacro2_semver_exempt, you need to ensure that it \
    is turned on for the compilation of the proc-macro2 \
    build script as well.
"}

#[cfg(all(
    procmacro2_nightly_testing,
    feature = "proc-macro",
    not(proc_macro_span)
))]
compile_error! {"\
    Build script probe failed to compile.
"}

extern crate alloc;

#[cfg(feature = "proc-macro")]
extern crate proc_macro;

mod marker;
mod parse;
mod probe;
mod rcvec;

#[cfg(wrap_proc_macro)]
mod detection;

// Public for proc_macro2::fallback::force() and unforce(), but those are quite
// a niche use case so we omit it from rustdoc.
#[doc(hidden)]
pub mod fallback;

pub mod extra;

#[cfg(not(wrap_proc_macro))]
use crate::fallback as imp;
#[path = "wrapper.rs"]
#[cfg(wrap_proc_macro)]
mod imp;

#[cfg(span_locations)]
mod location;

use crate::extra::DelimSpan;
use crate::marker::{ProcMacroAutoTraits, MARKER};
use core::cmp::Ordering;
use core::fmt::{self, Debug, Display};
use core::hash::{Hash, Hasher};
#[cfg(span_locations)]
use core::ops::Range;
use core::ops::RangeBounds;
use core::str::FromStr;
use std::error::Error;
use std::ffi::CStr;
#[cfg(span_locations)]
use std::path::PathBuf;

#[cfg(span_locations)]
#[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
pub use crate::location::LineColumn;

/// An abstract stream of tokens, or more concretely a sequence of token trees.
///
/// This type provides interfaces for iterating over token trees and for
/// collecting token trees into one stream.
///
/// Token stream is both the input and output of `#[proc_macro]`,
/// `#[proc_macro_attribute]` and `#[proc_macro_derive]` definitions.
#[derive(Clone)]
pub struct TokenStream {
    inner: imp::TokenStream,
    _marker: ProcMacroAutoTraits,
}

/// Error returned from `TokenStream::from_str`.
pub struct LexError {
    inner: imp::LexError,
    _marker: ProcMacroAutoTraits,
}

impl TokenStream {
    fn _new(inner: imp::TokenStream) -> Self {
        TokenStream {
            inner,
            _marker: MARKER,
        }
    }

    fn _new_fallback(inner: fallback::TokenStream) -> Self {
        TokenStream {
            inner: imp::TokenStream::from(inner),
            _marker: MARKER,
        }
    }

    /// Returns an empty `TokenStream` containing no token trees.
    pub fn new() -> Self {
        TokenStream::_new(imp::TokenStream::new())
    }

    /// Checks if this `TokenStream` is empty.
    pub fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }
}

/// `TokenStream::default()` returns an empty stream,
/// i.e. this is equivalent with `TokenStream::new()`.
impl Default for TokenStream {
    fn default() -> Self {
        TokenStream::new()
    }
}

/// Attempts to break the string into tokens and parse those tokens into a token
/// stream.
///
/// May fail for a number of reasons, for example, if the string contains
/// unbalanced delimiters or characters not existing in the language.
///
/// NOTE: Some errors may cause panics instead of returning `LexError`. We
/// reserve the right to change these errors into `LexError`s later.
impl FromStr for TokenStream {
    type Err = LexError;

    fn from_str(src: &str) -> Result<TokenStream, LexError> {
        match imp::TokenStream::from_str_checked(src) {
            Ok(tokens) => Ok(TokenStream::_new(tokens)),
            Err(lex) => Err(LexError {
                inner: lex,
                _marker: MARKER,
            }),
        }
    }
}

#[cfg(feature = "proc-macro")]
#[cfg_attr(docsrs, doc(cfg(feature = "proc-macro")))]
impl From<proc_macro::TokenStream> for TokenStream {
    fn from(inner: proc_macro::TokenStream) -> Self {
        TokenStream::_new(imp::TokenStream::from(inner))
    }
}

#[cfg(feature = "proc-macro")]
#[cfg_attr(docsrs, doc(cfg(feature = "proc-macro")))]
impl From<TokenStream> for proc_macro::TokenStream {
    fn from(inner: TokenStream) -> Self {
        proc_macro::TokenStream::from(inner.inner)
    }
}

impl From<TokenTree> for TokenStream {
    fn from(token: TokenTree) -> Self {
        TokenStream::_new(imp::TokenStream::from(token))
    }
}

impl Extend<TokenTree> for TokenStream {
    fn extend<I: IntoIterator<Item = TokenTree>>(&mut self, streams: I) {
        self.inner.extend(streams);
    }
}

impl Extend<TokenStream> for TokenStream {
    fn extend<I: IntoIterator<Item = TokenStream>>(&mut self, streams: I) {
        self.inner
            .extend(streams.into_iter().map(|stream| stream.inner));
    }
}

/// Collects a number of token trees into a single stream.
impl FromIterator<TokenTree> for TokenStream {
    fn from_iter<I: IntoIterator<Item = TokenTree>>(streams: I) -> Self {
        TokenStream::_new(streams.into_iter().collect())
    }
}
impl FromIterator<TokenStream> for TokenStream {
    fn from_iter<I: IntoIterator<Item = TokenStream>>(streams: I) -> Self {
        TokenStream::_new(streams.into_iter().map(|i| i.inner).collect())
    }
}

/// Prints the token stream as a string that is supposed to be losslessly
/// convertible back into the same token stream (modulo spans), except for
/// possibly `TokenTree::Group`s with `Delimiter::None` delimiters and negative
/// numeric literals.
impl Display for TokenStream {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.inner, f)
    }
}

/// Prints token in a form convenient for debugging.
impl Debug for TokenStream {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.inner, f)
    }
}

impl LexError {
    pub fn span(&self) -> Span {
        Span::_new(self.inner.span())
    }
}

impl Debug for LexError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.inner, f)
    }
}

impl Display for LexError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.inner, f)
    }
}

impl Error for LexError {}

/// A region of source code, along with macro expansion information.
#[derive(Copy, Clone)]
pub struct Span {
    inner: imp::Span,
    _marker: ProcMacroAutoTraits,
}

impl Span {
    fn _new(inner: imp::Span) -> Self {
        Span {
            inner,
            _marker: MARKER,
        }
    }

    fn _new_fallback(inner: fallback::Span) -> Self {
        Span {
            inner: imp::Span::from(inner),
            _marker: MARKER,
        }
    }

    /// The span of the invocation of the current procedural macro.
    ///
    /// Identifiers created with this span will be resolved as if they were
    /// written directly at the macro call location (call-site hygiene) and
    /// other code at the macro call site will be able to refer to them as well.
    pub fn call_site() -> Self {
        Span::_new(imp::Span::call_site())
    }

    /// The span located at the invocation of the procedural macro, but with
    /// local variables, labels, and `$crate` resolved at the definition site
    /// of the macro. This is the same hygiene behavior as `macro_rules`.
    pub fn mixed_site() -> Self {
        Span::_new(imp::Span::mixed_site())
    }

    /// A span that resolves at the macro definition site.
    ///
    /// This method is semver exempt and not exposed by default.
    #[cfg(procmacro2_semver_exempt)]
    #[cfg_attr(docsrs, doc(cfg(procmacro2_semver_exempt)))]
    pub fn def_site() -> Self {
        Span::_new(imp::Span::def_site())
    }

    /// Creates a new span with the same line/column information as `self` but
    /// that resolves symbols as though it were at `other`.
    pub fn resolved_at(&self, other: Span) -> Span {
        Span::_new(self.inner.resolved_at(other.inner))
    }

    /// Creates a new span with the same name resolution behavior as `self` but
    /// with the line/column information of `other`.
    pub fn located_at(&self, other: Span) -> Span {
        Span::_new(self.inner.located_at(other.inner))
    }

    /// Convert `proc_macro2::Span` to `proc_macro::Span`.
    ///
    /// This method is available when building with a nightly compiler, or when
    /// building with rustc 1.29+ *without* semver exempt features.
    ///
    /// # Panics
    ///
    /// Panics if called from outside of a procedural macro. Unlike
    /// `proc_macro2::Span`, the `proc_macro::Span` type can only exist within
    /// the context of a procedural macro invocation.
    #[cfg(wrap_proc_macro)]
    pub fn unwrap(self) -> proc_macro::Span {
        self.inner.unwrap()
    }

    // Soft deprecated. Please use Span::unwrap.
    #[cfg(wrap_proc_macro)]
    #[doc(hidden)]
    pub fn unstable(self) -> proc_macro::Span {
        self.unwrap()
    }

    /// Returns the span's byte position range in the source file.
    ///
    /// This method requires the `"span-locations"` feature to be enabled.
    ///
    /// When executing in a procedural macro context, the returned range is only
    /// accurate if compiled with a nightly toolchain. The stable toolchain does
    /// not have this information available. When executing outside of a
    /// procedural macro, such as main.rs or build.rs, the byte range is always
    /// accurate regardless of toolchain.
    #[cfg(span_locations)]
    #[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
    pub fn byte_range(&self) -> Range<usize> {
        self.inner.byte_range()
    }

    /// Get the starting line/column in the source file for this span.
    ///
    /// This method requires the `"span-locations"` feature to be enabled.
    ///
    /// When executing in a procedural macro context, the returned line/column
    /// are only meaningful if compiled with a nightly toolchain. The stable
    /// toolchain does not have this information available. When executing
    /// outside of a procedural macro, such as main.rs or build.rs, the
    /// line/column are always meaningful regardless of toolchain.
    #[cfg(span_locations)]
    #[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
    pub fn start(&self) -> LineColumn {
        self.inner.start()
    }

    /// Get the ending line/column in the source file for this span.
    ///
    /// This method requires the `"span-locations"` feature to be enabled.
    ///
    /// When executing in a procedural macro context, the returned line/column
    /// are only meaningful if compiled with a nightly toolchain. The stable
    /// toolchain does not have this information available. When executing
    /// outside of a procedural macro, such as main.rs or build.rs, the
    /// line/column are always meaningful regardless of toolchain.
    #[cfg(span_locations)]
    #[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
    pub fn end(&self) -> LineColumn {
        self.inner.end()
    }

    /// The path to the source file in which this span occurs, for display
    /// purposes.
    ///
    /// This might not correspond to a valid file system path. It might be
    /// remapped, or might be an artificial path such as `"<macro expansion>"`.
    #[cfg(span_locations)]
    #[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
    pub fn file(&self) -> String {
        self.inner.file()
    }

    /// The path to the source file in which this span occurs on disk.
    ///
    /// This is the actual path on disk. It is unaffected by path remapping.
    ///
    /// This path should not be embedded in the output of the macro; prefer
    /// `file()` instead.
    #[cfg(span_locations)]
    #[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
    pub fn local_file(&self) -> Option<PathBuf> {
        self.inner.local_file()
    }

    /// Create a new span encompassing `self` and `other`.
    ///
    /// Returns `None` if `self` and `other` are from different files.
    ///
    /// Warning: the underlying [`proc_macro::Span::join`] method is
    /// nightly-only. When called from within a procedural macro not using a
    /// nightly compiler, this method will always return `None`.
    pub fn join(&self, other: Span) -> Option<Span> {
        self.inner.join(other.inner).map(Span::_new)
    }

    /// Compares two spans to see if they're equal.
    ///
    /// This method is semver exempt and not exposed by default.
    #[cfg(procmacro2_semver_exempt)]
    #[cfg_attr(docsrs, doc(cfg(procmacro2_semver_exempt)))]
    pub fn eq(&self, other: &Span) -> bool {
        self.inner.eq(&other.inner)
    }

    /// Returns the source text behind a span. This preserves the original
    /// source code, including spaces and comments. It only returns a result if
    /// the span corresponds to real source code.
    ///
    /// Note: The observable result of a macro should only rely on the tokens
    /// and not on this source text. The result of this function is a best
    /// effort to be used for diagnostics only.
    pub fn source_text(&self) -> Option<String> {
        self.inner.source_text()
    }
}

/// Prints a span in a form convenient for debugging.
impl Debug for Span {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.inner, f)
    }
}

/// A single token or a delimited sequence of token trees (e.g. `[1, (), ..]`).
#[derive(Clone)]
pub enum TokenTree {
    /// A token stream surrounded by bracket delimiters.
    Group(Group),
    /// An identifier.
    Ident(Ident),
    /// A single punctuation character (`+`, `,`, `$`, etc.).
    Punct(Punct),
    /// A literal character (`'a'`), string (`"hello"`), number (`2.3`), etc.
    Literal(Literal),
}

impl TokenTree {
    /// Returns the span of this tree, delegating to the `span` method of
    /// the contained token or a delimited stream.
    pub fn span(&self) -> Span {
        match self {
            TokenTree::Group(t) => t.span(),
            TokenTree::Ident(t) => t.span(),
            TokenTree::Punct(t) => t.span(),
            TokenTree::Literal(t) => t.span(),
        }
    }

    /// Configures the span for *only this token*.
    ///
    /// Note that if this token is a `Group` then this method will not configure
    /// the span of each of the internal tokens, this will simply delegate to
    /// the `set_span` method of each variant.
    pub fn set_span(&mut self, span: Span) {
        match self {
            TokenTree::Group(t) => t.set_span(span),
            TokenTree::Ident(t) => t.set_span(span),
            TokenTree::Punct(t) => t.set_span(span),
            TokenTree::Literal(t) => t.set_span(span),
        }
    }
}

impl From<Group> for TokenTree {
    fn from(g: Group) -> Self {
        TokenTree::Group(g)
    }
}

impl From<Ident> for TokenTree {
    fn from(g: Ident) -> Self {
        TokenTree::Ident(g)
    }
}

impl From<Punct> for TokenTree {
    fn from(g: Punct) -> Self {
        TokenTree::Punct(g)
    }
}

impl From<Literal> for TokenTree {
    fn from(g: Literal) -> Self {
        TokenTree::Literal(g)
    }
}

/// Prints the token tree as a string that is supposed to be losslessly
/// convertible back into the same token tree (modulo spans), except for
/// possibly `TokenTree::Group`s with `Delimiter::None` delimiters and negative
/// numeric literals.
impl Display for TokenTree {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            TokenTree::Group(t) => Display::fmt(t, f),
            TokenTree::Ident(t) => Display::fmt(t, f),
            TokenTree::Punct(t) => Display::fmt(t, f),
            TokenTree::Literal(t) => Display::fmt(t, f),
        }
    }
}

/// Prints token tree in a form convenient for debugging.
impl Debug for TokenTree {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        // Each of these has the name in the struct type in the derived debug,
        // so don't bother with an extra layer of indirection
        match self {
            TokenTree::Group(t) => Debug::fmt(t, f),
            TokenTree::Ident(t) => {
                let mut debug = f.debug_struct("Ident");
                debug.field("sym", &format_args!("{}", t));
                imp::debug_span_field_if_nontrivial(&mut debug, t.span().inner);
                debug.finish()
            }
            TokenTree::Punct(t) => Debug::fmt(t, f),
            TokenTree::Literal(t) => Debug::fmt(t, f),
        }
    }
}

/// A delimited token stream.
///
/// A `Group` internally contains a `TokenStream` which is surrounded by
/// `Delimiter`s.
#[derive(Clone)]
pub struct Group {
    inner: imp::Group,
}

/// Describes how a sequence of token trees is delimited.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Delimiter {
    /// `( ... )`
    Parenthesis,
    /// `{ ... }`
    Brace,
    /// `[ ... ]`
    Bracket,
    /// `∅ ... ∅`
    ///
    /// An invisible delimiter, that may, for example, appear around tokens
    /// coming from a "macro variable" `$var`. It is important to preserve
    /// operator priorities in cases like `$var * 3` where `$var` is `1 + 2`.
    /// Invisible delimiters may not survive roundtrip of a token stream through
    /// a string.
    ///
    /// <div class="warning">
    ///
    /// Note: rustc currently can ignore the grouping of tokens delimited by `None` in the output
    /// of a proc_macro. Only `None`-delimited groups created by a macro_rules macro in the input
    /// of a proc_macro macro are preserved, and only in very specific circumstances.
    /// Any `None`-delimited groups (re)created by a proc_macro will therefore not preserve
    /// operator priorities as indicated above. The other `Delimiter` variants should be used
    /// instead in this context. This is a rustc bug. For details, see
    /// [rust-lang/rust#67062](https://github.com/rust-lang/rust/issues/67062).
    ///
    /// </div>
    None,
}

impl Group {
    fn _new(inner: imp::Group) -> Self {
        Group { inner }
    }

    fn _new_fallback(inner: fallback::Group) -> Self {
        Group {
            inner: imp::Group::from(inner),
        }
    }

    /// Creates a new `Group` with the given delimiter and token stream.
    ///
    /// This constructor will set the span for this group to
    /// `Span::call_site()`. To change the span you can use the `set_span`
    /// method below.
    pub fn new(delimiter: Delimiter, stream: TokenStream) -> Self {
        Group {
            inner: imp::Group::new(delimiter, stream.inner),
        }
    }

    /// Returns the punctuation used as the delimiter for this group: a set of
    /// parentheses, square brackets, or curly braces.
    pub fn delimiter(&self) -> Delimiter {
        self.inner.delimiter()
    }

    /// Returns the `TokenStream` of tokens that are delimited in this `Group`.
    ///
    /// Note that the returned token stream does not include the delimiter
    /// returned above.
    pub fn stream(&self) -> TokenStream {
        TokenStream::_new(self.inner.stream())
    }

    /// Returns the span for the delimiters of this token stream, spanning the
    /// entire `Group`.
    ///
    /// ```text
    /// pub fn span(&self) -> Span {
    ///            ^^^^^^^
    /// ```
    pub fn span(&self) -> Span {
        Span::_new(self.inner.span())
    }

    /// Returns the span pointing to the opening delimiter of this group.
    ///
    /// ```text
    /// pub fn span_open(&self) -> Span {
    ///                 ^
    /// ```
    pub fn span_open(&self) -> Span {
        Span::_new(self.inner.span_open())
    }

    /// Returns the span pointing to the closing delimiter of this group.
    ///
    /// ```text
    /// pub fn span_close(&self) -> Span {
    ///                        ^
    /// ```
    pub fn span_close(&self) -> Span {
        Span::_new(self.inner.span_close())
    }

    /// Returns an object that holds this group's `span_open()` and
    /// `span_close()` together (in a more compact representation than holding
    /// those 2 spans individually).
    pub fn delim_span(&self) -> DelimSpan {
        DelimSpan::new(&self.inner)
    }

    /// Configures the span for this `Group`'s delimiters, but not its internal
    /// tokens.
    ///
    /// This method will **not** set the span of all the internal tokens spanned
    /// by this group, but rather it will only set the span of the delimiter
    /// tokens at the level of the `Group`.
    pub fn set_span(&mut self, span: Span) {
        self.inner.set_span(span.inner);
    }
}

/// Prints the group as a string that should be losslessly convertible back
/// into the same group (modulo spans), except for possibly `TokenTree::Group`s
/// with `Delimiter::None` delimiters.
impl Display for Group {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.inner, formatter)
    }
}

impl Debug for Group {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.inner, formatter)
    }
}

/// A `Punct` is a single punctuation character like `+`, `-` or `#`.
///
/// Multicharacter operators like `+=` are represented as two instances of
/// `Punct` with different forms of `Spacing` returned.
#[derive(Clone)]
pub struct Punct {
    ch: char,
    spacing: Spacing,
    span: Span,
}

/// Whether a `Punct` is followed immediately by another `Punct` or followed by
/// another token or whitespace.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Spacing {
    /// E.g. `+` is `Alone` in `+ =`, `+ident` or `+()`.
    Alone,
    /// E.g. `+` is `Joint` in `+=` or `'` is `Joint` in `'#`.
    ///
    /// Additionally, single quote `'` can join with identifiers to form
    /// lifetimes `'ident`.
    Joint,
}

impl Punct {
    /// Creates a new `Punct` from the given character and spacing.
    ///
    /// The `ch` argument must be a valid punctuation character permitted by the
    /// language, otherwise the function will panic.
    ///
    /// The returned `Punct` will have the default span of `Span::call_site()`
    /// which can be further configured with the `set_span` method below.
    pub fn new(ch: char, spacing: Spacing) -> Self {
        if let '!' | '#' | '$' | '%' | '&' | '\'' | '*' | '+' | ',' | '-' | '.' | '/' | ':' | ';'
        | '<' | '=' | '>' | '?' | '@' | '^' | '|' | '~' = ch
        {
            Punct {
                ch,
                spacing,
                span: Span::call_site(),
            }
        } else {
            panic!("unsupported proc macro punctuation character {:?}", ch);
        }
    }

    /// Returns the value of this punctuation character as `char`.
    pub fn as_char(&self) -> char {
        self.ch
    }

    /// Returns the spacing of this punctuation character, indicating whether
    /// it's immediately followed by another `Punct` in the token stream, so
    /// they can potentially be combined into a multicharacter operator
    /// (`Joint`), or it's followed by some other token or whitespace (`Alone`)
    /// so the operator has certainly ended.
    pub fn spacing(&self) -> Spacing {
        self.spacing
    }

    /// Returns the span for this punctuation character.
    pub fn span(&self) -> Span {
        self.span
    }

    /// Configure the span for this punctuation character.
    pub fn set_span(&mut self, span: Span) {
        self.span = span;
    }
}

/// Prints the punctuation character as a string that should be losslessly
/// convertible back into the same character.
impl Display for Punct {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.ch, f)
    }
}

impl Debug for Punct {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        let mut debug = fmt.debug_struct("Punct");
        debug.field("char", &self.ch);
        debug.field("spacing", &self.spacing);
        imp::debug_span_field_if_nontrivial(&mut debug, self.span.inner);
        debug.finish()
    }
}

/// A word of Rust code, which may be a keyword or legal variable name.
///
/// An identifier consists of at least one Unicode code point, the first of
/// which has the XID_Start property and the rest of which have the XID_Continue
/// property.
///
/// - The empty string is not an identifier. Use `Option<Ident>`.
/// - A lifetime is not an identifier. Use `syn::Lifetime` instead.
///
/// An identifier constructed with `Ident::new` is permitted to be a Rust
/// keyword, though parsing one through its [`Parse`] implementation rejects
/// Rust keywords. Use `input.call(Ident::parse_any)` when parsing to match the
/// behaviour of `Ident::new`.
///
/// [`Parse`]: https://docs.rs/syn/2.0/syn/parse/trait.Parse.html
///
/// # Examples
///
/// A new ident can be created from a string using the `Ident::new` function.
/// A span must be provided explicitly which governs the name resolution
/// behavior of the resulting identifier.
///
/// ```
/// use proc_macro2::{Ident, Span};
///
/// fn main() {
///     let call_ident = Ident::new("calligraphy", Span::call_site());
///
///     println!("{}", call_ident);
/// }
/// ```
///
/// An ident can be interpolated into a token stream using the `quote!` macro.
///
/// ```
/// use proc_macro2::{Ident, Span};
/// use quote::quote;
///
/// fn main() {
///     let ident = Ident::new("demo", Span::call_site());
///
///     // Create a variable binding whose name is this ident.
///     let expanded = quote! { let #ident = 10; };
///
///     // Create a variable binding with a slightly different name.
///     let temp_ident = Ident::new(&format!("new_{}", ident), Span::call_site());
///     let expanded = quote! { let #temp_ident = 10; };
/// }
/// ```
///
/// A string representation of the ident is available through the `to_string()`
/// method.
///
/// ```
/// # use proc_macro2::{Ident, Span};
/// #
/// # let ident = Ident::new("another_identifier", Span::call_site());
/// #
/// // Examine the ident as a string.
/// let ident_string = ident.to_string();
/// if ident_string.len() > 60 {
///     println!("Very long identifier: {}", ident_string)
/// }
/// ```
#[derive(Clone)]
pub struct Ident {
    inner: imp::Ident,
    _marker: ProcMacroAutoTraits,
}

impl Ident {
    fn _new(inner: imp::Ident) -> Self {
        Ident {
            inner,
            _marker: MARKER,
        }
    }

    fn _new_fallback(inner: fallback::Ident) -> Self {
        Ident {
            inner: imp::Ident::from(inner),
            _marker: MARKER,
        }
    }

    /// Creates a new `Ident` with the given `string` as well as the specified
    /// `span`.
    ///
    /// The `string` argument must be a valid identifier permitted by the
    /// language, otherwise the function will panic.
    ///
    /// Note that `span`, currently in rustc, configures the hygiene information
    /// for this identifier.
    ///
    /// As of this time `Span::call_site()` explicitly opts-in to "call-site"
    /// hygiene meaning that identifiers created with this span will be resolved
    /// as if they were written directly at the location of the macro call, and
    /// other code at the macro call site will be able to refer to them as well.
    ///
    /// Later spans like `Span::def_site()` will allow to opt-in to
    /// "definition-site" hygiene meaning that identifiers created with this
    /// span will be resolved at the location of the macro definition and other
    /// code at the macro call site will not be able to refer to them.
    ///
    /// Due to the current importance of hygiene this constructor, unlike other
    /// tokens, requires a `Span` to be specified at construction.
    ///
    /// # Panics
    ///
    /// Panics if the input string is neither a keyword nor a legal variable
    /// name. If you are not sure whether the string contains an identifier and
    /// need to handle an error case, use
    /// <a href="https://docs.rs/syn/2.0/syn/fn.parse_str.html"><code
    ///   style="padding-right:0;">syn::parse_str</code></a><code
    ///   style="padding-left:0;">::&lt;Ident&gt;</code>
    /// rather than `Ident::new`.
    #[track_caller]
    pub fn new(string: &str, span: Span) -> Self {
        Ident::_new(imp::Ident::new_checked(string, span.inner))
    }

    /// Same as `Ident::new`, but creates a raw identifier (`r#ident`). The
    /// `string` argument must be a valid identifier permitted by the language
    /// (including keywords, e.g. `fn`). Keywords which are usable in path
    /// segments (e.g. `self`, `super`) are not supported, and will cause a
    /// panic.
    #[track_caller]
    pub fn new_raw(string: &str, span: Span) -> Self {
        Ident::_new(imp::Ident::new_raw_checked(string, span.inner))
    }

    /// Returns the span of this `Ident`.
    pub fn span(&self) -> Span {
        Span::_new(self.inner.span())
    }

    /// Configures the span of this `Ident`, possibly changing its hygiene
    /// context.
    pub fn set_span(&mut self, span: Span) {
        self.inner.set_span(span.inner);
    }
}

impl PartialEq for Ident {
    fn eq(&self, other: &Ident) -> bool {
        self.inner == other.inner
    }
}

impl<T> PartialEq<T> for Ident
where
    T: ?Sized + AsRef<str>,
{
    fn eq(&self, other: &T) -> bool {
        self.inner == other
    }
}

impl Eq for Ident {}

impl PartialOrd for Ident {
    fn partial_cmp(&self, other: &Ident) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Ident {
    fn cmp(&self, other: &Ident) -> Ordering {
        self.to_string().cmp(&other.to_string())
    }
}

impl Hash for Ident {
    fn hash<H: Hasher>(&self, hasher: &mut H) {
        self.to_string().hash(hasher);
    }
}

/// Prints the identifier as a string that should be losslessly convertible back
/// into the same identifier.
impl Display for Ident {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.inner, f)
    }
}

impl Debug for Ident {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.inner, f)
    }
}

/// A literal string (`"hello"`), byte string (`b"hello"`), character (`'a'`),
/// byte character (`b'a'`), an integer or floating point number with or without
/// a suffix (`1`, `1u8`, `2.3`, `2.3f32`).
///
/// Boolean literals like `true` and `false` do not belong here, they are
/// `Ident`s.
#[derive(Clone)]
pub struct Literal {
    inner: imp::Literal,
    _marker: ProcMacroAutoTraits,
}

macro_rules! suffixed_int_literals {
    ($($name:ident => $kind:ident,)*) => ($(
        /// Creates a new suffixed integer literal with the specified value.
        ///
        /// This function will create an integer like `1u32` where the integer
        /// value specified is the first part of the token and the integral is
        /// also suffixed at the end. Literals created from negative numbers may
        /// not survive roundtrips through `TokenStream` or strings and may be
        /// broken into two tokens (`-` and positive literal).
        ///
        /// Literals created through this method have the `Span::call_site()`
        /// span by default, which can be configured with the `set_span` method
        /// below.
        pub fn $name(n: $kind) -> Literal {
            Literal::_new(imp::Literal::$name(n))
        }
    )*)
}

macro_rules! unsuffixed_int_literals {
    ($($name:ident => $kind:ident,)*) => ($(
        /// Creates a new unsuffixed integer literal with the specified value.
        ///
        /// This function will create an integer like `1` where the integer
        /// value specified is the first part of the token. No suffix is
        /// specified on this token, meaning that invocations like
        /// `Literal::i8_unsuffixed(1)` are equivalent to
        /// `Literal::u32_unsuffixed(1)`. Literals created from negative numbers
        /// may not survive roundtrips through `TokenStream` or strings and may
        /// be broken into two tokens (`-` and positive literal).
        ///
        /// Literals created through this method have the `Span::call_site()`
        /// span by default, which can be configured with the `set_span` method
        /// below.
        pub fn $name(n: $kind) -> Literal {
            Literal::_new(imp::Literal::$name(n))
        }
    )*)
}

impl Literal {
    fn _new(inner: imp::Literal) -> Self {
        Literal {
            inner,
            _marker: MARKER,
        }
    }

    fn _new_fallback(inner: fallback::Literal) -> Self {
        Literal {
            inner: imp::Literal::from(inner),
            _marker: MARKER,
        }
    }

    suffixed_int_literals! {
        u8_suffixed => u8,
        u16_suffixed => u16,
        u32_suffixed => u32,
        u64_suffixed => u64,
        u128_suffixed => u128,
        usize_suffixed => usize,
        i8_suffixed => i8,
        i16_suffixed => i16,
        i32_suffixed => i32,
        i64_suffixed => i64,
        i128_suffixed => i128,
        isize_suffixed => isize,
    }

    unsuffixed_int_literals! {
        u8_unsuffixed => u8,
        u16_unsuffixed => u16,
        u32_unsuffixed => u32,
        u64_unsuffixed => u64,
        u128_unsuffixed => u128,
        usize_unsuffixed => usize,
        i8_unsuffixed => i8,
        i16_unsuffixed => i16,
        i32_unsuffixed => i32,
        i64_unsuffixed => i64,
        i128_unsuffixed => i128,
        isize_unsuffixed => isize,
    }

    /// Creates a new unsuffixed floating-point literal.
    ///
    /// This constructor is similar to those like `Literal::i8_unsuffixed` where
    /// the float's value is emitted directly into the token but no suffix is
    /// used, so it may be inferred to be a `f64` later in the compiler.
    /// Literals created from negative numbers may not survive round-trips
    /// through `TokenStream` or strings and may be broken into two tokens (`-`
    /// and positive literal).
    ///
    /// # Panics
    ///
    /// This function requires that the specified float is finite, for example
    /// if it is infinity or NaN this function will panic.
    pub fn f64_unsuffixed(f: f64) -> Literal {
        assert!(f.is_finite());
        Literal::_new(imp::Literal::f64_unsuffixed(f))
    }

    /// Creates a new suffixed floating-point literal.
    ///
    /// This constructor will create a literal like `1.0f64` where the value
    /// specified is the preceding part of the token and `f64` is the suffix of
    /// the token. This token will always be inferred to be an `f64` in the
    /// compiler. Literals created from negative numbers may not survive
    /// round-trips through `TokenStream` or strings and may be broken into two
    /// tokens (`-` and positive literal).
    ///
    /// # Panics
    ///
    /// This function requires that the specified float is finite, for example
    /// if it is infinity or NaN this function will panic.
    pub fn f64_suffixed(f: f64) -> Literal {
        assert!(f.is_finite());
        Literal::_new(imp::Literal::f64_suffixed(f))
    }

    /// Creates a new unsuffixed floating-point literal.
    ///
    /// This constructor is similar to those like `Literal::i8_unsuffixed` where
    /// the float's value is emitted directly into the token but no suffix is
    /// used, so it may be inferred to be a `f64` later in the compiler.
    /// Literals created from negative numbers may not survive round-trips
    /// through `TokenStream` or strings and may be broken into two tokens (`-`
    /// and positive literal).
    ///
    /// # Panics
    ///
    /// This function requires that the specified float is finite, for example
    /// if it is infinity or NaN this function will panic.
    pub fn f32_unsuffixed(f: f32) -> Literal {
        assert!(f.is_finite());
        Literal::_new(imp::Literal::f32_unsuffixed(f))
    }

    /// Creates a new suffixed floating-point literal.
    ///
    /// This constructor will create a literal like `1.0f32` where the value
    /// specified is the preceding part of the token and `f32` is the suffix of
    /// the token. This token will always be inferred to be an `f32` in the
    /// compiler. Literals created from negative numbers may not survive
    /// round-trips through `TokenStream` or strings and may be broken into two
    /// tokens (`-` and positive literal).
    ///
    /// # Panics
    ///
    /// This function requires that the specified float is finite, for example
    /// if it is infinity or NaN this function will panic.
    pub fn f32_suffixed(f: f32) -> Literal {
        assert!(f.is_finite());
        Literal::_new(imp::Literal::f32_suffixed(f))
    }

    /// String literal.
    pub fn string(string: &str) -> Literal {
        Literal::_new(imp::Literal::string(string))
    }

    /// Character literal.
    pub fn character(ch: char) -> Literal {
        Literal::_new(imp::Literal::character(ch))
    }

    /// Byte character literal.
    pub fn byte_character(byte: u8) -> Literal {
        Literal::_new(imp::Literal::byte_character(byte))
    }

    /// Byte string literal.
    pub fn byte_string(bytes: &[u8]) -> Literal {
        Literal::_new(imp::Literal::byte_string(bytes))
    }

    /// C string literal.
    pub fn c_string(string: &CStr) -> Literal {
        Literal::_new(imp::Literal::c_string(string))
    }

    /// Returns the span encompassing this literal.
    pub fn span(&self) -> Span {
        Span::_new(self.inner.span())
    }

    /// Configures the span associated for this literal.
    pub fn set_span(&mut self, span: Span) {
        self.inner.set_span(span.inner);
    }

    /// Returns a `Span` that is a subset of `self.span()` containing only
    /// the source bytes in range `range`. Returns `None` if the would-be
    /// trimmed span is outside the bounds of `self`.
    ///
    /// Warning: the underlying [`proc_macro::Literal::subspan`] method is
    /// nightly-only. When called from within a procedural macro not using a
    /// nightly compiler, this method will always return `None`.
    pub fn subspan<R: RangeBounds<usize>>(&self, range: R) -> Option<Span> {
        self.inner.subspan(range).map(Span::_new)
    }

    // Intended for the `quote!` macro to use when constructing a proc-macro2
    // token out of a macro_rules $:literal token, which is already known to be
    // a valid literal. This avoids reparsing/validating the literal's string
    // representation. This is not public API other than for quote.
    #[doc(hidden)]
    pub unsafe fn from_str_unchecked(repr: &str) -> Self {
        Literal::_new(unsafe { imp::Literal::from_str_unchecked(repr) })
    }
}

impl FromStr for Literal {
    type Err = LexError;

    fn from_str(repr: &str) -> Result<Self, LexError> {
        match imp::Literal::from_str_checked(repr) {
            Ok(lit) => Ok(Literal::_new(lit)),
            Err(lex) => Err(LexError {
                inner: lex,
                _marker: MARKER,
            }),
        }
    }
}

impl Debug for Literal {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.inner, f)
    }
}

impl Display for Literal {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.inner, f)
    }
}

/// Public implementation details for the `TokenStream` type, such as iterators.
pub mod token_stream {
    use crate::marker::{ProcMacroAutoTraits, MARKER};
    use crate::{imp, TokenTree};
    use core::fmt::{self, Debug};

    pub use crate::TokenStream;

    /// An iterator over `TokenStream`'s `TokenTree`s.
    ///
    /// The iteration is "shallow", e.g. the iterator doesn't recurse into
    /// delimited groups, and returns whole groups as token trees.
    #[derive(Clone)]
    pub struct IntoIter {
        inner: imp::TokenTreeIter,
        _marker: ProcMacroAutoTraits,
    }

    impl Iterator for IntoIter {
        type Item = TokenTree;

        fn next(&mut self) -> Option<TokenTree> {
            self.inner.next()
        }

        fn size_hint(&self) -> (usize, Option<usize>) {
            self.inner.size_hint()
        }
    }

    impl Debug for IntoIter {
        fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
            f.write_str("TokenStream ")?;
            f.debug_list().entries(self.clone()).finish()
        }
    }

    impl IntoIterator for TokenStream {
        type Item = TokenTree;
        type IntoIter = IntoIter;

        fn into_iter(self) -> IntoIter {
            IntoIter {
                inner: self.inner.into_iter(),
                _marker: MARKER,
            }
        }
    }
}
