import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import LinkageKind
from clang.cindex import Cursor
from clang.cindex import TranslationUnit

from .util import get_cursor
from .util import get_tu

import unittest


class TestLinkage(unittest.TestCase):
    def test_linkage(self):
        """Ensure that linkage specifers are available on cursors"""

        tu = get_tu(
            """
void foo() { int no_linkage; }
static int internal;
namespace { struct unique_external_type {} }
unique_external_type unique_external;
extern int external;
""",
            lang="cpp",
        )

        no_linkage = get_cursor(tu.cursor, "no_linkage")
        self.assertEqual(no_linkage.linkage, LinkageKind.NO_LINKAGE)

        internal = get_cursor(tu.cursor, "internal")
        self.assertEqual(internal.linkage, LinkageKind.INTERNAL)

        unique_external = get_cursor(tu.cursor, "unique_external")
        self.assertEqual(unique_external.linkage, LinkageKind.UNIQUE_EXTERNAL)

        external = get_cursor(tu.cursor, "external")
        self.assertEqual(external.linkage, LinkageKind.EXTERNAL)
