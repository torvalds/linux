/* Task credentials management
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/init_task.h>
#include <linux/security.h>

/*
 * The initial credentials for the initial task
 */
struct cred init_cred = {
	.usage			= ATOMIC_INIT(3),
	.securebits		= SECUREBITS_DEFAULT,
	.cap_inheritable	= CAP_INIT_INH_SET,
	.cap_permitted		= CAP_FULL_SET,
	.cap_effective		= CAP_INIT_EFF_SET,
	.cap_bset		= CAP_INIT_BSET,
	.user			= INIT_USER,
	.group_info		= &init_groups,
};

/*
 * The RCU callback to actually dispose of a set of credentials
 */
static void put_cred_rcu(struct rcu_head *rcu)
{
	struct cred *cred = container_of(rcu, struct cred, rcu);

	BUG_ON(atomic_read(&cred->usage) != 0);

	key_put(cred->thread_keyring);
	key_put(cred->request_key_auth);
	put_group_info(cred->group_info);
	free_uid(cred->user);
	security_cred_free(cred);
	kfree(cred);
}

/**
 * __put_cred - Destroy a set of credentials
 * @sec: The record to release
 *
 * Destroy a set of credentials on which no references remain.
 */
void __put_cred(struct cred *cred)
{
	call_rcu(&cred->rcu, put_cred_rcu);
}
EXPORT_SYMBOL(__put_cred);

/*
 * Copy credentials for the new process created by fork()
 */
int copy_creds(struct task_struct *p, unsigned long clone_flags)
{
	struct cred *pcred;
	int ret;

	pcred = kmemdup(p->cred, sizeof(*p->cred), GFP_KERNEL);
	if (!pcred)
		return -ENOMEM;

#ifdef CONFIG_SECURITY
	pcred->security = NULL;
#endif

	ret = security_cred_alloc(pcred);
	if (ret < 0) {
		kfree(pcred);
		return ret;
	}

	atomic_set(&pcred->usage, 1);
	get_group_info(pcred->group_info);
	get_uid(pcred->user);
	key_get(pcred->thread_keyring);
	key_get(pcred->request_key_auth);

	atomic_inc(&pcred->user->processes);

	/* RCU assignment is unneeded here as no-one can have accessed this
	 * pointer yet, barring us */
	p->cred = pcred;
	return 0;
}
