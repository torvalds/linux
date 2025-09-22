import os
from clang.cindex import Config

if "CLANG_LIBRARY_PATH" in os.environ:
    Config.set_library_path(os.environ["CLANG_LIBRARY_PATH"])

from clang.cindex import TranslationUnit

import unittest
from .util import skip_if_no_fspath
from .util import str_to_path


class TestCodeCompletion(unittest.TestCase):
    def check_completion_results(self, cr, expected):
        self.assertIsNotNone(cr)
        self.assertEqual(len(cr.diagnostics), 0)

        completions = [str(c) for c in cr.results]

        for c in expected:
            self.assertIn(c, completions)

    def test_code_complete(self):
        files = [
            (
                "fake.c",
                """
/// Aaa.
int test1;

/// Bbb.
void test2(void);

void f() {

}
""",
            )
        ]

        tu = TranslationUnit.from_source(
            "fake.c",
            ["-std=c99"],
            unsaved_files=files,
            options=TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION,
        )

        cr = tu.codeComplete(
            "fake.c", 9, 1, unsaved_files=files, include_brief_comments=True
        )

        expected = [
            "{'int', ResultType} | {'test1', TypedText} || Priority: 50 || Availability: Available || Brief comment: Aaa.",
            "{'void', ResultType} | {'test2', TypedText} | {'(', LeftParen} | {')', RightParen} || Priority: 50 || Availability: Available || Brief comment: Bbb.",
            "{'return', TypedText} | {';', SemiColon} || Priority: 40 || Availability: Available || Brief comment: None",
        ]
        self.check_completion_results(cr, expected)

    @skip_if_no_fspath
    def test_code_complete_pathlike(self):
        files = [
            (
                str_to_path("fake.c"),
                """
/// Aaa.
int test1;

/// Bbb.
void test2(void);

void f() {

}
""",
            )
        ]

        tu = TranslationUnit.from_source(
            str_to_path("fake.c"),
            ["-std=c99"],
            unsaved_files=files,
            options=TranslationUnit.PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION,
        )

        cr = tu.codeComplete(
            str_to_path("fake.c"),
            9,
            1,
            unsaved_files=files,
            include_brief_comments=True,
        )

        expected = [
            "{'int', ResultType} | {'test1', TypedText} || Priority: 50 || Availability: Available || Brief comment: Aaa.",
            "{'void', ResultType} | {'test2', TypedText} | {'(', LeftParen} | {')', RightParen} || Priority: 50 || Availability: Available || Brief comment: Bbb.",
            "{'return', TypedText} | {';', SemiColon} || Priority: 40 || Availability: Available || Brief comment: None",
        ]
        self.check_completion_results(cr, expected)

    def test_code_complete_availability(self):
        files = [
            (
                "fake.cpp",
                """
class P {
protected:
  int member;
};

class Q : public P {
public:
  using P::member;
};

void f(P x, Q y) {
  x.; // member is inaccessible
  y.; // member is accessible
}
""",
            )
        ]

        tu = TranslationUnit.from_source(
            "fake.cpp", ["-std=c++98"], unsaved_files=files
        )

        cr = tu.codeComplete("fake.cpp", 12, 5, unsaved_files=files)

        expected = [
            "{'const', TypedText} || Priority: 50 || Availability: Available || Brief comment: None",
            "{'volatile', TypedText} || Priority: 50 || Availability: Available || Brief comment: None",
            "{'operator', TypedText} || Priority: 40 || Availability: Available || Brief comment: None",
            "{'P', TypedText} || Priority: 50 || Availability: Available || Brief comment: None",
            "{'Q', TypedText} || Priority: 50 || Availability: Available || Brief comment: None",
        ]
        self.check_completion_results(cr, expected)

        cr = tu.codeComplete("fake.cpp", 13, 5, unsaved_files=files)
        expected = [
            "{'P', TypedText} | {'::', Text} || Priority: 75 || Availability: Available || Brief comment: None",
            "{'P &', ResultType} | {'operator=', TypedText} | {'(', LeftParen} | {'const P &', Placeholder} | {')', RightParen} || Priority: 79 || Availability: Available || Brief comment: None",
            "{'int', ResultType} | {'member', TypedText} || Priority: 35 || Availability: NotAccessible || Brief comment: None",
            "{'void', ResultType} | {'~P', TypedText} | {'(', LeftParen} | {')', RightParen} || Priority: 79 || Availability: Available || Brief comment: None",
        ]
        self.check_completion_results(cr, expected)
