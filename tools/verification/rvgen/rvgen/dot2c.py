#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# dot2c: parse an automaton in dot file digraph format into a C
#
# This program was written in the development of this paper:
#  de Oliveira, D. B. and Cucinotta, T. and de Oliveira, R. S.
#  "Efficient Formal Verification for the Linux Kernel." International
#  Conference on Software Engineering and Formal Methods. Springer, Cham, 2019.
#
# For further information, see:
#   Documentation/trace/rv/deterministic_automata.rst

from .automata import Automata, AutomataError

class Dot2c(Automata):
    enum_suffix = ""
    enum_states_def = "states"
    enum_events_def = "events"
    enum_envs_def = "envs"
    struct_automaton_def = "automaton"
    var_automaton_def = "aut"

    def __init__(self, file_path, model_name=None):
        super().__init__(file_path, model_name)
        self.line_length = 100

    def __get_enum_states_content(self) -> list[str]:
        buff = []
        buff.append(f"\t{self.initial_state}{self.enum_suffix},")
        for state in self.states:
            if state != self.initial_state:
                buff.append(f"\t{state}{self.enum_suffix},")
        buff.append(f"\tstate_max{self.enum_suffix},")

        return buff

    def format_states_enum(self) -> list[str]:
        buff = []
        buff.append(f"enum {self.enum_states_def} {{")
        buff += self.__get_enum_states_content()
        buff.append("};\n")

        return buff

    def __get_enum_events_content(self) -> list[str]:
        buff = []
        for event in self.events:
            buff.append(f"\t{event}{self.enum_suffix},")

        buff.append(f"\tevent_max{self.enum_suffix},")

        return buff

    def format_events_enum(self) -> list[str]:
        buff = []
        buff.append(f"enum {self.enum_events_def} {{")
        buff += self.__get_enum_events_content()
        buff.append("};\n")

        return buff

    def __get_non_stored_envs(self) -> list[str]:
        return [e for e in self.envs if e not in self.env_stored]

    def __get_enum_envs_content(self) -> list[str]:
        buff = []
        # We first place env variables that have a u64 storage.
        # Those are limited by MAX_HA_ENV_LEN, other variables
        # are read only and don't require a storage.
        unstored = self.__get_non_stored_envs()
        for env in list(self.env_stored) + unstored:
            buff.append(f"\t{env}{self.enum_suffix},")

        buff.append(f"\tenv_max{self.enum_suffix},")
        max_stored = unstored[0] if len(unstored) else "env_max"
        buff.append(f"\tenv_max_stored{self.enum_suffix} = {max_stored}{self.enum_suffix},")

        return buff

    def format_envs_enum(self) -> list[str]:
        buff = []
        if self.is_hybrid_automata():
            buff.append(f"enum {self.enum_envs_def} {{")
            buff += self.__get_enum_envs_content()
            buff.append("};\n")
            buff.append(f"_Static_assert(env_max_stored{self.enum_suffix} <= MAX_HA_ENV_LEN,"
                        ' "Not enough slots");')
            if {"ns", "us", "ms", "s"}.intersection(self.env_types.values()):
                buff.append("#define HA_CLK_NS")
            buff.append("")
        return buff

    def get_minimun_type(self) -> str:
        min_type = "unsigned char"

        if len(self.states) > 255:
            min_type = "unsigned short"

        if len(self.states) > 65535:
            min_type = "unsigned int"

        if len(self.states) > 1000000:
            raise AutomataError(f"Too many states: {len(self.states)}")

        return min_type

    def format_automaton_definition(self) -> list[str]:
        min_type = self.get_minimun_type()
        buff = []
        buff.append(f"struct {self.struct_automaton_def} {{")
        buff.append(f"\tchar *state_names[state_max{self.enum_suffix}];")
        buff.append(f"\tchar *event_names[event_max{self.enum_suffix}];")
        if self.is_hybrid_automata():
            buff.append(f"\tchar *env_names[env_max{self.enum_suffix}];")
        buff.append(f"\t{min_type} function[state_max{self.enum_suffix}][event_max{self.enum_suffix}];")
        buff.append(f"\t{min_type} initial_state;")
        buff.append(f"\tbool final_states[state_max{self.enum_suffix}];")
        buff.append("};\n")
        return buff

    def format_aut_init_header(self) -> list[str]:
        buff = []
        buff.append(f"static const struct {self.struct_automaton_def} {self.var_automaton_def} = {{")
        return buff

    def __get_string_vector_per_line_content(self, entries: list[str]) -> str:
        buff = []
        for entry in entries:
            buff.append(f"\t\t\"{entry}\",")
        return "\n".join(buff)

    def format_aut_init_events_string(self) -> list[str]:
        buff = []
        buff.append("\t.event_names = {")
        buff.append(self.__get_string_vector_per_line_content(self.events))
        buff.append("\t},")
        return buff

    def format_aut_init_states_string(self) -> list[str]:
        buff = []
        buff.append("\t.state_names = {")
        buff.append(self.__get_string_vector_per_line_content(self.states))
        buff.append("\t},")

        return buff

    def format_aut_init_envs_string(self) -> list[str]:
        buff = []
        if self.is_hybrid_automata():
            buff.append("\t.env_names = {")
            # maintain consistent order with the enum
            ordered_envs = list(self.env_stored) + self.__get_non_stored_envs()
            buff.append(self.__get_string_vector_per_line_content(ordered_envs))
            buff.append("\t},")

        return buff

    def __get_max_strlen_of_states(self) -> int:
        max_state_name = len(max(self.states, key=len))
        return max(max_state_name, len(self.invalid_state_str))

    def get_aut_init_function(self) -> str:
        nr_states = len(self.states)
        nr_events = len(self.events)
        buff = []

        maxlen = self.__get_max_strlen_of_states() + len(self.enum_suffix)
        tab_braces = 2 * 8 + 2 + 1  # "\t\t{ " ... "}"
        comma_space = 2  # ", " count last comma here
        linetoolong = tab_braces + (maxlen + comma_space) * nr_events > self.line_length
        for x in range(nr_states):
            line = "\t\t{\n" if linetoolong else "\t\t{ "
            for y in range(nr_events):
                next_state = self.function[x][y]
                if next_state != self.invalid_state_str:
                    next_state = self.function[x][y] + self.enum_suffix

                if linetoolong:
                    line += f"\t\t\t{next_state}"
                else:
                    line += f"{next_state:>{maxlen}}"
                if y != nr_events - 1:
                    line += ",\n" if linetoolong else ", "
                else:
                    line += ",\n\t\t}," if linetoolong else " },"
            buff.append(line)

        return '\n'.join(buff)

    def format_aut_init_function(self) -> list[str]:
        buff = []
        buff.append("\t.function = {")
        buff.append(self.get_aut_init_function())
        buff.append("\t},")

        return buff

    def get_aut_init_initial_state(self) -> str:
        return self.initial_state

    def format_aut_init_initial_state(self) -> list[str]:
        buff = []
        initial_state = self.get_aut_init_initial_state()
        buff.append("\t.initial_state = " + initial_state + self.enum_suffix + ",")

        return buff

    def get_aut_init_final_states(self) -> str:
        line = ""
        first = True
        for state in self.states:
            if not first:
                line = line + ', '
            else:
                first = False

            if state in self.final_states:
                line = line + '1'
            else:
                line = line + '0'
        return line

    def format_aut_init_final_states(self) -> list[str]:
       buff = []
       buff.append(f"\t.final_states = {{ {self.get_aut_init_final_states()} }},")

       return buff

    def __get_automaton_initialization_footer_string(self) -> str:
        footer = "};\n"
        return footer

    def format_aut_init_footer(self) -> list[str]:
        buff = []
        buff.append(self.__get_automaton_initialization_footer_string())

        return buff

    def format_invalid_state(self) -> list[str]:
        buff = []
        buff.append(f"#define {self.invalid_state_str} state_max{self.enum_suffix}\n")

        return buff

    def format_model(self) -> list[str]:
        buff = []
        buff += self.format_states_enum()
        buff += self.format_invalid_state()
        buff += self.format_events_enum()
        buff += self.format_envs_enum()
        buff += self.format_automaton_definition()
        buff += self.format_aut_init_header()
        buff += self.format_aut_init_states_string()
        buff += self.format_aut_init_events_string()
        buff += self.format_aut_init_envs_string()
        buff += self.format_aut_init_function()
        buff += self.format_aut_init_initial_state()
        buff += self.format_aut_init_final_states()
        buff += self.format_aut_init_footer()

        return buff

    def print_model_classic(self):
        buff = self.format_model()
        print('\n'.join(buff))
