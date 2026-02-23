#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# dot2k: transform dot files into a monitor for the Linux kernel.
#
# For further information, see:
#   Documentation/trace/rv/da_monitor_synthesis.rst

from collections import deque
from .dot2c import Dot2c
from .generator import Monitor
from .automata import _EventConstraintKey, _StateConstraintKey, AutomataError


class dot2k(Monitor, Dot2c):
    template_dir = "dot2k"

    def __init__(self, file_path, MonitorType, extra_params={}):
        self.monitor_type = MonitorType
        Monitor.__init__(self, extra_params)
        Dot2c.__init__(self, file_path, extra_params.get("model_name"))
        self.enum_suffix = f"_{self.name}"
        self.enum_suffix = f"_{self.name}"
        self.monitor_class = extra_params["monitor_class"]

    def fill_monitor_type(self) -> str:
        buff = [ self.monitor_type.upper() ]
        buff += self._fill_timer_type()
        if self.monitor_type == "per_obj":
            buff.append("typedef /* XXX: define the target type */ *monitor_target;")
        return "\n".join(buff)

    def fill_tracepoint_handlers_skel(self) -> str:
        buff = []
        buff += self._fill_hybrid_definitions()
        for event in self.events:
            buff.append(f"static void handle_{event}(void *data, /* XXX: fill header */)")
            buff.append("{")
            handle = "handle_event"
            if self.is_start_event(event):
                buff.append("\t/* XXX: validate that this event always leads to the initial state */")
                handle = "handle_start_event"
            elif self.is_start_run_event(event):
                buff.append("\t/* XXX: validate that this event is only valid in the initial state */")
                handle = "handle_start_run_event"
            if self.monitor_type == "per_task":
                buff.append("\tstruct task_struct *p = /* XXX: how do I get p? */;")
                buff.append(f"\tda_{handle}(p, {event}{self.enum_suffix});")
            elif self.monitor_type == "per_obj":
                buff.append("\tint id = /* XXX: how do I get the id? */;")
                buff.append("\tmonitor_target t = /* XXX: how do I get t? */;")
                buff.append(f"\tda_{handle}(id, t, {event}{self.enum_suffix});")
            else:
                buff.append(f"\tda_{handle}({event}{self.enum_suffix});")
            buff.append("}")
            buff.append("")
        return '\n'.join(buff)

    def fill_tracepoint_attach_probe(self) -> str:
        buff = []
        for event in self.events:
            buff.append(f"\trv_attach_trace_probe(\"{self.name}\", /* XXX: tracepoint */, handle_{event});")
        return '\n'.join(buff)

    def fill_tracepoint_detach_helper(self) -> str:
        buff = []
        for event in self.events:
            buff.append(f"\trv_detach_trace_probe(\"{self.name}\", /* XXX: tracepoint */, handle_{event});")
        return '\n'.join(buff)

    def fill_model_h_header(self) -> list[str]:
        buff = []
        buff.append("/* SPDX-License-Identifier: GPL-2.0 */")
        buff.append("/*")
        buff.append(f" * Automatically generated C representation of {self.name} automaton")
        buff.append(" * For further information about this format, see kernel documentation:")
        buff.append(" *   Documentation/trace/rv/deterministic_automata.rst")
        buff.append(" */")
        buff.append("")
        buff.append(f"#define MONITOR_NAME {self.name}")
        buff.append("")

        return buff

    def fill_model_h(self) -> str:
        #
        # Adjust the definition names
        #
        self.enum_states_def = f"states_{self.name}"
        self.enum_events_def = f"events_{self.name}"
        self.enum_envs_def = f"envs_{self.name}"
        self.struct_automaton_def = f"automaton_{self.name}"
        self.var_automaton_def = f"automaton_{self.name}"

        buff = self.fill_model_h_header()
        buff += self.format_model()

        return '\n'.join(buff)

    def _is_id_monitor(self) -> bool:
        return self.monitor_type in ("per_task", "per_obj")

    def fill_monitor_class_type(self) -> str:
        if self._is_id_monitor():
            return "DA_MON_EVENTS_ID"
        return "DA_MON_EVENTS_IMPLICIT"

    def fill_monitor_class(self) -> str:
        if self._is_id_monitor():
            return "da_monitor_id"
        return "da_monitor"

    def fill_tracepoint_args_skel(self, tp_type: str) -> str:
        buff = []
        tp_args_event = [
                ("char *", "state"),
                ("char *", "event"),
                ("char *", "next_state"),
                ("bool ",  "final_state"),
                ]
        tp_args_error = [
                ("char *", "state"),
                ("char *", "event"),
                ]
        tp_args_error_env = tp_args_error + [("char *", "env")]
        tp_args_dict = {
                "event": tp_args_event,
                "error": tp_args_error,
                "error_env": tp_args_error_env
                }
        tp_args_id = ("int ", "id")
        tp_args = tp_args_dict[tp_type]
        if self._is_id_monitor():
            tp_args.insert(0, tp_args_id)
        tp_proto_c = ", ".join([a + b for a, b in tp_args])
        tp_args_c = ", ".join([b for a, b in tp_args])
        buff.append(f"	     TP_PROTO({tp_proto_c}),")
        buff.append(f"	     TP_ARGS({tp_args_c})")
        return '\n'.join(buff)

    def _fill_hybrid_definitions(self) -> list:
        """Stub, not valid for deterministic automata"""
        return []

    def _fill_timer_type(self) -> list:
        """Stub, not valid for deterministic automata"""
        return []

    def fill_main_c(self) -> str:
        main_c = super().fill_main_c()

        min_type = self.get_minimun_type()
        nr_events = len(self.events)
        monitor_type = self.fill_monitor_type()

        main_c = main_c.replace("%%MIN_TYPE%%", min_type)
        main_c = main_c.replace("%%NR_EVENTS%%", str(nr_events))
        main_c = main_c.replace("%%MONITOR_TYPE%%", monitor_type)
        main_c = main_c.replace("%%MONITOR_CLASS%%", self.monitor_class)

        return main_c

class da2k(dot2k):
    """Deterministic automata only"""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if self.is_hybrid_automata():
            raise AutomataError("Detected hybrid automaton, use the 'ha' class")

class ha2k(dot2k):
    """Hybrid automata only"""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if not self.is_hybrid_automata():
            raise AutomataError("Detected deterministic automaton, use the 'da' class")
        self.trace_h = self._read_template_file("trace_hybrid.h")
        self.__parse_constraints()

    def fill_monitor_class_type(self) -> str:
        if self._is_id_monitor():
            return "HA_MON_EVENTS_ID"
        return "HA_MON_EVENTS_IMPLICIT"

    def fill_monitor_class(self) -> str:
        """
        Used for tracepoint classes, since they are shared we keep da
        instead of ha (also for the ha specific tracepoints).
        The tracepoint class is not visible to the tools.
        """
        return super().fill_monitor_class()

    def __adjust_value(self, value: str | int, unit: str | None) -> str:
        """Adjust the value in ns"""
        try:
            value = int(value)
        except ValueError:
            # it's a constant, a parameter or a function
            if value.endswith("()"):
                return value.replace("()", "(ha_mon)")
            return value
        match unit:
            case "us":
                value *= 10**3
            case "ms":
                value *= 10**6
            case "s":
                value *= 10**9
        return str(value) + "ull"

    def __parse_single_constraint(self, rule: dict, value: str) -> str:
        return f"ha_get_env(ha_mon, {rule["env"]}{self.enum_suffix}, time_ns) {rule["op"]} {value}"

    def __get_constraint_env(self, constr: str) -> str:
        """Extract the second argument from an ha_ function"""
        env = constr.split("(")[1].split()[1].rstrip(")").rstrip(",")
        assert env.rstrip(f"_{self.name}") in self.envs
        return env

    def __start_to_invariant_check(self, constr: str) -> str:
        # by default assume the timer has ns expiration
        env = self.__get_constraint_env(constr)
        clock_type = "ns"
        if self.env_types.get(env.rstrip(f"_{self.name}")) == "j":
            clock_type = "jiffy"

        return f"return ha_check_invariant_{clock_type}(ha_mon, {env}, time_ns)"

    def __start_to_conv(self, constr: str) -> str:
        """
        Undo the storage conversion done by ha_start_timer_
        """
        return "ha_inv_to_guard" + constr[constr.find("("):]

    def __parse_timer_constraint(self, rule: dict, value: str) -> str:
        # by default assume the timer has ns expiration
        clock_type = "ns"
        if self.env_types.get(rule["env"]) == "j":
            clock_type = "jiffy"

        return (f"ha_start_timer_{clock_type}(ha_mon, {rule["env"]}{self.enum_suffix},"
                f" {value}, time_ns)")

    def __format_guard_rules(self, rules: list[str]) -> list[str]:
        """
        Merge guard constraints as a single C return statement.
        If the rules include a stored env, also check its validity.
        Break lines in a best effort way that tries to keep readability.
        """
        if not rules:
            return []

        invalid_checks = [f"ha_monitor_env_invalid(ha_mon, {env}{self.enum_suffix}) ||"
                          for env in self.env_stored if any(env in rule for rule in rules)]
        if invalid_checks and len(rules) > 1:
            rules[0] = "(" + rules[0]
            rules[-1] = rules[-1] + ")"
        rules = invalid_checks + rules

        separator = "\n\t\t      " if sum(len(r) for r in rules) > 80 else " "
        return ["res = " + separator.join(rules)]

    def __validate_constraint(self, key: tuple[int, int] | int, constr: str,
                              rule, reset) -> None:
        # event constrains are tuples and allow both rules and reset
        # state constraints are only used for expirations (e.g. clk<N)
        if self.is_event_constraint(key):
            if not rule and not reset:
                raise AutomataError("Unrecognised event constraint "
                                    f"({self.states[key[0]]}/{self.events[key[1]]}: {constr})")
            if rule and (rule["env"] in self.env_types and
                         rule["env"] not in self.env_stored):
                raise AutomataError("Clocks in hybrid automata always require a storage"
                                    f" ({rule["env"]})")
        else:
            if not rule:
                raise AutomataError("Unrecognised state constraint "
                                    f"({self.states[key]}: {constr})")
            if rule["env"] not in self.env_stored:
                raise AutomataError("State constraints always require a storage "
                                    f"({rule["env"]})")
            if rule["op"] not in ["<", "<="]:
                raise AutomataError("State constraints must be clock expirations like"
                                    f" clk<N ({rule.string})")

    def __parse_constraints(self) -> None:
        self.guards: dict[_EventConstraintKey, str] = {}
        self.invariants: dict[_StateConstraintKey, str] = {}
        for key, constraint in self.constraints.items():
            rules = []
            resets = []
            for c, sep in self._split_constraint_expr(constraint):
                rule = self.constraint_rule.search(c)
                reset = self.constraint_reset.search(c)
                self.__validate_constraint(key, c, rule, reset)
                if rule:
                    value = rule["val"]
                    value_len = len(rule["val"])
                    unit = None
                    if rule.groupdict().get("unit"):
                        value_len += len(rule["unit"])
                        unit = rule["unit"]
                    c = c[:-(value_len)]
                    value = self.__adjust_value(value, unit)
                    if self.is_event_constraint(key):
                        c = self.__parse_single_constraint(rule, value)
                        if sep:
                            c += f" {sep}"
                    else:
                        c = self.__parse_timer_constraint(rule, value)
                    rules.append(c)
                if reset:
                    c = f"ha_reset_env(ha_mon, {reset["env"]}{self.enum_suffix}, time_ns)"
                    resets.append(c)
            if self.is_event_constraint(key):
                res = self.__format_guard_rules(rules) + resets
                self.guards[key] = ";".join(res)
            else:
                self.invariants[key] = rules[0]

    def __fill_verify_invariants_func(self) -> list[str]:
        buff = []
        if not self.invariants:
            return []

        buff.append(
f"""static inline bool ha_verify_invariants(struct ha_monitor *ha_mon,
\t\t\t\t\tenum {self.enum_states_def} curr_state, enum {self.enum_events_def} event,
\t\t\t\t\tenum {self.enum_states_def} next_state, u64 time_ns)
{{""")

        _else = ""
        for state, constr in sorted(self.invariants.items()):
            check_str = self.__start_to_invariant_check(constr)
            buff.append(f"\t{_else}if (curr_state == {self.states[state]}{self.enum_suffix})")
            buff.append(f"\t\t{check_str};")
            _else = "else "

        buff.append("\treturn true;\n}\n")
        return buff

    def __fill_convert_inv_guard_func(self) -> list[str]:
        buff = []
        if not self.invariants:
            return []

        conflict_guards, conflict_invs = self.__find_inv_conflicts()
        if not conflict_guards and not conflict_invs:
            return []

        buff.append(
f"""static inline void ha_convert_inv_guard(struct ha_monitor *ha_mon,
\t\t\t\t\tenum {self.enum_states_def} curr_state, enum {self.enum_events_def} event,
\t\t\t\t\tenum {self.enum_states_def} next_state, u64 time_ns)
{{""")
        buff.append("\tif (curr_state == next_state)\n\t\treturn;")

        _else = ""
        for state, constr in sorted(self.invariants.items()):
            # a state with invariant can reach us without reset
            # multiple conflicts must have the same invariant, otherwise we cannot
            # know how to reset the value
            conf_i = [start for start, end in conflict_invs if end == state]
            # we can reach a guard without reset
            conf_g = [e for s, e in conflict_guards if s == state]
            if not conf_i and not conf_g:
                continue
            buff.append(f"\t{_else}if (curr_state == {self.states[state]}{self.enum_suffix})")

            buff.append(f"\t\t{self.__start_to_conv(constr)};")
            _else = "else "

        buff.append("}\n")
        return buff

    def __fill_verify_guards_func(self) -> list[str]:
        buff = []
        if not self.guards:
            return []

        buff.append(
f"""static inline bool ha_verify_guards(struct ha_monitor *ha_mon,
\t\t\t\t    enum {self.enum_states_def} curr_state, enum {self.enum_events_def} event,
\t\t\t\t    enum {self.enum_states_def} next_state, u64 time_ns)
{{
\tbool res = true;
""")

        _else = ""
        for edge, constr in sorted(self.guards.items()):
            buff.append(f"\t{_else}if (curr_state == "
                        f"{self.states[edge[0]]}{self.enum_suffix} && "
                        f"event == {self.events[edge[1]]}{self.enum_suffix})")
            if constr.count(";") > 0:
                buff[-1] += " {"
            buff += [f"\t\t{c};" for c in constr.split(";")]
            if constr.count(";") > 0:
                _else = "} else "
            else:
                _else = "else "
        if _else[0] == "}":
            buff.append("\t}")
        buff.append("\treturn res;\n}\n")
        return buff

    def __find_inv_conflicts(self) -> tuple[set[tuple[int, _EventConstraintKey]],
                                            set[tuple[int, _StateConstraintKey]]]:
        """
        Run a breadth first search from all states with an invariant.
        Find any conflicting constraints reachable from there, this can be
        another state with an invariant or an edge with a non-reset guard.
        Stop when we find a reset.

        Return the set of conflicting guards and invariants as tuples of
        conflicting state and constraint key.
        """
        conflict_guards: set[tuple[int, _EventConstraintKey]] = set()
        conflict_invs: set[tuple[int, _StateConstraintKey]] = set()
        for start_idx in self.invariants:
            queue = deque([(start_idx, 0)])  # (state_idx, distance)
            env = self.__get_constraint_env(self.invariants[start_idx])

            while queue:
                curr_idx, distance = queue.popleft()

                # Check state condition
                if curr_idx != start_idx and curr_idx in self.invariants:
                    conflict_invs.add((start_idx, _StateConstraintKey(curr_idx)))
                    continue

                # Check if we should stop
                if distance > len(self.states):
                    break
                if curr_idx != start_idx and distance > 1:
                    continue

                for event_idx, next_state_name in enumerate(self.function[curr_idx]):
                    if next_state_name == self.invalid_state_str:
                        continue
                    curr_guard = self.guards.get((curr_idx, event_idx), "")
                    if "reset" in curr_guard and env in curr_guard:
                        continue

                    if env in curr_guard:
                        conflict_guards.add((start_idx,
                                             _EventConstraintKey(curr_idx, event_idx)))
                        continue

                    next_idx = self.states.index(next_state_name)
                    queue.append((next_idx, distance + 1))

        return conflict_guards, conflict_invs

    def __fill_setup_invariants_func(self) -> list[str]:
        buff = []
        if not self.invariants:
            return []

        buff.append(
f"""static inline void ha_setup_invariants(struct ha_monitor *ha_mon,
\t\t\t\t       enum {self.enum_states_def} curr_state, enum {self.enum_events_def} event,
\t\t\t\t       enum {self.enum_states_def} next_state, u64 time_ns)
{{""")

        conditions = ["next_state == curr_state"]
        conditions += [f"event != {e}{self.enum_suffix}"
                       for e in self.self_loop_reset_events]
        condition_str = " && ".join(conditions)
        buff.append(f"\tif ({condition_str})\n\t\treturn;")

        _else = ""
        for state, constr in sorted(self.invariants.items()):
            buff.append(f"\t{_else}if (next_state == {self.states[state]}{self.enum_suffix})")
            buff.append(f"\t\t{constr};")
            _else = "else "

        for state in self.invariants:
            buff.append(f"\telse if (curr_state == {self.states[state]}{self.enum_suffix})")
            buff.append("\t\tha_cancel_timer(ha_mon);")

        buff.append("}\n")
        return buff

    def __fill_constr_func(self) -> list[str]:
        buff = []
        if not self.constraints:
            return []

        buff.append(
"""/*
 * These functions are used to validate state transitions.
 *
 * They are generated by parsing the model, there is usually no need to change them.
 * If the monitor requires a timer, there are functions responsible to arm it when
 * the next state has a constraint, cancel it in any other case and to check
 * that it didn't expire before the callback run. Transitions to the same state
 * without a reset never affect timers.
 * Due to the different representations between invariants and guards, there is
 * a function to convert it in case invariants or guards are reachable from
 * another invariant without reset. Those are not present if not required in
 * the model. This is all automatic but is worth checking because it may show
 * errors in the model (e.g. missing resets).
 */""")

        buff += self.__fill_verify_invariants_func()
        inv_conflicts = self.__fill_convert_inv_guard_func()
        buff += inv_conflicts
        buff += self.__fill_verify_guards_func()
        buff += self.__fill_setup_invariants_func()

        buff.append(
f"""static bool ha_verify_constraint(struct ha_monitor *ha_mon,
\t\t\t\t enum {self.enum_states_def} curr_state, enum {self.enum_events_def} event,
\t\t\t\t enum {self.enum_states_def} next_state, u64 time_ns)
{{""")

        if self.invariants:
            buff.append("\tif (!ha_verify_invariants(ha_mon, curr_state, "
                        "event, next_state, time_ns))\n\t\treturn false;\n")
        if inv_conflicts:
            buff.append("\tha_convert_inv_guard(ha_mon, curr_state, event, "
                        "next_state, time_ns);\n")

        if self.guards:
            buff.append("\tif (!ha_verify_guards(ha_mon, curr_state, event, "
                        "next_state, time_ns))\n\t\treturn false;\n")

        if self.invariants:
            buff.append("\tha_setup_invariants(ha_mon, curr_state, event, next_state, time_ns);\n")

        buff.append("\treturn true;\n}\n")
        return buff

    def __fill_env_getter(self, env: str) -> str:
        if env in self.env_types:
            match self.env_types[env]:
                case "ns" | "us" | "ms" | "s":
                    return "ha_get_clk_ns(ha_mon, env, time_ns);"
                case "j":
                    return "ha_get_clk_jiffy(ha_mon, env);"
        return f"/* XXX: how do I read {env}? */"

    def __fill_env_resetter(self, env: str) -> str:
        if env in self.env_types:
            match self.env_types[env]:
                case "ns" | "us" | "ms" | "s":
                    return "ha_reset_clk_ns(ha_mon, env, time_ns);"
                case "j":
                    return "ha_reset_clk_jiffy(ha_mon, env);"
        return f"/* XXX: how do I reset {env}? */"

    def __fill_hybrid_get_reset_functions(self) -> list[str]:
        buff = []
        if self.is_hybrid_automata():
            for var in self.constraint_vars:
                if var.endswith("()"):
                    func_name = var.replace("()", "")
                    if func_name.isupper():
                        buff.append(f"#define {func_name}(ha_mon) "
                                    f"/* XXX: what is {func_name}(ha_mon)? */\n")
                    else:
                        buff.append(f"static inline u64 {func_name}(struct ha_monitor *ha_mon)\n{{")
                        buff.append(f"\treturn /* XXX: what is {func_name}(ha_mon)? */;")
                        buff.append("}\n")
                elif var.isupper():
                    buff.append(f"#define {var} /* XXX: what is {var}? */\n")
                else:
                    buff.append(f"static u64 {var} = /* XXX: default value */;")
                    buff.append(f"module_param({var}, ullong, 0644);\n")
            buff.append("""/*
 * These functions define how to read and reset the environment variable.
 *
 * Common environment variables like ns-based and jiffy-based clocks have
 * pre-define getters and resetters you can use. The parser can infer the type
 * of the environment variable if you supply a measure unit in the constraint.
 * If you define your own functions, make sure to add appropriate memory
 * barriers if required.
 * Some environment variables don't require a storage as they read a system
 * state (e.g. preemption count). Those variables are never reset, so we don't
 * define a reset function on monitors only relying on this type of variables.
 */""")
            buff.append("static u64 ha_get_env(struct ha_monitor *ha_mon, "
                        f"enum envs{self.enum_suffix} env, u64 time_ns)\n{{")
            _else = ""
            for env in self.envs:
                buff.append(f"\t{_else}if (env == {env}{self.enum_suffix})")
                buff.append(f"\t\treturn {self.__fill_env_getter(env)}")
                _else = "else "
            buff.append("\treturn ENV_INVALID_VALUE;\n}\n")
            if len(self.env_stored):
                buff.append("static void ha_reset_env(struct ha_monitor *ha_mon, "
                            f"enum envs{self.enum_suffix} env, u64 time_ns)\n{{")
                _else = ""
                for env in self.env_stored:
                    buff.append(f"\t{_else}if (env == {env}{self.enum_suffix})")
                    buff.append(f"\t\t{self.__fill_env_resetter(env)}")
                    _else = "else "
                buff.append("}\n")
        return buff

    def _fill_hybrid_definitions(self) -> list[str]:
        return self.__fill_hybrid_get_reset_functions() + self.__fill_constr_func()

    def _fill_timer_type(self) -> list:
        if self.invariants:
            return [
                    "/* XXX: If the monitor has several instances, consider HA_TIMER_WHEEL */",
                    "#define HA_TIMER_TYPE HA_TIMER_HRTIMER"
                    ]
        return []
