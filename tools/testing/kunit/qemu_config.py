# SPDX-License-Identifier: GPL-2.0
#
# Collection of configs for building non-UML kernels and running them on QEMU.
#
# Copyright (C) 2021, Google LLC.
# Author: Brendan Higgins <brendanhiggins@google.com>

from collections import namedtuple


QemuArchParams = namedtuple('QemuArchParams', ['linux_arch',
					       'kconfig',
					       'qemu_arch',
					       'kernel_path',
					       'kernel_command_line',
					       'extra_qemu_params'])
