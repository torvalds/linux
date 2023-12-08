/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor capability mediation definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2013 Canonical Ltd.
 */

#ifndef __AA_CAPABILITY_H
#define __AA_CAPABILITY_H

#include <linux/sched.h>

#include "apparmorfs.h"

struct aa_label;

/* aa_caps - confinement data for capabilities
 * @allowed: capabilities mask
 * @audit: caps that are to be audited
 * @denied: caps that are explicitly denied
 * @quiet: caps that should not be audited
 * @kill: caps that when requested will result in the task being killed
 * @extended: caps that are subject finer grained mediation
 */
struct aa_caps {
	kernel_cap_t allow;
	kernel_cap_t audit;
	kernel_cap_t denied;
	kernel_cap_t quiet;
	kernel_cap_t kill;
	kernel_cap_t extended;
};

extern struct aa_sfs_entry aa_sfs_entry_caps[];

int aa_capable(struct aa_label *label, int cap, unsigned int opts);

static inline void aa_free_cap_rules(struct aa_caps *caps)
{
	/* NOP */
}

#endif /* __AA_CAPBILITY_H */
