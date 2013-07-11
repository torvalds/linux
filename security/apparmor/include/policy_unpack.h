/*
 * AppArmor security module
 *
 * This file contains AppArmor policy loading interface function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __POLICY_INTERFACE_H
#define __POLICY_INTERFACE_H

#include <linux/list.h>

struct aa_load_ent {
	struct list_head list;
	struct aa_profile *new;
	struct aa_profile *old;
	struct aa_profile *rename;
};

void aa_load_ent_free(struct aa_load_ent *ent);
struct aa_load_ent *aa_load_ent_alloc(void);

int aa_unpack(void *udata, size_t size, struct list_head *lh, const char **ns);

#endif /* __POLICY_INTERFACE_H */
