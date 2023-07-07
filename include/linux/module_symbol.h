/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MODULE_SYMBOL_H
#define _LINUX_MODULE_SYMBOL_H

/* This ignores the intensely annoying "mapping symbols" found in ELF files. */
static inline int is_mapping_symbol(const char *str, int is_riscv)
{
	if (str[0] == '.' && str[1] == 'L')
		return true;
	if (str[0] == 'L' && str[1] == '0')
		return true;
	/*
	 * RISC-V defines various special symbols that start with "$".  The
	 * mapping symbols, which exist to differentiate between incompatible
	 * instruction encodings when disassembling, show up all over the place
	 * and are generally not meant to be treated like other symbols.  So
	 * just ignore any of the special symbols.
	 */
	if (is_riscv)
		return str[0] == '$';

	return str[0] == '$' &&
	       (str[1] == 'a' || str[1] == 'd' || str[1] == 't' || str[1] == 'x')
	       && (str[2] == '\0' || str[2] == '.');
}

#endif /* _LINUX_MODULE_SYMBOL_H */
