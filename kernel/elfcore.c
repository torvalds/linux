// SPDX-License-Identifier: GPL-2.0
#include <linux/elf.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/binfmts.h>
#include <linux/elfcore.h>

Elf_Half __weak elf_core_extra_phdrs(void)
{
	return 0;
}

int __weak elf_core_write_extra_phdrs(struct coredump_params *cprm, loff_t offset)
{
	return 1;
}

int __weak elf_core_write_extra_data(struct coredump_params *cprm)
{
	return 1;
}

size_t __weak elf_core_extra_data_size(void)
{
	return 0;
}
