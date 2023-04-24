// SPDX-License-Identifier: Apache-2.0 OR MIT

use crate::helpers::{parse_generics, Generics};
use proc_macro::TokenStream;

pub(crate) fn pin_data(args: TokenStream, input: TokenStream) -> TokenStream {
    // This proc-macro only does some pre-parsing and then delegates the actual parsing to
    // `kernel::__pin_data!`.

    let (
        Generics {
            impl_generics,
            ty_generics,
        },
        mut rest,
    ) = parse_generics(input);
    // This should be the body of the struct `{...}`.
    let last = rest.pop();
    quote!(::kernel::__pin_data! {
        parse_input:
        @args(#args),
        @sig(#(#rest)*),
        @impl_generics(#(#impl_generics)*),
        @ty_generics(#(#ty_generics)*),
        @body(#last),
    })
}
