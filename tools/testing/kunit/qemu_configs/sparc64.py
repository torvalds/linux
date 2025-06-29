# SPDX-License-Identifier: GPL-2.0

from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='sparc',
			   kconfig='''
CONFIG_64BIT=y
CONFIG_SPARC64=y
CONFIG_PCI=y
CONFIG_SERIAL_SUNSU=y
CONFIG_SERIAL_SUNSU_CONSOLE=y
''',
			   qemu_arch='sparc64',
			   kernel_path='arch/sparc/boot/image',
			   kernel_command_line='console=ttyS0 kunit_shutdown=poweroff',
			   extra_qemu_params=[])
