import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import *
from .util import get_tu

import unittest


# FIXME: We need support for invalid translation units to test better.


class TestDiagnostics(unittest.TestCase):
    def test_diagnostic_warning(self):
        tu = get_tu("int f0() {}\n")
        self.assertEqual(len(tu.diagnostics), 1)
        self.assertEqual(tu.diagnostics[0].severity, Diagnostic.Warning)
        self.assertEqual(tu.diagnostics[0].location.line, 1)
        self.assertEqual(tu.diagnostics[0].location.column, 11)
        self.assertEqual(
            tu.diagnostics[0].spelling, "non-void function does not return a value"
        )

    def test_diagnostic_note(self):
        # FIXME: We aren't getting notes here for some reason.
        tu = get_tu("#define A x\nvoid *A = 1;\n")
        self.assertEqual(len(tu.diagnostics), 1)
        self.assertEqual(tu.diagnostics[0].severity, Diagnostic.Error)
        self.assertEqual(tu.diagnostics[0].location.line, 2)
        self.assertEqual(tu.diagnostics[0].location.column, 7)
        self.assertIn("incompatible", tu.diagnostics[0].spelling)

    #       self.assertEqual(tu.diagnostics[1].severity, Diagnostic.Note)
    #       self.assertEqual(tu.diagnostics[1].location.line, 1)
    #       self.assertEqual(tu.diagnostics[1].location.column, 11)
    #       self.assertEqual(tu.diagnostics[1].spelling, 'instantiated from')

    def test_diagnostic_fixit(self):
        tu = get_tu("struct { int f0; } x = { f0 : 1 };")
        self.assertEqual(len(tu.diagnostics), 1)
        self.assertEqual(tu.diagnostics[0].severity, Diagnostic.Warning)
        self.assertEqual(tu.diagnostics[0].location.line, 1)
        self.assertEqual(tu.diagnostics[0].location.column, 26)
        self.assertRegex(tu.diagnostics[0].spelling, "use of GNU old-style.*")
        self.assertEqual(len(tu.diagnostics[0].fixits), 1)
        self.assertEqual(tu.diagnostics[0].fixits[0].range.start.line, 1)
        self.assertEqual(tu.diagnostics[0].fixits[0].range.start.column, 26)
        self.assertEqual(tu.diagnostics[0].fixits[0].range.end.line, 1)
        self.assertEqual(tu.diagnostics[0].fixits[0].range.end.column, 30)
        self.assertEqual(tu.diagnostics[0].fixits[0].value, ".f0 = ")

    def test_diagnostic_range(self):
        tu = get_tu('void f() { int i = "a"; }')
        self.assertEqual(len(tu.diagnostics), 1)
        self.assertEqual(tu.diagnostics[0].severity, Diagnostic.Error)
        self.assertEqual(tu.diagnostics[0].location.line, 1)
        self.assertEqual(tu.diagnostics[0].location.column, 16)
        self.assertRegex(tu.diagnostics[0].spelling, "incompatible pointer to.*")
        self.assertEqual(len(tu.diagnostics[0].fixits), 0)
        self.assertEqual(len(tu.diagnostics[0].ranges), 1)
        self.assertEqual(tu.diagnostics[0].ranges[0].start.line, 1)
        self.assertEqual(tu.diagnostics[0].ranges[0].start.column, 20)
        self.assertEqual(tu.diagnostics[0].ranges[0].end.line, 1)
        self.assertEqual(tu.diagnostics[0].ranges[0].end.column, 23)
        with self.assertRaises(IndexError):
            tu.diagnostics[0].ranges[1].start.line

    def test_diagnostic_category(self):
        """Ensure that category properties work."""
        tu = get_tu("int f(int i) { return 7; }", all_warnings=True)
        self.assertEqual(len(tu.diagnostics), 1)
        d = tu.diagnostics[0]

        self.assertEqual(d.severity, Diagnostic.Warning)
        self.assertEqual(d.location.line, 1)
        self.assertEqual(d.location.column, 11)

        self.assertEqual(d.category_number, 2)
        self.assertEqual(d.category_name, "Semantic Issue")

    def test_diagnostic_option(self):
        """Ensure that category option properties work."""
        tu = get_tu("int f(int i) { return 7; }", all_warnings=True)
        self.assertEqual(len(tu.diagnostics), 1)
        d = tu.diagnostics[0]

        self.assertEqual(d.option, "-Wunused-parameter")
        self.assertEqual(d.disable_option, "-Wno-unused-parameter")

    def test_diagnostic_children(self):
        tu = get_tu("void f(int x) {} void g() { f(); }")
        self.assertEqual(len(tu.diagnostics), 1)
        d = tu.diagnostics[0]

        children = d.children
        self.assertEqual(len(children), 1)
        self.assertEqual(children[0].severity, Diagnostic.Note)
        self.assertRegex(children[0].spelling, ".*declared here")
        self.assertEqual(children[0].location.line, 1)
        self.assertEqual(children[0].location.column, 6)

    def test_diagnostic_string_repr(self):
        tu = get_tu("struct MissingSemicolon{}")
        self.assertEqual(len(tu.diagnostics), 1)
        d = tu.diagnostics[0]

        self.assertEqual(
            repr(d),
            "<Diagnostic severity 3, location <SourceLocation file 't.c', line 1, column 26>, spelling \"expected ';' after struct\">",
        )
