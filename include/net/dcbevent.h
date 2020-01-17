/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010, Intel Corporation.
 *
 * Author: John Fastabend <john.r.fastabend@intel.com>
 */

#ifndef _DCB_EVENT_H
#define _DCB_EVENT_H

enum dcbevent_yestif_type {
	DCB_APP_EVENT = 1,
};

#ifdef CONFIG_DCB
int register_dcbevent_yestifier(struct yestifier_block *nb);
int unregister_dcbevent_yestifier(struct yestifier_block *nb);
int call_dcbevent_yestifiers(unsigned long val, void *v);
#else
static inline int
register_dcbevent_yestifier(struct yestifier_block *nb)
{
	return 0;
}

static inline int unregister_dcbevent_yestifier(struct yestifier_block *nb)
{
	return 0;
}

static inline int call_dcbevent_yestifiers(unsigned long val, void *v)
{
	return 0;
}
#endif /* CONFIG_DCB */

#endif
