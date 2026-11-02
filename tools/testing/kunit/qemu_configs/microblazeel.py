# SPDX-License-Identifier: GPL-2.0-only
from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='microblaze',
						   kconfig='''
CONFIG_KERNEL_BASE_ADDR=0x50000000
CONFIG_XILINX_MICROBLAZE0_FAMILY="virtex5"
CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR=1
CONFIG_XILINX_MICROBLAZE0_USE_PCMP_INSTR=1
CONFIG_XILINX_MICROBLAZE0_USE_BARREL=1
CONFIG_XILINX_MICROBLAZE0_USE_DIV=1
CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL=2
CONFIG_XILINX_MICROBLAZE0_USE_FPU=1
CONFIG_CPU_LITTLE_ENDIAN=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_SERIAL_8250_16550A_VARIANTS=y
CONFIG_SERIAL_OF_PLATFORM=y
CONFIG_MB_POWER_OFF_THROUGH_UNALIGNED_PC=y
''',
						   qemu_arch='microblaze',
						   kernel_path='arch/microblaze/boot/linux.bin',
						   kernel_command_line='kunit_shutdown=poweroff',
						   extra_qemu_params=[
							'-M', 'petalogix-ml605',
						   ],
)
