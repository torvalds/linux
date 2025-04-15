# SPDX-License-Identifier: GPL-2.0

from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='powerpc',
			   kconfig='''
CONFIG_PPC64=y
CONFIG_CPU_LITTLE_ENDIAN=y
CONFIG_HVC_CONSOLE=y
''',
			   qemu_arch='ppc64',
			   kernel_path='vmlinux',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=['-M', 'pseries', '-cpu', 'power8'])
