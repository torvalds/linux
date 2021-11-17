/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_LINUX_KALLSYMS_H_
#define _LIBLOCKDEP_LINUX_KALLSYMS_H_

#include <linux/kernel.h>
#include <stdio.h>
#include <unistd.h>

#define KSYM_NAME_LEN 128

struct module;

static inline const char *kallsyms_lookup(unsigned long addr,
					  unsigned long *symbolsize,
					  unsigned long *offset,
					  char **modname, char *namebuf)
{
	return NULL;
}

#include <execinfo.h>
#include <stdlib.h>
static inline void print_ip_sym(const char *loglvl, unsigned long ip)
{
	char **name;

	name = backtrace_symbols((void **)&ip, 1);

	dprintf(STDOUT_FILENO, "%s\n", *name);

	free(name);
}

#endif
