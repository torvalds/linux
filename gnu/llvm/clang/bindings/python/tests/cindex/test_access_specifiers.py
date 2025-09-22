import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import AccessSpecifier
from clang.cindex import Cursor
from clang.cindex import TranslationUnit

from .util import get_cursor
from .util import get_tu

import unittest


class TestAccessSpecifiers(unittest.TestCase):
    def test_access_specifiers(self):
        """Ensure that C++ access specifiers are available on cursors"""

        tu = get_tu(
            """
class test_class {
public:
  void public_member_function();
protected:
  void protected_member_function();
private:
  void private_member_function();
};
""",
            lang="cpp",
        )

        test_class = get_cursor(tu, "test_class")
        self.assertEqual(test_class.access_specifier, AccessSpecifier.INVALID)

        public = get_cursor(tu.cursor, "public_member_function")
        self.assertEqual(public.access_specifier, AccessSpecifier.PUBLIC)

        protected = get_cursor(tu.cursor, "protected_member_function")
        self.assertEqual(protected.access_specifier, AccessSpecifier.PROTECTED)

        private = get_cursor(tu.cursor, "private_member_function")
        self.assertEqual(private.access_specifier, AccessSpecifier.PRIVATE)
