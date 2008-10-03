/*
 * linux/ipc/ipcns_notifier.c
 * Copyright (C) 2007 BULL SA. Nadia Derbey
 *
 * Notification mechanism for ipc namespaces:
 * The callback routine registered in the memory chain invokes the ipcns
 * notifier chain with the IPCNS_MEMCHANGED event.
 * Each callback routine registered in the ipcns namespace recomputes msgmni
 * for the owning namespace.
 */

#include <linux/msg.h>
#include <linux/rcupdate.h>
#include <linux/notifier.h>
#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>

#include "util.h"



static BLOCKING_NOTIFIER_HEAD(ipcns_chain);


static int ipcns_callback(struct notifier_block *self,
				unsigned long action, void *arg)
{
	struct ipc_namespace *ns;

	switch (action) {
	case IPCNS_MEMCHANGED:   /* amount of lowmem has changed */
	case IPCNS_CREATED:
	case IPCNS_REMOVED:
		/*
		 * It's time to recompute msgmni
		 */
		ns = container_of(self, struct ipc_namespace, ipcns_nb);
		/*
		 * No need to get a reference on the ns: the 1st job of
		 * free_ipc_ns() is to unregister the callback routine.
		 * blocking_notifier_chain_unregister takes the wr lock to do
		 * it.
		 * When this callback routine is called the rd lock is held by
		 * blocking_notifier_call_chain.
		 * So the ipc ns cannot be freed while we are here.
		 */
		recompute_msgmni(ns);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

int register_ipcns_notifier(struct ipc_namespace *ns)
{
	int rc;

	memset(&ns->ipcns_nb, 0, sizeof(ns->ipcns_nb));
	ns->ipcns_nb.notifier_call = ipcns_callback;
	ns->ipcns_nb.priority = IPCNS_CALLBACK_PRI;
	rc = blocking_notifier_chain_register(&ipcns_chain, &ns->ipcns_nb);
	if (!rc)
		ns->auto_msgmni = 1;
	return rc;
}

int cond_register_ipcns_notifier(struct ipc_namespace *ns)
{
	int rc;

	memset(&ns->ipcns_nb, 0, sizeof(ns->ipcns_nb));
	ns->ipcns_nb.notifier_call = ipcns_callback;
	ns->ipcns_nb.priority = IPCNS_CALLBACK_PRI;
	rc = blocking_notifier_chain_cond_register(&ipcns_chain,
							&ns->ipcns_nb);
	if (!rc)
		ns->auto_msgmni = 1;
	return rc;
}

void unregister_ipcns_notifier(struct ipc_namespace *ns)
{
	blocking_notifier_chain_unregister(&ipcns_chain, &ns->ipcns_nb);
	ns->auto_msgmni = 0;
}

int ipcns_notify(unsigned long val)
{
	return blocking_notifier_call_chain(&ipcns_chain, val, NULL);
}
