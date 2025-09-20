from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='i386',
			   kconfig='''
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y''',
			   qemu_arch='i386',
			   kernel_path='arch/x86/boot/bzImage',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=[])
