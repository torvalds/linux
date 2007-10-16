/*
 * Copyright (C) 2004 Fujitsu Siemens Computers GmbH
 * Licensed under the GPL
 *
 * Author: Bodo Stroesser <bstroesser@fujitsu-siemens.com>
 */

#ifndef __ASM_LDT_H
#define __ASM_LDT_H

#include "asm/semaphore.h"
#include "asm/host_ldt.h"

extern void ldt_host_info(void);

#define LDT_PAGES_MAX \
	((LDT_ENTRIES * LDT_ENTRY_SIZE)/PAGE_SIZE)
#define LDT_ENTRIES_PER_PAGE \
	(PAGE_SIZE/LDT_ENTRY_SIZE)
#define LDT_DIRECT_ENTRIES \
	((LDT_PAGES_MAX*sizeof(void *))/LDT_ENTRY_SIZE)

struct ldt_entry {
	__u32 a;
	__u32 b;
};

typedef struct uml_ldt {
	int entry_count;
	struct semaphore semaphore;
	union {
		struct ldt_entry * pages[LDT_PAGES_MAX];
		struct ldt_entry entries[LDT_DIRECT_ENTRIES];
	} u;
} uml_ldt_t;

#endif
