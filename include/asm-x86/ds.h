/*
 * Debug Store (DS) support
 *
 * This provides a low-level interface to the hardware's Debug Store
 * feature that is used for last branch recording (LBR) and
 * precise-event based sampling (PEBS).
 *
 * Different architectures use a different DS layout/pointer size.
 * The below functions therefore work on a void*.
 *
 *
 * Since there is no user for PEBS, yet, only LBR (or branch
 * trace store, BTS) is supported.
 *
 *
 * Copyright (C) 2007 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, Dec 2007
 */

#ifndef _ASM_X86_DS_H
#define _ASM_X86_DS_H

#include <linux/types.h>
#include <linux/init.h>

struct cpuinfo_x86;


/* a branch trace record entry
 *
 * In order to unify the interface between various processor versions,
 * we use the below data structure for all processors.
 */
enum bts_qualifier {
	BTS_INVALID = 0,
	BTS_BRANCH,
	BTS_TASK_ARRIVES,
	BTS_TASK_DEPARTS
};

struct bts_struct {
	enum bts_qualifier qualifier;
	union {
		/* BTS_BRANCH */
		struct {
			long from_ip;
			long to_ip;
		} lbr;
		/* BTS_TASK_ARRIVES or
		   BTS_TASK_DEPARTS */
		unsigned long long timestamp;
	} variant;
};


extern int ds_allocate(void **, size_t);
extern int ds_free(void **);
extern int ds_get_bts_size(void *);
extern int ds_get_bts_index(void *);
extern int ds_read_bts(void *, size_t, struct bts_struct *);
extern int ds_write_bts(void *, const struct bts_struct *);
extern unsigned long ds_debugctl_mask(void);
extern void __cpuinit ds_init_intel(struct cpuinfo_x86 *c);

#endif /* _ASM_X86_DS_H */
