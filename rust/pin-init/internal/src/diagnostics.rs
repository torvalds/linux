// SPDX-License-Identifier: Apache-2.0 OR MIT

use std::fmt::Display;

use proc_macro2::TokenStream;
use quote::quote_spanned;
use syn::{spanned::Spanned, Error};

pub(crate) struct DiagCtxt(TokenStream);
pub(crate) struct ErrorGuaranteed(());

impl DiagCtxt {
    pub(crate) fn error(&mut self, span: impl Spanned, msg: impl Display) -> ErrorGuaranteed {
        let error = Error::new(span.span(), msg);
        self.0.extend(error.into_compile_error());
        ErrorGuaranteed(())
    }

    pub(crate) fn warn(&mut self, span: impl Spanned, msg: impl Display) {
        // Have the message start on a new line for visual clarity.
        let msg = format!("\n{}", msg);
        self.0.extend(quote_spanned!(span.span() =>
            // Approximate using deprecated warning while `proc_macro_diagnostic` is unstable.
            const _: () = {
                #[deprecated = #msg]
                const fn warn() {}
                warn();
            };
        ));
    }

    pub(crate) fn with(
        fun: impl FnOnce(&mut DiagCtxt) -> Result<TokenStream, ErrorGuaranteed>,
    ) -> TokenStream {
        let mut dcx = Self(TokenStream::new());
        match fun(&mut dcx) {
            Ok(mut stream) => {
                stream.extend(dcx.0);
                stream
            }
            Err(ErrorGuaranteed(())) => dcx.0,
        }
    }
}
