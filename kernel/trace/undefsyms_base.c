// SPDX-License-Identifier: GPL-2.0

/*
 * simple_ring_buffer is used by the pKVM hypervisor which does not have access
 * to all kernel symbols.  Whatever is undefined when compiling this file is
 * compiler and tooling-generated symbols that can safely be ignored for
 * simple_ring_buffer.
 */

#include <linux/atomic.h>
#include <linux/string.h>
#include <asm/local.h>
#include <asm/page.h>

void undefsyms_base(void *p, int n);

static char page[PAGE_SIZE] __aligned(PAGE_SIZE);

void undefsyms_base(void *p, int n)
{
	char buffer[256] = { 0 };
	u32 u = 0;

	memset((char * volatile)page, 8, PAGE_SIZE);
	memset((char * volatile)buffer, 8, sizeof(buffer));
	memcpy((void * volatile)p, buffer, sizeof(buffer));
	cmpxchg((u32 * volatile)&u, 0, 8);
	cmpxchg((unsigned long *)p, 0, 8);
	local_set((local_t *)p, 8);
	WARN_ON(n == 0xdeadbeef);
}
