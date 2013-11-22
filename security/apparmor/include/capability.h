/*
 * AppArmor security module
 *
 * This file contains AppArmor capability mediation definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_CAPABILITY_H
#define __AA_CAPABILITY_H

#include <linux/sched.h>

#include "apparmorfs.h"

struct aa_profile;

/* aa_caps - confinement data for capabilities
 * @allowed: capabilities mask
 * @audit: caps that are to be audited
 * @quiet: caps that should not be audited
 * @kill: caps that when requested will result in the task being killed
 * @extended: caps that are subject finer grained mediation
 */
struct aa_caps {
	kernel_cap_t allow;
	kernel_cap_t audit;
	kernel_cap_t quiet;
	kernel_cap_t kill;
	kernel_cap_t extended;
};

extern struct aa_fs_entry aa_fs_entry_caps[];

int aa_capable(struct task_struct *task, struct aa_profile *profile, int cap,
	       int audit);

static inline void aa_free_cap_rules(struct aa_caps *caps)
{
	/* NOP */
}

#endif /* __AA_CAPBILITY_H */
