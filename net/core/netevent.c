// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Network event yestifiers
 *
 *	Authors:
 *      Tom Tucker             <tom@opengridcomputing.com>
 *      Steve Wise             <swise@opengridcomputing.com>
 *
 *	Fixes:
 */

#include <linux/rtnetlink.h>
#include <linux/yestifier.h>
#include <linux/export.h>
#include <net/netevent.h>

static ATOMIC_NOTIFIER_HEAD(netevent_yestif_chain);

/**
 *	register_netevent_yestifier - register a netevent yestifier block
 *	@nb: yestifier
 *
 *	Register a yestifier to be called when a netevent occurs.
 *	The yestifier passed is linked into the kernel structures and must
 *	yest be reused until it has been unregistered. A negative erryes code
 *	is returned on a failure.
 */
int register_netevent_yestifier(struct yestifier_block *nb)
{
	return atomic_yestifier_chain_register(&netevent_yestif_chain, nb);
}
EXPORT_SYMBOL_GPL(register_netevent_yestifier);

/**
 *	netevent_unregister_yestifier - unregister a netevent yestifier block
 *	@nb: yestifier
 *
 *	Unregister a yestifier previously registered by
 *	register_neigh_yestifier(). The yestifier is unlinked into the
 *	kernel structures and may then be reused. A negative erryes code
 *	is returned on a failure.
 */

int unregister_netevent_yestifier(struct yestifier_block *nb)
{
	return atomic_yestifier_chain_unregister(&netevent_yestif_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_netevent_yestifier);

/**
 *	call_netevent_yestifiers - call all netevent yestifier blocks
 *      @val: value passed unmodified to yestifier function
 *      @v:   pointer passed unmodified to yestifier function
 *
 *	Call all neighbour yestifier blocks.  Parameters and return value
 *	are as for yestifier_call_chain().
 */

int call_netevent_yestifiers(unsigned long val, void *v)
{
	return atomic_yestifier_call_chain(&netevent_yestif_chain, val, v);
}
EXPORT_SYMBOL_GPL(call_netevent_yestifiers);
