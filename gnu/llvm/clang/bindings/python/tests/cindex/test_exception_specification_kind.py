import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

import clang.cindex
from clang.cindex import ExceptionSpecificationKind
from .util import get_tu

import unittest


def find_function_declarations(node, declarations=[]):
    if node.kind == clang.cindex.CursorKind.FUNCTION_DECL:
        declarations.append((node.spelling, node.exception_specification_kind))
    for child in node.get_children():
        declarations = find_function_declarations(child, declarations)
    return declarations


class TestExceptionSpecificationKind(unittest.TestCase):
    def test_exception_specification_kind(self):
        source = """int square1(int x);
                    int square2(int x) noexcept;
                    int square3(int x) noexcept(noexcept(x * x));"""

        tu = get_tu(source, lang="cpp", flags=["-std=c++14"])

        declarations = find_function_declarations(tu.cursor)
        expected = [
            ("square1", ExceptionSpecificationKind.NONE),
            ("square2", ExceptionSpecificationKind.BASIC_NOEXCEPT),
            ("square3", ExceptionSpecificationKind.COMPUTED_NOEXCEPT),
        ]
        self.assertListEqual(declarations, expected)
