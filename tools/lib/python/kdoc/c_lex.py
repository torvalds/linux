#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.

"""
Regular expression ancillary classes.

Those help caching regular expressions and do matching for kernel-doc.

Please notice that the code here may rise exceptions to indicate bad
usage inside kdoc to indicate problems at the replace pattern.

Other errors are logged via log instance.
"""

import logging
import re

from .kdoc_re import KernRe

log = logging.getLogger(__name__)


class CToken():
    """
    Data class to define a C token.
    """

    # Tokens that can be used by the parser. Works like an C enum.

    COMMENT = 0     #: A standard C or C99 comment, including delimiter.
    STRING = 1      #: A string, including quotation marks.
    CHAR = 2        #: A character, including apostophes.
    NUMBER = 3      #: A number.
    PUNC = 4        #: A puntuation mark: / ``,`` / ``.``.
    BEGIN = 5       #: A begin character: ``{`` / ``[`` / ``(``.
    END = 6         #: A end character: ``}`` / ``]`` / ``)``.
    CPP = 7         #: A preprocessor macro.
    HASH = 8        #: The hash character - useful to handle other macros.
    OP = 9          #: A C operator (add, subtract, ...).
    STRUCT = 10     #: A ``struct`` keyword.
    UNION = 11      #: An ``union`` keyword.
    ENUM = 12       #: A ``struct`` keyword.
    TYPEDEF = 13    #: A ``typedef`` keyword.
    NAME = 14       #: A name. Can be an ID or a type.
    SPACE = 15      #: Any space characters, including new lines
    ENDSTMT = 16    #: End of an statement (``;``).

    BACKREF = 17    #: Not a valid C sequence, but used at sub regex patterns.

    MISMATCH = 255  #: an error indicator: should never happen in practice.

    # Dict to convert from an enum interger into a string.
    _name_by_val = {v: k for k, v in dict(vars()).items() if isinstance(v, int)}

    # Dict to convert from string to an enum-like integer value.
    _name_to_val = {k: v for v, k in _name_by_val.items()}

    @staticmethod
    def to_name(val):
        """Convert from an integer value from CToken enum into a string"""

        return CToken._name_by_val.get(val, f"UNKNOWN({val})")

    @staticmethod
    def from_name(name):
        """Convert a string into a CToken enum value"""
        if name in CToken._name_to_val:
            return CToken._name_to_val[name]

        return CToken.MISMATCH


    def __init__(self, kind, value=None, pos=0,
                 brace_level=0, paren_level=0, bracket_level=0):
        self.kind = kind
        self.value = value
        self.pos = pos
        self.level = (bracket_level, paren_level, brace_level)

    def __repr__(self):
        name = self.to_name(self.kind)
        if isinstance(self.value, str):
            value = '"' + self.value + '"'
        else:
            value = self.value

        return f"CToken(CToken.{name}, {value}, {self.pos}, {self.level})"

#: Regexes to parse C code, transforming it into tokens.
RE_SCANNER_LIST = [
    #
    # Note that \s\S is different than .*, as it also catches \n
    #
    (CToken.COMMENT, r"//[^\n]*|/\*[\s\S]*?\*/"),

    (CToken.STRING,  r'"(?:\\.|[^"\\])*"'),
    (CToken.CHAR,    r"'(?:\\.|[^'\\])'"),

    (CToken.NUMBER,  r"0[xX][\da-fA-F]+[uUlL]*|0[0-7]+[uUlL]*|"
                     r"\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[fFlL]*"),

    (CToken.ENDSTMT, r"(?:\s+;|;)"),

    (CToken.PUNC,    r"[,\.]"),

    (CToken.BEGIN,   r"[\[\(\{]"),

    (CToken.END,     r"[\]\)\}]"),

    (CToken.CPP,     r"#\s*(?:define|include|ifdef|ifndef|if|else|elif|endif|undef|pragma)\b"),

    (CToken.HASH,    r"#"),

    (CToken.OP,      r"\+\+|\-\-|\->|==|\!=|<=|>=|&&|\|\||<<|>>|\+=|\-=|\*=|/=|%="
                     r"|&=|\|=|\^=|[=\+\-\*/%<>&\|\^~!\?\:]"),

    (CToken.STRUCT,  r"\bstruct\b"),
    (CToken.UNION,   r"\bunion\b"),
    (CToken.ENUM,    r"\benum\b"),
    (CToken.TYPEDEF, r"\btypedef\b"),

    (CToken.NAME,    r"[A-Za-z_]\w*"),

    (CToken.SPACE,   r"\s+"),

    (CToken.BACKREF, r"\\\d+"),

    (CToken.MISMATCH,r"."),
]

def fill_re_scanner(token_list):
    """Ancillary routine to convert RE_SCANNER_LIST into a finditer regex"""
    re_tokens = []

    for kind, pattern in token_list:
        name = CToken.to_name(kind)
        re_tokens.append(f"(?P<{name}>{pattern})")

    return KernRe("|".join(re_tokens), re.MULTILINE | re.DOTALL)

#: Handle C continuation lines.
RE_CONT = KernRe(r"\\\n")

RE_COMMENT_START = KernRe(r'/\*\s*')

#: tokenizer regex. Will be filled at the first CTokenizer usage.
RE_SCANNER = fill_re_scanner(RE_SCANNER_LIST)


class CTokenizer():
    """
    Scan C statements and definitions and produce tokens.

    When converted to string, it drops comments and handle public/private
    values, respecting depth.
    """

    # This class is inspired and follows the basic concepts of:
    #   https://docs.python.org/3/library/re.html#writing-a-tokenizer

    def __init__(self, source=None, log=None):
        """
        Create a regular expression to handle RE_SCANNER_LIST.

        While I generally don't like using regex group naming via:
            (?P<name>...)

        in this particular case, it makes sense, as we can pick the name
        when matching a code via RE_SCANNER.
        """

        self.tokens = []

        if not source:
            return

        if isinstance(source, list):
            self.tokens = source
            return

        #
        # While we could just use _tokenize directly via interator,
        # As we'll need to use the tokenizer several times inside kernel-doc
        # to handle macro transforms, cache the results on a list, as
        # re-using it is cheaper than having to parse everytime.
        #
        for tok in self._tokenize(source):
            self.tokens.append(tok)

    def _tokenize(self, source):
        """
        Iterator that parses ``source``, splitting it into tokens, as defined
        at ``self.RE_SCANNER_LIST``.

        The interactor returns a CToken class object.
        """

        # Handle continuation lines. Note that kdoc_parser already has a
        # logic to do that. Still, let's keep it for completeness, as we might
        # end re-using this tokenizer outsize kernel-doc some day - or we may
        # eventually remove from there as a future cleanup.
        source = RE_CONT.sub("", source)

        brace_level = 0
        paren_level = 0
        bracket_level = 0

        for match in RE_SCANNER.finditer(source):
            kind = CToken.from_name(match.lastgroup)
            pos = match.start()
            value = match.group()

            if kind == CToken.MISMATCH:
                log.error(f"Unexpected token '{value}' on pos {pos}:\n\t'{source}'")
            elif kind == CToken.BEGIN:
                if value == '(':
                    paren_level += 1
                elif value == '[':
                    bracket_level += 1
                else:  # value == '{'
                    brace_level += 1

            elif kind == CToken.END:
                if value == ')' and paren_level > 0:
                    paren_level -= 1
                elif value == ']' and bracket_level > 0:
                    bracket_level -= 1
                elif brace_level > 0:    # value == '}'
                    brace_level -= 1

            yield CToken(kind, value, pos,
                         brace_level, paren_level, bracket_level)

    def __str__(self):
        out=""
        show_stack = [True]

        for i, tok in enumerate(self.tokens):
            if tok.kind == CToken.BEGIN:
                show_stack.append(show_stack[-1])

            elif tok.kind == CToken.END:
                prev = show_stack[-1]
                if len(show_stack) > 1:
                    show_stack.pop()

                if not prev and show_stack[-1]:
                    #
                    # Try to preserve indent
                    #
                    out += "\t" * (len(show_stack) - 1)

                    out += str(tok.value)
                    continue

            elif tok.kind == CToken.COMMENT:
                comment = RE_COMMENT_START.sub("", tok.value)

                if comment.startswith("private:"):
                    show_stack[-1] = False
                    show = False
                elif comment.startswith("public:"):
                    show_stack[-1] = True

                continue

            if not show_stack[-1]:
                continue

            if i < len(self.tokens) - 1:
                next_tok = self.tokens[i + 1]

                # Do some cleanups before ";"

                if tok.kind == CToken.SPACE and next_tok.kind == CToken.ENDSTMT:
                    continue

                if tok.kind == CToken.ENDSTMT and next_tok.kind == tok.kind:
                    continue

            out += str(tok.value)

        return out


class CMatch:
    """
    Finding nested delimiters is hard with regular expressions. It is
    even harder on Python with its normal re module, as there are several
    advanced regular expressions that are missing.

    This is the case of this pattern::

            '\\bSTRUCT_GROUP(\\(((?:(?>[^)(]+)|(?1))*)\\))[^;]*;'

    which is used to properly match open/close parentheses of the
    string search STRUCT_GROUP(),

    Add a class that counts pairs of delimiters, using it to match and
    replace nested expressions.

    The original approach was suggested by:

        https://stackoverflow.com/questions/5454322/python-how-to-match-nested-parentheses-with-regex

    Although I re-implemented it to make it more generic and match 3 types
    of delimiters. The logic checks if delimiters are paired. If not, it
    will ignore the search string.
    """

    # TODO: add a sub method

    def __init__(self, regex):
        self.regex = KernRe(regex)

    def _search(self, tokenizer):
        """
        Finds paired blocks for a regex that ends with a delimiter.

        The suggestion of using finditer to match pairs came from:
        https://stackoverflow.com/questions/5454322/python-how-to-match-nested-parentheses-with-regex
        but I ended using a different implementation to align all three types
        of delimiters and seek for an initial regular expression.

        The algorithm seeks for open/close paired delimiters and places them
        into a stack, yielding a start/stop position of each match when the
        stack is zeroed.

        The algorithm should work fine for properly paired lines, but will
        silently ignore end delimiters that precede a start delimiter.
        This should be OK for kernel-doc parser, as unaligned delimiters
        would cause compilation errors. So, we don't need to raise exceptions
        to cover such issues.
        """

        start = None
        offset = -1
        started = False

        import sys

        stack = []

        for i, tok in enumerate(tokenizer.tokens):
            if start is None:
                if tok.kind == CToken.NAME and self.regex.match(tok.value):
                    start = i
                    stack.append((start, tok.level))
                    started = False

                continue

            if not started and tok.kind == CToken.BEGIN:
                started = True
                continue

            if tok.kind == CToken.END and tok.level == stack[-1][1]:
                start, level = stack.pop()
                offset = i

                yield CTokenizer(tokenizer.tokens[start:offset + 1])
                start = None

        #
        # If an END zeroing levels is not there, return remaining stuff
        # This is meant to solve cases where the caller logic might be
        # picking an incomplete block.
        #
        if start and offset < 0:
            print("WARNING: can't find an end", file=sys.stderr)
            yield CTokenizer(tokenizer.tokens[start:])

    def search(self, source):
        """
        This is similar to re.search:

        It matches a regex that it is followed by a delimiter,
        returning occurrences only if all delimiters are paired.
        """

        if isinstance(source, CTokenizer):
            tokenizer = source
            is_token = True
        else:
            tokenizer = CTokenizer(source)
            is_token = False

        for new_tokenizer in self._search(tokenizer):
            if is_token:
                yield new_tokenizer
            else:
                yield str(new_tokenizer)
