from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='powerpc',
			   kconfig='''
CONFIG_PPC64=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_HVC_CONSOLE=y''',
			   qemu_arch='ppc64',
			   kernel_path='vmlinux',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=['-M pseries', '-cpu power8'])
