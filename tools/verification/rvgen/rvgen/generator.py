#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# Abtract class for generating kernel runtime verification monitors from specification file

import platform
import os


class RVGenerator:
    rv_dir = "kernel/trace/rv"

    def __init__(self, extra_params={}):
        self.name = extra_params.get("model_name")
        self.parent = extra_params.get("parent")
        self.abs_template_dir = \
            os.path.join(os.path.dirname(__file__), "templates", self.template_dir)
        self.main_c = self._read_template_file("main.c")
        self.kconfig = self._read_template_file("Kconfig")
        self.description = extra_params.get("description", self.name) or "auto-generated"
        self.auto_patch = extra_params.get("auto_patch")
        if self.auto_patch:
            self.__fill_rv_kernel_dir()

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

    def _read_file(self, path):
        try:
            fd = open(path, 'r')
        except OSError:
            raise Exception("Cannot open the file: %s" % path)

        content = fd.read()

        fd.close()
        return content

    def _read_template_file(self, file):
        try:
            path = os.path.join(self.abs_template_dir, file)
            return self._read_file(path)
        except Exception:
            # Specific template file not found. Try the generic template file in the template/
            # directory, which is one level up
            path = os.path.join(self.abs_template_dir, "..", file)
            return self._read_file(path)

    def fill_parent(self):
        return "&rv_%s" % self.parent if self.parent else "NULL"

    def fill_include_parent(self):
        if self.parent:
            return "#include <monitors/%s/%s.h>\n" % (self.parent, self.parent)
        return ""

    def fill_tracepoint_handlers_skel(self):
        return "NotImplemented"

    def fill_tracepoint_attach_probe(self):
        return "NotImplemented"

    def fill_tracepoint_detach_helper(self):
        return "NotImplemented"

    def fill_main_c(self):
        main_c = self.main_c
        tracepoint_handlers = self.fill_tracepoint_handlers_skel()
        tracepoint_attach = self.fill_tracepoint_attach_probe()
        tracepoint_detach = self.fill_tracepoint_detach_helper()
        parent = self.fill_parent()
        parent_include = self.fill_include_parent()

        main_c = main_c.replace("%%MODEL_NAME%%", self.name)
        main_c = main_c.replace("%%TRACEPOINT_HANDLERS_SKEL%%", tracepoint_handlers)
        main_c = main_c.replace("%%TRACEPOINT_ATTACH%%", tracepoint_attach)
        main_c = main_c.replace("%%TRACEPOINT_DETACH%%", tracepoint_detach)
        main_c = main_c.replace("%%DESCRIPTION%%", self.description)
        main_c = main_c.replace("%%PARENT%%", parent)
        main_c = main_c.replace("%%INCLUDE_PARENT%%", parent_include)

        return main_c

    def fill_model_h(self):
        return "NotImplemented"

    def fill_monitor_class_type(self):
        return "NotImplemented"

    def fill_monitor_class(self):
        return "NotImplemented"

    def fill_tracepoint_args_skel(self, tp_type):
        return "NotImplemented"

    def fill_monitor_deps(self):
        buff = []
        buff.append("	# XXX: add dependencies if there")
        if self.parent:
            buff.append("	depends on RV_MON_%s" % self.parent.upper())
            buff.append("	default y")
        return '\n'.join(buff)

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

    def _patch_file(self, file, marker, line):
        assert self.auto_patch
        file_to_patch = os.path.join(self.rv_dir, file)
        content = self._read_file(file_to_patch)
        content = content.replace(marker, line + "\n" + marker)
        self.__write_file(file_to_patch, content)

    def fill_tracepoint_tooltip(self):
        monitor_class_type = self.fill_monitor_class_type()
        if self.auto_patch:
            self._patch_file("rv_trace.h",
                            "// Add new monitors based on CONFIG_%s here" % monitor_class_type,
                            "#include <monitors/%s/%s_trace.h>" % (self.name, self.name))
            return "  - Patching %s/rv_trace.h, double check the result" % self.rv_dir

        return """  - Edit %s/rv_trace.h:
Add this line where other tracepoints are included and %s is defined:
#include <monitors/%s/%s_trace.h>
""" % (self.rv_dir, monitor_class_type, self.name, self.name)

    def _kconfig_marker(self, container=None) -> str:
        return "# Add new %smonitors here" % (container + " "
                                              if container else "")

    def fill_kconfig_tooltip(self):
        if self.auto_patch:
            # monitors with a container should stay together in the Kconfig
            self._patch_file("Kconfig",
                             self._kconfig_marker(self.parent),
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
            self._patch_file("Makefile",
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

    def _create_file(self, file_name, content):
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
        self._create_file(path, main_c)

        model_h = self.fill_model_h()
        path = "%s.h" % self.name
        self._create_file(path, model_h)

        kconfig = self.fill_kconfig()
        self._create_file("Kconfig", kconfig)


class Monitor(RVGenerator):
    monitor_types = { "global" : 1, "per_cpu" : 2, "per_task" : 3 }

    def __init__(self, extra_params={}):
        super().__init__(extra_params)
        self.trace_h = self._read_template_file("trace.h")

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

    def print_files(self):
        super().print_files()
        trace_h = self.fill_trace_h()
        path = "%s_trace.h" % self.name
        self._create_file(path, trace_h)
