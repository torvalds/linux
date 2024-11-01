// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010, Intel Corporation.
 *
 * Author: John Fastabend <john.r.fastabend@intel.com>
 */

#include <linux/rtnetlink.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <net/dcbevent.h>

static ATOMIC_NOTIFIER_HEAD(dcbevent_notif_chain);

int register_dcbevent_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&dcbevent_notif_chain, nb);
}
EXPORT_SYMBOL(register_dcbevent_notifier);

int unregister_dcbevent_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&dcbevent_notif_chain, nb);
}
EXPORT_SYMBOL(unregister_dcbevent_notifier);

int call_dcbevent_notifiers(unsigned long val, void *v)
{
	return atomic_notifier_call_chain(&dcbevent_notif_chain, val, v);
}
