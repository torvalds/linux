use self::{Action::*, Input::*};
use proc_macro2::{Delimiter, Ident, Spacing, TokenTree};
use syn::parse::{ParseStream, Result};
use syn::{AngleBracketedGenericArguments, BinOp, Expr, ExprPath, Lifetime, Lit, Token, Type};

enum Input {
    Keyword(&'static str),
    Punct(&'static str),
    ConsumeAny,
    ConsumeBinOp,
    ConsumeBrace,
    ConsumeDelimiter,
    ConsumeIdent,
    ConsumeLifetime,
    ConsumeLiteral,
    ConsumeNestedBrace,
    ExpectPath,
    ExpectTurbofish,
    ExpectType,
    CanBeginExpr,
    Otherwise,
    Empty,
}

enum Action {
    SetState(&'static [(Input, Action)]),
    IncDepth,
    DecDepth,
    Finish,
}

static INIT: [(Input, Action); 28] = [
    (ConsumeDelimiter, SetState(&POSTFIX)),
    (Keyword("async"), SetState(&ASYNC)),
    (Keyword("break"), SetState(&BREAK_LABEL)),
    (Keyword("const"), SetState(&CONST)),
    (Keyword("continue"), SetState(&CONTINUE)),
    (Keyword("for"), SetState(&FOR)),
    (Keyword("if"), IncDepth),
    (Keyword("let"), SetState(&PATTERN)),
    (Keyword("loop"), SetState(&BLOCK)),
    (Keyword("match"), IncDepth),
    (Keyword("move"), SetState(&CLOSURE)),
    (Keyword("return"), SetState(&RETURN)),
    (Keyword("static"), SetState(&CLOSURE)),
    (Keyword("unsafe"), SetState(&BLOCK)),
    (Keyword("while"), IncDepth),
    (Keyword("yield"), SetState(&RETURN)),
    (Keyword("_"), SetState(&POSTFIX)),
    (Punct("!"), SetState(&INIT)),
    (Punct("#"), SetState(&[(ConsumeDelimiter, SetState(&INIT))])),
    (Punct("&"), SetState(&REFERENCE)),
    (Punct("*"), SetState(&INIT)),
    (Punct("-"), SetState(&INIT)),
    (Punct("..="), SetState(&INIT)),
    (Punct(".."), SetState(&RANGE)),
    (Punct("|"), SetState(&CLOSURE_ARGS)),
    (ConsumeLifetime, SetState(&[(Punct(":"), SetState(&INIT))])),
    (ConsumeLiteral, SetState(&POSTFIX)),
    (ExpectPath, SetState(&PATH)),
];

static POSTFIX: [(Input, Action); 10] = [
    (Keyword("as"), SetState(&[(ExpectType, SetState(&POSTFIX))])),
    (Punct("..="), SetState(&INIT)),
    (Punct(".."), SetState(&RANGE)),
    (Punct("."), SetState(&DOT)),
    (Punct("?"), SetState(&POSTFIX)),
    (ConsumeBinOp, SetState(&INIT)),
    (Punct("="), SetState(&INIT)),
    (ConsumeNestedBrace, SetState(&IF_THEN)),
    (ConsumeDelimiter, SetState(&POSTFIX)),
    (Empty, Finish),
];

static ASYNC: [(Input, Action); 3] = [
    (Keyword("move"), SetState(&ASYNC)),
    (Punct("|"), SetState(&CLOSURE_ARGS)),
    (ConsumeBrace, SetState(&POSTFIX)),
];

static BLOCK: [(Input, Action); 1] = [(ConsumeBrace, SetState(&POSTFIX))];

static BREAK_LABEL: [(Input, Action); 2] = [
    (ConsumeLifetime, SetState(&BREAK_VALUE)),
    (Otherwise, SetState(&BREAK_VALUE)),
];

static BREAK_VALUE: [(Input, Action); 3] = [
    (ConsumeNestedBrace, SetState(&IF_THEN)),
    (CanBeginExpr, SetState(&INIT)),
    (Otherwise, SetState(&POSTFIX)),
];

static CLOSURE: [(Input, Action); 7] = [
    (Keyword("async"), SetState(&CLOSURE)),
    (Keyword("move"), SetState(&CLOSURE)),
    (Punct(","), SetState(&CLOSURE)),
    (Punct(">"), SetState(&CLOSURE)),
    (Punct("|"), SetState(&CLOSURE_ARGS)),
    (ConsumeLifetime, SetState(&CLOSURE)),
    (ConsumeIdent, SetState(&CLOSURE)),
];

static CLOSURE_ARGS: [(Input, Action); 2] = [
    (Punct("|"), SetState(&CLOSURE_RET)),
    (ConsumeAny, SetState(&CLOSURE_ARGS)),
];

static CLOSURE_RET: [(Input, Action); 2] = [
    (Punct("->"), SetState(&[(ExpectType, SetState(&BLOCK))])),
    (Otherwise, SetState(&INIT)),
];

static CONST: [(Input, Action); 2] = [
    (Punct("|"), SetState(&CLOSURE_ARGS)),
    (ConsumeBrace, SetState(&POSTFIX)),
];

static CONTINUE: [(Input, Action); 2] = [
    (ConsumeLifetime, SetState(&POSTFIX)),
    (Otherwise, SetState(&POSTFIX)),
];

static DOT: [(Input, Action); 3] = [
    (Keyword("await"), SetState(&POSTFIX)),
    (ConsumeIdent, SetState(&METHOD)),
    (ConsumeLiteral, SetState(&POSTFIX)),
];

static FOR: [(Input, Action); 2] = [
    (Punct("<"), SetState(&CLOSURE)),
    (Otherwise, SetState(&PATTERN)),
];

static IF_ELSE: [(Input, Action); 2] = [(Keyword("if"), SetState(&INIT)), (ConsumeBrace, DecDepth)];
static IF_THEN: [(Input, Action); 2] =
    [(Keyword("else"), SetState(&IF_ELSE)), (Otherwise, DecDepth)];

static METHOD: [(Input, Action); 1] = [(ExpectTurbofish, SetState(&POSTFIX))];

static PATH: [(Input, Action); 4] = [
    (Punct("!="), SetState(&INIT)),
    (Punct("!"), SetState(&INIT)),
    (ConsumeNestedBrace, SetState(&IF_THEN)),
    (Otherwise, SetState(&POSTFIX)),
];

static PATTERN: [(Input, Action); 15] = [
    (ConsumeDelimiter, SetState(&PATTERN)),
    (Keyword("box"), SetState(&PATTERN)),
    (Keyword("in"), IncDepth),
    (Keyword("mut"), SetState(&PATTERN)),
    (Keyword("ref"), SetState(&PATTERN)),
    (Keyword("_"), SetState(&PATTERN)),
    (Punct("!"), SetState(&PATTERN)),
    (Punct("&"), SetState(&PATTERN)),
    (Punct("..="), SetState(&PATTERN)),
    (Punct(".."), SetState(&PATTERN)),
    (Punct("="), SetState(&INIT)),
    (Punct("@"), SetState(&PATTERN)),
    (Punct("|"), SetState(&PATTERN)),
    (ConsumeLiteral, SetState(&PATTERN)),
    (ExpectPath, SetState(&PATTERN)),
];

static RANGE: [(Input, Action); 6] = [
    (Punct("..="), SetState(&INIT)),
    (Punct(".."), SetState(&RANGE)),
    (Punct("."), SetState(&DOT)),
    (ConsumeNestedBrace, SetState(&IF_THEN)),
    (Empty, Finish),
    (Otherwise, SetState(&INIT)),
];

static RAW: [(Input, Action); 3] = [
    (Keyword("const"), SetState(&INIT)),
    (Keyword("mut"), SetState(&INIT)),
    (Otherwise, SetState(&POSTFIX)),
];

static REFERENCE: [(Input, Action); 3] = [
    (Keyword("mut"), SetState(&INIT)),
    (Keyword("raw"), SetState(&RAW)),
    (Otherwise, SetState(&INIT)),
];

static RETURN: [(Input, Action); 2] = [
    (CanBeginExpr, SetState(&INIT)),
    (Otherwise, SetState(&POSTFIX)),
];

pub(crate) fn scan_expr(input: ParseStream) -> Result<()> {
    let mut state = INIT.as_slice();
    let mut depth = 0usize;
    'table: loop {
        for rule in state {
            if match rule.0 {
                Input::Keyword(expected) => input.step(|cursor| match cursor.ident() {
                    Some((ident, rest)) if ident == expected => Ok((true, rest)),
                    _ => Ok((false, *cursor)),
                })?,
                Input::Punct(expected) => input.step(|cursor| {
                    let begin = *cursor;
                    let mut cursor = begin;
                    for (i, ch) in expected.chars().enumerate() {
                        match cursor.punct() {
                            Some((punct, _)) if punct.as_char() != ch => break,
                            Some((_, rest)) if i == expected.len() - 1 => {
                                return Ok((true, rest));
                            }
                            Some((punct, rest)) if punct.spacing() == Spacing::Joint => {
                                cursor = rest;
                            }
                            _ => break,
                        }
                    }
                    Ok((false, begin))
                })?,
                Input::ConsumeAny => input.parse::<Option<TokenTree>>()?.is_some(),
                Input::ConsumeBinOp => input.parse::<BinOp>().is_ok(),
                Input::ConsumeBrace | Input::ConsumeNestedBrace => {
                    (matches!(rule.0, Input::ConsumeBrace) || depth > 0)
                        && input.step(|cursor| match cursor.group(Delimiter::Brace) {
                            Some((_inside, _span, rest)) => Ok((true, rest)),
                            None => Ok((false, *cursor)),
                        })?
                }
                Input::ConsumeDelimiter => input.step(|cursor| match cursor.any_group() {
                    Some((_inside, _delimiter, _span, rest)) => Ok((true, rest)),
                    None => Ok((false, *cursor)),
                })?,
                Input::ConsumeIdent => input.parse::<Option<Ident>>()?.is_some(),
                Input::ConsumeLifetime => input.parse::<Option<Lifetime>>()?.is_some(),
                Input::ConsumeLiteral => input.parse::<Option<Lit>>()?.is_some(),
                Input::ExpectPath => {
                    input.parse::<ExprPath>()?;
                    true
                }
                Input::ExpectTurbofish => {
                    if input.peek(Token![::]) {
                        input.parse::<AngleBracketedGenericArguments>()?;
                    }
                    true
                }
                Input::ExpectType => {
                    Type::without_plus(input)?;
                    true
                }
                Input::CanBeginExpr => Expr::peek(input),
                Input::Otherwise => true,
                Input::Empty => input.is_empty() || input.peek(Token![,]),
            } {
                state = match rule.1 {
                    Action::SetState(next) => next,
                    Action::IncDepth => (depth += 1, &INIT).1,
                    Action::DecDepth => (depth -= 1, &POSTFIX).1,
                    Action::Finish => return if depth == 0 { Ok(()) } else { break },
                };
                continue 'table;
            }
        }
        return Err(input.error("unsupported expression"));
    }
}
