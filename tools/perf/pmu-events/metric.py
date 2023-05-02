# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
"""Parse or generate representations of perf metrics."""
import ast
import decimal
import json
import re
from typing import Dict, List, Optional, Set, Tuple, Union


class Expression:
  """Abstract base class of elements in a metric expression."""

  def ToPerfJson(self) -> str:
    """Returns a perf json file encoded representation."""
    raise NotImplementedError()

  def ToPython(self) -> str:
    """Returns a python expr parseable representation."""
    raise NotImplementedError()

  def Simplify(self):
    """Returns a simplified version of self."""
    raise NotImplementedError()

  def Equals(self, other) -> bool:
    """Returns true when two expressions are the same."""
    raise NotImplementedError()

  def Substitute(self, name: str, expression: 'Expression') -> 'Expression':
    raise NotImplementedError()

  def __str__(self) -> str:
    return self.ToPerfJson()

  def __or__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('|', self, other)

  def __ror__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('|', other, self)

  def __xor__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('^', self, other)

  def __and__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('&', self, other)

  def __rand__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('&', other, self)

  def __lt__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('<', self, other)

  def __gt__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('>', self, other)

  def __add__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('+', self, other)

  def __radd__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('+', other, self)

  def __sub__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('-', self, other)

  def __rsub__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('-', other, self)

  def __mul__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('*', self, other)

  def __rmul__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('*', other, self)

  def __truediv__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('/', self, other)

  def __rtruediv__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('/', other, self)

  def __mod__(self, other: Union[int, float, 'Expression']) -> 'Operator':
    return Operator('%', self, other)


def _Constify(val: Union[bool, int, float, Expression]) -> Expression:
  """Used to ensure that the nodes in the expression tree are all Expression."""
  if isinstance(val, bool):
    return Constant(1 if val else 0)
  if isinstance(val, (int, float)):
    return Constant(val)
  return val


# Simple lookup for operator precedence, used to avoid unnecessary
# brackets. Precedence matches that of the simple expression parser
# but differs from python where comparisons are lower precedence than
# the bitwise &, ^, | but not the logical versions that the expression
# parser doesn't have.
_PRECEDENCE = {
    '|': 0,
    '^': 1,
    '&': 2,
    '<': 3,
    '>': 3,
    '+': 4,
    '-': 4,
    '*': 5,
    '/': 5,
    '%': 5,
}


class Operator(Expression):
  """Represents a binary operator in the parse tree."""

  def __init__(self, operator: str, lhs: Union[int, float, Expression],
               rhs: Union[int, float, Expression]):
    self.operator = operator
    self.lhs = _Constify(lhs)
    self.rhs = _Constify(rhs)

  def Bracket(self,
              other: Expression,
              other_str: str,
              rhs: bool = False) -> str:
    """If necessary brackets the given other value.

    If ``other`` is an operator then a bracket is necessary when
    this/self operator has higher precedence. Consider: '(a + b) * c',
    ``other_str`` will be 'a + b'. A bracket is necessary as without
    the bracket 'a + b * c' will evaluate 'b * c' first. However, '(a
    * b) + c' doesn't need a bracket as 'a * b' will always be
    evaluated first. For 'a / (b * c)' (ie the same precedence level
    operations) then we add the bracket to best match the original
    input, but not for '(a / b) * c' where the bracket is unnecessary.

    Args:
      other (Expression): is a lhs or rhs operator
      other_str (str): ``other`` in the appropriate string form
      rhs (bool):  is ``other`` on the RHS

    Returns:
      str: possibly bracketed other_str
    """
    if isinstance(other, Operator):
      if _PRECEDENCE.get(self.operator, -1) > _PRECEDENCE.get(
          other.operator, -1):
        return f'({other_str})'
      if rhs and _PRECEDENCE.get(self.operator, -1) == _PRECEDENCE.get(
          other.operator, -1):
        return f'({other_str})'
    return other_str

  def ToPerfJson(self):
    return (f'{self.Bracket(self.lhs, self.lhs.ToPerfJson())} {self.operator} '
            f'{self.Bracket(self.rhs, self.rhs.ToPerfJson(), True)}')

  def ToPython(self):
    return (f'{self.Bracket(self.lhs, self.lhs.ToPython())} {self.operator} '
            f'{self.Bracket(self.rhs, self.rhs.ToPython(), True)}')

  def Simplify(self) -> Expression:
    lhs = self.lhs.Simplify()
    rhs = self.rhs.Simplify()
    if isinstance(lhs, Constant) and isinstance(rhs, Constant):
      return Constant(ast.literal_eval(lhs + self.operator + rhs))

    if isinstance(self.lhs, Constant):
      if self.operator in ('+', '|') and lhs.value == '0':
        return rhs

      # Simplify multiplication by 0 except for the slot event which
      # is deliberately introduced using this pattern.
      if self.operator == '*' and lhs.value == '0' and (
          not isinstance(rhs, Event) or 'slots' not in rhs.name.lower()):
        return Constant(0)

      if self.operator == '*' and lhs.value == '1':
        return rhs

    if isinstance(rhs, Constant):
      if self.operator in ('+', '|') and rhs.value == '0':
        return lhs

      if self.operator == '*' and rhs.value == '0':
        return Constant(0)

      if self.operator == '*' and self.rhs.value == '1':
        return lhs

    return Operator(self.operator, lhs, rhs)

  def Equals(self, other: Expression) -> bool:
    if isinstance(other, Operator):
      return self.operator == other.operator and self.lhs.Equals(
          other.lhs) and self.rhs.Equals(other.rhs)
    return False

  def Substitute(self, name: str, expression: Expression) -> Expression:
    if self.Equals(expression):
      return Event(name)
    lhs = self.lhs.Substitute(name, expression)
    rhs = None
    if self.rhs:
      rhs = self.rhs.Substitute(name, expression)
    return Operator(self.operator, lhs, rhs)


class Select(Expression):
  """Represents a select ternary in the parse tree."""

  def __init__(self, true_val: Union[int, float, Expression],
               cond: Union[int, float, Expression],
               false_val: Union[int, float, Expression]):
    self.true_val = _Constify(true_val)
    self.cond = _Constify(cond)
    self.false_val = _Constify(false_val)

  def ToPerfJson(self):
    true_str = self.true_val.ToPerfJson()
    cond_str = self.cond.ToPerfJson()
    false_str = self.false_val.ToPerfJson()
    return f'({true_str} if {cond_str} else {false_str})'

  def ToPython(self):
    return (f'Select({self.true_val.ToPython()}, {self.cond.ToPython()}, '
            f'{self.false_val.ToPython()})')

  def Simplify(self) -> Expression:
    cond = self.cond.Simplify()
    true_val = self.true_val.Simplify()
    false_val = self.false_val.Simplify()
    if isinstance(cond, Constant):
      return false_val if cond.value == '0' else true_val

    if true_val.Equals(false_val):
      return true_val

    return Select(true_val, cond, false_val)

  def Equals(self, other: Expression) -> bool:
    if isinstance(other, Select):
      return self.cond.Equals(other.cond) and self.false_val.Equals(
          other.false_val) and self.true_val.Equals(other.true_val)
    return False

  def Substitute(self, name: str, expression: Expression) -> Expression:
    if self.Equals(expression):
      return Event(name)
    true_val = self.true_val.Substitute(name, expression)
    cond = self.cond.Substitute(name, expression)
    false_val = self.false_val.Substitute(name, expression)
    return Select(true_val, cond, false_val)


class Function(Expression):
  """A function in an expression like min, max, d_ratio."""

  def __init__(self,
               fn: str,
               lhs: Union[int, float, Expression],
               rhs: Optional[Union[int, float, Expression]] = None):
    self.fn = fn
    self.lhs = _Constify(lhs)
    self.rhs = _Constify(rhs)

  def ToPerfJson(self):
    if self.rhs:
      return f'{self.fn}({self.lhs.ToPerfJson()}, {self.rhs.ToPerfJson()})'
    return f'{self.fn}({self.lhs.ToPerfJson()})'

  def ToPython(self):
    if self.rhs:
      return f'{self.fn}({self.lhs.ToPython()}, {self.rhs.ToPython()})'
    return f'{self.fn}({self.lhs.ToPython()})'

  def Simplify(self) -> Expression:
    lhs = self.lhs.Simplify()
    rhs = self.rhs.Simplify() if self.rhs else None
    if isinstance(lhs, Constant) and isinstance(rhs, Constant):
      if self.fn == 'd_ratio':
        if rhs.value == '0':
          return Constant(0)
        Constant(ast.literal_eval(f'{lhs} / {rhs}'))
      return Constant(ast.literal_eval(f'{self.fn}({lhs}, {rhs})'))

    return Function(self.fn, lhs, rhs)

  def Equals(self, other: Expression) -> bool:
    if isinstance(other, Function):
      result = self.fn == other.fn and self.lhs.Equals(other.lhs)
      if self.rhs:
        result = result and self.rhs.Equals(other.rhs)
      return result
    return False

  def Substitute(self, name: str, expression: Expression) -> Expression:
    if self.Equals(expression):
      return Event(name)
    lhs = self.lhs.Substitute(name, expression)
    rhs = None
    if self.rhs:
      rhs = self.rhs.Substitute(name, expression)
    return Function(self.fn, lhs, rhs)


def _FixEscapes(s: str) -> str:
  s = re.sub(r'([^\\]),', r'\1\\,', s)
  return re.sub(r'([^\\])=', r'\1\\=', s)


class Event(Expression):
  """An event in an expression."""

  def __init__(self, name: str, legacy_name: str = ''):
    self.name = _FixEscapes(name)
    self.legacy_name = _FixEscapes(legacy_name)

  def ToPerfJson(self):
    result = re.sub('/', '@', self.name)
    return result

  def ToPython(self):
    return f'Event(r"{self.name}")'

  def Simplify(self) -> Expression:
    return self

  def Equals(self, other: Expression) -> bool:
    return isinstance(other, Event) and self.name == other.name

  def Substitute(self, name: str, expression: Expression) -> Expression:
    return self


class Constant(Expression):
  """A constant within the expression tree."""

  def __init__(self, value: Union[float, str]):
    ctx = decimal.Context()
    ctx.prec = 20
    dec = ctx.create_decimal(repr(value) if isinstance(value, float) else value)
    self.value = dec.normalize().to_eng_string()
    self.value = self.value.replace('+', '')
    self.value = self.value.replace('E', 'e')

  def ToPerfJson(self):
    return self.value

  def ToPython(self):
    return f'Constant({self.value})'

  def Simplify(self) -> Expression:
    return self

  def Equals(self, other: Expression) -> bool:
    return isinstance(other, Constant) and self.value == other.value

  def Substitute(self, name: str, expression: Expression) -> Expression:
    return self


class Literal(Expression):
  """A runtime literal within the expression tree."""

  def __init__(self, value: str):
    self.value = value

  def ToPerfJson(self):
    return self.value

  def ToPython(self):
    return f'Literal({self.value})'

  def Simplify(self) -> Expression:
    return self

  def Equals(self, other: Expression) -> bool:
    return isinstance(other, Literal) and self.value == other.value

  def Substitute(self, name: str, expression: Expression) -> Expression:
    return self


def min(lhs: Union[int, float, Expression], rhs: Union[int, float,
                                                       Expression]) -> Function:
  # pylint: disable=redefined-builtin
  # pylint: disable=invalid-name
  return Function('min', lhs, rhs)


def max(lhs: Union[int, float, Expression], rhs: Union[int, float,
                                                       Expression]) -> Function:
  # pylint: disable=redefined-builtin
  # pylint: disable=invalid-name
  return Function('max', lhs, rhs)


def d_ratio(lhs: Union[int, float, Expression],
            rhs: Union[int, float, Expression]) -> Function:
  # pylint: disable=redefined-builtin
  # pylint: disable=invalid-name
  return Function('d_ratio', lhs, rhs)


def source_count(event: Event) -> Function:
  # pylint: disable=redefined-builtin
  # pylint: disable=invalid-name
  return Function('source_count', event)


class Metric:
  """An individual metric that will specifiable on the perf command line."""
  groups: Set[str]
  expr: Expression
  scale_unit: str
  constraint: bool

  def __init__(self,
               name: str,
               description: str,
               expr: Expression,
               scale_unit: str,
               constraint: bool = False):
    self.name = name
    self.description = description
    self.expr = expr.Simplify()
    # Workraound valid_only_metric hiding certain metrics based on unit.
    scale_unit = scale_unit.replace('/sec', ' per sec')
    if scale_unit[0].isdigit():
      self.scale_unit = scale_unit
    else:
      self.scale_unit = f'1{scale_unit}'
    self.constraint = constraint
    self.groups = set()

  def __lt__(self, other):
    """Sort order."""
    return self.name < other.name

  def AddToMetricGroup(self, group):
    """Callback used when being added to a MetricGroup."""
    self.groups.add(group.name)

  def Flatten(self) -> Set['Metric']:
    """Return a leaf metric."""
    return set([self])

  def ToPerfJson(self) -> Dict[str, str]:
    """Return as dictionary for Json generation."""
    result = {
        'MetricName': self.name,
        'MetricGroup': ';'.join(sorted(self.groups)),
        'BriefDescription': self.description,
        'MetricExpr': self.expr.ToPerfJson(),
        'ScaleUnit': self.scale_unit
    }
    if self.constraint:
      result['MetricConstraint'] = 'NO_NMI_WATCHDOG'

    return result


class _MetricJsonEncoder(json.JSONEncoder):
  """Special handling for Metric objects."""

  def default(self, o):
    if isinstance(o, Metric):
      return o.ToPerfJson()
    return json.JSONEncoder.default(self, o)


class MetricGroup:
  """A group of metrics.

  Metric groups may be specificd on the perf command line, but within
  the json they aren't encoded. Metrics may be in multiple groups
  which can facilitate arrangements similar to trees.
  """

  def __init__(self, name: str, metric_list: List[Union[Metric,
                                                        'MetricGroup']]):
    self.name = name
    self.metric_list = metric_list
    for metric in metric_list:
      metric.AddToMetricGroup(self)

  def AddToMetricGroup(self, group):
    """Callback used when a MetricGroup is added into another."""
    for metric in self.metric_list:
      metric.AddToMetricGroup(group)

  def Flatten(self) -> Set[Metric]:
    """Returns a set of all leaf metrics."""
    result = set()
    for x in self.metric_list:
      result = result.union(x.Flatten())

    return result

  def ToPerfJson(self) -> str:
    return json.dumps(sorted(self.Flatten()), indent=2, cls=_MetricJsonEncoder)

  def __str__(self) -> str:
    return self.ToPerfJson()


class _RewriteIfExpToSelect(ast.NodeTransformer):
  """Transformer to convert if-else nodes to Select expressions."""

  def visit_IfExp(self, node):
    # pylint: disable=invalid-name
    self.generic_visit(node)
    call = ast.Call(
        func=ast.Name(id='Select', ctx=ast.Load()),
        args=[node.body, node.test, node.orelse],
        keywords=[])
    ast.copy_location(call, node.test)
    return call


def ParsePerfJson(orig: str) -> Expression:
  """A simple json metric expression decoder.

  Converts a json encoded metric expression by way of python's ast and
  eval routine. First tokens are mapped to Event calls, then
  accidentally converted keywords or literals are mapped to their
  appropriate calls. Python's ast is used to match if-else that can't
  be handled via operator overloading. Finally the ast is evaluated.

  Args:
    orig (str): String to parse.

  Returns:
    Expression: The parsed string.
  """
  # pylint: disable=eval-used
  py = orig.strip()
  py = re.sub(r'([a-zA-Z][^-+/\* \\\(\),]*(?:\\.[^-+/\* \\\(\),]*)*)',
              r'Event(r"\1")', py)
  py = re.sub(r'#Event\(r"([^"]*)"\)', r'Literal("#\1")', py)
  py = re.sub(r'([0-9]+)Event\(r"(e[0-9]+)"\)', r'\1\2', py)
  keywords = ['if', 'else', 'min', 'max', 'd_ratio', 'source_count']
  for kw in keywords:
    py = re.sub(rf'Event\(r"{kw}"\)', kw, py)

  try:
    parsed = ast.parse(py, mode='eval')
  except SyntaxError as e:
    raise SyntaxError(f'Parsing expression:\n{orig}') from e
  _RewriteIfExpToSelect().visit(parsed)
  parsed = ast.fix_missing_locations(parsed)
  return _Constify(eval(compile(parsed, orig, 'eval')))


def RewriteMetricsInTermsOfOthers(metrics: List[Tuple[str, str, Expression]]
                                  )-> Dict[Tuple[str, str], Expression]:
  """Shorten metrics by rewriting in terms of others.

  Args:
    metrics (list): pmus, metric names and their expressions.
  Returns:
    Dict: mapping from a pmu, metric name pair to a shortened expression.
  """
  updates: Dict[Tuple[str, str], Expression] = dict()
  for outer_pmu, outer_name, outer_expression in metrics:
    if outer_pmu is None:
      outer_pmu = 'cpu'
    updated = outer_expression
    while True:
      for inner_pmu, inner_name, inner_expression in metrics:
        if inner_pmu is None:
          inner_pmu = 'cpu'
        if inner_pmu.lower() != outer_pmu.lower():
          continue
        if inner_name.lower() == outer_name.lower():
          continue
        if (inner_pmu, inner_name) in updates:
          inner_expression = updates[(inner_pmu, inner_name)]
        updated = updated.Substitute(inner_name, inner_expression)
      if updated.Equals(outer_expression):
        break
      if (outer_pmu, outer_name) in updates and updated.Equals(updates[(outer_pmu, outer_name)]):
        break
      updates[(outer_pmu, outer_name)] = updated
  return updates
