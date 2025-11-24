ast_enum! {
    /// A binary operator: `+`, `+=`, `&`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    #[non_exhaustive]
    pub enum BinOp {
        /// The `+` operator (addition)
        Add(Token![+]),
        /// The `-` operator (subtraction)
        Sub(Token![-]),
        /// The `*` operator (multiplication)
        Mul(Token![*]),
        /// The `/` operator (division)
        Div(Token![/]),
        /// The `%` operator (modulus)
        Rem(Token![%]),
        /// The `&&` operator (logical and)
        And(Token![&&]),
        /// The `||` operator (logical or)
        Or(Token![||]),
        /// The `^` operator (bitwise xor)
        BitXor(Token![^]),
        /// The `&` operator (bitwise and)
        BitAnd(Token![&]),
        /// The `|` operator (bitwise or)
        BitOr(Token![|]),
        /// The `<<` operator (shift left)
        Shl(Token![<<]),
        /// The `>>` operator (shift right)
        Shr(Token![>>]),
        /// The `==` operator (equality)
        Eq(Token![==]),
        /// The `<` operator (less than)
        Lt(Token![<]),
        /// The `<=` operator (less than or equal to)
        Le(Token![<=]),
        /// The `!=` operator (not equal to)
        Ne(Token![!=]),
        /// The `>=` operator (greater than or equal to)
        Ge(Token![>=]),
        /// The `>` operator (greater than)
        Gt(Token![>]),
        /// The `+=` operator
        AddAssign(Token![+=]),
        /// The `-=` operator
        SubAssign(Token![-=]),
        /// The `*=` operator
        MulAssign(Token![*=]),
        /// The `/=` operator
        DivAssign(Token![/=]),
        /// The `%=` operator
        RemAssign(Token![%=]),
        /// The `^=` operator
        BitXorAssign(Token![^=]),
        /// The `&=` operator
        BitAndAssign(Token![&=]),
        /// The `|=` operator
        BitOrAssign(Token![|=]),
        /// The `<<=` operator
        ShlAssign(Token![<<=]),
        /// The `>>=` operator
        ShrAssign(Token![>>=]),
    }
}

ast_enum! {
    /// A unary operator: `*`, `!`, `-`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    #[non_exhaustive]
    pub enum UnOp {
        /// The `*` operator for dereferencing
        Deref(Token![*]),
        /// The `!` operator for logical inversion
        Not(Token![!]),
        /// The `-` operator for negation
        Neg(Token![-]),
    }
}

#[cfg(feature = "parsing")]
pub(crate) mod parsing {
    use crate::error::Result;
    use crate::op::{BinOp, UnOp};
    use crate::parse::{Parse, ParseStream};

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for BinOp {
        fn parse(input: ParseStream) -> Result<Self> {
            if input.peek(Token![+=]) {
                input.parse().map(BinOp::AddAssign)
            } else if input.peek(Token![-=]) {
                input.parse().map(BinOp::SubAssign)
            } else if input.peek(Token![*=]) {
                input.parse().map(BinOp::MulAssign)
            } else if input.peek(Token![/=]) {
                input.parse().map(BinOp::DivAssign)
            } else if input.peek(Token![%=]) {
                input.parse().map(BinOp::RemAssign)
            } else if input.peek(Token![^=]) {
                input.parse().map(BinOp::BitXorAssign)
            } else if input.peek(Token![&=]) {
                input.parse().map(BinOp::BitAndAssign)
            } else if input.peek(Token![|=]) {
                input.parse().map(BinOp::BitOrAssign)
            } else if input.peek(Token![<<=]) {
                input.parse().map(BinOp::ShlAssign)
            } else if input.peek(Token![>>=]) {
                input.parse().map(BinOp::ShrAssign)
            } else if input.peek(Token![&&]) {
                input.parse().map(BinOp::And)
            } else if input.peek(Token![||]) {
                input.parse().map(BinOp::Or)
            } else if input.peek(Token![<<]) {
                input.parse().map(BinOp::Shl)
            } else if input.peek(Token![>>]) {
                input.parse().map(BinOp::Shr)
            } else if input.peek(Token![==]) {
                input.parse().map(BinOp::Eq)
            } else if input.peek(Token![<=]) {
                input.parse().map(BinOp::Le)
            } else if input.peek(Token![!=]) {
                input.parse().map(BinOp::Ne)
            } else if input.peek(Token![>=]) {
                input.parse().map(BinOp::Ge)
            } else if input.peek(Token![+]) {
                input.parse().map(BinOp::Add)
            } else if input.peek(Token![-]) {
                input.parse().map(BinOp::Sub)
            } else if input.peek(Token![*]) {
                input.parse().map(BinOp::Mul)
            } else if input.peek(Token![/]) {
                input.parse().map(BinOp::Div)
            } else if input.peek(Token![%]) {
                input.parse().map(BinOp::Rem)
            } else if input.peek(Token![^]) {
                input.parse().map(BinOp::BitXor)
            } else if input.peek(Token![&]) {
                input.parse().map(BinOp::BitAnd)
            } else if input.peek(Token![|]) {
                input.parse().map(BinOp::BitOr)
            } else if input.peek(Token![<]) {
                input.parse().map(BinOp::Lt)
            } else if input.peek(Token![>]) {
                input.parse().map(BinOp::Gt)
            } else {
                Err(input.error("expected binary operator"))
            }
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for UnOp {
        fn parse(input: ParseStream) -> Result<Self> {
            let lookahead = input.lookahead1();
            if lookahead.peek(Token![*]) {
                input.parse().map(UnOp::Deref)
            } else if lookahead.peek(Token![!]) {
                input.parse().map(UnOp::Not)
            } else if lookahead.peek(Token![-]) {
                input.parse().map(UnOp::Neg)
            } else {
                Err(lookahead.error())
            }
        }
    }
}

#[cfg(feature = "printing")]
mod printing {
    use crate::op::{BinOp, UnOp};
    use proc_macro2::TokenStream;
    use quote::ToTokens;

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for BinOp {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            match self {
                BinOp::Add(t) => t.to_tokens(tokens),
                BinOp::Sub(t) => t.to_tokens(tokens),
                BinOp::Mul(t) => t.to_tokens(tokens),
                BinOp::Div(t) => t.to_tokens(tokens),
                BinOp::Rem(t) => t.to_tokens(tokens),
                BinOp::And(t) => t.to_tokens(tokens),
                BinOp::Or(t) => t.to_tokens(tokens),
                BinOp::BitXor(t) => t.to_tokens(tokens),
                BinOp::BitAnd(t) => t.to_tokens(tokens),
                BinOp::BitOr(t) => t.to_tokens(tokens),
                BinOp::Shl(t) => t.to_tokens(tokens),
                BinOp::Shr(t) => t.to_tokens(tokens),
                BinOp::Eq(t) => t.to_tokens(tokens),
                BinOp::Lt(t) => t.to_tokens(tokens),
                BinOp::Le(t) => t.to_tokens(tokens),
                BinOp::Ne(t) => t.to_tokens(tokens),
                BinOp::Ge(t) => t.to_tokens(tokens),
                BinOp::Gt(t) => t.to_tokens(tokens),
                BinOp::AddAssign(t) => t.to_tokens(tokens),
                BinOp::SubAssign(t) => t.to_tokens(tokens),
                BinOp::MulAssign(t) => t.to_tokens(tokens),
                BinOp::DivAssign(t) => t.to_tokens(tokens),
                BinOp::RemAssign(t) => t.to_tokens(tokens),
                BinOp::BitXorAssign(t) => t.to_tokens(tokens),
                BinOp::BitAndAssign(t) => t.to_tokens(tokens),
                BinOp::BitOrAssign(t) => t.to_tokens(tokens),
                BinOp::ShlAssign(t) => t.to_tokens(tokens),
                BinOp::ShrAssign(t) => t.to_tokens(tokens),
            }
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for UnOp {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            match self {
                UnOp::Deref(t) => t.to_tokens(tokens),
                UnOp::Not(t) => t.to_tokens(tokens),
                UnOp::Neg(t) => t.to_tokens(tokens),
            }
        }
    }
}
