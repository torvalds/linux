from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='s390',
			   kconfig='''
CONFIG_EXPERT=y
CONFIG_TUNE_ZEC12=y
CONFIG_NUMA=y
CONFIG_MODULES=y''',
			   qemu_arch='s390x',
			   kernel_path='arch/s390/boot/bzImage',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=[
					   '-machine s390-ccw-virtio',
					   '-cpu qemu',])
