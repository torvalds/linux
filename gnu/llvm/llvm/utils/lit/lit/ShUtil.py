from __future__ import absolute_import
import itertools

import lit.util
from lit.ShCommands import Command, GlobItem, Pipeline, Seq


class ShLexer:
    def __init__(self, data, win32Escapes=False):
        self.data = data
        self.pos = 0
        self.end = len(data)
        self.win32Escapes = win32Escapes

    def eat(self):
        c = self.data[self.pos]
        self.pos += 1
        return c

    def look(self):
        return self.data[self.pos]

    def maybe_eat(self, c):
        """
        maybe_eat(c) - Consume the character c if it is the next character,
        returning True if a character was consumed."""
        if self.data[self.pos] == c:
            self.pos += 1
            return True
        return False

    def lex_arg_fast(self, c):
        # Get the leading whitespace free section.
        chunk = self.data[self.pos - 1 :].split(None, 1)[0]

        # If it has special characters, the fast path failed.
        if (
            "|" in chunk
            or "&" in chunk
            or "<" in chunk
            or ">" in chunk
            or "'" in chunk
            or '"' in chunk
            or ";" in chunk
            or "\\" in chunk
        ):
            return None

        self.pos = self.pos - 1 + len(chunk)
        return GlobItem(chunk) if "*" in chunk or "?" in chunk else chunk

    def lex_arg_slow(self, c):
        if c in "'\"":
            str = self.lex_arg_quoted(c)
        else:
            str = c
        unquoted_glob_char = False
        quoted_glob_char = False
        while self.pos != self.end:
            c = self.look()
            if c.isspace() or c in "|&;":
                break
            elif c in "><":
                # This is an annoying case; we treat '2>' as a single token so
                # we don't have to track whitespace tokens.

                # If the parse string isn't an integer, do the usual thing.
                if not str.isdigit():
                    break

                # Otherwise, lex the operator and convert to a redirection
                # token.
                num = int(str)
                tok = self.lex_one_token()
                assert isinstance(tok, tuple) and len(tok) == 1
                return (tok[0], num)
            elif c == '"' or c == "'":
                self.eat()
                quoted_arg = self.lex_arg_quoted(c)
                if "*" in quoted_arg or "?" in quoted_arg:
                    quoted_glob_char = True
                str += quoted_arg
            elif not self.win32Escapes and c == "\\":
                # Outside of a string, '\\' escapes everything.
                self.eat()
                if self.pos == self.end:
                    lit.util.warning(
                        "escape at end of quoted argument in: %r" % self.data
                    )
                    return str
                str += self.eat()
            elif c in "*?":
                unquoted_glob_char = True
                str += self.eat()
            else:
                str += self.eat()
        # If a quote character is present, lex_arg_quoted will remove the quotes
        # and append the argument directly.  This causes a problem when the
        # quoted portion contains a glob character, as the character will no
        # longer be treated literally.  If glob characters occur *only* inside
        # of quotes, then we can handle this by not globbing at all, and if
        # glob characters occur *only* outside of quotes, we can still glob just
        # fine.  But if a glob character occurs both inside and outside of
        # quotes this presents a problem.  In practice this is such an obscure
        # edge case that it doesn't seem worth the added complexity to support.
        # By adding an assertion, it means some bot somewhere will catch this
        # and flag the user of a non-portable test (which could almost certainly
        # be re-written to work correctly without triggering this).
        assert not (quoted_glob_char and unquoted_glob_char)
        return GlobItem(str) if unquoted_glob_char else str

    def lex_arg_quoted(self, delim):
        str = ""
        while self.pos != self.end:
            c = self.eat()
            if c == delim:
                return str
            elif c == "\\" and delim == '"':
                # Inside a '"' quoted string, '\\' only escapes the quote
                # character and backslash, otherwise it is preserved.
                if self.pos == self.end:
                    lit.util.warning(
                        "escape at end of quoted argument in: %r" % self.data
                    )
                    return str
                c = self.eat()
                if c == '"':  #
                    str += '"'
                elif c == "\\":
                    str += "\\"
                else:
                    str += "\\" + c
            else:
                str += c
        lit.util.warning("missing quote character in %r" % self.data)
        return str

    def lex_arg_checked(self, c):
        pos = self.pos
        res = self.lex_arg_fast(c)
        end = self.pos

        self.pos = pos
        reference = self.lex_arg_slow(c)
        if res is not None:
            if res != reference:
                raise ValueError("Fast path failure: %r != %r" % (res, reference))
            if self.pos != end:
                raise ValueError("Fast path failure: %r != %r" % (self.pos, end))
        return reference

    def lex_arg(self, c):
        return self.lex_arg_fast(c) or self.lex_arg_slow(c)

    def lex_one_token(self):
        """
        lex_one_token - Lex a single 'sh' token."""

        c = self.eat()
        if c == ";":
            return (c,)
        if c == "|":
            if self.maybe_eat("|"):
                return ("||",)
            return (c,)
        if c == "&":
            if self.maybe_eat("&"):
                return ("&&",)
            if self.maybe_eat(">"):
                return ("&>",)
            return (c,)
        if c == ">":
            if self.maybe_eat("&"):
                return (">&",)
            if self.maybe_eat(">"):
                return (">>",)
            return (c,)
        if c == "<":
            if self.maybe_eat("&"):
                return ("<&",)
            if self.maybe_eat(">"):
                return ("<<",)
            return (c,)

        return self.lex_arg(c)

    def lex(self):
        while self.pos != self.end:
            if self.look().isspace():
                self.eat()
            else:
                yield self.lex_one_token()


###


class ShParser:
    def __init__(self, data, win32Escapes=False, pipefail=False):
        self.data = data
        self.pipefail = pipefail
        self.tokens = ShLexer(data, win32Escapes=win32Escapes).lex()

    def lex(self):
        for item in self.tokens:
            return item
        return None

    def look(self):
        token = self.lex()
        if token is not None:
            self.tokens = itertools.chain([token], self.tokens)
        return token

    def parse_command(self):
        tok = self.lex()
        if not tok:
            raise ValueError("empty command!")
        if isinstance(tok, tuple):
            raise ValueError("syntax error near unexpected token %r" % tok[0])

        args = [tok]
        redirects = []
        while 1:
            tok = self.look()

            # EOF?
            if tok is None:
                break

            # If this is an argument, just add it to the current command.
            if isinstance(tok, (str, GlobItem)):
                args.append(self.lex())
                continue

            # Otherwise see if it is a terminator.
            assert isinstance(tok, tuple)
            if tok[0] in ("|", ";", "&", "||", "&&"):
                break

            # Otherwise it must be a redirection.
            op = self.lex()
            arg = self.lex()
            if not arg:
                raise ValueError("syntax error near token %r" % op[0])
            redirects.append((op, arg))

        return Command(args, redirects)

    def parse_pipeline(self):
        negate = False

        commands = [self.parse_command()]
        while self.look() == ("|",):
            self.lex()
            commands.append(self.parse_command())
        return Pipeline(commands, negate, self.pipefail)

    def parse(self):
        lhs = self.parse_pipeline()

        while self.look():
            operator = self.lex()
            assert isinstance(operator, tuple) and len(operator) == 1

            if not self.look():
                raise ValueError("missing argument to operator %r" % operator[0])

            # FIXME: Operator precedence!!
            lhs = Seq(lhs, operator[0], self.parse_pipeline())

        return lhs
