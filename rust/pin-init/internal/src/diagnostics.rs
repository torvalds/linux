// SPDX-License-Identifier: Apache-2.0 OR MIT

use std::cell::RefCell;
use std::fmt::Display;
use std::marker::PhantomData;

use proc_macro2::{Span, TokenStream};
use quote::{quote, quote_spanned};
use syn::{spanned::Spanned, Error};

pub(crate) struct DiagCtxt(PhantomData<*mut ()>);
pub(crate) struct ErrorGuaranteed(());

struct DiagCtxtData {
    diag: TokenStream,
}

thread_local! {
    static DIAGNOSTICS: RefCell<Option<DiagCtxtData>> = const { RefCell::new(None) };
}

// Allows `syn::Error` to be emitted into the current diagnostic context with just `?`.
impl From<syn::Error> for ErrorGuaranteed {
    fn from(error: syn::Error) -> Self {
        DIAGNOSTICS.with_borrow_mut(|data| {
            data.as_mut()
                .unwrap()
                .diag
                .extend(error.into_compile_error());
        });
        Self(())
    }
}

impl DiagCtxt {
    pub(crate) fn error(&self, span: impl Spanned, msg: impl Display) -> ErrorGuaranteed {
        Error::new(span.span(), msg).into()
    }

    pub(crate) fn warn(&self, span: impl Spanned, msg: impl Display) {
        // Have the message start on a new line for visual clarity.
        let msg = format!("\n{}", msg);
        DIAGNOSTICS.with_borrow_mut(|data| {
            data.as_mut()
                .unwrap()
                .diag
                .extend(quote_spanned!(span.span() =>
                    // Approximate using deprecated warning while `proc_macro_diagnostic` is
                    // unstable.
                    const _: () = {
                        #[deprecated = #msg]
                        const fn warn() {}
                        warn();
                    };
                ))
        });
    }

    /// Execute the provided function with the current diagnostic context.
    pub(crate) fn current<R>(f: impl FnOnce(&DiagCtxt) -> R) -> R {
        DIAGNOSTICS.with_borrow(|data| {
            assert!(data.is_some(), "No active `DiagCtxt`");
        });

        f(&DiagCtxt(PhantomData))
    }

    fn with(
        f: impl FnOnce(&mut DiagCtxt) -> Result<TokenStream, ErrorGuaranteed>,
        merge_diag: impl FnOnce(TokenStream, TokenStream) -> TokenStream,
        convert_diag: impl FnOnce(TokenStream) -> TokenStream,
    ) -> TokenStream {
        DIAGNOSTICS.with_borrow_mut(|data| {
            assert!(data.is_none(), "`DiagCtxt` cannot be nested");
            *data = Some(DiagCtxtData {
                diag: TokenStream::new(),
            });
        });

        let mut dcx = DiagCtxt(PhantomData);
        let result = match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| f(&mut dcx))) {
            Ok(result) => result,
            Err(payload) => {
                // Robustness against panicking in macros.
                //
                // Ensure that any error messages are still emitted when this happens.
                let message = if let Some(&s) = payload.downcast_ref::<&'static str>() {
                    s
                } else if let Some(s) = payload.downcast_ref::<String>() {
                    s.as_str()
                } else {
                    "Box<dyn Any>"
                };

                Err(dcx.error(
                    Span::mixed_site(),
                    format!("proc macro panicked: {message}"),
                ))
            }
        };

        let data = DIAGNOSTICS.with_borrow_mut(|data| data.take().unwrap());

        match result {
            Ok(stream) => {
                if data.diag.is_empty() {
                    stream
                } else {
                    merge_diag(stream, data.diag)
                }
            }
            Err(ErrorGuaranteed(())) => convert_diag(data.diag),
        }
    }

    pub(crate) fn for_item(
        f: impl FnOnce(&mut DiagCtxt) -> Result<TokenStream, ErrorGuaranteed>,
    ) -> TokenStream {
        Self::with(
            f,
            |mut out, diag| {
                out.extend(diag);
                out
            },
            std::convert::identity,
        )
    }

    pub(crate) fn for_expr(
        f: impl FnOnce(&mut DiagCtxt) -> Result<TokenStream, ErrorGuaranteed>,
    ) -> TokenStream {
        Self::with(
            f,
            |out, diag| {
                // Diagnostics that we generate are always items.
                // So for expressions create a block to place diagnostics in item position.
                quote!({
                    #diag
                    #out
                })
            },
            |diag| {
                quote!({
                    #diag
                })
            },
        )
    }
}
