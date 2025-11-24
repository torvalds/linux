use proc_macro2::extra::DelimSpan;
use proc_macro2::{Delimiter, Group, Span, TokenStream};

#[doc(hidden)]
pub trait IntoSpans<S> {
    fn into_spans(self) -> S;
}

impl IntoSpans<Span> for Span {
    fn into_spans(self) -> Span {
        self
    }
}

impl IntoSpans<[Span; 1]> for Span {
    fn into_spans(self) -> [Span; 1] {
        [self]
    }
}

impl IntoSpans<[Span; 2]> for Span {
    fn into_spans(self) -> [Span; 2] {
        [self, self]
    }
}

impl IntoSpans<[Span; 3]> for Span {
    fn into_spans(self) -> [Span; 3] {
        [self, self, self]
    }
}

impl IntoSpans<[Span; 1]> for [Span; 1] {
    fn into_spans(self) -> [Span; 1] {
        self
    }
}

impl IntoSpans<[Span; 2]> for [Span; 2] {
    fn into_spans(self) -> [Span; 2] {
        self
    }
}

impl IntoSpans<[Span; 3]> for [Span; 3] {
    fn into_spans(self) -> [Span; 3] {
        self
    }
}

impl IntoSpans<DelimSpan> for Span {
    fn into_spans(self) -> DelimSpan {
        let mut group = Group::new(Delimiter::None, TokenStream::new());
        group.set_span(self);
        group.delim_span()
    }
}

impl IntoSpans<DelimSpan> for DelimSpan {
    fn into_spans(self) -> DelimSpan {
        self
    }
}
