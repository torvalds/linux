/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmdomain.h
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
