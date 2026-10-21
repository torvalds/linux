#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Implementation based on
# Gerth, R., Peled, D., Vardi, M.Y., Wolper, P. (1996).
# Simple On-the-fly Automatic Verification of Linear Temporal Logic.
# https://doi.org/10.1007/978-0-387-34892-6_1
# With extra optimizations

import lark

# Grammar:
# 	ltl ::= opd | ( ltl ) | ltl binop ltl | unop ltl
#
# Operands (opd):
# 	true, false, user-defined names
#
# Unary Operators (unop):
#       always
#       eventually
#       next
#       not
#
# Binary Operators (binop):
#       until
#       and
#       or
#       imply
#       equivalent

GRAMMAR = r'''
start: assign+

assign: VARIABLE "=" _ltl

_ltl: _opd | binop | unop

_opd : VARIABLE
     | LITERAL
     | "(" _ltl ")"

unop: UNOP _ltl
UNOP: "always"
     | "eventually"
     | "next"
     | "not"

binop: _opd BINOP _ltl
BINOP: "until"
      | "and"
      | "or"
      | "imply"

VARIABLE: /[A-Z_][A-Z0-9_]*/
LITERAL: "true" | "false"

COMMENT: "#" /.*/ "\n"
%ignore COMMENT

%import common.WS
%ignore WS
'''

class LTLError(Exception):
    "Exception raised for malformed linear temporal logic"

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
    uid = 0

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
        if isinstance(self.op, (Literal, Variable)):
            return str(self.op)
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

class NextOp(UnaryOp):
    def normalize(self):
        return self

    def _is_temporal(self):
        return True

    def negate(self):
        # not (next A) == next (not A)
        self.child = self.child.negate()
        return self

    @staticmethod
    def expand(n: ASTNode, node: GraphNode, node_set) -> set[GraphNode]:
        tmp = GraphNode(node.incoming,
                        node.new,
                        node.old | {n},
                        node.next | {n.op.child})
        return tmp.expand(node_set)

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

    def __str__(self):
        return self.name.lower()

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
            if isinstance(f.op, NotOp) and f.op.child is n:
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

class Transform(lark.visitors.Transformer):
    def unop(self, node):
        if node[0] == "always":
            return ASTNode(AlwaysOp(node[1]))
        if node[0] == "eventually":
            return ASTNode(EventuallyOp(node[1]))
        if node[0] == "next":
            return ASTNode(NextOp(node[1]))
        if node[0] == "not":
            return ASTNode(NotOp(node[1]))
        raise ValueError("Unknown operator %s" % node[0])

    def binop(self, node):
        if node[1] == "until":
            return ASTNode(UntilOp(node[0], node[2]))
        if node[1] == "and":
            return ASTNode(AndOp(node[0], node[2]))
        if node[1] == "or":
            return ASTNode(OrOp(node[0], node[2]))
        if node[1] == "imply":
            return ASTNode(ImplyOp(node[0], node[2]))
        raise ValueError("Unknown operator %s" % node[1])

    def VARIABLE(self, args):
        return ASTNode(Variable(args))

    def LITERAL(self, args):
        return ASTNode(Literal(args == "true"))

    def start(self, node):
        return node

    def assign(self, node):
        return node[0].op.name, node[1]

parser = lark.Lark(GRAMMAR)

def parse_ltl(s: str) -> ASTNode:
    try:
        spec = parser.parse(s)
    except lark.exceptions.UnexpectedInput as e:
        raise LTLError(str(e))
    spec = Transform().transform(spec)

    rule = None
    subexpr = {}

    for assign in spec:
        if assign[0] == "RULE":
            rule = assign[1]
        else:
            subexpr[assign[0]] = assign[1]

    if rule is None:
        raise LTLError("Please define your specification in the \"RULE = <LTL spec>\" format")

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
