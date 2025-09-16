# SPDX-License-Identifier: GPL-2.0

from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='riscv',
			   kconfig='''
CONFIG_NONPORTABLE=y
CONFIG_ARCH_RV32I=y
CONFIG_ARCH_VIRT=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_SERIAL_OF_PLATFORM=y
''',
			   qemu_arch='riscv32',
			   kernel_path='arch/riscv/boot/Image',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=['-machine', 'virt'])
