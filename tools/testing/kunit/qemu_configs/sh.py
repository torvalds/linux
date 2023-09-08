# SPDX-License-Identifier: GPL-2.0-only
from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='sh',
			   kconfig='''
CONFIG_CPU_SUBTYPE_SH7751R=y
CONFIG_MEMORY_START=0x0c000000
CONFIG_SH_RTS7751R2D=y
CONFIG_RTS7751R2D_PLUS=y
CONFIG_SERIAL_SH_SCI=y''',
			   qemu_arch='sh4',
			   kernel_path='arch/sh/boot/zImage',
			   kernel_command_line='console=ttySC1',
			   serial='null',
			   extra_qemu_params=[
					    '-machine', 'r2d',
					    '-serial', 'mon:stdio'])
