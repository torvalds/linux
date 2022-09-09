from ..qemu_config import QemuArchParams
import os
import os.path
import sys

GITHUB_OPENSBI_URL = 'https://github.com/qemu/qemu/raw/master/pc-bios/opensbi-riscv64-generic-fw_dynamic.bin'
OPENSBI_FILE = os.path.basename(GITHUB_OPENSBI_URL)

if not os.path.isfile(OPENSBI_FILE):
	print('\n\nOpenSBI file is not in the current working directory.\n'
	      'Would you like me to download it for you from:\n' + GITHUB_OPENSBI_URL + ' ?\n')
	response = input('yes/[no]: ')
	if response.strip() == 'yes':
		os.system('wget ' + GITHUB_OPENSBI_URL)
	else:
		sys.exit()

QEMU_ARCH = QemuArchParams(linux_arch='riscv',
			   kconfig='''
CONFIG_SOC_VIRT=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_SERIAL_OF_PLATFORM=y
CONFIG_RISCV_SBI_V01=y
CONFIG_SERIAL_EARLYCON_RISCV_SBI=y''',
			   qemu_arch='riscv64',
			   kernel_path='arch/riscv/boot/Image',
			   kernel_command_line='console=ttyS0',
			   extra_qemu_params=[
					   '-machine', 'virt',
					   '-cpu', 'rv64',
					   '-bios', 'opensbi-riscv64-generic-fw_dynamic.bin'])
