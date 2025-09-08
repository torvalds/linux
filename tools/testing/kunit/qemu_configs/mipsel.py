# SPDX-License-Identifier: GPL-2.0

from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='mips',
                           kconfig='''
CONFIG_32BIT=y
CONFIG_CPU_LITTLE_ENDIAN=y
CONFIG_MIPS_MALTA=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_POWER_RESET=y
CONFIG_POWER_RESET_SYSCON=y
''',
                           qemu_arch='mipsel',
                           kernel_path='vmlinuz',
                           kernel_command_line='console=ttyS0',
                           extra_qemu_params=['-M', 'malta'])
