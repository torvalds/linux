//! Items which do not have a correspondence to any API in the proc_macro crate,
//! but are necessary to include in proc-macro2.

use crate::fallback;
use crate::imp;
use crate::marker::{ProcMacroAutoTraits, MARKER};
use crate::Span;
use core::fmt::{self, Debug};

/// Invalidate any `proc_macro2::Span` that exist on the current thread.
///
/// The implementation of `Span` uses thread-local data structures and this
/// function clears them. Calling any method on a `Span` on the current thread
/// created prior to the invalidation will return incorrect values or crash.
///
/// This function is useful for programs that process more than 2<sup>32</sup>
/// bytes of Rust source code on the same thread. Just like rustc, proc-macro2
/// uses 32-bit source locations, and these wrap around when the total source
/// code processed by the same thread exceeds 2<sup>32</sup> bytes (4
/// gigabytes). After a wraparound, `Span` methods such as `source_text()` can
/// return wrong data.
///
/// # Example
///
/// As of late 2023, there is 200 GB of Rust code published on crates.io.
/// Looking at just the newest version of every crate, it is 16 GB of code. So a
/// workload that involves parsing it all would overflow a 32-bit source
/// location unless spans are being invalidated.
///
/// ```
/// use flate2::read::GzDecoder;
/// use std::ffi::OsStr;
/// use std::io::{BufReader, Read};
/// use std::str::FromStr;
/// use tar::Archive;
///
/// rayon::scope(|s| {
///     for krate in every_version_of_every_crate() {
///         s.spawn(move |_| {
///             proc_macro2::extra::invalidate_current_thread_spans();
///
///             let reader = BufReader::new(krate);
///             let tar = GzDecoder::new(reader);
///             let mut archive = Archive::new(tar);
///             for entry in archive.entries().unwrap() {
///                 let mut entry = entry.unwrap();
///                 let path = entry.path().unwrap();
///                 if path.extension() != Some(OsStr::new("rs")) {
///                     continue;
///                 }
///                 let mut content = String::new();
///                 entry.read_to_string(&mut content).unwrap();
///                 match proc_macro2::TokenStream::from_str(&content) {
///                     Ok(tokens) => {/* ... */},
///                     Err(_) => continue,
///                 }
///             }
///         });
///     }
/// });
/// #
/// # fn every_version_of_every_crate() -> Vec<std::fs::File> {
/// #     Vec::new()
/// # }
/// ```
///
/// # Panics
///
/// This function is not applicable to and will panic if called from a
/// procedural macro.
#[cfg(span_locations)]
#[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
pub fn invalidate_current_thread_spans() {
    crate::imp::invalidate_current_thread_spans();
}

/// An object that holds a [`Group`]'s `span_open()` and `span_close()` together
/// in a more compact representation than holding those 2 spans individually.
///
/// [`Group`]: crate::Group
#[derive(Copy, Clone)]
pub struct DelimSpan {
    inner: DelimSpanEnum,
    _marker: ProcMacroAutoTraits,
}

#[derive(Copy, Clone)]
enum DelimSpanEnum {
    #[cfg(wrap_proc_macro)]
    Compiler {
        join: proc_macro::Span,
        open: proc_macro::Span,
        close: proc_macro::Span,
    },
    Fallback(fallback::Span),
}

impl DelimSpan {
    pub(crate) fn new(group: &imp::Group) -> Self {
        #[cfg(wrap_proc_macro)]
        let inner = match group {
            imp::Group::Compiler(group) => DelimSpanEnum::Compiler {
                join: group.span(),
                open: group.span_open(),
                close: group.span_close(),
            },
            imp::Group::Fallback(group) => DelimSpanEnum::Fallback(group.span()),
        };

        #[cfg(not(wrap_proc_macro))]
        let inner = DelimSpanEnum::Fallback(group.span());

        DelimSpan {
            inner,
            _marker: MARKER,
        }
    }

    /// Returns a span covering the entire delimited group.
    pub fn join(&self) -> Span {
        match &self.inner {
            #[cfg(wrap_proc_macro)]
            DelimSpanEnum::Compiler { join, .. } => Span::_new(imp::Span::Compiler(*join)),
            DelimSpanEnum::Fallback(span) => Span::_new_fallback(*span),
        }
    }

    /// Returns a span for the opening punctuation of the group only.
    pub fn open(&self) -> Span {
        match &self.inner {
            #[cfg(wrap_proc_macro)]
            DelimSpanEnum::Compiler { open, .. } => Span::_new(imp::Span::Compiler(*open)),
            DelimSpanEnum::Fallback(span) => Span::_new_fallback(span.first_byte()),
        }
    }

    /// Returns a span for the closing punctuation of the group only.
    pub fn close(&self) -> Span {
        match &self.inner {
            #[cfg(wrap_proc_macro)]
            DelimSpanEnum::Compiler { close, .. } => Span::_new(imp::Span::Compiler(*close)),
            DelimSpanEnum::Fallback(span) => Span::_new_fallback(span.last_byte()),
        }
    }
}

impl Debug for DelimSpan {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.join(), f)
    }
}
