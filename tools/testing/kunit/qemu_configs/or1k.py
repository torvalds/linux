# SPDX-License-Identifier: GPL-2.0-only
from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='openrisc',
			   kconfig='''
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_SERIAL_OF_PLATFORM=y
CONFIG_POWER_RESET=y
CONFIG_POWER_RESET_SYSCON=y
''',
			   qemu_arch='or1k',
			   kernel_path='vmlinux',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=[
					    '-machine', 'virt',
                        '-m', '512',
			  ])
