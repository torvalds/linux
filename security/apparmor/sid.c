/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (sid) manipulation fns
 *
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * AppArmor allocates a unique sid for every profile loaded.  If a profile
 * is replaced it receives the sid of the profile it is replacing.
 *
 * The sid value of 0 is invalid.
 */

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>

#include "include/sid.h"

/* global counter from which sids are allocated */
static u32 global_sid;
static DEFINE_SPINLOCK(sid_lock);

/* TODO FIXME: add sid to profile mapping, and sid recycling */

/**
 * aa_alloc_sid - allocate a new sid for a profile
 */
u32 aa_alloc_sid(void)
{
	u32 sid;

	/*
	 * TODO FIXME: sid recycling - part of profile mapping table
	 */
	spin_lock(&sid_lock);
	sid = (++global_sid);
	spin_unlock(&sid_lock);
	return sid;
}

/**
 * aa_free_sid - free a sid
 * @sid: sid to free
 */
void aa_free_sid(u32 sid)
{
	;			/* NOP ATM */
}
