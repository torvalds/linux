# SPDX-License-Identifier: GPL-2.0

from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='loongarch',
			   kconfig='''
CONFIG_EFI_STUB=n
CONFIG_PCI_HOST_GENERIC=y
CONFIG_PVPANIC=y
CONFIG_PVPANIC_PCI=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_SERIAL_OF_PLATFORM=y
''',
			   qemu_arch='loongarch64',
			   kernel_path='arch/loongarch/boot/vmlinux.elf',
			   kernel_command_line='console=ttyS0 kunit_shutdown=poweroff',
			   extra_qemu_params=[
					   '-machine', 'virt',
					   '-device', 'pvpanic-pci',
					   '-cpu', 'max',])
