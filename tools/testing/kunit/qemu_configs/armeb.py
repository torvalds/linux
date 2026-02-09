# SPDX-License-Identifier: GPL-2.0

from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='arm',
			   kconfig='''
CONFIG_CPU_BIG_ENDIAN=y
CONFIG_ARCH_VIRT=y
CONFIG_SERIAL_AMBA_PL010=y
CONFIG_SERIAL_AMBA_PL010_CONSOLE=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y''',
			   qemu_arch='arm',
			   kernel_path='arch/arm/boot/zImage',
			   kernel_command_line='console=ttyAMA0',
			   extra_qemu_params=['-machine', 'virt'])
