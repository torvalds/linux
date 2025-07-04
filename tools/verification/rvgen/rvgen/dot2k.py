#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# dot2k: transform dot files into a monitor for the Linux kernel.
#
# For further information, see:
#   Documentation/trace/rv/da_monitor_synthesis.rst

from .dot2c import Dot2c
from .generator import Monitor


class dot2k(Monitor, Dot2c):
    template_dir = "dot2k"

    def __init__(self, file_path, MonitorType, extra_params={}):
        self.monitor_type = MonitorType
        Monitor.__init__(self, extra_params)
        Dot2c.__init__(self, file_path, extra_params.get("model_name"))
        self.enum_suffix = "_%s" % self.name

    def fill_monitor_type(self):
        return self.monitor_type.upper()

    def fill_tracepoint_handlers_skel(self):
        buff = []
        for event in self.events:
            buff.append("static void handle_%s(void *data, /* XXX: fill header */)" % event)
            buff.append("{")
            handle = "handle_event"
            if self.is_start_event(event):
                buff.append("\t/* XXX: validate that this event always leads to the initial state */")
                handle = "handle_start_event"
            elif self.is_start_run_event(event):
                buff.append("\t/* XXX: validate that this event is only valid in the initial state */")
                handle = "handle_start_run_event"
            if self.monitor_type == "per_task":
                buff.append("\tstruct task_struct *p = /* XXX: how do I get p? */;");
                buff.append("\tda_%s_%s(p, %s%s);" % (handle, self.name, event, self.enum_suffix));
            else:
                buff.append("\tda_%s_%s(%s%s);" % (handle, self.name, event, self.enum_suffix));
            buff.append("}")
            buff.append("")
        return '\n'.join(buff)

    def fill_tracepoint_attach_probe(self):
        buff = []
        for event in self.events:
            buff.append("\trv_attach_trace_probe(\"%s\", /* XXX: tracepoint */, handle_%s);" % (self.name, event))
        return '\n'.join(buff)

    def fill_tracepoint_detach_helper(self):
        buff = []
        for event in self.events:
            buff.append("\trv_detach_trace_probe(\"%s\", /* XXX: tracepoint */, handle_%s);" % (self.name, event))
        return '\n'.join(buff)

    def fill_model_h_header(self):
        buff = []
        buff.append("/* SPDX-License-Identifier: GPL-2.0 */")
        buff.append("/*")
        buff.append(" * Automatically generated C representation of %s automaton" % (self.name))
        buff.append(" * For further information about this format, see kernel documentation:")
        buff.append(" *   Documentation/trace/rv/deterministic_automata.rst")
        buff.append(" */")
        buff.append("")

        return buff

    def fill_model_h(self):
        #
        # Adjust the definition names
        #
        self.enum_states_def = "states_%s" % self.name
        self.enum_events_def = "events_%s" % self.name
        self.struct_automaton_def = "automaton_%s" % self.name
        self.var_automaton_def = "automaton_%s" % self.name

        buff = self.fill_model_h_header()
        buff += self.format_model()

        return '\n'.join(buff)

    def fill_monitor_class_type(self):
        if self.monitor_type == "per_task":
            return "DA_MON_EVENTS_ID"
        return "DA_MON_EVENTS_IMPLICIT"

    def fill_monitor_class(self):
        if self.monitor_type == "per_task":
            return "da_monitor_id"
        return "da_monitor"

    def fill_tracepoint_args_skel(self, tp_type):
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
        tp_args_id = ("int ", "id")
        tp_args = tp_args_event if tp_type == "event" else tp_args_error
        if self.monitor_type == "per_task":
            tp_args.insert(0, tp_args_id)
        tp_proto_c = ", ".join([a+b for a,b in tp_args])
        tp_args_c = ", ".join([b for a,b in tp_args])
        buff.append("	     TP_PROTO(%s)," % tp_proto_c)
        buff.append("	     TP_ARGS(%s)" % tp_args_c)
        return '\n'.join(buff)

    def fill_main_c(self):
        main_c = super().fill_main_c()

        min_type = self.get_minimun_type()
        nr_events = len(self.events)
        monitor_type = self.fill_monitor_type()

        main_c = main_c.replace("%%MIN_TYPE%%", min_type)
        main_c = main_c.replace("%%NR_EVENTS%%", str(nr_events))
        main_c = main_c.replace("%%MONITOR_TYPE%%", monitor_type)

        return main_c
