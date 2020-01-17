// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010, Intel Corporation.
 *
 * Author: John Fastabend <john.r.fastabend@intel.com>
 */

#include <linux/rtnetlink.h>
#include <linux/yestifier.h>
#include <linux/export.h>
#include <net/dcbevent.h>

static ATOMIC_NOTIFIER_HEAD(dcbevent_yestif_chain);

int register_dcbevent_yestifier(struct yestifier_block *nb)
{
	return atomic_yestifier_chain_register(&dcbevent_yestif_chain, nb);
}
EXPORT_SYMBOL(register_dcbevent_yestifier);

int unregister_dcbevent_yestifier(struct yestifier_block *nb)
{
	return atomic_yestifier_chain_unregister(&dcbevent_yestif_chain, nb);
}
EXPORT_SYMBOL(unregister_dcbevent_yestifier);

int call_dcbevent_yestifiers(unsigned long val, void *v)
{
	return atomic_yestifier_call_chain(&dcbevent_yestif_chain, val, v);
}
