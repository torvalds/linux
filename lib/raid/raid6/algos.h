/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2003 H. Peter Anvin - All Rights Reserved
 */
#ifndef _PQ_IMPL_H
#define _PQ_IMPL_H

#include <linux/init.h>
#include <linux/raid/pq_tables.h>

/* Routine choices */
struct raid6_calls {
	const char *name;
	void (*gen_syndrome)(int disks, size_t bytes, void **ptrs);
	void (*xor_syndrome)(int disks, int start, int stop, size_t bytes,
			void **ptrs);
};

struct raid6_recov_calls {
	const char *name;
	void (*data2)(int disks, size_t bytes, int faila, int failb,
			void **ptrs);
	void (*datap)(int disks, size_t bytes, int faila, void **ptrs);
};

void __init raid6_algo_add(const struct raid6_calls *algo);
void __init raid6_algo_add_default(void);
void __init raid6_recov_algo_add(const struct raid6_recov_calls *algo);

/* for the kunit test */
const struct raid6_calls *raid6_algo_find(unsigned int idx);
const struct raid6_recov_calls *raid6_recov_algo_find(unsigned int idx);

/* generic implementations */
extern const struct raid6_calls raid6_intx1;
extern const struct raid6_calls raid6_intx2;
extern const struct raid6_calls raid6_intx4;
extern const struct raid6_calls raid6_intx8;
extern const struct raid6_recov_calls raid6_recov_intx1;

#endif /* _PQ_IMPL_H */
