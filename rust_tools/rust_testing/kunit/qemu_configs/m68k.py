# SPDX-License-Identifier: GPL-2.0-only
from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='m68k',
			   kconfig='''
CONFIG_VIRT=y''',
			   qemu_arch='m68k',
			   kernel_path='vmlinux',
			   kernel_command_line='console=hvc0',
			   extra_qemu_params=['-machine', 'virt'])
