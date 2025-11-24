use crate::fallback::{
    self, is_ident_continue, is_ident_start, Group, Ident, LexError, Literal, Span, TokenStream,
    TokenStreamBuilder,
};
use crate::{Delimiter, Punct, Spacing, TokenTree};
use core::char;
use core::str::{Bytes, CharIndices, Chars};

#[derive(Copy, Clone, Eq, PartialEq)]
pub(crate) struct Cursor<'a> {
    pub(crate) rest: &'a str,
    #[cfg(span_locations)]
    pub(crate) off: u32,
}

impl<'a> Cursor<'a> {
    pub(crate) fn advance(&self, bytes: usize) -> Cursor<'a> {
        let (_front, rest) = self.rest.split_at(bytes);
        Cursor {
            rest,
            #[cfg(span_locations)]
            off: self.off + _front.chars().count() as u32,
        }
    }

    pub(crate) fn starts_with(&self, s: &str) -> bool {
        self.rest.starts_with(s)
    }

    pub(crate) fn starts_with_char(&self, ch: char) -> bool {
        self.rest.starts_with(ch)
    }

    pub(crate) fn starts_with_fn<Pattern>(&self, f: Pattern) -> bool
    where
        Pattern: FnMut(char) -> bool,
    {
        self.rest.starts_with(f)
    }

    pub(crate) fn is_empty(&self) -> bool {
        self.rest.is_empty()
    }

    fn len(&self) -> usize {
        self.rest.len()
    }

    fn as_bytes(&self) -> &'a [u8] {
        self.rest.as_bytes()
    }

    fn bytes(&self) -> Bytes<'a> {
        self.rest.bytes()
    }

    fn chars(&self) -> Chars<'a> {
        self.rest.chars()
    }

    fn char_indices(&self) -> CharIndices<'a> {
        self.rest.char_indices()
    }

    fn parse(&self, tag: &str) -> Result<Cursor<'a>, Reject> {
        if self.starts_with(tag) {
            Ok(self.advance(tag.len()))
        } else {
            Err(Reject)
        }
    }
}

pub(crate) struct Reject;
type PResult<'a, O> = Result<(Cursor<'a>, O), Reject>;

fn skip_whitespace(input: Cursor) -> Cursor {
    let mut s = input;

    while !s.is_empty() {
        let byte = s.as_bytes()[0];
        if byte == b'/' {
            if s.starts_with("//")
                && (!s.starts_with("///") || s.starts_with("////"))
                && !s.starts_with("//!")
            {
                let (cursor, _) = take_until_newline_or_eof(s);
                s = cursor;
                continue;
            } else if s.starts_with("/**/") {
                s = s.advance(4);
                continue;
            } else if s.starts_with("/*")
                && (!s.starts_with("/**") || s.starts_with("/***"))
                && !s.starts_with("/*!")
            {
                match block_comment(s) {
                    Ok((rest, _)) => {
                        s = rest;
                        continue;
                    }
                    Err(Reject) => return s,
                }
            }
        }
        match byte {
            b' ' | 0x09..=0x0d => {
                s = s.advance(1);
                continue;
            }
            b if b.is_ascii() => {}
            _ => {
                let ch = s.chars().next().unwrap();
                if is_whitespace(ch) {
                    s = s.advance(ch.len_utf8());
                    continue;
                }
            }
        }
        return s;
    }
    s
}

fn block_comment(input: Cursor) -> PResult<&str> {
    if !input.starts_with("/*") {
        return Err(Reject);
    }

    let mut depth = 0usize;
    let bytes = input.as_bytes();
    let mut i = 0usize;
    let upper = bytes.len() - 1;

    while i < upper {
        if bytes[i] == b'/' && bytes[i + 1] == b'*' {
            depth += 1;
            i += 1; // eat '*'
        } else if bytes[i] == b'*' && bytes[i + 1] == b'/' {
            depth -= 1;
            if depth == 0 {
                return Ok((input.advance(i + 2), &input.rest[..i + 2]));
            }
            i += 1; // eat '/'
        }
        i += 1;
    }

    Err(Reject)
}

fn is_whitespace(ch: char) -> bool {
    // Rust treats left-to-right mark and right-to-left mark as whitespace
    ch.is_whitespace() || ch == '\u{200e}' || ch == '\u{200f}'
}

fn word_break(input: Cursor) -> Result<Cursor, Reject> {
    match input.chars().next() {
        Some(ch) if is_ident_continue(ch) => Err(Reject),
        Some(_) | None => Ok(input),
    }
}

// Rustc's representation of a macro expansion error in expression position or
// type position.
const ERROR: &str = "(/*ERROR*/)";

pub(crate) fn token_stream(mut input: Cursor) -> Result<TokenStream, LexError> {
    let mut trees = TokenStreamBuilder::new();
    let mut stack = Vec::new();

    loop {
        input = skip_whitespace(input);

        if let Ok((rest, ())) = doc_comment(input, &mut trees) {
            input = rest;
            continue;
        }

        #[cfg(span_locations)]
        let lo = input.off;

        let first = match input.bytes().next() {
            Some(first) => first,
            None => match stack.last() {
                None => return Ok(trees.build()),
                #[cfg(span_locations)]
                Some((lo, _frame)) => {
                    return Err(LexError {
                        span: Span { lo: *lo, hi: *lo },
                    })
                }
                #[cfg(not(span_locations))]
                Some(_frame) => return Err(LexError { span: Span {} }),
            },
        };

        if let Some(open_delimiter) = match first {
            b'(' if !input.starts_with(ERROR) => Some(Delimiter::Parenthesis),
            b'[' => Some(Delimiter::Bracket),
            b'{' => Some(Delimiter::Brace),
            _ => None,
        } {
            input = input.advance(1);
            let frame = (open_delimiter, trees);
            #[cfg(span_locations)]
            let frame = (lo, frame);
            stack.push(frame);
            trees = TokenStreamBuilder::new();
        } else if let Some(close_delimiter) = match first {
            b')' => Some(Delimiter::Parenthesis),
            b']' => Some(Delimiter::Bracket),
            b'}' => Some(Delimiter::Brace),
            _ => None,
        } {
            let frame = match stack.pop() {
                Some(frame) => frame,
                None => return Err(lex_error(input)),
            };
            #[cfg(span_locations)]
            let (lo, frame) = frame;
            let (open_delimiter, outer) = frame;
            if open_delimiter != close_delimiter {
                return Err(lex_error(input));
            }
            input = input.advance(1);
            let mut g = Group::new(open_delimiter, trees.build());
            g.set_span(Span {
                #[cfg(span_locations)]
                lo,
                #[cfg(span_locations)]
                hi: input.off,
            });
            trees = outer;
            trees.push_token_from_parser(TokenTree::Group(crate::Group::_new_fallback(g)));
        } else {
            let (rest, mut tt) = match leaf_token(input) {
                Ok((rest, tt)) => (rest, tt),
                Err(Reject) => return Err(lex_error(input)),
            };
            tt.set_span(crate::Span::_new_fallback(Span {
                #[cfg(span_locations)]
                lo,
                #[cfg(span_locations)]
                hi: rest.off,
            }));
            trees.push_token_from_parser(tt);
            input = rest;
        }
    }
}

fn lex_error(cursor: Cursor) -> LexError {
    #[cfg(not(span_locations))]
    let _ = cursor;
    LexError {
        span: Span {
            #[cfg(span_locations)]
            lo: cursor.off,
            #[cfg(span_locations)]
            hi: cursor.off,
        },
    }
}

fn leaf_token(input: Cursor) -> PResult<TokenTree> {
    if let Ok((input, l)) = literal(input) {
        // must be parsed before ident
        Ok((input, TokenTree::Literal(crate::Literal::_new_fallback(l))))
    } else if let Ok((input, p)) = punct(input) {
        Ok((input, TokenTree::Punct(p)))
    } else if let Ok((input, i)) = ident(input) {
        Ok((input, TokenTree::Ident(i)))
    } else if input.starts_with(ERROR) {
        let rest = input.advance(ERROR.len());
        let repr = crate::Literal::_new_fallback(Literal::_new(ERROR.to_owned()));
        Ok((rest, TokenTree::Literal(repr)))
    } else {
        Err(Reject)
    }
}

fn ident(input: Cursor) -> PResult<crate::Ident> {
    if [
        "r\"", "r#\"", "r##", "b\"", "b\'", "br\"", "br#", "c\"", "cr\"", "cr#",
    ]
    .iter()
    .any(|prefix| input.starts_with(prefix))
    {
        Err(Reject)
    } else {
        ident_any(input)
    }
}

fn ident_any(input: Cursor) -> PResult<crate::Ident> {
    let raw = input.starts_with("r#");
    let rest = input.advance((raw as usize) << 1);

    let (rest, sym) = ident_not_raw(rest)?;

    if !raw {
        let ident =
            crate::Ident::_new_fallback(Ident::new_unchecked(sym, fallback::Span::call_site()));
        return Ok((rest, ident));
    }

    match sym {
        "_" | "super" | "self" | "Self" | "crate" => return Err(Reject),
        _ => {}
    }

    let ident =
        crate::Ident::_new_fallback(Ident::new_raw_unchecked(sym, fallback::Span::call_site()));
    Ok((rest, ident))
}

fn ident_not_raw(input: Cursor) -> PResult<&str> {
    let mut chars = input.char_indices();

    match chars.next() {
        Some((_, ch)) if is_ident_start(ch) => {}
        _ => return Err(Reject),
    }

    let mut end = input.len();
    for (i, ch) in chars {
        if !is_ident_continue(ch) {
            end = i;
            break;
        }
    }

    Ok((input.advance(end), &input.rest[..end]))
}

pub(crate) fn literal(input: Cursor) -> PResult<Literal> {
    let rest = literal_nocapture(input)?;
    let end = input.len() - rest.len();
    Ok((rest, Literal::_new(input.rest[..end].to_string())))
}

fn literal_nocapture(input: Cursor) -> Result<Cursor, Reject> {
    if let Ok(ok) = string(input) {
        Ok(ok)
    } else if let Ok(ok) = byte_string(input) {
        Ok(ok)
    } else if let Ok(ok) = c_string(input) {
        Ok(ok)
    } else if let Ok(ok) = byte(input) {
        Ok(ok)
    } else if let Ok(ok) = character(input) {
        Ok(ok)
    } else if let Ok(ok) = float(input) {
        Ok(ok)
    } else if let Ok(ok) = int(input) {
        Ok(ok)
    } else {
        Err(Reject)
    }
}

fn literal_suffix(input: Cursor) -> Cursor {
    match ident_not_raw(input) {
        Ok((input, _)) => input,
        Err(Reject) => input,
    }
}

fn string(input: Cursor) -> Result<Cursor, Reject> {
    if let Ok(input) = input.parse("\"") {
        cooked_string(input)
    } else if let Ok(input) = input.parse("r") {
        raw_string(input)
    } else {
        Err(Reject)
    }
}

fn cooked_string(mut input: Cursor) -> Result<Cursor, Reject> {
    let mut chars = input.char_indices();

    while let Some((i, ch)) = chars.next() {
        match ch {
            '"' => {
                let input = input.advance(i + 1);
                return Ok(literal_suffix(input));
            }
            '\r' => match chars.next() {
                Some((_, '\n')) => {}
                _ => break,
            },
            '\\' => match chars.next() {
                Some((_, 'x')) => {
                    backslash_x_char(&mut chars)?;
                }
                Some((_, 'n' | 'r' | 't' | '\\' | '\'' | '"' | '0')) => {}
                Some((_, 'u')) => {
                    backslash_u(&mut chars)?;
                }
                Some((newline, ch @ ('\n' | '\r'))) => {
                    input = input.advance(newline + 1);
                    trailing_backslash(&mut input, ch as u8)?;
                    chars = input.char_indices();
                }
                _ => break,
            },
            _ch => {}
        }
    }
    Err(Reject)
}

fn raw_string(input: Cursor) -> Result<Cursor, Reject> {
    let (input, delimiter) = delimiter_of_raw_string(input)?;
    let mut bytes = input.bytes().enumerate();
    while let Some((i, byte)) = bytes.next() {
        match byte {
            b'"' if input.rest[i + 1..].starts_with(delimiter) => {
                let rest = input.advance(i + 1 + delimiter.len());
                return Ok(literal_suffix(rest));
            }
            b'\r' => match bytes.next() {
                Some((_, b'\n')) => {}
                _ => break,
            },
            _ => {}
        }
    }
    Err(Reject)
}

fn byte_string(input: Cursor) -> Result<Cursor, Reject> {
    if let Ok(input) = input.parse("b\"") {
        cooked_byte_string(input)
    } else if let Ok(input) = input.parse("br") {
        raw_byte_string(input)
    } else {
        Err(Reject)
    }
}

fn cooked_byte_string(mut input: Cursor) -> Result<Cursor, Reject> {
    let mut bytes = input.bytes().enumerate();
    while let Some((offset, b)) = bytes.next() {
        match b {
            b'"' => {
                let input = input.advance(offset + 1);
                return Ok(literal_suffix(input));
            }
            b'\r' => match bytes.next() {
                Some((_, b'\n')) => {}
                _ => break,
            },
            b'\\' => match bytes.next() {
                Some((_, b'x')) => {
                    backslash_x_byte(&mut bytes)?;
                }
                Some((_, b'n' | b'r' | b't' | b'\\' | b'0' | b'\'' | b'"')) => {}
                Some((newline, b @ (b'\n' | b'\r'))) => {
                    input = input.advance(newline + 1);
                    trailing_backslash(&mut input, b)?;
                    bytes = input.bytes().enumerate();
                }
                _ => break,
            },
            b if b.is_ascii() => {}
            _ => break,
        }
    }
    Err(Reject)
}

fn delimiter_of_raw_string(input: Cursor) -> PResult<&str> {
    for (i, byte) in input.bytes().enumerate() {
        match byte {
            b'"' => {
                if i > 255 {
                    // https://github.com/rust-lang/rust/pull/95251
                    return Err(Reject);
                }
                return Ok((input.advance(i + 1), &input.rest[..i]));
            }
            b'#' => {}
            _ => break,
        }
    }
    Err(Reject)
}

fn raw_byte_string(input: Cursor) -> Result<Cursor, Reject> {
    let (input, delimiter) = delimiter_of_raw_string(input)?;
    let mut bytes = input.bytes().enumerate();
    while let Some((i, byte)) = bytes.next() {
        match byte {
            b'"' if input.rest[i + 1..].starts_with(delimiter) => {
                let rest = input.advance(i + 1 + delimiter.len());
                return Ok(literal_suffix(rest));
            }
            b'\r' => match bytes.next() {
                Some((_, b'\n')) => {}
                _ => break,
            },
            other => {
                if !other.is_ascii() {
                    break;
                }
            }
        }
    }
    Err(Reject)
}

fn c_string(input: Cursor) -> Result<Cursor, Reject> {
    if let Ok(input) = input.parse("c\"") {
        cooked_c_string(input)
    } else if let Ok(input) = input.parse("cr") {
        raw_c_string(input)
    } else {
        Err(Reject)
    }
}

fn raw_c_string(input: Cursor) -> Result<Cursor, Reject> {
    let (input, delimiter) = delimiter_of_raw_string(input)?;
    let mut bytes = input.bytes().enumerate();
    while let Some((i, byte)) = bytes.next() {
        match byte {
            b'"' if input.rest[i + 1..].starts_with(delimiter) => {
                let rest = input.advance(i + 1 + delimiter.len());
                return Ok(literal_suffix(rest));
            }
            b'\r' => match bytes.next() {
                Some((_, b'\n')) => {}
                _ => break,
            },
            b'\0' => break,
            _ => {}
        }
    }
    Err(Reject)
}

fn cooked_c_string(mut input: Cursor) -> Result<Cursor, Reject> {
    let mut chars = input.char_indices();

    while let Some((i, ch)) = chars.next() {
        match ch {
            '"' => {
                let input = input.advance(i + 1);
                return Ok(literal_suffix(input));
            }
            '\r' => match chars.next() {
                Some((_, '\n')) => {}
                _ => break,
            },
            '\\' => match chars.next() {
                Some((_, 'x')) => {
                    backslash_x_nonzero(&mut chars)?;
                }
                Some((_, 'n' | 'r' | 't' | '\\' | '\'' | '"')) => {}
                Some((_, 'u')) => {
                    if backslash_u(&mut chars)? == '\0' {
                        break;
                    }
                }
                Some((newline, ch @ ('\n' | '\r'))) => {
                    input = input.advance(newline + 1);
                    trailing_backslash(&mut input, ch as u8)?;
                    chars = input.char_indices();
                }
                _ => break,
            },
            '\0' => break,
            _ch => {}
        }
    }
    Err(Reject)
}

fn byte(input: Cursor) -> Result<Cursor, Reject> {
    let input = input.parse("b'")?;
    let mut bytes = input.bytes().enumerate();
    let ok = match bytes.next().map(|(_, b)| b) {
        Some(b'\\') => match bytes.next().map(|(_, b)| b) {
            Some(b'x') => backslash_x_byte(&mut bytes).is_ok(),
            Some(b'n' | b'r' | b't' | b'\\' | b'0' | b'\'' | b'"') => true,
            _ => false,
        },
        b => b.is_some(),
    };
    if !ok {
        return Err(Reject);
    }
    let (offset, _) = bytes.next().ok_or(Reject)?;
    if !input.chars().as_str().is_char_boundary(offset) {
        return Err(Reject);
    }
    let input = input.advance(offset).parse("'")?;
    Ok(literal_suffix(input))
}

fn character(input: Cursor) -> Result<Cursor, Reject> {
    let input = input.parse("'")?;
    let mut chars = input.char_indices();
    let ok = match chars.next().map(|(_, ch)| ch) {
        Some('\\') => match chars.next().map(|(_, ch)| ch) {
            Some('x') => backslash_x_char(&mut chars).is_ok(),
            Some('u') => backslash_u(&mut chars).is_ok(),
            Some('n' | 'r' | 't' | '\\' | '0' | '\'' | '"') => true,
            _ => false,
        },
        ch => ch.is_some(),
    };
    if !ok {
        return Err(Reject);
    }
    let (idx, _) = chars.next().ok_or(Reject)?;
    let input = input.advance(idx).parse("'")?;
    Ok(literal_suffix(input))
}

macro_rules! next_ch {
    ($chars:ident @ $pat:pat) => {
        match $chars.next() {
            Some((_, ch)) => match ch {
                $pat => ch,
                _ => return Err(Reject),
            },
            None => return Err(Reject),
        }
    };
}

fn backslash_x_char<I>(chars: &mut I) -> Result<(), Reject>
where
    I: Iterator<Item = (usize, char)>,
{
    next_ch!(chars @ '0'..='7');
    next_ch!(chars @ '0'..='9' | 'a'..='f' | 'A'..='F');
    Ok(())
}

fn backslash_x_byte<I>(chars: &mut I) -> Result<(), Reject>
where
    I: Iterator<Item = (usize, u8)>,
{
    next_ch!(chars @ b'0'..=b'9' | b'a'..=b'f' | b'A'..=b'F');
    next_ch!(chars @ b'0'..=b'9' | b'a'..=b'f' | b'A'..=b'F');
    Ok(())
}

fn backslash_x_nonzero<I>(chars: &mut I) -> Result<(), Reject>
where
    I: Iterator<Item = (usize, char)>,
{
    let first = next_ch!(chars @ '0'..='9' | 'a'..='f' | 'A'..='F');
    let second = next_ch!(chars @ '0'..='9' | 'a'..='f' | 'A'..='F');
    if first == '0' && second == '0' {
        Err(Reject)
    } else {
        Ok(())
    }
}

fn backslash_u<I>(chars: &mut I) -> Result<char, Reject>
where
    I: Iterator<Item = (usize, char)>,
{
    next_ch!(chars @ '{');
    let mut value = 0;
    let mut len = 0;
    for (_, ch) in chars {
        let digit = match ch {
            '0'..='9' => ch as u8 - b'0',
            'a'..='f' => 10 + ch as u8 - b'a',
            'A'..='F' => 10 + ch as u8 - b'A',
            '_' if len > 0 => continue,
            '}' if len > 0 => return char::from_u32(value).ok_or(Reject),
            _ => break,
        };
        if len == 6 {
            break;
        }
        value *= 0x10;
        value += u32::from(digit);
        len += 1;
    }
    Err(Reject)
}

fn trailing_backslash(input: &mut Cursor, mut last: u8) -> Result<(), Reject> {
    let mut whitespace = input.bytes().enumerate();
    loop {
        if last == b'\r' && whitespace.next().map_or(true, |(_, b)| b != b'\n') {
            return Err(Reject);
        }
        match whitespace.next() {
            Some((_, b @ (b' ' | b'\t' | b'\n' | b'\r'))) => {
                last = b;
            }
            Some((offset, _)) => {
                *input = input.advance(offset);
                return Ok(());
            }
            None => return Err(Reject),
        }
    }
}

fn float(input: Cursor) -> Result<Cursor, Reject> {
    let mut rest = float_digits(input)?;
    if let Some(ch) = rest.chars().next() {
        if is_ident_start(ch) {
            rest = ident_not_raw(rest)?.0;
        }
    }
    word_break(rest)
}

fn float_digits(input: Cursor) -> Result<Cursor, Reject> {
    let mut chars = input.chars().peekable();
    match chars.next() {
        Some(ch) if '0' <= ch && ch <= '9' => {}
        _ => return Err(Reject),
    }

    let mut len = 1;
    let mut has_dot = false;
    let mut has_exp = false;
    while let Some(&ch) = chars.peek() {
        match ch {
            '0'..='9' | '_' => {
                chars.next();
                len += 1;
            }
            '.' => {
                if has_dot {
                    break;
                }
                chars.next();
                if chars
                    .peek()
                    .map_or(false, |&ch| ch == '.' || is_ident_start(ch))
                {
                    return Err(Reject);
                }
                len += 1;
                has_dot = true;
            }
            'e' | 'E' => {
                chars.next();
                len += 1;
                has_exp = true;
                break;
            }
            _ => break,
        }
    }

    if !(has_dot || has_exp) {
        return Err(Reject);
    }

    if has_exp {
        let token_before_exp = if has_dot {
            Ok(input.advance(len - 1))
        } else {
            Err(Reject)
        };
        let mut has_sign = false;
        let mut has_exp_value = false;
        while let Some(&ch) = chars.peek() {
            match ch {
                '+' | '-' => {
                    if has_exp_value {
                        break;
                    }
                    if has_sign {
                        return token_before_exp;
                    }
                    chars.next();
                    len += 1;
                    has_sign = true;
                }
                '0'..='9' => {
                    chars.next();
                    len += 1;
                    has_exp_value = true;
                }
                '_' => {
                    chars.next();
                    len += 1;
                }
                _ => break,
            }
        }
        if !has_exp_value {
            return token_before_exp;
        }
    }

    Ok(input.advance(len))
}

fn int(input: Cursor) -> Result<Cursor, Reject> {
    let mut rest = digits(input)?;
    if let Some(ch) = rest.chars().next() {
        if is_ident_start(ch) {
            rest = ident_not_raw(rest)?.0;
        }
    }
    word_break(rest)
}

fn digits(mut input: Cursor) -> Result<Cursor, Reject> {
    let base = if input.starts_with("0x") {
        input = input.advance(2);
        16
    } else if input.starts_with("0o") {
        input = input.advance(2);
        8
    } else if input.starts_with("0b") {
        input = input.advance(2);
        2
    } else {
        10
    };

    let mut len = 0;
    let mut empty = true;
    for b in input.bytes() {
        match b {
            b'0'..=b'9' => {
                let digit = (b - b'0') as u64;
                if digit >= base {
                    return Err(Reject);
                }
            }
            b'a'..=b'f' => {
                let digit = 10 + (b - b'a') as u64;
                if digit >= base {
                    break;
                }
            }
            b'A'..=b'F' => {
                let digit = 10 + (b - b'A') as u64;
                if digit >= base {
                    break;
                }
            }
            b'_' => {
                if empty && base == 10 {
                    return Err(Reject);
                }
                len += 1;
                continue;
            }
            _ => break,
        }
        len += 1;
        empty = false;
    }
    if empty {
        Err(Reject)
    } else {
        Ok(input.advance(len))
    }
}

fn punct(input: Cursor) -> PResult<Punct> {
    let (rest, ch) = punct_char(input)?;
    if ch == '\'' {
        let (after_lifetime, _ident) = ident_any(rest)?;
        if after_lifetime.starts_with_char('\'')
            || (after_lifetime.starts_with_char('#') && !rest.starts_with("r#"))
        {
            Err(Reject)
        } else {
            Ok((rest, Punct::new('\'', Spacing::Joint)))
        }
    } else {
        let kind = match punct_char(rest) {
            Ok(_) => Spacing::Joint,
            Err(Reject) => Spacing::Alone,
        };
        Ok((rest, Punct::new(ch, kind)))
    }
}

fn punct_char(input: Cursor) -> PResult<char> {
    if input.starts_with("//") || input.starts_with("/*") {
        // Do not accept `/` of a comment as a punct.
        return Err(Reject);
    }

    let mut chars = input.chars();
    let first = match chars.next() {
        Some(ch) => ch,
        None => {
            return Err(Reject);
        }
    };
    let recognized = "~!@#$%^&*-=+|;:,<.>/?'";
    if recognized.contains(first) {
        Ok((input.advance(first.len_utf8()), first))
    } else {
        Err(Reject)
    }
}

fn doc_comment<'a>(input: Cursor<'a>, trees: &mut TokenStreamBuilder) -> PResult<'a, ()> {
    #[cfg(span_locations)]
    let lo = input.off;
    let (rest, (comment, inner)) = doc_comment_contents(input)?;
    let fallback_span = Span {
        #[cfg(span_locations)]
        lo,
        #[cfg(span_locations)]
        hi: rest.off,
    };
    let span = crate::Span::_new_fallback(fallback_span);

    let mut scan_for_bare_cr = comment;
    while let Some(cr) = scan_for_bare_cr.find('\r') {
        let rest = &scan_for_bare_cr[cr + 1..];
        if !rest.starts_with('\n') {
            return Err(Reject);
        }
        scan_for_bare_cr = rest;
    }

    let mut pound = Punct::new('#', Spacing::Alone);
    pound.set_span(span);
    trees.push_token_from_parser(TokenTree::Punct(pound));

    if inner {
        let mut bang = Punct::new('!', Spacing::Alone);
        bang.set_span(span);
        trees.push_token_from_parser(TokenTree::Punct(bang));
    }

    let doc_ident = crate::Ident::_new_fallback(Ident::new_unchecked("doc", fallback_span));
    let mut equal = Punct::new('=', Spacing::Alone);
    equal.set_span(span);
    let mut literal = crate::Literal::_new_fallback(Literal::string(comment));
    literal.set_span(span);
    let mut bracketed = TokenStreamBuilder::with_capacity(3);
    bracketed.push_token_from_parser(TokenTree::Ident(doc_ident));
    bracketed.push_token_from_parser(TokenTree::Punct(equal));
    bracketed.push_token_from_parser(TokenTree::Literal(literal));
    let group = Group::new(Delimiter::Bracket, bracketed.build());
    let mut group = crate::Group::_new_fallback(group);
    group.set_span(span);
    trees.push_token_from_parser(TokenTree::Group(group));

    Ok((rest, ()))
}

fn doc_comment_contents(input: Cursor) -> PResult<(&str, bool)> {
    if input.starts_with("//!") {
        let input = input.advance(3);
        let (input, s) = take_until_newline_or_eof(input);
        Ok((input, (s, true)))
    } else if input.starts_with("/*!") {
        let (input, s) = block_comment(input)?;
        Ok((input, (&s[3..s.len() - 2], true)))
    } else if input.starts_with("///") {
        let input = input.advance(3);
        if input.starts_with_char('/') {
            return Err(Reject);
        }
        let (input, s) = take_until_newline_or_eof(input);
        Ok((input, (s, false)))
    } else if input.starts_with("/**") && !input.rest[3..].starts_with('*') {
        let (input, s) = block_comment(input)?;
        Ok((input, (&s[3..s.len() - 2], false)))
    } else {
        Err(Reject)
    }
}

fn take_until_newline_or_eof(input: Cursor) -> (Cursor, &str) {
    let chars = input.char_indices();

    for (i, ch) in chars {
        if ch == '\n' {
            return (input.advance(i), &input.rest[..i]);
        } else if ch == '\r' && input.rest[i + 1..].starts_with('\n') {
            return (input.advance(i + 1), &input.rest[..i]);
        }
    }

    (input.advance(input.len()), input.rest)
}
