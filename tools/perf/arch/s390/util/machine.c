// SPDX-License-Identifier: GPL-2.0
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <internal/lib.h> // page_size
#include "machine.h"
#include "api/fs/fs.h"
#include "debug.h"
#include "symbol.h"

int arch__fix_module_text_start(u64 *start, u64 *size, const char *name)
{
	u64 m_start = *start;
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "module/%.*s/sections/.text",
				(int)strlen(name) - 2, name + 1);
	if (sysfs__read_ull(path, (unsigned long long *)start) < 0) {
		pr_debug2("Using module %s start:%#lx\n", path, m_start);
		*start = m_start;
	} else {
		/* Successful read of the modules segment text start address.
		 * Calculate difference between module start address
		 * in memory and module text segment start address.
		 * For example module load address is 0x3ff8011b000
		 * (from /proc/modules) and module text segment start
		 * address is 0x3ff8011b870 (from file above).
		 *
		 * Adjust the module size and subtract the GOT table
		 * size located at the beginning of the module.
		 */
		*size -= (*start - m_start);
	}

	return 0;
}
