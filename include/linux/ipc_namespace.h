#ifndef __IPC_NAMESPACE_H__
#define __IPC_NAMESPACE_H__

#include <linux/err.h>
#include <linux/idr.h>
#include <linux/rwsem.h>

struct ipc_ids {
	int in_use;
	unsigned short seq;
	unsigned short seq_max;
	struct rw_semaphore rw_mutex;
	struct idr ipcs_idr;
};

struct ipc_namespace {
	struct kref	kref;
	struct ipc_ids	ids[3];

	int		sem_ctls[4];
	int		used_sems;

	int		msg_ctlmax;
	int		msg_ctlmnb;
	int		msg_ctlmni;
	atomic_t	msg_bytes;
	atomic_t	msg_hdrs;

	size_t		shm_ctlmax;
	size_t		shm_ctlall;
	int		shm_ctlmni;
	int		shm_tot;
};

extern struct ipc_namespace init_ipc_ns;
extern atomic_t nr_ipc_ns;

#ifdef CONFIG_SYSVIPC
#define INIT_IPC_NS(ns)		.ns		= &init_ipc_ns,
#else
#define INIT_IPC_NS(ns)
#endif

#if defined(CONFIG_SYSVIPC) && defined(CONFIG_IPC_NS)
extern void free_ipc_ns(struct kref *kref);
extern struct ipc_namespace *copy_ipcs(unsigned long flags,
				       struct ipc_namespace *ns);
extern void free_ipcs(struct ipc_namespace *ns, struct ipc_ids *ids,
		      void (*free)(struct ipc_namespace *,
				   struct kern_ipc_perm *));

static inline struct ipc_namespace *get_ipc_ns(struct ipc_namespace *ns)
{
	if (ns)
		kref_get(&ns->kref);
	return ns;
}

static inline void put_ipc_ns(struct ipc_namespace *ns)
{
	kref_put(&ns->kref, free_ipc_ns);
}
#else
static inline struct ipc_namespace *copy_ipcs(unsigned long flags,
		struct ipc_namespace *ns)
{
	if (flags & CLONE_NEWIPC)
		return ERR_PTR(-EINVAL);

	return ns;
}

static inline struct ipc_namespace *get_ipc_ns(struct ipc_namespace *ns)
{
	return ns;
}

static inline void put_ipc_ns(struct ipc_namespace *ns)
{
}
#endif
#endif
