#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
import unittest
from metric import Constant
from metric import Event
from metric import Expression
from metric import ParsePerfJson
from metric import RewriteMetricsInTermsOfOthers


class TestMetricExpressions(unittest.TestCase):

  def test_Operators(self):
    a = Event('a')
    b = Event('b')
    self.assertEqual((a | b).ToPerfJson(), 'a | b')
    self.assertEqual((a ^ b).ToPerfJson(), 'a ^ b')
    self.assertEqual((a & b).ToPerfJson(), 'a & b')
    self.assertEqual((a < b).ToPerfJson(), 'a < b')
    self.assertEqual((a > b).ToPerfJson(), 'a > b')
    self.assertEqual((a + b).ToPerfJson(), 'a + b')
    self.assertEqual((a - b).ToPerfJson(), 'a - b')
    self.assertEqual((a * b).ToPerfJson(), 'a * b')
    self.assertEqual((a / b).ToPerfJson(), 'a / b')
    self.assertEqual((a % b).ToPerfJson(), 'a % b')
    one = Constant(1)
    self.assertEqual((a + one).ToPerfJson(), 'a + 1')

  def test_Brackets(self):
    a = Event('a')
    b = Event('b')
    c = Event('c')
    self.assertEqual((a * b + c).ToPerfJson(), 'a * b + c')
    self.assertEqual((a + b * c).ToPerfJson(), 'a + b * c')
    self.assertEqual(((a + a) + a).ToPerfJson(), 'a + a + a')
    self.assertEqual(((a + b) * c).ToPerfJson(), '(a + b) * c')
    self.assertEqual((a + (b * c)).ToPerfJson(), 'a + b * c')
    self.assertEqual(((a / b) * c).ToPerfJson(), 'a / b * c')
    self.assertEqual((a / (b * c)).ToPerfJson(), 'a / (b * c)')

  def test_ParsePerfJson(self):
    # Based on an example of a real metric.
    before = '(a + b + c + d) / (2 * e)'
    after = before
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

    # Parsing should handle events with '-' in their name. Note, in
    # the json file the '\' are doubled to '\\'.
    before = r'topdown\-fe\-bound / topdown\-slots - 1'
    after = before
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

    # Parsing should handle escaped modifiers. Note, in the json file
    # the '\' are doubled to '\\'.
    before = r'arb@event\=0x81\,umask\=0x1@ + arb@event\=0x84\,umask\=0x1@'
    after = before
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

    # Parsing should handle exponents in numbers.
    before = r'a + 1e12 + b'
    after = before
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

  def test_IfElseTests(self):
    # if-else needs rewriting to Select and back.
    before = r'Event1 if #smt_on else Event2'
    after = f'({before})'
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

    before = r'Event1 if 0 else Event2'
    after = f'({before})'
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

    before = r'Event1 if 1 else Event2'
    after = f'({before})'
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

    # Ensure the select is evaluate last.
    before = r'Event1 + 1 if Event2 < 2 else Event3 + 3'
    after = (r'Select(Event(r"Event1") + Constant(1), Event(r"Event2") < '
             r'Constant(2), Event(r"Event3") + Constant(3))')
    self.assertEqual(ParsePerfJson(before).ToPython(), after)

    before = r'Event1 > 1 if Event2 < 2 else Event3 > 3'
    after = (r'Select(Event(r"Event1") > Constant(1), Event(r"Event2") < '
             r'Constant(2), Event(r"Event3") > Constant(3))')
    self.assertEqual(ParsePerfJson(before).ToPython(), after)

    before = r'min(a + b if c > 1 else c + d, e + f)'
    after = r'min((a + b if c > 1 else c + d), e + f)'
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

    before = r'a if b else c if d else e'
    after = r'(a if b else (c if d else e))'
    self.assertEqual(ParsePerfJson(before).ToPerfJson(), after)

  def test_ToPython(self):
    # pylint: disable=eval-used
    # Based on an example of a real metric.
    before = '(a + b + c + d) / (2 * e)'
    py = ParsePerfJson(before).ToPython()
    after = eval(py).ToPerfJson()
    self.assertEqual(before, after)

  def test_Simplify(self):
    before = '1 + 2 + 3'
    after = '6'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = 'a + 0'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = '0 + a'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = 'a | 0'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = '0 | a'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = 'a * 0'
    after = '0'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = '0 * a'
    after = '0'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = 'a * 1'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = '1 * a'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = 'a if 0 else b'
    after = 'b'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = 'a if 1 else b'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    before = 'a if b else a'
    after = 'a'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

    # Pattern used to add a slots event to metrics that require it.
    before = '0 * SLOTS'
    after = '0 * SLOTS'
    self.assertEqual(ParsePerfJson(before).Simplify().ToPerfJson(), after)

  def test_RewriteMetricsInTermsOfOthers(self):
    Expression.__eq__ = lambda e1, e2: e1.Equals(e2)
    before = [('cpu', 'm1', ParsePerfJson('a + b + c + d')),
              ('cpu', 'm2', ParsePerfJson('a + b + c'))]
    after = {('cpu', 'm1'): ParsePerfJson('m2 + d')}
    self.assertEqual(RewriteMetricsInTermsOfOthers(before), after)
    Expression.__eq__ = None

if __name__ == '__main__':
  unittest.main()
