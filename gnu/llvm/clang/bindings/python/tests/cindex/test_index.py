import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import *
import os
import unittest


kInputsDir = os.path.join(os.path.dirname(__file__), "INPUTS")


class TestIndex(unittest.TestCase):
    def test_create(self):
        index = Index.create()

    # FIXME: test Index.read

    def test_parse(self):
        index = Index.create()
        self.assertIsInstance(index, Index)
        tu = index.parse(os.path.join(kInputsDir, "hello.cpp"))
        self.assertIsInstance(tu, TranslationUnit)
        tu = index.parse(None, ["-c", os.path.join(kInputsDir, "hello.cpp")])
        self.assertIsInstance(tu, TranslationUnit)
