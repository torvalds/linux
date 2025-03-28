from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='sparc',
			   kconfig='''
CONFIG_SERIAL_SUNZILOG=y
CONFIG_SERIAL_SUNZILOG_CONSOLE=y
''',
			   qemu_arch='sparc',
			   kernel_path='arch/sparc/boot/zImage',
			   kernel_command_line='console=ttyS0 mem=256M',
			   extra_qemu_params=['-m', '256'])
