#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Implementation based on
# Gerth, R., Peled, D., Vardi, M.Y., Wolper, P. (1996).
# Simple On-the-fly Automatic Verification of Linear Temporal Logic.
# https://doi.org/10.1007/978-0-387-34892-6_1
# With extra optimizations

from ply.lex import lex
from ply.yacc import yacc

# Grammar:
# 	ltl ::= opd | ( ltl ) | ltl binop ltl | unop ltl
#
# Operands (opd):
# 	true, false, user-defined names
#
# Unary Operators (unop):
#       always
#       eventually
#       not
#
# Binary Operators (binop):
#       until
#       and
#       or
#       imply
#       equivalent

tokens = (
   'AND',
   'OR',
   'IMPLY',
   'UNTIL',
   'ALWAYS',
   'EVENTUALLY',
   'VARIABLE',
   'LITERAL',
   'NOT',
   'LPAREN',
   'RPAREN',
   'ASSIGN',
)

t_AND = r'and'
t_OR = r'or'
t_IMPLY = r'imply'
t_UNTIL = r'until'
t_ALWAYS = r'always'
t_EVENTUALLY = r'eventually'
t_VARIABLE = r'[A-Z_0-9]+'
t_LITERAL = r'true|false'
t_NOT = r'not'
t_LPAREN = r'\('
t_RPAREN = r'\)'
t_ASSIGN = r'='
t_ignore_COMMENT = r'\#.*'
t_ignore = ' \t\n'

def t_error(t):
    raise ValueError(f"Illegal character '{t.value[0]}'")

lexer = lex()

class GraphNode:
    uid = 0

    def __init__(self, incoming: set['GraphNode'], new, old, _next):
        self.init = False
        self.outgoing = set()
        self.labels = set()
        self.incoming = incoming.copy()
        self.new = new.copy()
        self.old = old.copy()
        self.next = _next.copy()
        self.id = GraphNode.uid
        GraphNode.uid += 1

    def expand(self, node_set):
        if not self.new:
            for nd in node_set:
                if nd.old == self.old and nd.next == self.next:
                    nd.incoming |= self.incoming
                    return node_set

            new_current_node = GraphNode({self}, self.next, set(), set())
            return new_current_node.expand({self} | node_set)
        n = self.new.pop()
        return n.expand(self, node_set)

    def __lt__(self, other):
        return self.id < other.id

class ASTNode:
    uid = 1

    def __init__(self, op):
        self.op = op
        self.id = ASTNode.uid
        ASTNode.uid += 1

    def __hash__(self):
        return hash(self.op)

    def __eq__(self, other):
        return self is other

    def __iter__(self):
        yield self
        yield from self.op

    def negate(self):
        self.op = self.op.negate()
        return self

    def expand(self, node, node_set):
        return self.op.expand(self, node, node_set)

    def __str__(self):
        if isinstance(self.op, Literal):
            return str(self.op.value)
        if isinstance(self.op, Variable):
            return self.op.name.lower()
        return "val" + str(self.id)

    def normalize(self):
        # Get rid of:
        #   - ALWAYS
        #   - EVENTUALLY
        #   - IMPLY
        # And move all the NOT to be inside
        self.op = self.op.normalize()
        return self

class BinaryOp:
    op_str = "not_supported"

    def __init__(self, left: ASTNode, right: ASTNode):
        self.left = left
        self.right = right

    def __hash__(self):
        return hash((self.left, self.right))

    def __iter__(self):
        yield from self.left
        yield from self.right

    def normalize(self):
        raise NotImplementedError

    def negate(self):
        raise NotImplementedError

    def _is_temporal(self):
        raise NotImplementedError

    def is_temporal(self):
        if self.left.op.is_temporal():
            return True
        if self.right.op.is_temporal():
            return True
        return self._is_temporal()

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        raise NotImplementedError

class AndOp(BinaryOp):
    op_str = '&&'

    def normalize(self):
        return self

    def negate(self):
        return OrOp(self.left.negate(), self.right.negate())

    def _is_temporal(self):
        return False

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        if not n.op.is_temporal():
            node.old.add(n)
            return node.expand(node_set)

        tmp = GraphNode(node.incoming,
                        node.new | ({n.op.left, n.op.right} - node.old),
                        node.old | {n},
                        node.next)
        return tmp.expand(node_set)

class OrOp(BinaryOp):
    op_str = '||'

    def normalize(self):
        return self

    def negate(self):
        return AndOp(self.left.negate(), self.right.negate())

    def _is_temporal(self):
        return False

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        if not n.op.is_temporal():
            node.old |= {n}
            return node.expand(node_set)

        node1 = GraphNode(node.incoming,
                          node.new | ({n.op.left} - node.old),
                          node.old | {n},
                          node.next)
        node2 = GraphNode(node.incoming,
                          node.new | ({n.op.right} - node.old),
                          node.old | {n},
                          node.next)
        return node2.expand(node1.expand(node_set))

class UntilOp(BinaryOp):
    def normalize(self):
        return self

    def negate(self):
        return VOp(self.left.negate(), self.right.negate())

    def _is_temporal(self):
        return True

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        node1 = GraphNode(node.incoming,
                          node.new | ({n.op.left} - node.old),
                          node.old | {n},
                          node.next | {n})
        node2 = GraphNode(node.incoming,
                          node.new | ({n.op.right} - node.old),
                          node.old | {n},
                          node.next)
        return node2.expand(node1.expand(node_set))

class VOp(BinaryOp):
    def normalize(self):
        return self

    def negate(self):
        return UntilOp(self.left.negate(), self.right.negate())

    def _is_temporal(self):
        return True

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        node1 = GraphNode(node.incoming,
                          node.new | ({n.op.right} - node.old),
                          node.old | {n},
                          node.next | {n})
        node2 = GraphNode(node.incoming,
                          node.new | ({n.op.left, n.op.right} - node.old),
                          node.old | {n},
                          node.next)
        return node2.expand(node1.expand(node_set))

class ImplyOp(BinaryOp):
    def normalize(self):
        # P -> Q === !P | Q
        return OrOp(self.left.negate(), self.right)

    def _is_temporal(self):
        return False

    def negate(self):
        # !(P -> Q) === !(!P | Q) === P & !Q
        return AndOp(self.left, self.right.negate())

class UnaryOp:
    def __init__(self, child: ASTNode):
        self.child = child

    def __iter__(self):
        yield from self.child

    def __hash__(self):
        return hash(self.child)

    def normalize(self):
        raise NotImplementedError

    def _is_temporal(self):
        raise NotImplementedError

    def is_temporal(self):
        if self.child.op.is_temporal():
            return True
        return self._is_temporal()

    def negate(self):
        raise NotImplementedError

class EventuallyOp(UnaryOp):
    def __str__(self):
        return "eventually " + str(self.child)

    def normalize(self):
        # <>F == true U F
        return UntilOp(ASTNode(Literal(True)), self.child)

    def _is_temporal(self):
        return True

    def negate(self):
        # !<>F == [](!F)
        return AlwaysOp(self.child.negate()).normalize()

class AlwaysOp(UnaryOp):
    def normalize(self):
        # []F === !(true U !F) == false V F
        new = ASTNode(Literal(False))
        return VOp(new, self.child)

    def _is_temporal(self):
        return True

    def negate(self):
        # ![]F == <>(!F)
        return EventuallyOp(self.child.negate()).normalize()

class NotOp(UnaryOp):
    def __str__(self):
        return "!" + str(self.child)

    def normalize(self):
        return self.child.op.negate()

    def negate(self):
        return self.child.op

    def _is_temporal(self):
        return False

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        for f in node.old:
            if n.op.child is f:
                return node_set
        node.old |= {n}
        return node.expand(node_set)

class Variable:
    def __init__(self, name: str):
        self.name = name

    def __hash__(self):
        return hash(self.name)

    def __iter__(self):
        yield from ()

    def negate(self):
        new = ASTNode(self)
        return NotOp(new)

    def normalize(self):
        return self

    def is_temporal(self):
        return False

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        for f in node.old:
            if isinstance(f, NotOp) and f.op.child is n:
                return node_set
        node.old |= {n}
        return node.expand(node_set)

class Literal:
    def __init__(self, value: bool):
        self.value = value

    def __iter__(self):
        yield from ()

    def __hash__(self):
        return hash(self.value)

    def __str__(self):
        if self.value:
            return "true"
        return "false"

    def negate(self):
        self.value = not self.value
        return self

    def normalize(self):
        return self

    def is_temporal(self):
        return False

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        if not n.op.value:
            return node_set
        node.old |= {n}
        return node.expand(node_set)

def p_spec(p):
    '''
    spec : assign
         | assign spec
    '''
    if len(p) == 3:
        p[2].append(p[1])
        p[0] = p[2]
    else:
        p[0] = [p[1]]

def p_assign(p):
    '''
    assign : VARIABLE ASSIGN ltl
    '''
    p[0] = (p[1], p[3])

def p_ltl(p):
    '''
    ltl : opd
        | binop
        | unop
    '''
    p[0] = p[1]

def p_opd(p):
    '''
    opd : VARIABLE
        | LITERAL
        | LPAREN ltl RPAREN
    '''
    if p[1] == "true":
        p[0] = ASTNode(Literal(True))
    elif p[1] == "false":
        p[0] = ASTNode(Literal(False))
    elif p[1] == '(':
        p[0] = p[2]
    else:
        p[0] = ASTNode(Variable(p[1]))

def p_unop(p):
    '''
    unop : ALWAYS ltl
         | EVENTUALLY ltl
         | NOT ltl
    '''
    if p[1] == "always":
        op = AlwaysOp(p[2])
    elif p[1] == "eventually":
        op = EventuallyOp(p[2])
    elif p[1] == "not":
        op = NotOp(p[2])
    else:
        raise ValueError(f"Invalid unary operator {p[1]}")

    p[0] = ASTNode(op)

def p_binop(p):
    '''
    binop : opd UNTIL ltl
          | opd AND ltl
          | opd OR ltl
          | opd IMPLY ltl
    '''
    if p[2] == "and":
        op = AndOp(p[1], p[3])
    elif p[2] == "until":
        op = UntilOp(p[1], p[3])
    elif p[2] == "or":
        op = OrOp(p[1], p[3])
    elif p[2] == "imply":
        op = ImplyOp(p[1], p[3])
    else:
        raise ValueError(f"Invalid binary operator {p[2]}")

    p[0] = ASTNode(op)

parser = yacc()

def parse_ltl(s: str) -> ASTNode:
    spec = parser.parse(s)

    rule = None
    subexpr = {}

    for assign in spec:
        if assign[0] == "RULE":
            rule = assign[1]
        else:
            subexpr[assign[0]] = assign[1]

    if rule is None:
        raise ValueError("Please define your specification in the \"RULE = <LTL spec>\" format")

    for node in rule:
        if not isinstance(node.op, Variable):
            continue
        replace = subexpr.get(node.op.name)
        if replace is not None:
            node.op = replace.op

    return rule

def create_graph(s: str):
    atoms = set()

    ltl = parse_ltl(s)
    for c in ltl:
        c.normalize()
        if isinstance(c.op, Variable):
            atoms.add(c.op.name)

    init = GraphNode(set(), set(), set(), set())
    head = GraphNode({init}, {ltl}, set(), set())
    graph = sorted(head.expand(set()))

    for i, node in enumerate(graph):
        # The id assignment during graph generation has gaps. Reassign them
        node.id = i

        for incoming in node.incoming:
            if incoming is init:
                node.init = True
            else:
                incoming.outgoing.add(node)
        for o in node.old:
            if not o.op.is_temporal():
                node.labels.add(str(o))

    return sorted(atoms), graph, ltl
