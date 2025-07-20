# SPDX-License-Identifier: GPL-2.0

from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='powerpc',
			   kconfig='''
CONFIG_PPC32=y
CONFIG_CPU_BIG_ENDIAN=y
CONFIG_ADB_CUDA=y
CONFIG_SERIAL_PMACZILOG=y
CONFIG_SERIAL_PMACZILOG_TTYS=y
CONFIG_SERIAL_PMACZILOG_CONSOLE=y
''',
			   qemu_arch='ppc',
			   kernel_path='vmlinux',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=['-M', 'g3beige', '-cpu', 'max'])
