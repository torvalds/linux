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
import re
from typing import Iterator
from itertools import islice

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

class _ConstraintKey:
    """Base class for constraint keys."""

class _StateConstraintKey(_ConstraintKey, int):
    """Key for a state constraint. Under the hood just state_id."""
    def __new__(cls, state_id: int):
        return super().__new__(cls, state_id)

class _EventConstraintKey(_ConstraintKey, tuple):
    """Key for an event constraint. Under the hood just tuple(state_id,event_id)."""
    def __new__(cls, state_id: int, event_id: int):
        return super().__new__(cls, (state_id, event_id))

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
    node_marker = "{node"
    # val can be numerical, uppercase (constant or macro), lowercase (parameter or function)
    # only numerical values should have units
    constraint_rule = re.compile(r"""
        ^
        (?P<env>[a-zA-Z_][a-zA-Z0-9_]+)  # C-like identifier for the env var
        (?P<op>[!<=>]{1,2})              # operator
        (?P<val>
            [0-9]+ |                     # numerical value
            [A-Z_]+\(\) |                # macro
            [A-Z_]+ |                    # constant
            [a-z_]+\(\) |                # function
            [a-z_]+                      # parameter
        )
        (?P<unit>[a-z]{1,2})?            # optional unit for numerical values
        """, re.VERBOSE)
    constraint_reset = re.compile(r"^reset\((?P<env>[a-zA-Z_][a-zA-Z0-9_]+)\)")

    def __init__(self, file_path, model_name=None):
        self.__dot_path = file_path
        self.name = model_name or self.__get_model_name()
        self.__dot_lines = self.__open_dot()
        self.__parse_tree = ParseTree(file_path)
        self.transitions = self.__parse_transitions()
        self._states, self._initial_state, self._final_states = self.__parse_states()
        self.states, self.initial_state, self.final_states = self.__get_state_variables()
        self.env_types = {}
        self.env_stored = set()
        self.constraint_vars = set()
        self.self_loop_reset_events = set()
        self.events, self.envs = self.__get_event_variables()
        self.function, self.constraints = self.__create_matrix()
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

    def __open_dot(self) -> list[str]:
        dot_lines = []
        try:
            with open(self.__dot_path) as dot_file:
                dot_lines = dot_file.readlines()
        except OSError as exc:
            raise AutomataError(exc.strerror) from exc

        if not dot_lines:
            raise AutomataError(f"{self.__dot_path} is empty")

        # checking the first line:
        line = dot_lines[0].split()

        if len(line) < 2 or line[0] != "digraph" or line[1] != "state_automaton":
            raise AutomataError(f"Not a valid .dot format: {self.__dot_path}")

        return dot_lines

    def __get_cursor_begin_states(self) -> int:
        for cursor, line in enumerate(self.__dot_lines):
            split_line = line.split()

            if len(split_line) and split_line[0] == self.node_marker:
                return cursor

        raise AutomataError("Could not find a beginning state")

    def __get_cursor_begin_events(self) -> int:
        state = 0
        cursor = 0 # make pyright happy

        for cursor, line in enumerate(self.__dot_lines):
            line = line.split()
            if not line:
                continue

            if state == 0:
                if line[0] == self.node_marker:
                    state = 1
            elif line[0] != self.node_marker:
                break
        else:
            raise AutomataError("Could not find beginning event")

        cursor += 1 # skip initial state transition
        if cursor == len(self.__dot_lines):
            raise AutomataError("Dot file ended after event beginning")

        return cursor

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

    def __get_state_variables(self) -> tuple[list[str], str, list[str]]:
        # wait for node declaration
        states = []
        final_states = []
        initial_state = ""

        has_final_states = False
        cursor = self.__get_cursor_begin_states()

        # process nodes
        for line in islice(self.__dot_lines, cursor, None):
            split_line = line.split()
            if not split_line or split_line[0] != self.node_marker:
                break

            raw_state = split_line[-1]

            #  "enabled_fired"}; -> enabled_fired
            state = raw_state.replace('"', '').replace('};', '').replace(',', '_')
            if state.startswith(self.init_marker):
                initial_state = state[len(self.init_marker):]
            else:
                states.append(state)
                if "doublecircle" in line:
                    final_states.append(state)
                    has_final_states = True

                if "ellipse" in line:
                    final_states.append(state)
                    has_final_states = True

        if not initial_state:
            raise AutomataError("The automaton doesn't have an initial state")

        states = sorted(set(states))
        states.remove(initial_state)

        # Insert the initial state at the beginning of the states
        states.insert(0, initial_state)

        if not has_final_states:
            final_states.append(initial_state)

        return states, initial_state, final_states

    def __get_event_variables(self) -> tuple[list[str], list[str]]:
        events: list[str] = []
        envs: list[str] = []
        # here we are at the begin of transitions, take a note, we will return later.
        cursor = self.__get_cursor_begin_events()

        for line in map(str.lstrip, islice(self.__dot_lines, cursor, None)):
            if not line.startswith('"'):
                break

            # transitions have the format:
            # "all_fired" -> "both_fired" [ label = "disable_irq" ];
            #  ------------ event is here ------------^^^^^
            split_line = line.split()
            if len(split_line) > 1 and split_line[1] == "->":
                event = "".join(split_line[split_line.index("label") + 2:-1]).replace('"', '')

                # when a transition has more than one label, they are like this
                # "local_irq_enable\nhw_local_irq_enable_n"
                # so split them.

                for i in event.split("\\n"):
                    # if the event contains a constraint (hybrid automata),
                    # it will be separated by a ";":
                    # "sched_switch;x<1000;reset(x)"
                    ev, *constr = i.split(";")
                    if constr:
                        if len(constr) > 2:
                            raise AutomataError("Only 1 constraint and 1 reset are supported")
                        envs += self.__extract_env_var(constr)
                    events.append(ev)
            else:
                # state labels have the format:
                # "enable_fired" [label = "enable_fired\ncondition"];
                #  ----- label is here -----^^^^^
                # label and node name must be the same, condition is optional
                state = line.split("label")[1].split('"')[1]
                _, *constr = state.split("\\n")
                if constr:
                    if len(constr) > 1:
                        raise AutomataError("Only 1 constraint is supported in the state")
                    envs += self.__extract_env_var([constr[0].replace(" ", "")])

        return sorted(set(events)), sorted(set(envs))

    def _split_constraint_expr(self, constr: list[str]) -> Iterator[tuple[str,
                                                                          str | None]]:
        """
        Get a list of strings of the type constr1 && constr2 and returns a list of
        constraints and separators: [[constr1,"&&"],[constr2,None]]
        """
        exprs = []
        seps = []
        for c in constr:
            while "&&" in c or "||" in c:
                a = c.find("&&")
                o = c.find("||")
                pos = a if o < 0 or 0 < a < o else o
                exprs.append(c[:pos].replace(" ", ""))
                seps.append(c[pos:pos + 2].replace(" ", ""))
                c = c[pos + 2:].replace(" ", "")
            exprs.append(c)
            seps.append(None)
        return zip(exprs, seps)

    def __extract_env_var(self, constraint: list[str]) -> list[str]:
        env = []
        for c, _ in self._split_constraint_expr(constraint):
            rule = self.constraint_rule.search(c)
            reset = self.constraint_reset.search(c)
            if rule:
                env.append(rule["env"])
                if rule.groupdict().get("unit"):
                    self.env_types[rule["env"]] = rule["unit"]
                if rule["val"][0].isalpha():
                    self.constraint_vars.add(rule["val"])
                # try to infer unit from constants or parameters
                val_for_unit = rule["val"].lower().replace("()", "")
                if val_for_unit.endswith("_ns"):
                    self.env_types[rule["env"]] = "ns"
                if val_for_unit.endswith("_jiffies"):
                    self.env_types[rule["env"]] = "j"
            if reset:
                env.append(reset["env"])
                # environment variables that are reset need a storage
                self.env_stored.add(reset["env"])
        return env

    def __create_matrix(self) -> tuple[list[list[str]], dict[_ConstraintKey, list[str]]]:
        # transform the array into a dictionary
        events = self.events
        states = self.states
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
        constraints: dict[_ConstraintKey, list[str]] = {}

        # and we are back! Let's fill the matrix
        cursor = self.__get_cursor_begin_events()

        for line in map(str.lstrip,
                        islice(self.__dot_lines, cursor, None)):

            if not line or line[0] != '"':
                break

            split_line = line.split()

            if len(split_line) > 2 and split_line[1] == "->":
                origin_state = split_line[0].replace('"', '').replace(',', '_')
                dest_state = split_line[2].replace('"', '').replace(',', '_')
                possible_events = "".join(split_line[split_line.index("label") + 2:-1]).replace('"', '')
                for event in possible_events.split("\\n"):
                    event, *constr = event.split(";")
                    if constr:
                        key = _EventConstraintKey(states_dict[origin_state], events_dict[event])
                        constraints[key] = constr
                        # those events reset also on self loops
                        if origin_state == dest_state and "reset" in "".join(constr):
                            self.self_loop_reset_events.add(event)
                    matrix[states_dict[origin_state]][events_dict[event]] = dest_state
            else:
                state = line.split("label")[1].split('"')[1]
                state, *constr = state.replace(" ", "").split("\\n")
                if constr:
                    constraints[_StateConstraintKey(states_dict[state])] = constr

        return matrix, constraints

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
                if self.function[j][i] == self.initial_state:
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

    def is_event_constraint(self, key: _ConstraintKey) -> bool:
        """
        Given the key in self.constraints return true if it is an event
        constraint, false if it is a state constraint
        """
        return isinstance(key, _EventConstraintKey)
