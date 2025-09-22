# RUN: %{python} %s

import unittest

from lit.ShUtil import Command, Pipeline, Seq, ShLexer, ShParser


class TestShLexer(unittest.TestCase):
    def lex(self, str, *args, **kwargs):
        return list(ShLexer(str, *args, **kwargs).lex())

    def test_basic(self):
        self.assertEqual(
            self.lex("a|b>c&d<e;f"),
            ["a", ("|",), "b", (">",), "c", ("&",), "d", ("<",), "e", (";",), "f"],
        )

    def test_redirection_tokens(self):
        self.assertEqual(self.lex("a2>c"), ["a2", (">",), "c"])
        self.assertEqual(self.lex("a 2>c"), ["a", (">", 2), "c"])

    def test_quoting(self):
        self.assertEqual(self.lex(""" 'a' """), ["a"])
        self.assertEqual(self.lex(""" "hello\\"world" """), ['hello"world'])
        self.assertEqual(self.lex(""" "hello\\'world" """), ["hello\\'world"])
        self.assertEqual(self.lex(""" "hello\\\\world" """), ["hello\\world"])
        self.assertEqual(self.lex(""" he"llo wo"rld """), ["hello world"])
        self.assertEqual(self.lex(""" a\\ b a\\\\b """), ["a b", "a\\b"])
        self.assertEqual(self.lex(""" "" "" """), ["", ""])
        self.assertEqual(self.lex(""" a\\ b """, win32Escapes=True), ["a\\", "b"])


class TestShParse(unittest.TestCase):
    def parse(self, str):
        return ShParser(str).parse()

    def test_basic(self):
        self.assertEqual(
            self.parse("echo hello"), Pipeline([Command(["echo", "hello"], [])], False)
        )
        self.assertEqual(
            self.parse('echo ""'), Pipeline([Command(["echo", ""], [])], False)
        )
        self.assertEqual(
            self.parse("""echo -DFOO='a'"""),
            Pipeline([Command(["echo", "-DFOO=a"], [])], False),
        )
        self.assertEqual(
            self.parse('echo -DFOO="a"'),
            Pipeline([Command(["echo", "-DFOO=a"], [])], False),
        )

    def test_redirection(self):
        self.assertEqual(
            self.parse("echo hello > c"),
            Pipeline([Command(["echo", "hello"], [(((">"),), "c")])], False),
        )
        self.assertEqual(
            self.parse("echo hello > c >> d"),
            Pipeline(
                [Command(["echo", "hello"], [((">",), "c"), ((">>",), "d")])], False
            ),
        )
        self.assertEqual(
            self.parse("a 2>&1"), Pipeline([Command(["a"], [((">&", 2), "1")])], False)
        )

    def test_pipeline(self):
        self.assertEqual(
            self.parse("a | b"),
            Pipeline([Command(["a"], []), Command(["b"], [])], False),
        )

        self.assertEqual(
            self.parse("a | b | c"),
            Pipeline(
                [Command(["a"], []), Command(["b"], []), Command(["c"], [])], False
            ),
        )

    def test_list(self):
        self.assertEqual(
            self.parse("a ; b"),
            Seq(
                Pipeline([Command(["a"], [])], False),
                ";",
                Pipeline([Command(["b"], [])], False),
            ),
        )

        self.assertEqual(
            self.parse("a & b"),
            Seq(
                Pipeline([Command(["a"], [])], False),
                "&",
                Pipeline([Command(["b"], [])], False),
            ),
        )

        self.assertEqual(
            self.parse("a && b"),
            Seq(
                Pipeline([Command(["a"], [])], False),
                "&&",
                Pipeline([Command(["b"], [])], False),
            ),
        )

        self.assertEqual(
            self.parse("a || b"),
            Seq(
                Pipeline([Command(["a"], [])], False),
                "||",
                Pipeline([Command(["b"], [])], False),
            ),
        )

        self.assertEqual(
            self.parse("a && b || c"),
            Seq(
                Seq(
                    Pipeline([Command(["a"], [])], False),
                    "&&",
                    Pipeline([Command(["b"], [])], False),
                ),
                "||",
                Pipeline([Command(["c"], [])], False),
            ),
        )

        self.assertEqual(
            self.parse("a; b"),
            Seq(
                Pipeline([Command(["a"], [])], False),
                ";",
                Pipeline([Command(["b"], [])], False),
            ),
        )


if __name__ == "__main__":
    unittest.main()
