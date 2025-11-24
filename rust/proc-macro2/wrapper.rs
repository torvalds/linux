use crate::detection::inside_proc_macro;
use crate::fallback::{self, FromStr2 as _};
#[cfg(span_locations)]
use crate::location::LineColumn;
#[cfg(proc_macro_span)]
use crate::probe::proc_macro_span;
#[cfg(all(span_locations, proc_macro_span_file))]
use crate::probe::proc_macro_span_file;
#[cfg(all(span_locations, proc_macro_span_location))]
use crate::probe::proc_macro_span_location;
use crate::{Delimiter, Punct, Spacing, TokenTree};
use core::fmt::{self, Debug, Display};
#[cfg(span_locations)]
use core::ops::Range;
use core::ops::RangeBounds;
use std::ffi::CStr;
#[cfg(span_locations)]
use std::path::PathBuf;

#[derive(Clone)]
pub(crate) enum TokenStream {
    Compiler(DeferredTokenStream),
    Fallback(fallback::TokenStream),
}

// Work around https://github.com/rust-lang/rust/issues/65080.
// In `impl Extend<TokenTree> for TokenStream` which is used heavily by quote,
// we hold on to the appended tokens and do proc_macro::TokenStream::extend as
// late as possible to batch together consecutive uses of the Extend impl.
#[derive(Clone)]
pub(crate) struct DeferredTokenStream {
    stream: proc_macro::TokenStream,
    extra: Vec<proc_macro::TokenTree>,
}

pub(crate) enum LexError {
    Compiler(proc_macro::LexError),
    Fallback(fallback::LexError),

    // Rustc was supposed to return a LexError, but it panicked instead.
    // https://github.com/rust-lang/rust/issues/58736
    CompilerPanic,
}

#[cold]
fn mismatch(line: u32) -> ! {
    #[cfg(procmacro2_backtrace)]
    {
        let backtrace = std::backtrace::Backtrace::force_capture();
        panic!("compiler/fallback mismatch L{}\n\n{}", line, backtrace)
    }
    #[cfg(not(procmacro2_backtrace))]
    {
        panic!("compiler/fallback mismatch L{}", line)
    }
}

impl DeferredTokenStream {
    fn new(stream: proc_macro::TokenStream) -> Self {
        DeferredTokenStream {
            stream,
            extra: Vec::new(),
        }
    }

    fn is_empty(&self) -> bool {
        self.stream.is_empty() && self.extra.is_empty()
    }

    fn evaluate_now(&mut self) {
        // If-check provides a fast short circuit for the common case of `extra`
        // being empty, which saves a round trip over the proc macro bridge.
        // Improves macro expansion time in winrt by 6% in debug mode.
        if !self.extra.is_empty() {
            self.stream.extend(self.extra.drain(..));
        }
    }

    fn into_token_stream(mut self) -> proc_macro::TokenStream {
        self.evaluate_now();
        self.stream
    }
}

impl TokenStream {
    pub(crate) fn new() -> Self {
        if inside_proc_macro() {
            TokenStream::Compiler(DeferredTokenStream::new(proc_macro::TokenStream::new()))
        } else {
            TokenStream::Fallback(fallback::TokenStream::new())
        }
    }

    pub(crate) fn from_str_checked(src: &str) -> Result<Self, LexError> {
        if inside_proc_macro() {
            Ok(TokenStream::Compiler(DeferredTokenStream::new(
                proc_macro::TokenStream::from_str_checked(src)?,
            )))
        } else {
            Ok(TokenStream::Fallback(
                fallback::TokenStream::from_str_checked(src)?,
            ))
        }
    }

    pub(crate) fn is_empty(&self) -> bool {
        match self {
            TokenStream::Compiler(tts) => tts.is_empty(),
            TokenStream::Fallback(tts) => tts.is_empty(),
        }
    }

    fn unwrap_nightly(self) -> proc_macro::TokenStream {
        match self {
            TokenStream::Compiler(s) => s.into_token_stream(),
            TokenStream::Fallback(_) => mismatch(line!()),
        }
    }

    fn unwrap_stable(self) -> fallback::TokenStream {
        match self {
            TokenStream::Compiler(_) => mismatch(line!()),
            TokenStream::Fallback(s) => s,
        }
    }
}

impl Display for TokenStream {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            TokenStream::Compiler(tts) => Display::fmt(&tts.clone().into_token_stream(), f),
            TokenStream::Fallback(tts) => Display::fmt(tts, f),
        }
    }
}

impl From<proc_macro::TokenStream> for TokenStream {
    fn from(inner: proc_macro::TokenStream) -> Self {
        TokenStream::Compiler(DeferredTokenStream::new(inner))
    }
}

impl From<TokenStream> for proc_macro::TokenStream {
    fn from(inner: TokenStream) -> Self {
        match inner {
            TokenStream::Compiler(inner) => inner.into_token_stream(),
            TokenStream::Fallback(inner) => {
                proc_macro::TokenStream::from_str_unchecked(&inner.to_string())
            }
        }
    }
}

impl From<fallback::TokenStream> for TokenStream {
    fn from(inner: fallback::TokenStream) -> Self {
        TokenStream::Fallback(inner)
    }
}

// Assumes inside_proc_macro().
fn into_compiler_token(token: TokenTree) -> proc_macro::TokenTree {
    match token {
        TokenTree::Group(tt) => proc_macro::TokenTree::Group(tt.inner.unwrap_nightly()),
        TokenTree::Punct(tt) => {
            let spacing = match tt.spacing() {
                Spacing::Joint => proc_macro::Spacing::Joint,
                Spacing::Alone => proc_macro::Spacing::Alone,
            };
            let mut punct = proc_macro::Punct::new(tt.as_char(), spacing);
            punct.set_span(tt.span().inner.unwrap_nightly());
            proc_macro::TokenTree::Punct(punct)
        }
        TokenTree::Ident(tt) => proc_macro::TokenTree::Ident(tt.inner.unwrap_nightly()),
        TokenTree::Literal(tt) => proc_macro::TokenTree::Literal(tt.inner.unwrap_nightly()),
    }
}

impl From<TokenTree> for TokenStream {
    fn from(token: TokenTree) -> Self {
        if inside_proc_macro() {
            TokenStream::Compiler(DeferredTokenStream::new(proc_macro::TokenStream::from(
                into_compiler_token(token),
            )))
        } else {
            TokenStream::Fallback(fallback::TokenStream::from(token))
        }
    }
}

impl FromIterator<TokenTree> for TokenStream {
    fn from_iter<I: IntoIterator<Item = TokenTree>>(trees: I) -> Self {
        if inside_proc_macro() {
            TokenStream::Compiler(DeferredTokenStream::new(
                trees.into_iter().map(into_compiler_token).collect(),
            ))
        } else {
            TokenStream::Fallback(trees.into_iter().collect())
        }
    }
}

impl FromIterator<TokenStream> for TokenStream {
    fn from_iter<I: IntoIterator<Item = TokenStream>>(streams: I) -> Self {
        let mut streams = streams.into_iter();
        match streams.next() {
            Some(TokenStream::Compiler(mut first)) => {
                first.evaluate_now();
                first.stream.extend(streams.map(|s| match s {
                    TokenStream::Compiler(s) => s.into_token_stream(),
                    TokenStream::Fallback(_) => mismatch(line!()),
                }));
                TokenStream::Compiler(first)
            }
            Some(TokenStream::Fallback(mut first)) => {
                first.extend(streams.map(|s| match s {
                    TokenStream::Fallback(s) => s,
                    TokenStream::Compiler(_) => mismatch(line!()),
                }));
                TokenStream::Fallback(first)
            }
            None => TokenStream::new(),
        }
    }
}

impl Extend<TokenTree> for TokenStream {
    fn extend<I: IntoIterator<Item = TokenTree>>(&mut self, stream: I) {
        match self {
            TokenStream::Compiler(tts) => {
                // Here is the reason for DeferredTokenStream.
                for token in stream {
                    tts.extra.push(into_compiler_token(token));
                }
            }
            TokenStream::Fallback(tts) => tts.extend(stream),
        }
    }
}

impl Extend<TokenStream> for TokenStream {
    fn extend<I: IntoIterator<Item = TokenStream>>(&mut self, streams: I) {
        match self {
            TokenStream::Compiler(tts) => {
                tts.evaluate_now();
                tts.stream
                    .extend(streams.into_iter().map(TokenStream::unwrap_nightly));
            }
            TokenStream::Fallback(tts) => {
                tts.extend(streams.into_iter().map(TokenStream::unwrap_stable));
            }
        }
    }
}

impl Debug for TokenStream {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            TokenStream::Compiler(tts) => Debug::fmt(&tts.clone().into_token_stream(), f),
            TokenStream::Fallback(tts) => Debug::fmt(tts, f),
        }
    }
}

impl LexError {
    pub(crate) fn span(&self) -> Span {
        match self {
            LexError::Compiler(_) | LexError::CompilerPanic => Span::call_site(),
            LexError::Fallback(e) => Span::Fallback(e.span()),
        }
    }
}

impl From<proc_macro::LexError> for LexError {
    fn from(e: proc_macro::LexError) -> Self {
        LexError::Compiler(e)
    }
}

impl From<fallback::LexError> for LexError {
    fn from(e: fallback::LexError) -> Self {
        LexError::Fallback(e)
    }
}

impl Debug for LexError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            LexError::Compiler(e) => Debug::fmt(e, f),
            LexError::Fallback(e) => Debug::fmt(e, f),
            LexError::CompilerPanic => {
                let fallback = fallback::LexError::call_site();
                Debug::fmt(&fallback, f)
            }
        }
    }
}

impl Display for LexError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            LexError::Compiler(e) => Display::fmt(e, f),
            LexError::Fallback(e) => Display::fmt(e, f),
            LexError::CompilerPanic => {
                let fallback = fallback::LexError::call_site();
                Display::fmt(&fallback, f)
            }
        }
    }
}

#[derive(Clone)]
pub(crate) enum TokenTreeIter {
    Compiler(proc_macro::token_stream::IntoIter),
    Fallback(fallback::TokenTreeIter),
}

impl IntoIterator for TokenStream {
    type Item = TokenTree;
    type IntoIter = TokenTreeIter;

    fn into_iter(self) -> TokenTreeIter {
        match self {
            TokenStream::Compiler(tts) => {
                TokenTreeIter::Compiler(tts.into_token_stream().into_iter())
            }
            TokenStream::Fallback(tts) => TokenTreeIter::Fallback(tts.into_iter()),
        }
    }
}

impl Iterator for TokenTreeIter {
    type Item = TokenTree;

    fn next(&mut self) -> Option<TokenTree> {
        let token = match self {
            TokenTreeIter::Compiler(iter) => iter.next()?,
            TokenTreeIter::Fallback(iter) => return iter.next(),
        };
        Some(match token {
            proc_macro::TokenTree::Group(tt) => {
                TokenTree::Group(crate::Group::_new(Group::Compiler(tt)))
            }
            proc_macro::TokenTree::Punct(tt) => {
                let spacing = match tt.spacing() {
                    proc_macro::Spacing::Joint => Spacing::Joint,
                    proc_macro::Spacing::Alone => Spacing::Alone,
                };
                let mut o = Punct::new(tt.as_char(), spacing);
                o.set_span(crate::Span::_new(Span::Compiler(tt.span())));
                TokenTree::Punct(o)
            }
            proc_macro::TokenTree::Ident(s) => {
                TokenTree::Ident(crate::Ident::_new(Ident::Compiler(s)))
            }
            proc_macro::TokenTree::Literal(l) => {
                TokenTree::Literal(crate::Literal::_new(Literal::Compiler(l)))
            }
        })
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        match self {
            TokenTreeIter::Compiler(tts) => tts.size_hint(),
            TokenTreeIter::Fallback(tts) => tts.size_hint(),
        }
    }
}

#[derive(Copy, Clone)]
pub(crate) enum Span {
    Compiler(proc_macro::Span),
    Fallback(fallback::Span),
}

impl Span {
    pub(crate) fn call_site() -> Self {
        if inside_proc_macro() {
            Span::Compiler(proc_macro::Span::call_site())
        } else {
            Span::Fallback(fallback::Span::call_site())
        }
    }

    pub(crate) fn mixed_site() -> Self {
        if inside_proc_macro() {
            Span::Compiler(proc_macro::Span::mixed_site())
        } else {
            Span::Fallback(fallback::Span::mixed_site())
        }
    }

    #[cfg(super_unstable)]
    pub(crate) fn def_site() -> Self {
        if inside_proc_macro() {
            Span::Compiler(proc_macro::Span::def_site())
        } else {
            Span::Fallback(fallback::Span::def_site())
        }
    }

    pub(crate) fn resolved_at(&self, other: Span) -> Span {
        match (self, other) {
            (Span::Compiler(a), Span::Compiler(b)) => Span::Compiler(a.resolved_at(b)),
            (Span::Fallback(a), Span::Fallback(b)) => Span::Fallback(a.resolved_at(b)),
            (Span::Compiler(_), Span::Fallback(_)) => mismatch(line!()),
            (Span::Fallback(_), Span::Compiler(_)) => mismatch(line!()),
        }
    }

    pub(crate) fn located_at(&self, other: Span) -> Span {
        match (self, other) {
            (Span::Compiler(a), Span::Compiler(b)) => Span::Compiler(a.located_at(b)),
            (Span::Fallback(a), Span::Fallback(b)) => Span::Fallback(a.located_at(b)),
            (Span::Compiler(_), Span::Fallback(_)) => mismatch(line!()),
            (Span::Fallback(_), Span::Compiler(_)) => mismatch(line!()),
        }
    }

    pub(crate) fn unwrap(self) -> proc_macro::Span {
        match self {
            Span::Compiler(s) => s,
            Span::Fallback(_) => panic!("proc_macro::Span is only available in procedural macros"),
        }
    }

    #[cfg(span_locations)]
    pub(crate) fn byte_range(&self) -> Range<usize> {
        match self {
            #[cfg(proc_macro_span)]
            Span::Compiler(s) => proc_macro_span::byte_range(s),
            #[cfg(not(proc_macro_span))]
            Span::Compiler(_) => 0..0,
            Span::Fallback(s) => s.byte_range(),
        }
    }

    #[cfg(span_locations)]
    pub(crate) fn start(&self) -> LineColumn {
        match self {
            #[cfg(proc_macro_span_location)]
            Span::Compiler(s) => LineColumn {
                line: proc_macro_span_location::line(s),
                column: proc_macro_span_location::column(s).saturating_sub(1),
            },
            #[cfg(not(proc_macro_span_location))]
            Span::Compiler(_) => LineColumn { line: 0, column: 0 },
            Span::Fallback(s) => s.start(),
        }
    }

    #[cfg(span_locations)]
    pub(crate) fn end(&self) -> LineColumn {
        match self {
            #[cfg(proc_macro_span_location)]
            Span::Compiler(s) => {
                let end = proc_macro_span_location::end(s);
                LineColumn {
                    line: proc_macro_span_location::line(&end),
                    column: proc_macro_span_location::column(&end).saturating_sub(1),
                }
            }
            #[cfg(not(proc_macro_span_location))]
            Span::Compiler(_) => LineColumn { line: 0, column: 0 },
            Span::Fallback(s) => s.end(),
        }
    }

    #[cfg(span_locations)]
    pub(crate) fn file(&self) -> String {
        match self {
            #[cfg(proc_macro_span_file)]
            Span::Compiler(s) => proc_macro_span_file::file(s),
            #[cfg(not(proc_macro_span_file))]
            Span::Compiler(_) => "<token stream>".to_owned(),
            Span::Fallback(s) => s.file(),
        }
    }

    #[cfg(span_locations)]
    pub(crate) fn local_file(&self) -> Option<PathBuf> {
        match self {
            #[cfg(proc_macro_span_file)]
            Span::Compiler(s) => proc_macro_span_file::local_file(s),
            #[cfg(not(proc_macro_span_file))]
            Span::Compiler(_) => None,
            Span::Fallback(s) => s.local_file(),
        }
    }

    pub(crate) fn join(&self, other: Span) -> Option<Span> {
        let ret = match (self, other) {
            #[cfg(proc_macro_span)]
            (Span::Compiler(a), Span::Compiler(b)) => Span::Compiler(proc_macro_span::join(a, b)?),
            (Span::Fallback(a), Span::Fallback(b)) => Span::Fallback(a.join(b)?),
            _ => return None,
        };
        Some(ret)
    }

    #[cfg(super_unstable)]
    pub(crate) fn eq(&self, other: &Span) -> bool {
        match (self, other) {
            (Span::Compiler(a), Span::Compiler(b)) => a.eq(b),
            (Span::Fallback(a), Span::Fallback(b)) => a.eq(b),
            _ => false,
        }
    }

    pub(crate) fn source_text(&self) -> Option<String> {
        match self {
            #[cfg(not(no_source_text))]
            Span::Compiler(s) => s.source_text(),
            #[cfg(no_source_text)]
            Span::Compiler(_) => None,
            Span::Fallback(s) => s.source_text(),
        }
    }

    fn unwrap_nightly(self) -> proc_macro::Span {
        match self {
            Span::Compiler(s) => s,
            Span::Fallback(_) => mismatch(line!()),
        }
    }
}

impl From<proc_macro::Span> for crate::Span {
    fn from(proc_span: proc_macro::Span) -> Self {
        crate::Span::_new(Span::Compiler(proc_span))
    }
}

impl From<fallback::Span> for Span {
    fn from(inner: fallback::Span) -> Self {
        Span::Fallback(inner)
    }
}

impl Debug for Span {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Span::Compiler(s) => Debug::fmt(s, f),
            Span::Fallback(s) => Debug::fmt(s, f),
        }
    }
}

pub(crate) fn debug_span_field_if_nontrivial(debug: &mut fmt::DebugStruct, span: Span) {
    match span {
        Span::Compiler(s) => {
            debug.field("span", &s);
        }
        Span::Fallback(s) => fallback::debug_span_field_if_nontrivial(debug, s),
    }
}

#[derive(Clone)]
pub(crate) enum Group {
    Compiler(proc_macro::Group),
    Fallback(fallback::Group),
}

impl Group {
    pub(crate) fn new(delimiter: Delimiter, stream: TokenStream) -> Self {
        match stream {
            TokenStream::Compiler(tts) => {
                let delimiter = match delimiter {
                    Delimiter::Parenthesis => proc_macro::Delimiter::Parenthesis,
                    Delimiter::Bracket => proc_macro::Delimiter::Bracket,
                    Delimiter::Brace => proc_macro::Delimiter::Brace,
                    Delimiter::None => proc_macro::Delimiter::None,
                };
                Group::Compiler(proc_macro::Group::new(delimiter, tts.into_token_stream()))
            }
            TokenStream::Fallback(stream) => {
                Group::Fallback(fallback::Group::new(delimiter, stream))
            }
        }
    }

    pub(crate) fn delimiter(&self) -> Delimiter {
        match self {
            Group::Compiler(g) => match g.delimiter() {
                proc_macro::Delimiter::Parenthesis => Delimiter::Parenthesis,
                proc_macro::Delimiter::Bracket => Delimiter::Bracket,
                proc_macro::Delimiter::Brace => Delimiter::Brace,
                proc_macro::Delimiter::None => Delimiter::None,
            },
            Group::Fallback(g) => g.delimiter(),
        }
    }

    pub(crate) fn stream(&self) -> TokenStream {
        match self {
            Group::Compiler(g) => TokenStream::Compiler(DeferredTokenStream::new(g.stream())),
            Group::Fallback(g) => TokenStream::Fallback(g.stream()),
        }
    }

    pub(crate) fn span(&self) -> Span {
        match self {
            Group::Compiler(g) => Span::Compiler(g.span()),
            Group::Fallback(g) => Span::Fallback(g.span()),
        }
    }

    pub(crate) fn span_open(&self) -> Span {
        match self {
            Group::Compiler(g) => Span::Compiler(g.span_open()),
            Group::Fallback(g) => Span::Fallback(g.span_open()),
        }
    }

    pub(crate) fn span_close(&self) -> Span {
        match self {
            Group::Compiler(g) => Span::Compiler(g.span_close()),
            Group::Fallback(g) => Span::Fallback(g.span_close()),
        }
    }

    pub(crate) fn set_span(&mut self, span: Span) {
        match (self, span) {
            (Group::Compiler(g), Span::Compiler(s)) => g.set_span(s),
            (Group::Fallback(g), Span::Fallback(s)) => g.set_span(s),
            (Group::Compiler(_), Span::Fallback(_)) => mismatch(line!()),
            (Group::Fallback(_), Span::Compiler(_)) => mismatch(line!()),
        }
    }

    fn unwrap_nightly(self) -> proc_macro::Group {
        match self {
            Group::Compiler(g) => g,
            Group::Fallback(_) => mismatch(line!()),
        }
    }
}

impl From<fallback::Group> for Group {
    fn from(g: fallback::Group) -> Self {
        Group::Fallback(g)
    }
}

impl Display for Group {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Group::Compiler(group) => Display::fmt(group, formatter),
            Group::Fallback(group) => Display::fmt(group, formatter),
        }
    }
}

impl Debug for Group {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Group::Compiler(group) => Debug::fmt(group, formatter),
            Group::Fallback(group) => Debug::fmt(group, formatter),
        }
    }
}

#[derive(Clone)]
pub(crate) enum Ident {
    Compiler(proc_macro::Ident),
    Fallback(fallback::Ident),
}

impl Ident {
    #[track_caller]
    pub(crate) fn new_checked(string: &str, span: Span) -> Self {
        match span {
            Span::Compiler(s) => Ident::Compiler(proc_macro::Ident::new(string, s)),
            Span::Fallback(s) => Ident::Fallback(fallback::Ident::new_checked(string, s)),
        }
    }

    #[track_caller]
    pub(crate) fn new_raw_checked(string: &str, span: Span) -> Self {
        match span {
            Span::Compiler(s) => Ident::Compiler(proc_macro::Ident::new_raw(string, s)),
            Span::Fallback(s) => Ident::Fallback(fallback::Ident::new_raw_checked(string, s)),
        }
    }

    pub(crate) fn span(&self) -> Span {
        match self {
            Ident::Compiler(t) => Span::Compiler(t.span()),
            Ident::Fallback(t) => Span::Fallback(t.span()),
        }
    }

    pub(crate) fn set_span(&mut self, span: Span) {
        match (self, span) {
            (Ident::Compiler(t), Span::Compiler(s)) => t.set_span(s),
            (Ident::Fallback(t), Span::Fallback(s)) => t.set_span(s),
            (Ident::Compiler(_), Span::Fallback(_)) => mismatch(line!()),
            (Ident::Fallback(_), Span::Compiler(_)) => mismatch(line!()),
        }
    }

    fn unwrap_nightly(self) -> proc_macro::Ident {
        match self {
            Ident::Compiler(s) => s,
            Ident::Fallback(_) => mismatch(line!()),
        }
    }
}

impl From<fallback::Ident> for Ident {
    fn from(inner: fallback::Ident) -> Self {
        Ident::Fallback(inner)
    }
}

impl PartialEq for Ident {
    fn eq(&self, other: &Ident) -> bool {
        match (self, other) {
            (Ident::Compiler(t), Ident::Compiler(o)) => t.to_string() == o.to_string(),
            (Ident::Fallback(t), Ident::Fallback(o)) => t == o,
            (Ident::Compiler(_), Ident::Fallback(_)) => mismatch(line!()),
            (Ident::Fallback(_), Ident::Compiler(_)) => mismatch(line!()),
        }
    }
}

impl<T> PartialEq<T> for Ident
where
    T: ?Sized + AsRef<str>,
{
    fn eq(&self, other: &T) -> bool {
        let other = other.as_ref();
        match self {
            Ident::Compiler(t) => t.to_string() == other,
            Ident::Fallback(t) => t == other,
        }
    }
}

impl Display for Ident {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Ident::Compiler(t) => Display::fmt(t, f),
            Ident::Fallback(t) => Display::fmt(t, f),
        }
    }
}

impl Debug for Ident {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Ident::Compiler(t) => Debug::fmt(t, f),
            Ident::Fallback(t) => Debug::fmt(t, f),
        }
    }
}

#[derive(Clone)]
pub(crate) enum Literal {
    Compiler(proc_macro::Literal),
    Fallback(fallback::Literal),
}

macro_rules! suffixed_numbers {
    ($($name:ident => $kind:ident,)*) => ($(
        pub(crate) fn $name(n: $kind) -> Literal {
            if inside_proc_macro() {
                Literal::Compiler(proc_macro::Literal::$name(n))
            } else {
                Literal::Fallback(fallback::Literal::$name(n))
            }
        }
    )*)
}

macro_rules! unsuffixed_integers {
    ($($name:ident => $kind:ident,)*) => ($(
        pub(crate) fn $name(n: $kind) -> Literal {
            if inside_proc_macro() {
                Literal::Compiler(proc_macro::Literal::$name(n))
            } else {
                Literal::Fallback(fallback::Literal::$name(n))
            }
        }
    )*)
}

impl Literal {
    pub(crate) fn from_str_checked(repr: &str) -> Result<Self, LexError> {
        if inside_proc_macro() {
            let literal = proc_macro::Literal::from_str_checked(repr)?;
            Ok(Literal::Compiler(literal))
        } else {
            let literal = fallback::Literal::from_str_checked(repr)?;
            Ok(Literal::Fallback(literal))
        }
    }

    pub(crate) unsafe fn from_str_unchecked(repr: &str) -> Self {
        if inside_proc_macro() {
            Literal::Compiler(proc_macro::Literal::from_str_unchecked(repr))
        } else {
            Literal::Fallback(unsafe { fallback::Literal::from_str_unchecked(repr) })
        }
    }

    suffixed_numbers! {
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

        f32_suffixed => f32,
        f64_suffixed => f64,
    }

    unsuffixed_integers! {
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

    pub(crate) fn f32_unsuffixed(f: f32) -> Literal {
        if inside_proc_macro() {
            Literal::Compiler(proc_macro::Literal::f32_unsuffixed(f))
        } else {
            Literal::Fallback(fallback::Literal::f32_unsuffixed(f))
        }
    }

    pub(crate) fn f64_unsuffixed(f: f64) -> Literal {
        if inside_proc_macro() {
            Literal::Compiler(proc_macro::Literal::f64_unsuffixed(f))
        } else {
            Literal::Fallback(fallback::Literal::f64_unsuffixed(f))
        }
    }

    pub(crate) fn string(string: &str) -> Literal {
        if inside_proc_macro() {
            Literal::Compiler(proc_macro::Literal::string(string))
        } else {
            Literal::Fallback(fallback::Literal::string(string))
        }
    }

    pub(crate) fn character(ch: char) -> Literal {
        if inside_proc_macro() {
            Literal::Compiler(proc_macro::Literal::character(ch))
        } else {
            Literal::Fallback(fallback::Literal::character(ch))
        }
    }

    pub(crate) fn byte_character(byte: u8) -> Literal {
        if inside_proc_macro() {
            Literal::Compiler({
                #[cfg(not(no_literal_byte_character))]
                {
                    proc_macro::Literal::byte_character(byte)
                }

                #[cfg(no_literal_byte_character)]
                {
                    let fallback = fallback::Literal::byte_character(byte);
                    proc_macro::Literal::from_str_unchecked(&fallback.repr)
                }
            })
        } else {
            Literal::Fallback(fallback::Literal::byte_character(byte))
        }
    }

    pub(crate) fn byte_string(bytes: &[u8]) -> Literal {
        if inside_proc_macro() {
            Literal::Compiler(proc_macro::Literal::byte_string(bytes))
        } else {
            Literal::Fallback(fallback::Literal::byte_string(bytes))
        }
    }

    pub(crate) fn c_string(string: &CStr) -> Literal {
        if inside_proc_macro() {
            Literal::Compiler({
                #[cfg(not(no_literal_c_string))]
                {
                    proc_macro::Literal::c_string(string)
                }

                #[cfg(no_literal_c_string)]
                {
                    let fallback = fallback::Literal::c_string(string);
                    proc_macro::Literal::from_str_unchecked(&fallback.repr)
                }
            })
        } else {
            Literal::Fallback(fallback::Literal::c_string(string))
        }
    }

    pub(crate) fn span(&self) -> Span {
        match self {
            Literal::Compiler(lit) => Span::Compiler(lit.span()),
            Literal::Fallback(lit) => Span::Fallback(lit.span()),
        }
    }

    pub(crate) fn set_span(&mut self, span: Span) {
        match (self, span) {
            (Literal::Compiler(lit), Span::Compiler(s)) => lit.set_span(s),
            (Literal::Fallback(lit), Span::Fallback(s)) => lit.set_span(s),
            (Literal::Compiler(_), Span::Fallback(_)) => mismatch(line!()),
            (Literal::Fallback(_), Span::Compiler(_)) => mismatch(line!()),
        }
    }

    pub(crate) fn subspan<R: RangeBounds<usize>>(&self, range: R) -> Option<Span> {
        match self {
            #[cfg(proc_macro_span)]
            Literal::Compiler(lit) => proc_macro_span::subspan(lit, range).map(Span::Compiler),
            #[cfg(not(proc_macro_span))]
            Literal::Compiler(_lit) => None,
            Literal::Fallback(lit) => lit.subspan(range).map(Span::Fallback),
        }
    }

    fn unwrap_nightly(self) -> proc_macro::Literal {
        match self {
            Literal::Compiler(s) => s,
            Literal::Fallback(_) => mismatch(line!()),
        }
    }
}

impl From<fallback::Literal> for Literal {
    fn from(s: fallback::Literal) -> Self {
        Literal::Fallback(s)
    }
}

impl Display for Literal {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Literal::Compiler(t) => Display::fmt(t, f),
            Literal::Fallback(t) => Display::fmt(t, f),
        }
    }
}

impl Debug for Literal {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Literal::Compiler(t) => Debug::fmt(t, f),
            Literal::Fallback(t) => Debug::fmt(t, f),
        }
    }
}

#[cfg(span_locations)]
pub(crate) fn invalidate_current_thread_spans() {
    if inside_proc_macro() {
        panic!(
            "proc_macro2::extra::invalidate_current_thread_spans is not available in procedural macros"
        );
    } else {
        crate::fallback::invalidate_current_thread_spans();
    }
}
