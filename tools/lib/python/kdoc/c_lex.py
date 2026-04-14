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

from copy import copy

from .kdoc_re import KernRe

log = logging.getLogger(__name__)

def tokenizer_set_log(logger, prefix = ""):
    """
    Replace the module‑level logger with a LoggerAdapter that
    prepends *prefix* to every message.
    """
    global log

    class PrefixAdapter(logging.LoggerAdapter):
        """
        Ancillary class to set prefix on all message logs.
        """
        def process(self, msg, kwargs):
            return f"{prefix}{msg}", kwargs

    # Wrap the provided logger in our adapter
    log = PrefixAdapter(logger, {"prefix": prefix})

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

    def __init__(self, source=None):
        """
        Create a regular expression to handle RE_SCANNER_LIST.

        While I generally don't like using regex group naming via:
            (?P<name>...)

        in this particular case, it makes sense, as we can pick the name
        when matching a code via RE_SCANNER.
        """

        #
        # Store logger to allow parser classes to re-use it
        #
        global log
        self.log = log

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


class CTokenArgs:
    """
    Ancillary class to help using backrefs from sub matches.

    If the highest backref contain a "+" at the last element,
    the logic will be greedy, picking all other delims.

    This is needed to parse struct_group macros with end with ``MEMBERS...``.
    """
    def __init__(self, sub_str):
        self.sub_groups = set()
        self.max_group = -1
        self.greedy = None

        for m in KernRe(r'\\(\d+)([+]?)').finditer(sub_str):
            group = int(m.group(1))
            if m.group(2) == "+":
                if self.greedy and self.greedy != group:
                    raise ValueError("There are multiple greedy patterns!")
                self.greedy = group

            self.sub_groups.add(group)
            self.max_group = max(self.max_group, group)

        if self.greedy:
            if self.greedy != self.max_group:
                raise ValueError("Greedy pattern is not the last one!")

            sub_str = KernRe(r'(\\\d+)[+]').sub(r"\1", sub_str)

        self.sub_str = sub_str
        self.sub_tokeninzer = CTokenizer(sub_str)

    def groups(self, new_tokenizer):
        r"""
        Create replacement arguments for backrefs like:

        ``\0``, ``\1``, ``\2``, ... ``\{number}``

        It also accepts a ``+`` character to the highest backref, like
        ``\4+``. When used, the backref will be greedy, picking all other
        arguments afterwards.

        The logic is smart enough to only go up to the maximum required
        argument, even if there are more.

        If there is a backref for an argument above the limit, it will
        raise an exception. Please notice that, on C, square brackets
        don't have any separator on it. Trying to use ``\1``..``\n`` for
        brackets also raise an exception.
        """

        level = (0, 0, 0)

        if self.max_group < 0:
            return level, []

        tokens = new_tokenizer.tokens

        #
        # Fill \0 with the full token contents
        #
        groups_list = [ [] ]

        if 0 in self.sub_groups:
            inner_level = 0

            for i in range(0, len(tokens)):
                tok = tokens[i]

                if tok.kind == CToken.BEGIN:
                    inner_level += 1

                    #
                    # Discard first begin
                    #
                    if not groups_list[0]:
                        continue
                elif tok.kind == CToken.END:
                    inner_level -= 1
                    if inner_level < 0:
                        break

                if inner_level:
                    groups_list[0].append(tok)

        if not self.max_group:
            return level, groups_list

        delim = None

        #
        # Ignore everything before BEGIN. The value of begin gives the
        # delimiter to be used for the matches
        #
        for i in range(0, len(tokens)):
            tok = tokens[i]
            if tok.kind == CToken.BEGIN:
                if tok.value == "{":
                    delim = ";"
                elif tok.value == "(":
                    delim = ","
                else:
                    self.log.error(fr"Can't handle \1..\n on {sub_str}")

                level = tok.level
                break

        pos = 1
        groups_list.append([])

        inner_level = 0
        for i in range(i + 1, len(tokens)):
            tok = tokens[i]

            if tok.kind == CToken.BEGIN:
                inner_level += 1
            if tok.kind == CToken.END:
                inner_level -= 1
                if inner_level < 0:
                    break

            if tok.kind in [CToken.PUNC, CToken.ENDSTMT] and delim == tok.value:
                pos += 1
                if self.greedy and pos > self.max_group:
                    pos -= 1
                else:
                    groups_list.append([])

                    if pos > self.max_group:
                        break

                    continue

            groups_list[pos].append(tok)

        if pos < self.max_group:
            log.error(fr"{self.sub_str} groups are up to {pos} instead of {self.max_group}")

        return level, groups_list

    def tokens(self, new_tokenizer):
        level, groups = self.groups(new_tokenizer)

        new = CTokenizer()

        for tok in self.sub_tokeninzer.tokens:
            if tok.kind == CToken.BACKREF:
                group = int(tok.value[1:])

                for group_tok in groups[group]:
                    new_tok = copy(group_tok)

                    new_level = [0, 0, 0]

                    for i in range(0, len(level)):
                        new_level[i] = new_tok.level[i] + level[i]

                    new_tok.level = tuple(new_level)

                    new.tokens += [ new_tok ]
            else:
                new.tokens += [ tok ]

        return new.tokens


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


    def __init__(self, regex, delim="("):
        self.regex = KernRe("^" + regex + r"\b")
        self.start_delim = delim

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

            if not started:
                if tok.kind == CToken.SPACE:
                    continue

                if tok.kind == CToken.BEGIN and tok.value == self.start_delim:
                    started = True
                    continue

                # Name only token without BEGIN/END
                if i > start:
                    i -= 1
                yield start, i
                start = None

            if tok.kind == CToken.END and tok.level == stack[-1][1]:
                start, level = stack.pop()

                yield start, i
                start = None

        #
        # If an END zeroing levels is not there, return remaining stuff
        # This is meant to solve cases where the caller logic might be
        # picking an incomplete block.
        #
        if start and stack:
            if started:
                s = str(tokenizer)
                log.warning(f"can't find a final end at {s}")

            yield start, len(tokenizer.tokens)

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

        for start, end in self._search(tokenizer):
            new_tokenizer = CTokenizer(tokenizer.tokens[start:end + 1])

            if is_token:
                yield new_tokenizer
            else:
                yield str(new_tokenizer)

    def sub(self, sub_str, source, count=0):
        """
        This is similar to re.sub:

        It matches a regex that it is followed by a delimiter,
        replacing occurrences only if all delimiters are paired.

        if the sub argument contains::

            r'\0'

        it will work just like re: it places there the matched paired data
        with the delimiter stripped.

        If count is different than zero, it will replace at most count
        items.
        """
        if isinstance(source, CTokenizer):
            is_token = True
            tokenizer = source
        else:
            is_token = False
            tokenizer = CTokenizer(source)

        # Detect if sub_str contains sub arguments

        args_match = CTokenArgs(sub_str)

        new_tokenizer = CTokenizer()
        pos = 0
        n = 0

        #
        # NOTE: the code below doesn't consider overlays at sub.
        # We may need to add some extra unit tests to check if those
        # would cause problems. When replacing by "", this should not
        # be a problem, but other transformations could be problematic
        #
        for start, end in self._search(tokenizer):
            new_tokenizer.tokens += tokenizer.tokens[pos:start]

            new = CTokenizer(tokenizer.tokens[start:end + 1])

            new_tokenizer.tokens += args_match.tokens(new)

            pos = end + 1

            n += 1
            if count and n >= count:
                break

        new_tokenizer.tokens += tokenizer.tokens[pos:]

        if not is_token:
            return str(new_tokenizer)

        return new_tokenizer

    def __repr__(self):
        """
        Returns a displayable version of the class init.
        """

        return f'CMatch("{self.regex.regex.pattern}")'
