import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from contextlib import contextmanager
import gc
import os
import sys
import tempfile
import unittest

from clang.cindex import CursorKind
from clang.cindex import Cursor
from clang.cindex import File
from clang.cindex import Index
from clang.cindex import SourceLocation
from clang.cindex import SourceRange
from clang.cindex import TranslationUnitSaveError
from clang.cindex import TranslationUnitLoadError
from clang.cindex import TranslationUnit
from .util import get_cursor
from .util import get_tu
from .util import skip_if_no_fspath
from .util import str_to_path


kInputsDir = os.path.join(os.path.dirname(__file__), "INPUTS")


@contextmanager
def save_tu(tu):
    """Convenience API to save a TranslationUnit to a file.

    Returns the filename it was saved to.
    """
    with tempfile.NamedTemporaryFile() as t:
        tu.save(t.name)
        yield t.name


@contextmanager
def save_tu_pathlike(tu):
    """Convenience API to save a TranslationUnit to a file.

    Returns the filename it was saved to.
    """
    with tempfile.NamedTemporaryFile() as t:
        tu.save(str_to_path(t.name))
        yield t.name


class TestTranslationUnit(unittest.TestCase):
    def test_spelling(self):
        path = os.path.join(kInputsDir, "hello.cpp")
        tu = TranslationUnit.from_source(path)
        self.assertEqual(tu.spelling, path)

    def test_cursor(self):
        path = os.path.join(kInputsDir, "hello.cpp")
        tu = get_tu(path)
        c = tu.cursor
        self.assertIsInstance(c, Cursor)
        self.assertIs(c.kind, CursorKind.TRANSLATION_UNIT)

    def test_parse_arguments(self):
        path = os.path.join(kInputsDir, "parse_arguments.c")
        tu = TranslationUnit.from_source(path, ["-DDECL_ONE=hello", "-DDECL_TWO=hi"])
        spellings = [c.spelling for c in tu.cursor.get_children()]
        self.assertEqual(spellings[-2], "hello")
        self.assertEqual(spellings[-1], "hi")

    def test_reparse_arguments(self):
        path = os.path.join(kInputsDir, "parse_arguments.c")
        tu = TranslationUnit.from_source(path, ["-DDECL_ONE=hello", "-DDECL_TWO=hi"])
        tu.reparse()
        spellings = [c.spelling for c in tu.cursor.get_children()]
        self.assertEqual(spellings[-2], "hello")
        self.assertEqual(spellings[-1], "hi")

    def test_unsaved_files(self):
        tu = TranslationUnit.from_source(
            "fake.c",
            ["-I./"],
            unsaved_files=[
                (
                    "fake.c",
                    """
#include "fake.h"
int x;
int SOME_DEFINE;
""",
                ),
                (
                    "./fake.h",
                    """
#define SOME_DEFINE y
""",
                ),
            ],
        )
        spellings = [c.spelling for c in tu.cursor.get_children()]
        self.assertEqual(spellings[-2], "x")
        self.assertEqual(spellings[-1], "y")

    def test_unsaved_files_2(self):
        if sys.version_info.major >= 3:
            from io import StringIO
        else:
            from io import BytesIO as StringIO
        tu = TranslationUnit.from_source(
            "fake.c", unsaved_files=[("fake.c", StringIO("int x;"))]
        )
        spellings = [c.spelling for c in tu.cursor.get_children()]
        self.assertEqual(spellings[-1], "x")

    @skip_if_no_fspath
    def test_from_source_accepts_pathlike(self):
        tu = TranslationUnit.from_source(
            str_to_path("fake.c"),
            ["-Iincludes"],
            unsaved_files=[
                (
                    str_to_path("fake.c"),
                    """
#include "fake.h"
    int x;
    int SOME_DEFINE;
    """,
                ),
                (
                    str_to_path("includes/fake.h"),
                    """
#define SOME_DEFINE y
    """,
                ),
            ],
        )
        spellings = [c.spelling for c in tu.cursor.get_children()]
        self.assertEqual(spellings[-2], "x")
        self.assertEqual(spellings[-1], "y")

    def assert_normpaths_equal(self, path1, path2):
        """Compares two paths for equality after normalizing them with
        os.path.normpath
        """
        self.assertEqual(os.path.normpath(path1), os.path.normpath(path2))

    def test_includes(self):
        def eq(expected, actual):
            if not actual.is_input_file:
                self.assert_normpaths_equal(expected[0], actual.source.name)
                self.assert_normpaths_equal(expected[1], actual.include.name)
            else:
                self.assert_normpaths_equal(expected[1], actual.include.name)

        src = os.path.join(kInputsDir, "include.cpp")
        h1 = os.path.join(kInputsDir, "header1.h")
        h2 = os.path.join(kInputsDir, "header2.h")
        h3 = os.path.join(kInputsDir, "header3.h")
        inc = [(src, h1), (h1, h3), (src, h2), (h2, h3)]

        tu = TranslationUnit.from_source(src)
        for i in zip(inc, tu.get_includes()):
            eq(i[0], i[1])

    def test_inclusion_directive(self):
        src = os.path.join(kInputsDir, "include.cpp")
        h1 = os.path.join(kInputsDir, "header1.h")
        h2 = os.path.join(kInputsDir, "header2.h")
        h3 = os.path.join(kInputsDir, "header3.h")
        inc = [h1, h3, h2, h3, h1]

        tu = TranslationUnit.from_source(
            src, options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
        )
        inclusion_directive_files = [
            c.get_included_file().name
            for c in tu.cursor.get_children()
            if c.kind == CursorKind.INCLUSION_DIRECTIVE
        ]
        for i in zip(inc, inclusion_directive_files):
            self.assert_normpaths_equal(i[0], i[1])

    def test_save(self):
        """Ensure TranslationUnit.save() works."""

        tu = get_tu("int foo();")

        with save_tu(tu) as path:
            self.assertTrue(os.path.exists(path))
            self.assertGreater(os.path.getsize(path), 0)

    @skip_if_no_fspath
    def test_save_pathlike(self):
        """Ensure TranslationUnit.save() works with PathLike filename."""

        tu = get_tu("int foo();")

        with save_tu_pathlike(tu) as path:
            self.assertTrue(os.path.exists(path))
            self.assertGreater(os.path.getsize(path), 0)

    def test_save_translation_errors(self):
        """Ensure that saving to an invalid directory raises."""

        tu = get_tu("int foo();")

        path = "/does/not/exist/llvm-test.ast"
        self.assertFalse(os.path.exists(os.path.dirname(path)))

        with self.assertRaises(TranslationUnitSaveError) as cm:
            tu.save(path)
        ex = cm.exception
        expected = TranslationUnitSaveError.ERROR_UNKNOWN
        self.assertEqual(ex.save_error, expected)

    def test_load(self):
        """Ensure TranslationUnits can be constructed from saved files."""

        tu = get_tu("int foo();")
        self.assertEqual(len(tu.diagnostics), 0)
        with save_tu(tu) as path:
            self.assertTrue(os.path.exists(path))
            self.assertGreater(os.path.getsize(path), 0)

            tu2 = TranslationUnit.from_ast_file(filename=path)
            self.assertEqual(len(tu2.diagnostics), 0)

            foo = get_cursor(tu2, "foo")
            self.assertIsNotNone(foo)

            # Just in case there is an open file descriptor somewhere.
            del tu2

    @skip_if_no_fspath
    def test_load_pathlike(self):
        """Ensure TranslationUnits can be constructed from saved files -
        PathLike variant."""
        tu = get_tu("int foo();")
        self.assertEqual(len(tu.diagnostics), 0)
        with save_tu(tu) as path:
            tu2 = TranslationUnit.from_ast_file(filename=str_to_path(path))
            self.assertEqual(len(tu2.diagnostics), 0)

            foo = get_cursor(tu2, "foo")
            self.assertIsNotNone(foo)

            # Just in case there is an open file descriptor somewhere.
            del tu2

    def test_index_parse(self):
        path = os.path.join(kInputsDir, "hello.cpp")
        index = Index.create()
        tu = index.parse(path)
        self.assertIsInstance(tu, TranslationUnit)

    def test_get_file(self):
        """Ensure tu.get_file() works appropriately."""

        tu = get_tu("int foo();")

        f = tu.get_file("t.c")
        self.assertIsInstance(f, File)
        self.assertEqual(f.name, "t.c")

        with self.assertRaises(Exception):
            f = tu.get_file("foobar.cpp")

    @skip_if_no_fspath
    def test_get_file_pathlike(self):
        """Ensure tu.get_file() works appropriately with PathLike filenames."""

        tu = get_tu("int foo();")

        f = tu.get_file(str_to_path("t.c"))
        self.assertIsInstance(f, File)
        self.assertEqual(f.name, "t.c")

        with self.assertRaises(Exception):
            f = tu.get_file(str_to_path("foobar.cpp"))

    def test_get_source_location(self):
        """Ensure tu.get_source_location() works."""

        tu = get_tu("int foo();")

        location = tu.get_location("t.c", 2)
        self.assertIsInstance(location, SourceLocation)
        self.assertEqual(location.offset, 2)
        self.assertEqual(location.file.name, "t.c")

        location = tu.get_location("t.c", (1, 3))
        self.assertIsInstance(location, SourceLocation)
        self.assertEqual(location.line, 1)
        self.assertEqual(location.column, 3)
        self.assertEqual(location.file.name, "t.c")

    def test_get_source_range(self):
        """Ensure tu.get_source_range() works."""

        tu = get_tu("int foo();")

        r = tu.get_extent("t.c", (1, 4))
        self.assertIsInstance(r, SourceRange)
        self.assertEqual(r.start.offset, 1)
        self.assertEqual(r.end.offset, 4)
        self.assertEqual(r.start.file.name, "t.c")
        self.assertEqual(r.end.file.name, "t.c")

        r = tu.get_extent("t.c", ((1, 2), (1, 3)))
        self.assertIsInstance(r, SourceRange)
        self.assertEqual(r.start.line, 1)
        self.assertEqual(r.start.column, 2)
        self.assertEqual(r.end.line, 1)
        self.assertEqual(r.end.column, 3)
        self.assertEqual(r.start.file.name, "t.c")
        self.assertEqual(r.end.file.name, "t.c")

        start = tu.get_location("t.c", 0)
        end = tu.get_location("t.c", 5)

        r = tu.get_extent("t.c", (start, end))
        self.assertIsInstance(r, SourceRange)
        self.assertEqual(r.start.offset, 0)
        self.assertEqual(r.end.offset, 5)
        self.assertEqual(r.start.file.name, "t.c")
        self.assertEqual(r.end.file.name, "t.c")

    def test_get_tokens_gc(self):
        """Ensures get_tokens() works properly with garbage collection."""

        tu = get_tu("int foo();")
        r = tu.get_extent("t.c", (0, 10))
        tokens = list(tu.get_tokens(extent=r))

        self.assertEqual(tokens[0].spelling, "int")
        gc.collect()
        self.assertEqual(tokens[0].spelling, "int")

        del tokens[1]
        gc.collect()
        self.assertEqual(tokens[0].spelling, "int")

        # May trigger segfault if we don't do our job properly.
        del tokens
        gc.collect()
        gc.collect()  # Just in case.

    def test_fail_from_source(self):
        path = os.path.join(kInputsDir, "non-existent.cpp")
        try:
            tu = TranslationUnit.from_source(path)
        except TranslationUnitLoadError:
            tu = None
        self.assertEqual(tu, None)

    def test_fail_from_ast_file(self):
        path = os.path.join(kInputsDir, "non-existent.ast")
        try:
            tu = TranslationUnit.from_ast_file(path)
        except TranslationUnitLoadError:
            tu = None
        self.assertEqual(tu, None)
