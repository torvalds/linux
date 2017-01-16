/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (secid) manipulation fns
 *
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * AppArmor allocates a unique secid for every profile loaded.  If a profile
 * is replaced it receives the secid of the profile it is replacing.
 *
 * The secid value of 0 is invalid.
 */

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>

#include "include/secid.h"

/* global counter from which secids are allocated */
static u32 global_secid;
static DEFINE_SPINLOCK(secid_lock);

/* TODO FIXME: add secid to profile mapping, and secid recycling */

/**
 * aa_alloc_secid - allocate a new secid for a profile
 */
u32 aa_alloc_secid(void)
{
	u32 secid;

	/*
	 * TODO FIXME: secid recycling - part of profile mapping table
	 */
	spin_lock(&secid_lock);
	secid = (++global_secid);
	spin_unlock(&secid_lock);
	return secid;
}

/**
 * aa_free_secid - free a secid
 * @secid: secid to free
 */
void aa_free_secid(u32 secid)
{
	;			/* NOP ATM */
}
