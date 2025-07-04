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
