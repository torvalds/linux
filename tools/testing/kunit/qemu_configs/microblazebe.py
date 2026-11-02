# SPDX-License-Identifier: GPL-2.0-only
from ..qemu_config import QemuArchParams

QEMU_ARCH = QemuArchParams(linux_arch='microblaze',
						   kconfig='''
CONFIG_KERNEL_BASE_ADDR=0x90000000
CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR=1
CONFIG_XILINX_MICROBLAZE0_USE_PCMP_INSTR=1
CONFIG_XILINX_MICROBLAZE0_USE_BARREL=1
CONFIG_XILINX_MICROBLAZE0_USE_DIV=0
CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL=1
CONFIG_XILINX_MICROBLAZE0_USE_FPU=0
CONFIG_CPU_BIG_ENDIAN=y
CONFIG_SERIAL_UARTLITE=y
CONFIG_SERIAL_UARTLITE_CONSOLE=y
CONFIG_MB_POWER_OFF_THROUGH_UNALIGNED_PC=y
''',
						   qemu_arch='microblaze',
						   kernel_path='arch/microblaze/boot/linux.bin',
						   kernel_command_line='kunit_shutdown=poweroff',
						   extra_qemu_params=[
							'-M', 'petalogix-s3adsp1800',
						   ],
)
