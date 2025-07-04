#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# dot2k: transform dot files into a monitor for the Linux kernel.
#
# For further information, see:
#   Documentation/trace/rv/da_monitor_synthesis.rst

from dot2.dot2c import Dot2c
import platform
import os

class dot2k(Dot2c):
    monitor_types = { "global" : 1, "per_cpu" : 2, "per_task" : 3 }
    monitor_templates_dir = "dot2/dot2k_templates/"
    rv_dir = "kernel/trace/rv"
    monitor_type = "per_cpu"

    def __init__(self, file_path, MonitorType, extra_params={}):
        self.container = extra_params.get("container")
        self.parent = extra_params.get("parent")
        self.__fill_rv_templates_dir()

        if self.container:
            if file_path:
                raise ValueError("A container does not require a dot file")
            if MonitorType:
                raise ValueError("A container does not require a monitor type")
            if self.parent:
                raise ValueError("A container cannot have a parent")
            self.name = extra_params.get("model_name")
            self.events = []
            self.states = []
            self.main_c = self.__read_file(self.monitor_templates_dir + "main_container.c")
            self.main_h = self.__read_file(self.monitor_templates_dir + "main_container.h")
            self.kconfig = self.__read_file(self.monitor_templates_dir + "Kconfig_container")
        else:
            super().__init__(file_path, extra_params.get("model_name"))

            self.monitor_type = self.monitor_types.get(MonitorType)
            if self.monitor_type is None:
                raise ValueError("Unknown monitor type: %s" % MonitorType)
            self.monitor_type = MonitorType
            self.main_c = self.__read_file(self.monitor_templates_dir + "main.c")
            self.trace_h = self.__read_file(self.monitor_templates_dir + "trace.h")
            self.kconfig = self.__read_file(self.monitor_templates_dir + "Kconfig")
        self.enum_suffix = "_%s" % self.name
        self.description = extra_params.get("description", self.name) or "auto-generated"
        self.auto_patch = extra_params.get("auto_patch")
        if self.auto_patch:
            self.__fill_rv_kernel_dir()

    def __fill_rv_templates_dir(self):

        if os.path.exists(self.monitor_templates_dir):
            return

        if platform.system() != "Linux":
            raise OSError("I can only run on Linux.")

        kernel_path = "/lib/modules/%s/build/tools/verification/dot2/dot2k_templates/" % (platform.release())

        if os.path.exists(kernel_path):
            self.monitor_templates_dir = kernel_path
            return

        if os.path.exists("/usr/share/dot2/dot2k_templates/"):
            self.monitor_templates_dir = "/usr/share/dot2/dot2k_templates/"
            return

        raise FileNotFoundError("Could not find the template directory, do you have the kernel source installed?")

    def __fill_rv_kernel_dir(self):

        # first try if we are running in the kernel tree root
        if os.path.exists(self.rv_dir):
            return

        # offset if we are running inside the kernel tree from verification/dot2
        kernel_path = os.path.join("../..", self.rv_dir)

        if os.path.exists(kernel_path):
            self.rv_dir = kernel_path
            return

        if platform.system() != "Linux":
            raise OSError("I can only run on Linux.")

        kernel_path = os.path.join("/lib/modules/%s/build" % platform.release(), self.rv_dir)

        # if the current kernel is from a distro this may not be a full kernel tree
        # verify that one of the files we are going to modify is available
        if os.path.exists(os.path.join(kernel_path, "rv_trace.h")):
            self.rv_dir = kernel_path
            return

        raise FileNotFoundError("Could not find the rv directory, do you have the kernel source installed?")

    def __read_file(self, path):
        try:
            fd = open(path, 'r')
        except OSError:
            raise Exception("Cannot open the file: %s" % path)

        content = fd.read()

        fd.close()
        return content

    def __buff_to_string(self, buff):
        string = ""

        for line in buff:
            string = string + line + "\n"

        # cut off the last \n
        return string[:-1]

    def fill_monitor_type(self):
        return self.monitor_type.upper()

    def fill_parent(self):
        return "&rv_%s" % self.parent if self.parent else "NULL"

    def fill_include_parent(self):
        if self.parent:
            return "#include <monitors/%s/%s.h>\n" % (self.parent, self.parent)
        return ""

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
        return self.__buff_to_string(buff)

    def fill_tracepoint_attach_probe(self):
        buff = []
        for event in self.events:
            buff.append("\trv_attach_trace_probe(\"%s\", /* XXX: tracepoint */, handle_%s);" % (self.name, event))
        return self.__buff_to_string(buff)

    def fill_tracepoint_detach_helper(self):
        buff = []
        for event in self.events:
            buff.append("\trv_detach_trace_probe(\"%s\", /* XXX: tracepoint */, handle_%s);" % (self.name, event))
        return self.__buff_to_string(buff)

    def fill_main_c(self):
        main_c = self.main_c
        monitor_type = self.fill_monitor_type()
        min_type = self.get_minimun_type()
        nr_events = len(self.events)
        tracepoint_handlers = self.fill_tracepoint_handlers_skel()
        tracepoint_attach = self.fill_tracepoint_attach_probe()
        tracepoint_detach = self.fill_tracepoint_detach_helper()
        parent = self.fill_parent()
        parent_include = self.fill_include_parent()

        main_c = main_c.replace("%%MONITOR_TYPE%%", monitor_type)
        main_c = main_c.replace("%%MIN_TYPE%%", min_type)
        main_c = main_c.replace("%%MODEL_NAME%%", self.name)
        main_c = main_c.replace("%%NR_EVENTS%%", str(nr_events))
        main_c = main_c.replace("%%TRACEPOINT_HANDLERS_SKEL%%", tracepoint_handlers)
        main_c = main_c.replace("%%TRACEPOINT_ATTACH%%", tracepoint_attach)
        main_c = main_c.replace("%%TRACEPOINT_DETACH%%", tracepoint_detach)
        main_c = main_c.replace("%%DESCRIPTION%%", self.description)
        main_c = main_c.replace("%%PARENT%%", parent)
        main_c = main_c.replace("%%INCLUDE_PARENT%%", parent_include)

        return main_c

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

        return self.__buff_to_string(buff)

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
        return self.__buff_to_string(buff)

    def fill_monitor_deps(self):
        buff = []
        buff.append("	# XXX: add dependencies if there")
        if self.parent:
            buff.append("	depends on RV_MON_%s" % self.parent.upper())
            buff.append("	default y")
        return self.__buff_to_string(buff)

    def fill_trace_h(self):
        trace_h = self.trace_h
        monitor_class = self.fill_monitor_class()
        monitor_class_type = self.fill_monitor_class_type()
        tracepoint_args_skel_event = self.fill_tracepoint_args_skel("event")
        tracepoint_args_skel_error = self.fill_tracepoint_args_skel("error")
        trace_h = trace_h.replace("%%MODEL_NAME%%", self.name)
        trace_h = trace_h.replace("%%MODEL_NAME_UP%%", self.name.upper())
        trace_h = trace_h.replace("%%MONITOR_CLASS%%", monitor_class)
        trace_h = trace_h.replace("%%MONITOR_CLASS_TYPE%%", monitor_class_type)
        trace_h = trace_h.replace("%%TRACEPOINT_ARGS_SKEL_EVENT%%", tracepoint_args_skel_event)
        trace_h = trace_h.replace("%%TRACEPOINT_ARGS_SKEL_ERROR%%", tracepoint_args_skel_error)
        return trace_h

    def fill_kconfig(self):
        kconfig = self.kconfig
        monitor_class_type = self.fill_monitor_class_type()
        monitor_deps = self.fill_monitor_deps()
        kconfig = kconfig.replace("%%MODEL_NAME%%", self.name)
        kconfig = kconfig.replace("%%MODEL_NAME_UP%%", self.name.upper())
        kconfig = kconfig.replace("%%MONITOR_CLASS_TYPE%%", monitor_class_type)
        kconfig = kconfig.replace("%%DESCRIPTION%%", self.description)
        kconfig = kconfig.replace("%%MONITOR_DEPS%%", monitor_deps)
        return kconfig

    def fill_main_container_h(self):
        main_h = self.main_h
        main_h = main_h.replace("%%MODEL_NAME%%", self.name)
        return main_h

    def __patch_file(self, file, marker, line):
        file_to_patch = os.path.join(self.rv_dir, file)
        content = self.__read_file(file_to_patch)
        content = content.replace(marker, line + "\n" + marker)
        self.__write_file(file_to_patch, content)

    def fill_tracepoint_tooltip(self):
        monitor_class_type = self.fill_monitor_class_type()
        if self.auto_patch:
            self.__patch_file("rv_trace.h",
                            "// Add new monitors based on CONFIG_%s here" % monitor_class_type,
                            "#include <monitors/%s/%s_trace.h>" % (self.name, self.name))
            return "  - Patching %s/rv_trace.h, double check the result" % self.rv_dir

        return """  - Edit %s/rv_trace.h:
Add this line where other tracepoints are included and %s is defined:
#include <monitors/%s/%s_trace.h>
""" % (self.rv_dir, monitor_class_type, self.name, self.name)

    def fill_kconfig_tooltip(self):
        if self.auto_patch:
            self.__patch_file("Kconfig",
                            "# Add new monitors here",
                            "source \"kernel/trace/rv/monitors/%s/Kconfig\"" % (self.name))
            return "  - Patching %s/Kconfig, double check the result" % self.rv_dir

        return """  - Edit %s/Kconfig:
Add this line where other monitors are included:
source \"kernel/trace/rv/monitors/%s/Kconfig\"
""" % (self.rv_dir, self.name)

    def fill_makefile_tooltip(self):
        name = self.name
        name_up = name.upper()
        if self.auto_patch:
            self.__patch_file("Makefile",
                            "# Add new monitors here",
                            "obj-$(CONFIG_RV_MON_%s) += monitors/%s/%s.o" % (name_up, name, name))
            return "  - Patching %s/Makefile, double check the result" % self.rv_dir

        return """  - Edit %s/Makefile:
Add this line where other monitors are included:
obj-$(CONFIG_RV_MON_%s) += monitors/%s/%s.o
""" % (self.rv_dir, name_up, name, name)

    def fill_monitor_tooltip(self):
        if self.auto_patch:
            return "  - Monitor created in %s/monitors/%s" % (self.rv_dir, self. name)
        return "  - Move %s/ to the kernel's monitor directory (%s/monitors)" % (self.name, self.rv_dir)

    def __create_directory(self):
        path = self.name
        if self.auto_patch:
            path = os.path.join(self.rv_dir, "monitors", path)
        try:
            os.mkdir(path)
        except FileExistsError:
            return
        except:
            print("Fail creating the output dir: %s" % self.name)

    def __write_file(self, file_name, content):
        try:
            file = open(file_name, 'w')
        except:
            print("Fail writing to file: %s" % file_name)

        file.write(content)

        file.close()

    def __create_file(self, file_name, content):
        path = "%s/%s" % (self.name, file_name)
        if self.auto_patch:
            path = os.path.join(self.rv_dir, "monitors", path)
        self.__write_file(path, content)

    def __get_main_name(self):
        path = "%s/%s" % (self.name, "main.c")
        if not os.path.exists(path):
            return "main.c"
        return "__main.c"

    def print_files(self):
        main_c = self.fill_main_c()

        self.__create_directory()

        path = "%s.c" % self.name
        self.__create_file(path, main_c)

        if self.container:
            main_h = self.fill_main_container_h()
            path = "%s.h" % self.name
            self.__create_file(path, main_h)
        else:
            model_h = self.fill_model_h()
            path = "%s.h" % self.name
            self.__create_file(path, model_h)

            trace_h = self.fill_trace_h()
            path = "%s_trace.h" % self.name
            self.__create_file(path, trace_h)

        kconfig = self.fill_kconfig()
        self.__create_file("Kconfig", kconfig)
