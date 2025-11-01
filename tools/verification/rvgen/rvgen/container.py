#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# Generator for runtime verification monitor container

from . import generator


class Container(generator.RVGenerator):
    template_dir = "container"

    def __init__(self, extra_params={}):
        super().__init__(extra_params)
        self.name = extra_params.get("model_name")
        self.main_h = self._read_template_file("main.h")

    def fill_model_h(self):
        main_h = self.main_h
        main_h = main_h.replace("%%MODEL_NAME%%", self.name)
        return main_h

    def fill_kconfig_tooltip(self):
        """Override to produce a marker for this container in the Kconfig"""
        container_marker = self._kconfig_marker(self.name) + "\n"
        result = super().fill_kconfig_tooltip()
        if self.auto_patch:
            self._patch_file("Kconfig",
                             self._kconfig_marker(), container_marker)
            return result
        return result + container_marker
