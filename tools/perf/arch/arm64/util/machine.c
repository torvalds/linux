// SPDX-License-Identifier: GPL-2.0

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "symbol.h"

/* On arm64, kernel text segment start at high memory address,
 * for example 0xffff 0000 8xxx xxxx. Modules start at a low memory
 * address, like 0xffff 0000 00ax xxxx. When only samll amount of
 * memory is used by modules, gap between end of module's text segment
 * and start of kernel text segment may be reach 2G.
 * Therefore do not fill this gap and do not assign it to the kernel dso map.
 */

#define SYMBOL_LIMIT (1 << 12) /* 4K */

void arch__symbols__fixup_end(struct symbol *p, struct symbol *c)
{
	if ((strchr(p->name, '[') && strchr(c->name, '[') == NULL) ||
			(strchr(p->name, '[') == NULL && strchr(c->name, '[')))
		/* Limit range of last symbol in module and kernel */
		p->end += SYMBOL_LIMIT;
	else
		p->end = c->start;
	pr_debug4("%s sym:%s end:%#" PRIx64 "\n", __func__, p->name, p->end);
}
