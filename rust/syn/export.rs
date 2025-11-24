#[doc(hidden)]
pub use std::clone::Clone;
#[doc(hidden)]
pub use std::cmp::{Eq, PartialEq};
#[doc(hidden)]
pub use std::concat;
#[doc(hidden)]
pub use std::default::Default;
#[doc(hidden)]
pub use std::fmt::Debug;
#[doc(hidden)]
pub use std::hash::{Hash, Hasher};
#[doc(hidden)]
pub use std::marker::Copy;
#[doc(hidden)]
pub use std::option::Option::{None, Some};
#[doc(hidden)]
pub use std::result::Result::{Err, Ok};
#[doc(hidden)]
pub use std::stringify;

#[doc(hidden)]
pub type Formatter<'a> = std::fmt::Formatter<'a>;
#[doc(hidden)]
pub type FmtResult = std::fmt::Result;

#[doc(hidden)]
pub type bool = std::primitive::bool;
#[doc(hidden)]
pub type str = std::primitive::str;

#[cfg(feature = "printing")]
#[doc(hidden)]
pub use quote;

#[doc(hidden)]
pub type Span = proc_macro2::Span;
#[doc(hidden)]
pub type TokenStream2 = proc_macro2::TokenStream;

#[cfg(feature = "parsing")]
#[doc(hidden)]
pub use crate::group::{parse_braces, parse_brackets, parse_parens};

#[doc(hidden)]
pub use crate::span::IntoSpans;

#[cfg(all(feature = "parsing", feature = "printing"))]
#[doc(hidden)]
pub use crate::parse_quote::parse as parse_quote;

#[cfg(feature = "parsing")]
#[doc(hidden)]
pub use crate::token::parsing::{peek_punct, punct as parse_punct};

#[cfg(feature = "printing")]
#[doc(hidden)]
pub use crate::token::printing::punct as print_punct;

#[cfg(feature = "parsing")]
#[doc(hidden)]
pub use crate::token::private::CustomToken;

#[cfg(feature = "proc-macro")]
#[doc(hidden)]
pub type TokenStream = proc_macro::TokenStream;

#[cfg(feature = "printing")]
#[doc(hidden)]
pub use quote::{ToTokens, TokenStreamExt};

#[doc(hidden)]
pub struct private(pub(crate) ());
