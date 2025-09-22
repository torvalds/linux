import re


class BooleanExpression:
    # A simple evaluator of boolean expressions.
    #
    # Grammar:
    #   expr         :: or_expr
    #   or_expr      :: and_expr ('||' and_expr)*
    #   and_expr     :: not_expr ('&&' not_expr)*
    #   not_expr     :: '!' not_expr
    #                   '(' or_expr ')'
    #                   match_expr
    #   match_expr   :: braced_regex
    #                   identifier
    #                   braced_regex match_expr
    #                   identifier match_expr
    #   identifier   :: [-+=._a-zA-Z0-9]+
    #   braced_regex :: '{{' python_regex '}}'

    # Evaluates `string` as a boolean expression.
    # Returns True or False. Throws a ValueError on syntax error.
    #
    # Variables in `variables` are true.
    # Regexes that match any variable in `variables` are true.
    # 'true' is true.
    # All other identifiers are false.
    @staticmethod
    def evaluate(string, variables):
        try:
            parser = BooleanExpression(string, set(variables))
            return parser.parseAll()
        except ValueError as e:
            raise ValueError(str(e) + ("\nin expression: %r" % string))

    #####

    def __init__(self, string, variables):
        self.tokens = BooleanExpression.tokenize(string)
        self.variables = variables
        self.variables.add("true")
        self.value = None
        self.token = None

    # Singleton end-of-expression marker.
    END = object()

    # Tokenization pattern.
    Pattern = re.compile(
        r"\A\s*([()]|&&|\|\||!|(?:[-+=._a-zA-Z0-9]+|\{\{.+?\}\})+)\s*(.*)\Z"
    )

    @staticmethod
    def tokenize(string):
        while True:
            m = re.match(BooleanExpression.Pattern, string)
            if m is None:
                if string == "":
                    yield BooleanExpression.END
                    return
                else:
                    raise ValueError("couldn't parse text: %r" % string)

            token = m.group(1)
            string = m.group(2)
            yield token

    def quote(self, token):
        if token is BooleanExpression.END:
            return "<end of expression>"
        else:
            return repr(token)

    def accept(self, t):
        if self.token == t:
            self.token = next(self.tokens)
            return True
        else:
            return False

    def expect(self, t):
        if self.token == t:
            if self.token != BooleanExpression.END:
                self.token = next(self.tokens)
        else:
            raise ValueError(
                "expected: %s\nhave: %s" % (self.quote(t), self.quote(self.token))
            )

    @staticmethod
    def isMatchExpression(token):
        if (
            token is BooleanExpression.END
            or token == "&&"
            or token == "||"
            or token == "!"
            or token == "("
            or token == ")"
        ):
            return False
        return True

    def parseMATCH(self):
        regex = ""
        for part in filter(None, re.split(r"(\{\{.+?\}\})", self.token)):
            if part.startswith("{{"):
                assert part.endswith("}}")
                regex += "(?:{})".format(part[2:-2])
            else:
                regex += re.escape(part)
        regex = re.compile(regex)
        self.value = any(regex.fullmatch(var) for var in self.variables)
        self.token = next(self.tokens)

    def parseNOT(self):
        if self.accept("!"):
            self.parseNOT()
            self.value = not self.value
        elif self.accept("("):
            self.parseOR()
            self.expect(")")
        elif not BooleanExpression.isMatchExpression(self.token):
            raise ValueError(
                "expected: '!', '(', '{{', or identifier\nhave: %s"
                % self.quote(self.token)
            )
        else:
            self.parseMATCH()

    def parseAND(self):
        self.parseNOT()
        while self.accept("&&"):
            left = self.value
            self.parseNOT()
            right = self.value
            # this is technically the wrong associativity, but it
            # doesn't matter for this limited expression grammar
            self.value = left and right

    def parseOR(self):
        self.parseAND()
        while self.accept("||"):
            left = self.value
            self.parseAND()
            right = self.value
            # this is technically the wrong associativity, but it
            # doesn't matter for this limited expression grammar
            self.value = left or right

    def parseAll(self):
        self.token = next(self.tokens)
        self.parseOR()
        self.expect(BooleanExpression.END)
        return self.value


#######
# Tests

import unittest


class TestBooleanExpression(unittest.TestCase):
    def test_variables(self):
        variables = {"its-true", "false-lol-true", "under_score", "e=quals", "d1g1ts"}
        self.assertTrue(BooleanExpression.evaluate("true", variables))
        self.assertTrue(BooleanExpression.evaluate("its-true", variables))
        self.assertTrue(BooleanExpression.evaluate("false-lol-true", variables))
        self.assertTrue(BooleanExpression.evaluate("under_score", variables))
        self.assertTrue(BooleanExpression.evaluate("e=quals", variables))
        self.assertTrue(BooleanExpression.evaluate("d1g1ts", variables))
        self.assertTrue(BooleanExpression.evaluate("{{its.+}}", variables))
        self.assertTrue(BooleanExpression.evaluate("{{false-[lo]+-true}}", variables))
        self.assertTrue(
            BooleanExpression.evaluate("{{(true|false)-lol-(true|false)}}", variables)
        )
        self.assertTrue(BooleanExpression.evaluate("d1g{{[0-9]}}ts", variables))
        self.assertTrue(BooleanExpression.evaluate("d1g{{[0-9]}}t{{[a-z]}}", variables))
        self.assertTrue(
            BooleanExpression.evaluate("{{d}}1g{{[0-9]}}t{{[a-z]}}", variables)
        )
        self.assertTrue(BooleanExpression.evaluate("d1{{(g|1)+}}ts", variables))

        self.assertFalse(BooleanExpression.evaluate("false", variables))
        self.assertFalse(BooleanExpression.evaluate("True", variables))
        self.assertFalse(BooleanExpression.evaluate("true-ish", variables))
        self.assertFalse(BooleanExpression.evaluate("not_true", variables))
        self.assertFalse(BooleanExpression.evaluate("tru", variables))
        self.assertFalse(BooleanExpression.evaluate("{{its-true.+}}", variables))

    def test_matching(self):
        expr1 = "linux && (target={{aarch64-.+}} || target={{x86_64-.+}})"
        self.assertTrue(
            BooleanExpression.evaluate(
                expr1, {"linux", "target=x86_64-unknown-linux-gnu"}
            )
        )
        self.assertFalse(
            BooleanExpression.evaluate(
                expr1, {"linux", "target=i386-unknown-linux-gnu"}
            )
        )

        expr2 = "use_system_cxx_lib && target={{.+}}-apple-macosx10.{{9|10|11|12}} && !no-exceptions"
        self.assertTrue(
            BooleanExpression.evaluate(
                expr2, {"use_system_cxx_lib", "target=arm64-apple-macosx10.12"}
            )
        )
        self.assertFalse(
            BooleanExpression.evaluate(
                expr2,
                {
                    "use_system_cxx_lib",
                    "target=arm64-apple-macosx10.12",
                    "no-exceptions",
                },
            )
        )
        self.assertFalse(
            BooleanExpression.evaluate(
                expr2, {"use_system_cxx_lib", "target=arm64-apple-macosx10.15"}
            )
        )

    def test_operators(self):
        self.assertTrue(BooleanExpression.evaluate("true || true", {}))
        self.assertTrue(BooleanExpression.evaluate("true || false", {}))
        self.assertTrue(BooleanExpression.evaluate("false || true", {}))
        self.assertFalse(BooleanExpression.evaluate("false || false", {}))

        self.assertTrue(BooleanExpression.evaluate("true && true", {}))
        self.assertFalse(BooleanExpression.evaluate("true && false", {}))
        self.assertFalse(BooleanExpression.evaluate("false && true", {}))
        self.assertFalse(BooleanExpression.evaluate("false && false", {}))

        self.assertFalse(BooleanExpression.evaluate("!true", {}))
        self.assertTrue(BooleanExpression.evaluate("!false", {}))

        self.assertTrue(BooleanExpression.evaluate("   ((!((false) ))   ) ", {}))
        self.assertTrue(BooleanExpression.evaluate("true && (true && (true))", {}))
        self.assertTrue(BooleanExpression.evaluate("!false && !false && !! !false", {}))
        self.assertTrue(BooleanExpression.evaluate("false && false || true", {}))
        self.assertTrue(BooleanExpression.evaluate("(false && false) || true", {}))
        self.assertFalse(BooleanExpression.evaluate("false && (false || true)", {}))

    # Evaluate boolean expression `expr`.
    # Fail if it does not throw a ValueError containing the text `error`.
    def checkException(self, expr, error):
        try:
            BooleanExpression.evaluate(expr, {})
            self.fail("expression %r didn't cause an exception" % expr)
        except ValueError as e:
            if -1 == str(e).find(error):
                self.fail(
                    (
                        "expression %r caused the wrong ValueError\n"
                        + "actual error was:\n%s\n"
                        + "expected error was:\n%s\n"
                    )
                    % (expr, e, error)
                )
        except BaseException as e:
            self.fail(
                (
                    "expression %r caused the wrong exception; actual "
                    + "exception was: \n%r"
                )
                % (expr, e)
            )

    def test_errors(self):
        self.checkException(
            "ba#d", "couldn't parse text: '#d'\n" + "in expression: 'ba#d'"
        )

        self.checkException(
            "true and true",
            "expected: <end of expression>\n"
            + "have: 'and'\n"
            + "in expression: 'true and true'",
        )

        self.checkException(
            "|| true",
            "expected: '!', '(', '{{', or identifier\n"
            + "have: '||'\n"
            + "in expression: '|| true'",
        )

        self.checkException(
            "true &&",
            "expected: '!', '(', '{{', or identifier\n"
            + "have: <end of expression>\n"
            + "in expression: 'true &&'",
        )

        self.checkException(
            "",
            "expected: '!', '(', '{{', or identifier\n"
            + "have: <end of expression>\n"
            + "in expression: ''",
        )

        self.checkException("*", "couldn't parse text: '*'\n" + "in expression: '*'")

        self.checkException(
            "no wait stop",
            "expected: <end of expression>\n"
            + "have: 'wait'\n"
            + "in expression: 'no wait stop'",
        )

        self.checkException(
            "no-$-please",
            "couldn't parse text: '$-please'\n" + "in expression: 'no-$-please'",
        )

        self.checkException(
            "(((true && true) || true)",
            "expected: ')'\n"
            + "have: <end of expression>\n"
            + "in expression: '(((true && true) || true)'",
        )

        self.checkException(
            "true (true)",
            "expected: <end of expression>\n"
            + "have: '('\n"
            + "in expression: 'true (true)'",
        )

        self.checkException(
            "( )",
            "expected: '!', '(', '{{', or identifier\n"
            + "have: ')'\n"
            + "in expression: '( )'",
        )

        self.checkException(
            "abc{{def", "couldn't parse text: '{{def'\n" + "in expression: 'abc{{def'"
        )

        self.checkException(
            "{{}}", "couldn't parse text: '{{}}'\n" + "in expression: '{{}}'"
        )


if __name__ == "__main__":
    unittest.main()
