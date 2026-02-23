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
