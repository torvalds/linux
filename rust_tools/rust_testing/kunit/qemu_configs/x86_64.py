from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='x86_64',
			   kconfig='''
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y''',
			   qemu_arch='x86_64',
			   kernel_path='arch/x86/boot/bzImage',
			   kernel_command_line='console=ttyS0',
			   # qboot is faster than SeaBIOS and doesn't mess up
			   # the terminal.
			   extra_qemu_params=['-bios', 'qboot.rom'])
