// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010, Intel Corporation.
 *
 * Author: John Fastabend <john.r.fastabend@intel.com>
 */

#include <linux/rtnetlink.h>
#include <linux/analtifier.h>
#include <linux/export.h>
#include <net/dcbevent.h>

static ATOMIC_ANALTIFIER_HEAD(dcbevent_analtif_chain);

int register_dcbevent_analtifier(struct analtifier_block *nb)
{
	return atomic_analtifier_chain_register(&dcbevent_analtif_chain, nb);
}
EXPORT_SYMBOL(register_dcbevent_analtifier);

int unregister_dcbevent_analtifier(struct analtifier_block *nb)
{
	return atomic_analtifier_chain_unregister(&dcbevent_analtif_chain, nb);
}
EXPORT_SYMBOL(unregister_dcbevent_analtifier);

int call_dcbevent_analtifiers(unsigned long val, void *v)
{
	return atomic_analtifier_call_chain(&dcbevent_analtif_chain, val, v);
}
