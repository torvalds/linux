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
    monitor_templates_dir = "dot2k/rv_templates/"
    monitor_type = "per_cpu"

    def __init__(self, file_path, MonitorType):
        super().__init__(file_path)

        self.monitor_type = self.monitor_types.get(MonitorType)
        if self.monitor_type == None:
            raise Exception("Unknown monitor type: %s" % MonitorType)

        self.monitor_type = MonitorType
        self.__fill_rv_templates_dir()
        self.main_c = self.__open_file(self.monitor_templates_dir + "main_" + MonitorType + ".c")
        self.enum_suffix = "_%s" % self.name

    def __fill_rv_templates_dir(self):

        if os.path.exists(self.monitor_templates_dir) == True:
            return

        if platform.system() != "Linux":
            raise Exception("I can only run on Linux.")

        kernel_path = "/lib/modules/%s/build/tools/verification/dot2/dot2k_templates/" % (platform.release())

        if os.path.exists(kernel_path) == True:
            self.monitor_templates_dir = kernel_path
            return

        if os.path.exists("/usr/share/dot2/dot2k_templates/") == True:
            self.monitor_templates_dir = "/usr/share/dot2/dot2k_templates/"
            return

        raise Exception("Could not find the template directory, do you have the kernel source installed?")


    def __open_file(self, path):
        try:
            fd = open(path)
        except OSError:
            raise Exception("Cannot open the file: %s" % path)

        content = fd.read()

        return content

    def __buff_to_string(self, buff):
        string = ""

        for line in buff:
            string = string + line + "\n"

        # cut off the last \n
        return string[:-1]

    def fill_tracepoint_handlers_skel(self):
        buff = []
        for event in self.events:
            buff.append("static void handle_%s(void *data, /* XXX: fill header */)" % event)
            buff.append("{")
            if self.monitor_type == "per_task":
                buff.append("\tstruct task_struct *p = /* XXX: how do I get p? */;");
                buff.append("\tda_handle_event_%s(p, %s%s);" % (self.name, event, self.enum_suffix));
            else:
                buff.append("\tda_handle_event_%s(%s%s);" % (self.name, event, self.enum_suffix));
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
        min_type = self.get_minimun_type()
        nr_events = self.events.__len__()
        tracepoint_handlers = self.fill_tracepoint_handlers_skel()
        tracepoint_attach = self.fill_tracepoint_attach_probe()
        tracepoint_detach = self.fill_tracepoint_detach_helper()

        main_c = main_c.replace("MIN_TYPE", min_type)
        main_c = main_c.replace("MODEL_NAME", self.name)
        main_c = main_c.replace("NR_EVENTS", str(nr_events))
        main_c = main_c.replace("TRACEPOINT_HANDLERS_SKEL", tracepoint_handlers)
        main_c = main_c.replace("TRACEPOINT_ATTACH", tracepoint_attach)
        main_c = main_c.replace("TRACEPOINT_DETACH", tracepoint_detach)

        return main_c

    def fill_model_h_header(self):
        buff = []
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

    def __create_directory(self):
        try:
            os.mkdir(self.name)
        except FileExistsError:
            return
        except:
            print("Fail creating the output dir: %s" % self.name)

    def __create_file(self, file_name, content):
        path = "%s/%s" % (self.name, file_name)
        try:
            file = open(path, 'w')
        except FileExistsError:
            return
        except:
            print("Fail creating file: %s" % path)

        file.write(content)

        file.close()

    def __get_main_name(self):
        path = "%s/%s" % (self.name, "main.c")
        if os.path.exists(path) == False:
           return "main.c"
        return "__main.c"

    def print_files(self):
        main_c = self.fill_main_c()
        model_h = self.fill_model_h()

        self.__create_directory()

        path = "%s.c" % self.name
        self.__create_file(path, main_c)

        path = "%s.h" % self.name
        self.__create_file(path, model_h)
