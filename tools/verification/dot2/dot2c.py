#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# dot2c: parse an automata in dot file digraph format into a C
#
# This program was written in the development of this paper:
#  de Oliveira, D. B. and Cucinotta, T. and de Oliveira, R. S.
#  "Efficient Formal Verification for the Linux Kernel." International
#  Conference on Software Engineering and Formal Methods. Springer, Cham, 2019.
#
# For further information, see:
#   Documentation/trace/rv/deterministic_automata.rst

from dot2.automata import Automata

class Dot2c(Automata):
    enum_suffix = ""
    enum_states_def = "states"
    enum_events_def = "events"
    struct_automaton_def = "automaton"
    var_automaton_def = "aut"

    def __init__(self, file_path):
        super().__init__(file_path)
        self.line_length = 100

    def __buff_to_string(self, buff):
        string = ""

        for line in buff:
            string = string + line + "\n"

        # cut off the last \n
        return string[:-1]

    def __get_enum_states_content(self):
        buff = []
        buff.append("\t%s%s = 0," % (self.initial_state, self.enum_suffix))
        for state in self.states:
            if state != self.initial_state:
                buff.append("\t%s%s," % (state, self.enum_suffix))
        buff.append("\tstate_max%s" % (self.enum_suffix))

        return buff

    def get_enum_states_string(self):
        buff = self.__get_enum_states_content()
        return self.__buff_to_string(buff)

    def format_states_enum(self):
        buff = []
        buff.append("enum %s {" % self.enum_states_def)
        buff.append(self.get_enum_states_string())
        buff.append("};\n")

        return buff

    def __get_enum_events_content(self):
        buff = []
        first = True
        for event in self.events:
            if first:
                buff.append("\t%s%s = 0," % (event, self.enum_suffix))
                first = False
            else:
                buff.append("\t%s%s," % (event, self.enum_suffix))

        buff.append("\tevent_max%s" % self.enum_suffix)

        return buff

    def get_enum_events_string(self):
        buff = self.__get_enum_events_content()
        return self.__buff_to_string(buff)

    def format_events_enum(self):
        buff = []
        buff.append("enum %s {" % self.enum_events_def)
        buff.append(self.get_enum_events_string())
        buff.append("};\n")

        return buff

    def get_minimun_type(self):
        min_type = "unsigned char"

        if self.states.__len__() > 255:
            min_type = "unsigned short"

        if self.states.__len__() > 65535:
            min_type = "unsigned int"

        if self.states.__len__() > 1000000:
            raise Exception("Too many states: %d" % self.states.__len__())

        return min_type

    def format_automaton_definition(self):
        min_type = self.get_minimun_type()
        buff = []
        buff.append("struct %s {" % self.struct_automaton_def)
        buff.append("\tchar *state_names[state_max%s];" % (self.enum_suffix))
        buff.append("\tchar *event_names[event_max%s];" % (self.enum_suffix))
        buff.append("\t%s function[state_max%s][event_max%s];" % (min_type, self.enum_suffix, self.enum_suffix))
        buff.append("\t%s initial_state;" % min_type)
        buff.append("\tbool final_states[state_max%s];" % (self.enum_suffix))
        buff.append("};\n")
        return buff

    def format_aut_init_header(self):
        buff = []
        buff.append("static const struct %s %s = {" % (self.struct_automaton_def, self.var_automaton_def))
        return buff

    def __get_string_vector_per_line_content(self, buff):
        first = True
        string = ""
        for entry in buff:
            if first:
                string = string + "\t\t\"" + entry
                first = False;
            else:
                string = string + "\",\n\t\t\"" + entry
        string = string + "\""

        return string

    def get_aut_init_events_string(self):
        return self.__get_string_vector_per_line_content(self.events)

    def get_aut_init_states_string(self):
        return self.__get_string_vector_per_line_content(self.states)

    def format_aut_init_events_string(self):
        buff = []
        buff.append("\t.event_names = {")
        buff.append(self.get_aut_init_events_string())
        buff.append("\t},")
        return buff

    def format_aut_init_states_string(self):
        buff = []
        buff.append("\t.state_names = {")
        buff.append(self.get_aut_init_states_string())
        buff.append("\t},")

        return buff

    def __get_max_strlen_of_states(self):
        max_state_name = max(self.states, key = len).__len__()
        return max(max_state_name, self.invalid_state_str.__len__())

    def __get_state_string_length(self):
        maxlen = self.__get_max_strlen_of_states() + self.enum_suffix.__len__()
        return "%" + str(maxlen) + "s"

    def get_aut_init_function(self):
        nr_states = self.states.__len__()
        nr_events = self.events.__len__()
        buff = []

        strformat = self.__get_state_string_length()

        for x in range(nr_states):
            line = "\t\t{ "
            for y in range(nr_events):
                next_state = self.function[x][y]
                if next_state != self.invalid_state_str:
                    next_state = self.function[x][y] + self.enum_suffix

                if y != nr_events-1:
                    line = line + strformat % next_state + ", "
                else:
                    line = line + strformat % next_state + " },"
            buff.append(line)

        return self.__buff_to_string(buff)

    def format_aut_init_function(self):
        buff = []
        buff.append("\t.function = {")
        buff.append(self.get_aut_init_function())
        buff.append("\t},")

        return buff

    def get_aut_init_initial_state(self):
        return self.initial_state

    def format_aut_init_initial_state(self):
        buff = []
        initial_state = self.get_aut_init_initial_state()
        buff.append("\t.initial_state = " + initial_state + self.enum_suffix + ",")

        return buff

    def get_aut_init_final_states(self):
        line = ""
        first = True
        for state in self.states:
            if first == False:
                line = line + ', '
            else:
                first = False

            if self.final_states.__contains__(state):
                line = line + '1'
            else:
                line = line + '0'
        return line

    def format_aut_init_final_states(self):
       buff = []
       buff.append("\t.final_states = { %s }," % self.get_aut_init_final_states())

       return buff

    def __get_automaton_initialization_footer_string(self):
        footer = "};\n"
        return footer

    def format_aut_init_footer(self):
        buff = []
        buff.append(self.__get_automaton_initialization_footer_string())

        return buff

    def format_invalid_state(self):
        buff = []
        buff.append("#define %s state_max%s\n" % (self.invalid_state_str, self.enum_suffix))

        return buff

    def format_model(self):
        buff = []
        buff += self.format_states_enum()
        buff += self.format_invalid_state()
        buff += self.format_events_enum()
        buff += self.format_automaton_definition()
        buff += self.format_aut_init_header()
        buff += self.format_aut_init_states_string()
        buff += self.format_aut_init_events_string()
        buff += self.format_aut_init_function()
        buff += self.format_aut_init_initial_state()
        buff += self.format_aut_init_final_states()
        buff += self.format_aut_init_footer()

        return buff

    def print_model_classic(self):
        buff = self.format_model()
        print(self.__buff_to_string(buff))
