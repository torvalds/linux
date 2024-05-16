from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='arm64',
			   kconfig='''
CONFIG_SERIAL_AMBA_PL010=y
CONFIG_SERIAL_AMBA_PL010_CONSOLE=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y''',
			   qemu_arch='aarch64',
			   kernel_path='arch/arm64/boot/Image.gz',
			   kernel_command_line='console=ttyAMA0',
			   extra_qemu_params=['-machine', 'virt', '-cpu', 'max,pauth-impdef=on'])
