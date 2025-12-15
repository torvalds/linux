// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LSM notifier functions
 *
 */

#include <linux/notifier.h>
#include <linux/security.h>

static BLOCKING_NOTIFIER_HEAD(blocking_lsm_notifier_chain);

int call_blocking_lsm_notifier(enum lsm_event event, void *data)
{
	return blocking_notifier_call_chain(&blocking_lsm_notifier_chain,
					    event, data);
}
EXPORT_SYMBOL(call_blocking_lsm_notifier);

int register_blocking_lsm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&blocking_lsm_notifier_chain,
						nb);
}
EXPORT_SYMBOL(register_blocking_lsm_notifier);

int unregister_blocking_lsm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&blocking_lsm_notifier_chain,
						  nb);
}
EXPORT_SYMBOL(unregister_blocking_lsm_notifier);
