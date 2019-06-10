// SPDX-License-Identifier: GPL-2.0
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "util.h"
#include "machine.h"
#include "api/fs/fs.h"
#include "debug.h"

int arch__fix_module_text_start(u64 *start, const char *name)
{
	u64 m_start = *start;
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "module/%.*s/sections/.text",
				(int)strlen(name) - 2, name + 1);
	if (sysfs__read_ull(path, (unsigned long long *)start) < 0) {
		pr_debug2("Using module %s start:%#lx\n", path, m_start);
		*start = m_start;
	}

	return 0;
}
