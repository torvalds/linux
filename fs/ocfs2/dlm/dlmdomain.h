/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmdomain.h
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#ifndef DLMDOMAIN_H
#define DLMDOMAIN_H

extern spinlock_t dlm_domain_lock;
extern struct list_head dlm_domains;

static inline int dlm_joined(struct dlm_ctxt *dlm)
{
	int ret = 0;

	spin_lock(&dlm_domain_lock);
	if (dlm->dlm_state == DLM_CTXT_JOINED)
		ret = 1;
	spin_unlock(&dlm_domain_lock);

	return ret;
}

static inline int dlm_shutting_down(struct dlm_ctxt *dlm)
{
	int ret = 0;

	spin_lock(&dlm_domain_lock);
	if (dlm->dlm_state == DLM_CTXT_IN_SHUTDOWN)
		ret = 1;
	spin_unlock(&dlm_domain_lock);

	return ret;
}

void dlm_fire_domain_eviction_callbacks(struct dlm_ctxt *dlm,
					int node_num);

#endif
