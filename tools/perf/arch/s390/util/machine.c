// SPDX-License-Identifier: GPL-2.0
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "util.h"
#include "machine.h"
#include "api/fs/fs.h"
#include "debug.h"
#include "symbol.h"

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

/* On s390 kernel text segment start is located at very low memory addresses,
 * for example 0x10000. Modules are located at very high memory addresses,
 * for example 0x3ff xxxx xxxx. The gap between end of kernel text segment
 * and beginning of first module's text segment is very big.
 * Therefore do not fill this gap and do not assign it to the kernel dso map.
 */
void arch__symbols__fixup_end(struct symbol *p, struct symbol *c)
{
	if (strchr(p->name, '[') == NULL && strchr(c->name, '['))
		/* Last kernel symbol mapped to end of page */
		p->end = roundup(p->end, page_size);
	else
		p->end = c->start;
	pr_debug4("%s sym:%s end:%#lx\n", __func__, p->name, p->end);
}
