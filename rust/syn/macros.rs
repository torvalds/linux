#[cfg_attr(
    not(any(feature = "full", feature = "derive")),
    allow(unknown_lints, unused_macro_rules)
)]
macro_rules! ast_struct {
    (
        $(#[$attr:meta])*
        $pub:ident $struct:ident $name:ident #full $body:tt
    ) => {
        check_keyword_matches!(pub $pub);
        check_keyword_matches!(struct $struct);

        #[cfg(feature = "full")]
        $(#[$attr])* $pub $struct $name $body

        #[cfg(not(feature = "full"))]
        $(#[$attr])* $pub $struct $name {
            _noconstruct: ::std::marker::PhantomData<::proc_macro2::Span>,
        }

        #[cfg(all(not(feature = "full"), feature = "printing"))]
        impl ::quote::ToTokens for $name {
            fn to_tokens(&self, _: &mut ::proc_macro2::TokenStream) {
                unreachable!()
            }
        }
    };

    (
        $(#[$attr:meta])*
        $pub:ident $struct:ident $name:ident $body:tt
    ) => {
        check_keyword_matches!(pub $pub);
        check_keyword_matches!(struct $struct);

        $(#[$attr])* $pub $struct $name $body
    };
}

#[cfg(any(feature = "full", feature = "derive"))]
macro_rules! ast_enum {
    (
        $(#[$enum_attr:meta])*
        $pub:ident $enum:ident $name:ident $body:tt
    ) => {
        check_keyword_matches!(pub $pub);
        check_keyword_matches!(enum $enum);

        $(#[$enum_attr])* $pub $enum $name $body
    };
}

macro_rules! ast_enum_of_structs {
    (
        $(#[$enum_attr:meta])*
        $pub:ident $enum:ident $name:ident $body:tt
    ) => {
        check_keyword_matches!(pub $pub);
        check_keyword_matches!(enum $enum);

        $(#[$enum_attr])* $pub $enum $name $body

        ast_enum_of_structs_impl!($name $body);

        #[cfg(feature = "printing")]
        generate_to_tokens!(() tokens $name $body);
    };
}

macro_rules! ast_enum_of_structs_impl {
    (
        $name:ident {
            $(
                $(#[cfg $cfg_attr:tt])*
                $(#[doc $($doc_attr:tt)*])*
                $variant:ident $( ($member:ident) )*,
            )*
        }
    ) => {
        $($(
            ast_enum_from_struct!($name::$variant, $member);
        )*)*
    };
}

macro_rules! ast_enum_from_struct {
    // No From<TokenStream> for verbatim variants.
    ($name:ident::Verbatim, $member:ident) => {};

    ($name:ident::$variant:ident, $member:ident) => {
        impl From<$member> for $name {
            fn from(e: $member) -> $name {
                $name::$variant(e)
            }
        }
    };
}

#[cfg(feature = "printing")]
macro_rules! generate_to_tokens {
    (
        ($($arms:tt)*) $tokens:ident $name:ident {
            $(#[cfg $cfg_attr:tt])*
            $(#[doc $($doc_attr:tt)*])*
            $variant:ident,
            $($next:tt)*
        }
    ) => {
        generate_to_tokens!(
            ($($arms)* $(#[cfg $cfg_attr])* $name::$variant => {})
            $tokens $name { $($next)* }
        );
    };

    (
        ($($arms:tt)*) $tokens:ident $name:ident {
            $(#[cfg $cfg_attr:tt])*
            $(#[doc $($doc_attr:tt)*])*
            $variant:ident($member:ident),
            $($next:tt)*
        }
    ) => {
        generate_to_tokens!(
            ($($arms)* $(#[cfg $cfg_attr])* $name::$variant(_e) => _e.to_tokens($tokens),)
            $tokens $name { $($next)* }
        );
    };

    (($($arms:tt)*) $tokens:ident $name:ident {}) => {
        #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
        impl ::quote::ToTokens for $name {
            fn to_tokens(&self, $tokens: &mut ::proc_macro2::TokenStream) {
                match self {
                    $($arms)*
                }
            }
        }
    };
}

// Rustdoc bug: does not respect the doc(hidden) on some items.
#[cfg(all(doc, feature = "parsing"))]
macro_rules! pub_if_not_doc {
    ($(#[$m:meta])* $pub:ident $($item:tt)*) => {
        check_keyword_matches!(pub $pub);

        $(#[$m])*
        $pub(crate) $($item)*
    };
}

#[cfg(all(not(doc), feature = "parsing"))]
macro_rules! pub_if_not_doc {
    ($(#[$m:meta])* $pub:ident $($item:tt)*) => {
        check_keyword_matches!(pub $pub);

        $(#[$m])*
        $pub $($item)*
    };
}

macro_rules! check_keyword_matches {
    (enum enum) => {};
    (pub pub) => {};
    (struct struct) => {};
}

#[cfg(any(feature = "full", feature = "derive"))]
macro_rules! return_impl_trait {
    (
        $(#[$attr:meta])*
        $vis:vis fn $name:ident $args:tt -> $impl_trait:ty [$concrete:ty] $body:block
    ) => {
        #[cfg(not(docsrs))]
        $(#[$attr])*
        $vis fn $name $args -> $concrete $body

        #[cfg(docsrs)]
        $(#[$attr])*
        $vis fn $name $args -> $impl_trait $body
    };
}
