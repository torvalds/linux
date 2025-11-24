/// Formatting macro for constructing `Ident`s.
///
/// <br>
///
/// # Syntax
///
/// Syntax is copied from the [`format!`] macro, supporting both positional and
/// named arguments.
///
/// Only a limited set of formatting traits are supported. The current mapping
/// of format types to traits is:
///
/// * `{}` ⇒ [`IdentFragment`]
/// * `{:o}` ⇒ [`Octal`](std::fmt::Octal)
/// * `{:x}` ⇒ [`LowerHex`](std::fmt::LowerHex)
/// * `{:X}` ⇒ [`UpperHex`](std::fmt::UpperHex)
/// * `{:b}` ⇒ [`Binary`](std::fmt::Binary)
///
/// See [`std::fmt`] for more information.
///
/// <br>
///
/// # IdentFragment
///
/// Unlike `format!`, this macro uses the [`IdentFragment`] formatting trait by
/// default. This trait is like `Display`, with a few differences:
///
/// * `IdentFragment` is only implemented for a limited set of types, such as
///   unsigned integers and strings.
/// * [`Ident`] arguments will have their `r#` prefixes stripped, if present.
///
/// [`IdentFragment`]: crate::IdentFragment
/// [`Ident`]: proc_macro2::Ident
///
/// <br>
///
/// # Hygiene
///
/// The [`Span`] of the first `Ident` argument is used as the span of the final
/// identifier, falling back to [`Span::call_site`] when no identifiers are
/// provided.
///
/// ```
/// # use quote::format_ident;
/// # let ident = format_ident!("Ident");
/// // If `ident` is an Ident, the span of `my_ident` will be inherited from it.
/// let my_ident = format_ident!("My{}{}", ident, "IsCool");
/// assert_eq!(my_ident, "MyIdentIsCool");
/// ```
///
/// Alternatively, the span can be overridden by passing the `span` named
/// argument.
///
/// ```
/// # use quote::format_ident;
/// # const IGNORE_TOKENS: &'static str = stringify! {
/// let my_span = /* ... */;
/// # };
/// # let my_span = proc_macro2::Span::call_site();
/// format_ident!("MyIdent", span = my_span);
/// ```
///
/// [`Span`]: proc_macro2::Span
/// [`Span::call_site`]: proc_macro2::Span::call_site
///
/// <p><br></p>
///
/// # Panics
///
/// This method will panic if the resulting formatted string is not a valid
/// identifier.
///
/// <br>
///
/// # Examples
///
/// Composing raw and non-raw identifiers:
/// ```
/// # use quote::format_ident;
/// let my_ident = format_ident!("My{}", "Ident");
/// assert_eq!(my_ident, "MyIdent");
///
/// let raw = format_ident!("r#Raw");
/// assert_eq!(raw, "r#Raw");
///
/// let my_ident_raw = format_ident!("{}Is{}", my_ident, raw);
/// assert_eq!(my_ident_raw, "MyIdentIsRaw");
/// ```
///
/// Integer formatting options:
/// ```
/// # use quote::format_ident;
/// let num: u32 = 10;
///
/// let decimal = format_ident!("Id_{}", num);
/// assert_eq!(decimal, "Id_10");
///
/// let octal = format_ident!("Id_{:o}", num);
/// assert_eq!(octal, "Id_12");
///
/// let binary = format_ident!("Id_{:b}", num);
/// assert_eq!(binary, "Id_1010");
///
/// let lower_hex = format_ident!("Id_{:x}", num);
/// assert_eq!(lower_hex, "Id_a");
///
/// let upper_hex = format_ident!("Id_{:X}", num);
/// assert_eq!(upper_hex, "Id_A");
/// ```
#[macro_export]
macro_rules! format_ident {
    ($fmt:expr) => {
        $crate::format_ident_impl!([
            $crate::__private::Option::None,
            $fmt
        ])
    };

    ($fmt:expr, $($rest:tt)*) => {
        $crate::format_ident_impl!([
            $crate::__private::Option::None,
            $fmt
        ] $($rest)*)
    };
}

#[macro_export]
#[doc(hidden)]
macro_rules! format_ident_impl {
    // Final state
    ([$span:expr, $($fmt:tt)*]) => {
        $crate::__private::mk_ident(
            &$crate::__private::format!($($fmt)*),
            $span,
        )
    };

    // Span argument
    ([$old:expr, $($fmt:tt)*] span = $span:expr) => {
        $crate::format_ident_impl!([$old, $($fmt)*] span = $span,)
    };
    ([$old:expr, $($fmt:tt)*] span = $span:expr, $($rest:tt)*) => {
        $crate::format_ident_impl!([
            $crate::__private::Option::Some::<$crate::__private::Span>($span),
            $($fmt)*
        ] $($rest)*)
    };

    // Named argument
    ([$span:expr, $($fmt:tt)*] $name:ident = $arg:expr) => {
        $crate::format_ident_impl!([$span, $($fmt)*] $name = $arg,)
    };
    ([$span:expr, $($fmt:tt)*] $name:ident = $arg:expr, $($rest:tt)*) => {
        match $crate::__private::IdentFragmentAdapter(&$arg) {
            arg => $crate::format_ident_impl!([$span.or(arg.span()), $($fmt)*, $name = arg] $($rest)*),
        }
    };

    // Positional argument
    ([$span:expr, $($fmt:tt)*] $arg:expr) => {
        $crate::format_ident_impl!([$span, $($fmt)*] $arg,)
    };
    ([$span:expr, $($fmt:tt)*] $arg:expr, $($rest:tt)*) => {
        match $crate::__private::IdentFragmentAdapter(&$arg) {
            arg => $crate::format_ident_impl!([$span.or(arg.span()), $($fmt)*, arg] $($rest)*),
        }
    };
}
