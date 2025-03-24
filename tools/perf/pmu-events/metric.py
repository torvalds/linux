# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
"""Parse or generate representations of perf metrics."""

import ast
import decimal
import json
import re
from typing import Dict, List, Optional, Set, Tuple, Union


class Expression:
    """Abstract base class representing elements in a metric expression."""

    def to_perf_json(self) -> str:
        """Returns a perf JSON representation."""
        raise NotImplementedError()

    def to_python(self) -> str:
        """Returns a Python parseable representation."""
        raise NotImplementedError()

    def simplify(self):
        """Returns a simplified version of the expression."""
        raise NotImplementedError()

    def equals(self, other) -> bool:
        """Checks whether two expressions are equivalent."""
        raise NotImplementedError()

    def substitute(self, name: str, expression: 'Expression') -> 'Expression':
        raise NotImplementedError()

    def __str__(self) -> str:
        return self.to_perf_json()


def _constify(val: Union[bool, int, float, Expression]) -> Expression:
    """Ensure all nodes in the expression tree are Expression instances."""
    if isinstance(val, bool):
        return Constant(1 if val else 0)
    if isinstance(val, (int, float)):
        return Constant(val)
    return val


class Operator(Expression):
    """Represents a binary operator in the parse tree."""

    PRECEDENCE = {
        '|': 0, '^': 1, '&': 2, '<': 3, '>': 3,
        '+': 4, '-': 4, '*': 5, '/': 5, '%': 5,
    }

    def __init__(self, operator: str, lhs: Union[int, float, Expression], rhs: Union[int, float, Expression]):
        self.operator = operator
        self.lhs = _constify(lhs)
        self.rhs = _constify(rhs)

    def to_perf_json(self):
        """Convert expression to perf JSON format."""
        return f'{self.lhs.to_perf_json()} {self.operator} {self.rhs.to_perf_json()}'

    def to_python(self):
        """Convert expression to a Python-compatible representation."""
        return f'{self.lhs.to_python()} {self.operator} {self.rhs.to_python()}'

    def simplify(self) -> Expression:
        lhs = self.lhs.simplify()
        rhs = self.rhs.simplify()

        # Constant evaluation optimization
        if isinstance(lhs, Constant) and isinstance(rhs, Constant):
            try:
                return Constant(ast.literal_eval(f"{lhs.value} {self.operator} {rhs.value}"))
            except Exception as e:
                raise ValueError(f"Invalid constant operation: {lhs} {self.operator} {rhs}") from e

        # Simplify based on identity properties
        if isinstance(lhs, Constant):
            if self.operator in ('+', '|') and lhs.value == '0':
                return rhs
            if self.operator == '*' and lhs.value == '1':
                return rhs
            if self.operator == '*' and lhs.value == '0':
                return Constant(0)

        if isinstance(rhs, Constant):
            if self.operator in ('+', '|') and rhs.value == '0':
                return lhs
            if self.operator == '*' and rhs.value == '1':
                return lhs
            if self.operator == '*' and rhs.value == '0':
                return Constant(0)

        return Operator(self.operator, lhs, rhs)

    def equals(self, other: Expression) -> bool:
        """Check for expression equality."""
        return isinstance(other, Operator) and self.operator == other.operator and \
               self.lhs.equals(other.lhs) and self.rhs.equals(other.rhs)


class Constant(Expression):
    """Represents a constant value in the expression tree."""

    def __init__(self, value: Union[float, str]):
        try:
            ctx = decimal.Context()
            ctx.prec = 20
            dec = ctx.create_decimal(repr(value) if isinstance(value, float) else value)
            self.value = dec.normalize().to_eng_string().replace('+', '').replace('E', 'e')
        except Exception as e:
            raise ValueError(f"Invalid constant value: {value}") from e

    def to_perf_json(self):
        return self.value

    def to_python(self):
        return f'Constant({self.value})'

    def simplify(self) -> Expression:
        return self

    def equals(self, other: Expression) -> bool:
        return isinstance(other, Constant) and self.value == other.value


class Event(Expression):
    """Represents an event in the expression tree."""

    def __init__(self, name: str, legacy_name: str = ''):
        self.name = self._fix_escapes(name)
        self.legacy_name = self._fix_escapes(legacy_name)

    @staticmethod
    def _fix_escapes(s: str) -> str:
        """Fix escaped characters in event names."""
        s = re.sub(r'([^\\]),', r'\1\\,', s)
        return re.sub(r'([^\\])=', r'\1\\=', s)

    def to_perf_json(self):
        return self.name.replace('/', '@')

    def to_python(self):
        return f'Event(r"{self.name}")'

    def simplify(self) -> Expression:
        return self

    def equals(self, other: Expression) -> bool:
        return isinstance(other, Event) and self.name == other.name


def min(lhs: Union[int, float, Expression], rhs: Union[int, float, Expression]) -> Operator:
    """Perform a minimum operation."""
    return Operator('min', lhs, rhs)


def max(lhs: Union[int, float, Expression], rhs: Union[int, float, Expression]) -> Operator:
    """Perform a maximum operation."""
    return Operator('max', lhs, rhs)


def d_ratio(lhs: Union[int, float, Expression], rhs: Union[int, float, Expression]) -> Operator:
    """Calculate a ratio using a custom function."""
    if isinstance(rhs, Constant) and rhs.value == '0':
        return Constant(0)
    return Operator('d_ratio', lhs, rhs)


class Metric:
    """Represents a single performance metric."""

    def __init__(self, name: str, description: str, expr: Expression, scale_unit: str, constraint: bool = False):
        self.name = name
        self.description = description
        self.expr = expr.simplify()
        self.scale_unit = scale_unit.replace('/sec', ' per sec')
        self.constraint = constraint
        self.groups = set()

    def to_perf_json(self) -> Dict[str, str]:
        """Convert metric to a perf JSON object."""
        result = {
            'MetricName': self.name,
            'MetricGroup': ';'.join(sorted(self.groups)),
            'BriefDescription': self.description,
            'MetricExpr': self.expr.to_perf_json(),
            'ScaleUnit': self.scale_unit
        }
        if self.constraint:
            result['MetricConstraint'] = 'NO_NMI_WATCHDOG'
        return result
