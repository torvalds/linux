import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import TokenKind

import unittest


class TestTokenKind(unittest.TestCase):
    def test_registration(self):
        """Ensure that items registered appear as class attributes."""
        self.assertTrue(hasattr(TokenKind, "LITERAL"))
        literal = TokenKind.LITERAL

        self.assertIsInstance(literal, TokenKind)

    def test_from_value(self):
        """Ensure registered values can be obtained from from_value()."""
        t = TokenKind.from_value(3)
        self.assertIsInstance(t, TokenKind)
        self.assertEqual(t, TokenKind.LITERAL)

    def test_repr(self):
        """Ensure repr() works."""

        r = repr(TokenKind.LITERAL)
        self.assertEqual(r, "TokenKind.LITERAL")
