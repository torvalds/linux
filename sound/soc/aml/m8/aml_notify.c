/*
 *  linux/drivers/video/apollo/aout_notify.c
 *
 *  Copyright (C) 2009 amlogic
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>

static BLOCKING_NOTIFIER_HEAD(aout_notifier_list);
/**
 *	aout_register_client - register a client notifier
 *	@nb: notifier block to callback on events
 */
int aout_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&aout_notifier_list, nb);
}
EXPORT_SYMBOL(aout_register_client);

/**
 *	aout_unregister_client - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int aout_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&aout_notifier_list, nb);
}
EXPORT_SYMBOL(aout_unregister_client);

/**
 * aout_notifier_call_chain - notify clients of fb_events
 *
 */
int aout_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&aout_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(aout_notifier_call_chain);


