#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# Automata class: parse an automaton in dot file digraph format into a python object
#
# For further information, see:
#   Documentation/trace/rv/deterministic_automata.rst

import ntpath

import lark

class ParseTree:
    # based on https://graphviz.org/doc/info/lang.html
    # with the irrelevant stuffs (port and compass) removed
    grammar = r'''
    start: "strict"? ("graph" | "digraph") ID? "{" stmt_list "}"

    stmt_list: (stmt ";"? stmt_list)?

    stmt: node_stmt
        | edge_stmt
        | attr_stmt
        | ID "=" ID
        | subgraph

    attr_stmt: attr_type attr_list

    attr_type: "graph" -> graph
            | "node"  -> node
            | "edge"  -> edge

    attr_list: "[" a_list? "]" attr_list?

    a_list: ID "=" ID (";" | ",")? a_list?

    edge_stmt: (node_id | subgraph) edgerhs attr_list?

    edgerhs: edgeop (node_id | subgraph) edgerhs?

    edgeop: "->" | "--"

    node_stmt: node_id attr_list?

    node_id: ID

    subgraph: ("subgraph" ID?)? "{" stmt_list "}"

    ID: CNAME
      | /-?(\.[0-9]+|[0-9]+(\.[0-9]*))/
      | ESCAPED_STRING

    %import common.CNAME
    %import common.ESCAPED_STRING
    %import common.WS
    %ignore WS
    '''

    @staticmethod
    def parse_edge(tree: lark.Tree) -> tuple[str, str]:
        # only support a simple node-to-node edge
        nodes = []
        for node in tree.iter_subtrees_topdown():
            if node.data == "node_id":
                nodes.append(node.children[0].strip('"'))

        if len(nodes) != 2:
            raise AutomataError("Only state-to-state transition is supported")

        return tuple(nodes)

    class ParseNodes(lark.visitors.Visitor):
        def __init__(self, *args, **kwargs):
            self.nodes = set()
            super().__init__(*args, **kwargs)

        def node_stmt(self, tree):
            node_id = tree.children[0]
            node = node_id.children[0].strip('"')
            self.nodes.add(node)

    class ParseEdges(lark.visitors.Visitor):
        def __init__(self, *args, **kwargs):
            self.edges = set()
            super().__init__(*args, **kwargs)

        def edge_stmt(self, tree):
            edge = ParseTree.parse_edge(tree)
            self.edges.add(edge)

    class ParseAttributes(lark.visitors.Interpreter):
        def __init__(self, *args, **kwargs):
            '''
            Stacks of default attributes. [0] is the default
            attributes for the outermost scope, while [-1] is the
            default attributes for the current scope.
            '''
            self.default_node_attrs = [{}]
            self.default_edge_attrs = [{}]

            self.node_attrs = {}
            self.edge_attrs = {}

            super().__init__(*args, **kwargs)

        @staticmethod
        def __get_attrs(stmt: lark.Tree) -> dict[str, str]:
            attrs = {}

            for node in stmt.iter_subtrees():
                if node.data == "a_list":
                    attrs[node.children[0]] = node.children[1].strip('"')

            return attrs


        def subgraph(self, tree):
            # We are entering a new scope, inherit the default
            # attributes of the outer scope
            self.default_node_attrs.append(self.default_node_attrs[-1].copy())
            self.default_edge_attrs.append(self.default_edge_attrs[-1].copy())

            children = self.visit_children(tree)

            # Exiting the scope
            del self.default_node_attrs[-1]
            del self.default_edge_attrs[-1]

            return children

        def node_stmt(self, tree):
            node_id = tree.children[0]
            node = node_id.children[0].strip('"')

            attrs = self.default_node_attrs[-1].copy()
            attrs |= self.__get_attrs(tree)

            if attrs:
                if node in self.node_attrs:
                    self.node_attrs[node] = attrs | self.node_attrs[node]
                else:
                    self.node_attrs[node] = attrs

            return self.visit_children(tree)

        def edge_stmt(self, tree):
            edge = ParseTree.parse_edge(tree)

            attrs = self.default_edge_attrs[-1].copy()
            attrs |= self.__get_attrs(tree)

            if attrs:
                if edge in self.edge_attrs:
                    self.edge_attrs[edge] = attrs | self.edge_attrs[edge]
                else:
                    self.edge_attrs[edge] = attrs

            return self.visit_children(tree)

        def attr_stmt(self, tree):
            attr_type = tree.children[0].data
            attrs = self.__get_attrs(tree)

            if attr_type == "node":
                self.default_node_attrs[-1] |= attrs
            elif attr_type == "edge":
                self.default_edge_attrs[-1] |= attrs
            else:
                # graph attributes are irrelevant
                pass

            self.visit_children(tree)

    def __init__(self, dot_file):
        parser = lark.Lark(self.grammar, parser='lalr')
        node_parser = self.ParseNodes()
        edge_parser = self.ParseEdges()
        attributes_parser = self.ParseAttributes()

        try:
            with open(dot_file, "r") as f:
                tree = parser.parse(f.read())
                attributes_parser.visit(tree)
                node_parser.visit(tree)
                edge_parser.visit(tree)
        except OSError as exc:
            raise AutomataError(exc.strerror) from exc
        except lark.exceptions.UnexpectedInput as exc:
            raise AutomataError(str(exc))

        self.nodes = node_parser.nodes
        self.edges = edge_parser.edges
        self.node_attrs = attributes_parser.node_attrs
        self.edge_attrs = attributes_parser.edge_attrs

class ConstraintCondition:
    def __init__(self, env: str, op: str, val: str, unit=None):
        self.env = env
        self.op = op
        self.val = val
        self.unit = unit
        if unit is None:
            # try to infer unit from constants or parameters
            val_for_unit = val.lower().replace("()", "")
            if val_for_unit.endswith("_ns"):
                self.unit = "ns"
            if val_for_unit.endswith("_jiffies"):
                self.unit = "j"

class ConstraintRule:
    grammar = r'''
        rule: condition (OP condition)*

        OP: "&&" | "||"

        condition: ENV CMP_OP VAL UNIT?

        ENV: CNAME

        CMP_OP: "==" | "!=" | "<=" | "<" | ">=" | ">"

        VAL: /[0-9]+/
           | /[A-Z_]+\(\)/
           | /[A-Z_]+/
           | /[a-z_]+\(\)/
           | /[a-z_]+/

        UNIT: "ns" | "us" | "ms" | "s" | "j"
    '''

    def __init__(self, c: ConstraintCondition):
        '''
        A list of pairs of
          - the condition (e.g. is_constr_dl == 1)
          - the logical operator ("||" or "&&") combining this
            condition with the next one if it exists, otherwise None

        TODO: Perhaps use an abstract syntax tree instead, because
              this representation cannot capture precedence
        '''
        self.rules = [[c, None]]

    def chain(self, op: str, c: ConstraintCondition):
        self.rules[-1][1] = op
        self.rules.append([c, None])

class ConstraintReset:
    def __init__(self, env):
        self.env = env

class StateLabelParser:
    grammar = r'''
    label: CNAME ("\\n" condition)?

    %import common.CNAME
    %import common.WS
    %ignore WS
    ''' + ConstraintRule.grammar

    parser = lark.Lark(grammar, parser='lalr', start="label")

    def __init__(self, label: str):
        try:
            tree = self.parser.parse(label)
        except lark.exceptions.UnexpectedInput as exc:
            raise(AutomataError(f"Unrecognised state \"{label}\"\n{exc}"))

        self.state = tree.children[0]
        self.constraint = None

        if len(tree.children) == 2:
            self.constraint = ConstraintCondition(*tree.children[1].children)
            if self.constraint.op not in ("<", "<="):
                raise AutomataError("State constraints must be clock expirations like"
                                    f" clk<N ({label})")

class EventLabelParser:
    grammar = r'''
    events: event ("\\n" event)*

    event: name (";" guard)?

    guard: reset
         | rule
         | rule ";" reset
         | reset ";" rule

    name: CNAME

    reset: "reset" "(" ENV ")"

    %import common.CNAME
    %import common.WS
    %ignore WS
    ''' + ConstraintRule.grammar

    parser = lark.Lark(grammar, parser='lalr', start="events")

    class GetEvents(lark.visitors.Transformer):
        def guard(self, args):
            reset = None
            rule = None
            for arg in args:
                if arg.data == "reset":
                    reset = ConstraintReset(arg.children[0])
                elif arg.data == "rule":
                    conditions = arg.children
                    rule = ConstraintRule(conditions[0])
                    for i in range(1, len(conditions), 2):
                        rule.chain(conditions[i], conditions[i + 1])
            return reset, rule

        def OP(self, args):
            return args

        def condition(self, args):
            return ConstraintCondition(*args)

        def event(self, args):
            assert(len(args) <= 2)
            name = args[0]
            rule, reset = None, None
            if len(args) == 2:
                reset, rule = args[1]
            return name, reset, rule

        def events(self, args):
            return args

        def name(self, args):
            return args[0]

    def __init__(self, label: str):
        try:
            tree = self.parser.parse(label)
            self.events = self.GetEvents().transform(tree)
        except lark.exceptions.UnexpectedInput as exc:
            raise(AutomataError(f"Unrecognised event \"{label}\"\n{exc}"))

class Transition:
    def __init__(self, src: str, dst: str, event: str,
                 reset: ConstraintReset, rule: ConstraintRule):
        self.src = src
        self.dst = dst
        self.event = event
        self.rule = rule
        self.reset = reset

class State:
    def __init__(self, name: str, inv: ConstraintCondition):
        self.name = name
        self.inv = inv

class AutomataError(Exception):
    """Exception raised for errors in automata parsing and validation.

    Raised when DOT file processing fails due to invalid format, I/O errors,
    or malformed automaton definitions.
    """

class Automata:
    """Automata class: Reads a dot file and parses it as an automaton.

    It supports both deterministic and hybrid automata.

    Attributes:
        dot_file: A dot file with an state_automaton definition.
    """

    invalid_state_str = "INVALID_STATE"
    init_marker = "__init_"

    def __init__(self, file_path, model_name=None):
        self.__dot_path = file_path
        self.name = model_name or self.__get_model_name()
        self.__parse_tree = ParseTree(file_path)
        self.transitions = self.__parse_transitions()
        self.states, self.initial_state, self.final_states = self.__parse_states()
        self.env_types = {}
        self.env_stored = set()
        self.constraint_vars = set()
        self.self_loop_reset_events = set()
        self.events, self.envs = self.__get_event_variables()
        self.function = self.__create_matrix()
        self.events_start, self.events_start_run = self.__store_init_events()
        self.env_stored = sorted(self.env_stored)
        self.constraint_vars = sorted(self.constraint_vars)
        self.self_loop_reset_events = sorted(self.self_loop_reset_events)

    def __get_model_name(self) -> str:
        basename = ntpath.basename(self.__dot_path)
        if not basename.endswith(".dot") and not basename.endswith(".gv"):
            print("not a dot file")
            raise AutomataError(f"not a dot file: {self.__dot_path}")

        model_name = ntpath.splitext(basename)[0]
        if not model_name:
            raise AutomataError(f"not a dot file: {self.__dot_path}")

        return model_name

    def __parse_transitions(self):
        transitions = []

        for edge in self.__parse_tree.edges:
            attr = self.__parse_tree.edge_attrs.get(edge)
            if not attr:
                continue

            label = attr.get("label")

            src, dst = edge

            parser = EventLabelParser(label)
            for event, reset, rule in parser.events:
                transitions.append(Transition(src, dst, event, reset, rule))

        transitions.sort(key=lambda t : (t.src, t.event))
        return transitions

    def __parse_states(self):
        initial_state = ""
        states = []
        final_states = []

        for node in self.__parse_tree.nodes:
            attr = self.__parse_tree.node_attrs[node]
            label = attr.get("label")

            if node.startswith(Automata.init_marker):
                initial_state = node[len(Automata.init_marker):]

            if not label:
                continue

            parser = StateLabelParser(label)
            state = State(parser.state, parser.constraint)

            states.append(state)

            shape = attr.get("shape")
            if shape in ("doublecircle", "ellipse"):
                final_states.append(state)


        initial_state = next((s for s in states if s.name == initial_state), None)
        if not initial_state:
            raise AutomataError("The automaton doesn't have an initial state")

        if not final_states:
            final_states.append(initial_state)

        states.remove(initial_state)
        states.sort(key=lambda s : s.name)
        states.insert(0, initial_state)
        return states, initial_state, final_states

    def __get_event_variables(self) -> tuple[list[str], list[str]]:
        events: list[str] = []
        envs: list[str] = []

        for transition in self.transitions:
            events.append(transition.event)

            if transition.reset:
                envs.append(transition.reset.env)
                self.env_stored.add(transition.reset.env)
            if transition.rule:
                for c, _ in transition.rule.rules:
                    envs.append(c.env)
                    self.__extract_env_var(c)

        for state in self.states:
            if state.inv:
                envs.append(state.inv.env)
                self.__extract_env_var(state.inv)

        return sorted(set(events)), sorted(set(envs))

    def __extract_env_var(self, constraint: ConstraintCondition):
        if constraint.unit:
            self.env_types[constraint.env] = constraint.unit
        if constraint.val[0].isalpha():
            self.constraint_vars.add(constraint.val)

    def __create_matrix(self) -> list[list[str]]:
        # transform the array into a dictionary
        events = self.events
        states = [s.name for s in self.states]
        events_dict = {}
        states_dict = {}
        nr_event = 0
        for event in events:
            events_dict[event] = nr_event
            nr_event += 1

        nr_state = 0
        for state in states:
            states_dict[state] = nr_state
            nr_state += 1

        # declare the matrix....
        matrix = [[self.invalid_state_str for _ in range(nr_event)] for _ in range(nr_state)]

        for transition in self.transitions:
            src, dst = transition.src, transition.dst
            event = transition.event
            if src == dst and transition.reset:
                # those events reset also on self loops
                self.self_loop_reset_events.add(event)
            matrix[states_dict[src]][events_dict[event]] = dst

        return matrix

    def __store_init_events(self) -> tuple[list[bool], list[bool]]:
        events_start = [False] * len(self.events)
        events_start_run = [False] * len(self.events)
        for i in range(len(self.events)):
            curr_event_will_init = 0
            curr_event_from_init = False
            curr_event_used = 0
            for j in range(len(self.states)):
                if self.function[j][i] != self.invalid_state_str:
                    curr_event_used += 1
                if self.function[j][i] == self.initial_state.name:
                    curr_event_will_init += 1
            if self.function[0][i] != self.invalid_state_str:
                curr_event_from_init = True
            # this event always leads to init
            if curr_event_will_init and curr_event_used == curr_event_will_init:
                events_start[i] = True
            # this event is only called from init
            if curr_event_from_init and curr_event_used == 1:
                events_start_run[i] = True
        return events_start, events_start_run

    def is_start_event(self, event: str) -> bool:
        return self.events_start[self.events.index(event)]

    def is_start_run_event(self, event: str) -> bool:
        # prefer handle_start_event if there
        if any(self.events_start):
            return False
        return self.events_start_run[self.events.index(event)]

    def is_hybrid_automata(self) -> bool:
        return bool(self.envs)
