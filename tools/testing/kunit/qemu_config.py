# SPDX-License-Identifier: GPL-2.0
#
# Collection of configs for building non-UML kernels and running them on QEMU.
#
# Copyright (C) 2021, Google LLC.
# Author: Brendan Higgins <brendanhiggins@google.com>

from dataclasses import dataclass
from typing import List


@dataclass(frozen=True)
class QemuArchParams:
  linux_arch: str
  kconfig: str
  qemu_arch: str
  kernel_path: str
  kernel_command_line: str
  extra_qemu_params: List[str]
  serial: str = 'stdio'
