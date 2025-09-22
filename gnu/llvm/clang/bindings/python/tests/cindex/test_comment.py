import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import TranslationUnit
from tests.cindex.util import get_cursor

import unittest


class TestComment(unittest.TestCase):
    def test_comment(self):
        files = [
            (
                "fake.c",
                """
/// Aaa.
int test1;

/// Bbb.
/// x
void test2(void);

void f() {

}
""",
            )
        ]
        # make a comment-aware TU
        tu = TranslationUnit.from_source(
            "fake.c",
            ["-std=c99"],
            unsaved_files=files,
            options=TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION,
        )
        test1 = get_cursor(tu, "test1")
        self.assertIsNotNone(test1, "Could not find test1.")
        self.assertTrue(test1.type.is_pod())
        raw = test1.raw_comment
        brief = test1.brief_comment
        self.assertEqual(raw, """/// Aaa.""")
        self.assertEqual(brief, """Aaa.""")

        test2 = get_cursor(tu, "test2")
        raw = test2.raw_comment
        brief = test2.brief_comment
        self.assertEqual(raw, """/// Bbb.\n/// x""")
        self.assertEqual(brief, """Bbb. x""")

        f = get_cursor(tu, "f")
        raw = f.raw_comment
        brief = f.brief_comment
        self.assertIsNone(raw)
        self.assertIsNone(brief)
