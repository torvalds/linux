/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DISK_ACCOUNTING_TYPES_H
#define _BCACHEFS_DISK_ACCOUNTING_TYPES_H

#include "darray.h"

struct accounting_pos_offset {
	struct bpos				pos;
	struct bversion				version;
	u32					offset:24,
						nr_counters:8;
};

struct bch_accounting_mem {
	DARRAY(struct accounting_pos_offset)	k;
	u64 __percpu				*v;
	unsigned				nr_counters;
};

#endif /* _BCACHEFS_DISK_ACCOUNTING_TYPES_H */
