/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IPC_H
#define _LINUX_IPC_H

#include <linux/spinlock.h>
#include <linux/uidgid.h>
#include <linux/rhashtable-types.h>
#include <uapi/linux/ipc.h>
#include <linux/refcount.h>

/* used by in-kernel data structures */
struct kern_ipc_perm {
	spinlock_t	lock;
	bool		deleted;
	int		id;
	key_t		key;
	kuid_t		uid;
	kgid_t		gid;
	kuid_t		cuid;
	kgid_t		cgid;
	umode_t		mode;
	unsigned long	seq;
	void		*security;

	struct rhash_head khtnode;

	struct rcu_head rcu;
	refcount_t refcount;
	/* routine to call to free an existing IPC object */
	/* it is used by ipc_rcu_putref() if refcount is zero */
	/* can be one of sem_rcu_free, shm_rcu_free, msg_rcu_free */
	void (*rcu_free)(struct rcu_head *head);
} ____cacheline_aligned_in_smp __randomize_layout;

#endif /* _LINUX_IPC_H */
