import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import CursorKind
from clang.cindex import Index
from clang.cindex import SourceLocation
from clang.cindex import SourceRange
from clang.cindex import TokenKind

from .util import get_tu

import unittest


class TestTokens(unittest.TestCase):
    def test_token_to_cursor(self):
        """Ensure we can obtain a Cursor from a Token instance."""
        tu = get_tu("int i = 5;")
        r = tu.get_extent("t.c", (0, 9))
        tokens = list(tu.get_tokens(extent=r))

        self.assertEqual(len(tokens), 4)
        self.assertEqual(tokens[1].spelling, "i")
        self.assertEqual(tokens[1].kind, TokenKind.IDENTIFIER)

        cursor = tokens[1].cursor
        self.assertEqual(cursor.kind, CursorKind.VAR_DECL)
        self.assertEqual(tokens[1].cursor, tokens[2].cursor)

    def test_token_location(self):
        """Ensure Token.location works."""

        tu = get_tu("int foo = 10;")
        r = tu.get_extent("t.c", (0, 11))

        tokens = list(tu.get_tokens(extent=r))
        self.assertEqual(len(tokens), 4)

        loc = tokens[1].location
        self.assertIsInstance(loc, SourceLocation)
        self.assertEqual(loc.line, 1)
        self.assertEqual(loc.column, 5)
        self.assertEqual(loc.offset, 4)

    def test_token_extent(self):
        """Ensure Token.extent works."""
        tu = get_tu("int foo = 10;")
        r = tu.get_extent("t.c", (0, 11))

        tokens = list(tu.get_tokens(extent=r))
        self.assertEqual(len(tokens), 4)

        extent = tokens[1].extent
        self.assertIsInstance(extent, SourceRange)

        self.assertEqual(extent.start.offset, 4)
        self.assertEqual(extent.end.offset, 7)
