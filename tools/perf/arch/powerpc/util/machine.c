// SPDX-License-Identifier: GPL-2.0

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <internal/lib.h> // page_size
#include "debug.h"
#include "symbol.h"

/* On powerpc kernel text segment start at memory addresses, 0xc000000000000000
 * whereas the modules are located at very high memory addresses,
 * for example 0xc00800000xxxxxxx. The gap between end of kernel text segment
 * and beginning of first module's text segment is very high.
 * Therefore do not fill this gap and do not assign it to the kernel dso map.
 */

void arch__symbols__fixup_end(struct symbol *p, struct symbol *c)
{
	if (strchr(p->name, '[') == NULL && strchr(c->name, '['))
		/* Limit the range of last kernel symbol */
		p->end += page_size;
	else
		p->end = c->start;
	pr_debug4("%s sym:%s end:%#" PRIx64 "\n", __func__, p->name, p->end);
}
