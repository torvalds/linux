/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/semaphore.h>

#include "gfs2.h"

#define pv(struct, member, fmt) printk("  "#member" = "fmt"\n", struct->member);

void gfs2_quota_lvb_in(struct gfs2_quota_lvb *qb, char *lvb)
{
	struct gfs2_quota_lvb *str = (struct gfs2_quota_lvb *)lvb;

	qb->qb_magic = be32_to_cpu(str->qb_magic);
	qb->qb_limit = be64_to_cpu(str->qb_limit);
	qb->qb_warn  = be64_to_cpu(str->qb_warn);
	qb->qb_value = be64_to_cpu(str->qb_value);
}

void gfs2_quota_lvb_out(struct gfs2_quota_lvb *qb, char *lvb)
{
	struct gfs2_quota_lvb *str = (struct gfs2_quota_lvb *)lvb;

	str->qb_magic = cpu_to_be32(qb->qb_magic);
	str->qb_limit = cpu_to_be64(qb->qb_limit);
	str->qb_warn  = cpu_to_be64(qb->qb_warn);
	str->qb_value = cpu_to_be64(qb->qb_value);
}

void gfs2_quota_lvb_print(struct gfs2_quota_lvb *qb)
{
	pv(qb, qb_magic, "%u");
	pv(qb, qb_limit, "%llu");
	pv(qb, qb_warn, "%llu");
	pv(qb, qb_value, "%lld");
}

