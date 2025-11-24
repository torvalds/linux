#[cfg(all(feature = "printing", feature = "full"))]
use crate::attr::{AttrStyle, Attribute};
#[cfg(feature = "printing")]
use crate::expr::Expr;
#[cfg(all(feature = "printing", feature = "full"))]
use crate::expr::{
    ExprArray, ExprAsync, ExprAwait, ExprBlock, ExprBreak, ExprCall, ExprConst, ExprContinue,
    ExprField, ExprForLoop, ExprGroup, ExprIf, ExprIndex, ExprInfer, ExprLit, ExprLoop, ExprMacro,
    ExprMatch, ExprMethodCall, ExprParen, ExprPath, ExprRepeat, ExprReturn, ExprStruct, ExprTry,
    ExprTryBlock, ExprTuple, ExprUnsafe, ExprWhile, ExprYield,
};
use crate::op::BinOp;
#[cfg(all(feature = "printing", feature = "full"))]
use crate::ty::ReturnType;
use std::cmp::Ordering;

// Reference: https://doc.rust-lang.org/reference/expressions.html#expression-precedence
pub(crate) enum Precedence {
    // return, break, closures
    Jump,
    // = += -= *= /= %= &= |= ^= <<= >>=
    Assign,
    // .. ..=
    Range,
    // ||
    Or,
    // &&
    And,
    // let
    #[cfg(feature = "printing")]
    Let,
    // == != < > <= >=
    Compare,
    // |
    BitOr,
    // ^
    BitXor,
    // &
    BitAnd,
    // << >>
    Shift,
    // + -
    Sum,
    // * / %
    Product,
    // as
    Cast,
    // unary - * ! & &mut
    #[cfg(feature = "printing")]
    Prefix,
    // paths, loops, function calls, array indexing, field expressions, method calls
    #[cfg(feature = "printing")]
    Unambiguous,
}

impl Precedence {
    pub(crate) const MIN: Self = Precedence::Jump;

    pub(crate) fn of_binop(op: &BinOp) -> Self {
        match op {
            BinOp::Add(_) | BinOp::Sub(_) => Precedence::Sum,
            BinOp::Mul(_) | BinOp::Div(_) | BinOp::Rem(_) => Precedence::Product,
            BinOp::And(_) => Precedence::And,
            BinOp::Or(_) => Precedence::Or,
            BinOp::BitXor(_) => Precedence::BitXor,
            BinOp::BitAnd(_) => Precedence::BitAnd,
            BinOp::BitOr(_) => Precedence::BitOr,
            BinOp::Shl(_) | BinOp::Shr(_) => Precedence::Shift,

            BinOp::Eq(_)
            | BinOp::Lt(_)
            | BinOp::Le(_)
            | BinOp::Ne(_)
            | BinOp::Ge(_)
            | BinOp::Gt(_) => Precedence::Compare,

            BinOp::AddAssign(_)
            | BinOp::SubAssign(_)
            | BinOp::MulAssign(_)
            | BinOp::DivAssign(_)
            | BinOp::RemAssign(_)
            | BinOp::BitXorAssign(_)
            | BinOp::BitAndAssign(_)
            | BinOp::BitOrAssign(_)
            | BinOp::ShlAssign(_)
            | BinOp::ShrAssign(_) => Precedence::Assign,
        }
    }

    #[cfg(feature = "printing")]
    pub(crate) fn of(e: &Expr) -> Self {
        #[cfg(feature = "full")]
        fn prefix_attrs(attrs: &[Attribute]) -> Precedence {
            for attr in attrs {
                if let AttrStyle::Outer = attr.style {
                    return Precedence::Prefix;
                }
            }
            Precedence::Unambiguous
        }

        match e {
            #[cfg(feature = "full")]
            Expr::Closure(e) => match e.output {
                ReturnType::Default => Precedence::Jump,
                ReturnType::Type(..) => prefix_attrs(&e.attrs),
            },

            #[cfg(feature = "full")]
            Expr::Break(ExprBreak { expr, .. })
            | Expr::Return(ExprReturn { expr, .. })
            | Expr::Yield(ExprYield { expr, .. }) => match expr {
                Some(_) => Precedence::Jump,
                None => Precedence::Unambiguous,
            },

            Expr::Assign(_) => Precedence::Assign,
            Expr::Range(_) => Precedence::Range,
            Expr::Binary(e) => Precedence::of_binop(&e.op),
            Expr::Let(_) => Precedence::Let,
            Expr::Cast(_) => Precedence::Cast,
            Expr::RawAddr(_) | Expr::Reference(_) | Expr::Unary(_) => Precedence::Prefix,

            #[cfg(feature = "full")]
            Expr::Array(ExprArray { attrs, .. })
            | Expr::Async(ExprAsync { attrs, .. })
            | Expr::Await(ExprAwait { attrs, .. })
            | Expr::Block(ExprBlock { attrs, .. })
            | Expr::Call(ExprCall { attrs, .. })
            | Expr::Const(ExprConst { attrs, .. })
            | Expr::Continue(ExprContinue { attrs, .. })
            | Expr::Field(ExprField { attrs, .. })
            | Expr::ForLoop(ExprForLoop { attrs, .. })
            | Expr::Group(ExprGroup { attrs, .. })
            | Expr::If(ExprIf { attrs, .. })
            | Expr::Index(ExprIndex { attrs, .. })
            | Expr::Infer(ExprInfer { attrs, .. })
            | Expr::Lit(ExprLit { attrs, .. })
            | Expr::Loop(ExprLoop { attrs, .. })
            | Expr::Macro(ExprMacro { attrs, .. })
            | Expr::Match(ExprMatch { attrs, .. })
            | Expr::MethodCall(ExprMethodCall { attrs, .. })
            | Expr::Paren(ExprParen { attrs, .. })
            | Expr::Path(ExprPath { attrs, .. })
            | Expr::Repeat(ExprRepeat { attrs, .. })
            | Expr::Struct(ExprStruct { attrs, .. })
            | Expr::Try(ExprTry { attrs, .. })
            | Expr::TryBlock(ExprTryBlock { attrs, .. })
            | Expr::Tuple(ExprTuple { attrs, .. })
            | Expr::Unsafe(ExprUnsafe { attrs, .. })
            | Expr::While(ExprWhile { attrs, .. }) => prefix_attrs(attrs),

            #[cfg(not(feature = "full"))]
            Expr::Array(_)
            | Expr::Async(_)
            | Expr::Await(_)
            | Expr::Block(_)
            | Expr::Call(_)
            | Expr::Const(_)
            | Expr::Continue(_)
            | Expr::Field(_)
            | Expr::ForLoop(_)
            | Expr::Group(_)
            | Expr::If(_)
            | Expr::Index(_)
            | Expr::Infer(_)
            | Expr::Lit(_)
            | Expr::Loop(_)
            | Expr::Macro(_)
            | Expr::Match(_)
            | Expr::MethodCall(_)
            | Expr::Paren(_)
            | Expr::Path(_)
            | Expr::Repeat(_)
            | Expr::Struct(_)
            | Expr::Try(_)
            | Expr::TryBlock(_)
            | Expr::Tuple(_)
            | Expr::Unsafe(_)
            | Expr::While(_) => Precedence::Unambiguous,

            Expr::Verbatim(_) => Precedence::Unambiguous,

            #[cfg(not(feature = "full"))]
            Expr::Break(_) | Expr::Closure(_) | Expr::Return(_) | Expr::Yield(_) => unreachable!(),
        }
    }
}

impl Copy for Precedence {}

impl Clone for Precedence {
    fn clone(&self) -> Self {
        *self
    }
}

impl PartialEq for Precedence {
    fn eq(&self, other: &Self) -> bool {
        *self as u8 == *other as u8
    }
}

impl PartialOrd for Precedence {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        let this = *self as u8;
        let other = *other as u8;
        Some(this.cmp(&other))
    }
}
