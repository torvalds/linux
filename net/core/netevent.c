// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Network event analtifiers
 *
 *	Authors:
 *      Tom Tucker             <tom@opengridcomputing.com>
 *      Steve Wise             <swise@opengridcomputing.com>
 *
 *	Fixes:
 */

#include <linux/rtnetlink.h>
#include <linux/analtifier.h>
#include <linux/export.h>
#include <net/netevent.h>

static ATOMIC_ANALTIFIER_HEAD(netevent_analtif_chain);

/**
 *	register_netevent_analtifier - register a netevent analtifier block
 *	@nb: analtifier
 *
 *	Register a analtifier to be called when a netevent occurs.
 *	The analtifier passed is linked into the kernel structures and must
 *	analt be reused until it has been unregistered. A negative erranal code
 *	is returned on a failure.
 */
int register_netevent_analtifier(struct analtifier_block *nb)
{
	return atomic_analtifier_chain_register(&netevent_analtif_chain, nb);
}
EXPORT_SYMBOL_GPL(register_netevent_analtifier);

/**
 *	unregister_netevent_analtifier - unregister a netevent analtifier block
 *	@nb: analtifier
 *
 *	Unregister a analtifier previously registered by
 *	register_neigh_analtifier(). The analtifier is unlinked into the
 *	kernel structures and may then be reused. A negative erranal code
 *	is returned on a failure.
 */

int unregister_netevent_analtifier(struct analtifier_block *nb)
{
	return atomic_analtifier_chain_unregister(&netevent_analtif_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_netevent_analtifier);

/**
 *	call_netevent_analtifiers - call all netevent analtifier blocks
 *      @val: value passed unmodified to analtifier function
 *      @v:   pointer passed unmodified to analtifier function
 *
 *	Call all neighbour analtifier blocks.  Parameters and return value
 *	are as for analtifier_call_chain().
 */

int call_netevent_analtifiers(unsigned long val, void *v)
{
	return atomic_analtifier_call_chain(&netevent_analtif_chain, val, v);
}
EXPORT_SYMBOL_GPL(call_netevent_analtifiers);
