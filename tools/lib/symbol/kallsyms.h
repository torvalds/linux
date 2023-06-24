/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_KALLSYMS_H_
#define __TOOLS_KALLSYMS_H_ 1

#include <elf.h>
#include <linux/ctype.h>
#include <linux/types.h>

#ifndef KSYM_NAME_LEN
#define KSYM_NAME_LEN 512
#endif

static inline u8 kallsyms2elf_binding(char type)
{
	if (type == 'W')
		return STB_WEAK;

	return isupper(type) ? STB_GLOBAL : STB_LOCAL;
}

u8 kallsyms2elf_type(char type);

bool kallsyms__is_function(char symbol_type);

int kallsyms__parse(const char *filename, void *arg,
		    int (*process_symbol)(void *arg, const char *name,
					  char type, u64 start));

#endif /* __TOOLS_KALLSYMS_H_ */
