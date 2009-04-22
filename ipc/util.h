/*
 * linux/ipc/util.h
 * Copyright (C) 1999 Christoph Rohland
 *
 * ipc helper functions (c) 1999 Manfred Spraul <manfred@colorfullife.com>
 * namespaces support.      2006 OpenVZ, SWsoft Inc.
 *                               Pavel Emelianov <xemul@openvz.org>
 */

#ifndef _IPC_UTIL_H
#define _IPC_UTIL_H

#include <linux/err.h>

#define SEQ_MULTIPLIER	(IPCMNI)

void sem_init (void);
void msg_init (void);
void shm_init (void);

struct ipc_namespace;

#ifdef CONFIG_POSIX_MQUEUE
extern void mq_clear_sbinfo(struct ipc_namespace *ns);
extern void mq_put_mnt(struct ipc_namespace *ns);
#else
static inline void mq_clear_sbinfo(struct ipc_namespace *ns) { }
static inline void mq_put_mnt(struct ipc_namespace *ns) { }
#endif

#ifdef CONFIG_SYSVIPC
void sem_init_ns(struct ipc_namespace *ns);
void msg_init_ns(struct ipc_namespace *ns);
void shm_init_ns(struct ipc_namespace *ns);

void sem_exit_ns(struct ipc_namespace *ns);
void msg_exit_ns(struct ipc_namespace *ns);
void shm_exit_ns(struct ipc_namespace *ns);
#else
static inline void sem_init_ns(struct ipc_namespace *ns) { }
static inline void msg_init_ns(struct ipc_namespace *ns) { }
static inline void shm_init_ns(struct ipc_namespace *ns) { }

static inline void sem_exit_ns(struct ipc_namespace *ns) { }
static inline void msg_exit_ns(struct ipc_namespace *ns) { }
static inline void shm_exit_ns(struct ipc_namespace *ns) { }
#endif

/*
 * Structure that holds the parameters needed by the ipc operations
 * (see after)
 */
struct ipc_params {
	key_t key;
	int flg;
	union {
		size_t size;	/* for shared memories */
		int nsems;	/* for semaphores */
	} u;			/* holds the getnew() specific param */
};

/*
 * Structure that holds some ipc operations. This structure is used to unify
 * the calls to sys_msgget(), sys_semget(), sys_shmget()
 *      . routine to call to create a new ipc object. Can be one of newque,
 *        newary, newseg
 *      . routine to call to check permissions for a new ipc object.
 *        Can be one of security_msg_associate, security_sem_associate,
 *        security_shm_associate
 *      . routine to call for an extra check if needed
 */
struct ipc_ops {
	int (*getnew) (struct ipc_namespace *, struct ipc_params *);
	int (*associate) (struct kern_ipc_perm *, int);
	int (*more_checks) (struct kern_ipc_perm *, struct ipc_params *);
};

struct seq_file;
struct ipc_ids;

void ipc_init_ids(struct ipc_ids *);
#ifdef CONFIG_PROC_FS
void __init ipc_init_proc_interface(const char *path, const char *header,
		int ids, int (*show)(struct seq_file *, void *));
#else
#define ipc_init_proc_interface(path, header, ids, show) do {} while (0)
#endif

#define IPC_SEM_IDS	0
#define IPC_MSG_IDS	1
#define IPC_SHM_IDS	2

#define ipcid_to_idx(id) ((id) % SEQ_MULTIPLIER)

/* must be called with ids->rw_mutex acquired for writing */
int ipc_addid(struct ipc_ids *, struct kern_ipc_perm *, int);

/* must be called with ids->rw_mutex acquired for reading */
int ipc_get_maxid(struct ipc_ids *);

/* must be called with both locks acquired. */
void ipc_rmid(struct ipc_ids *, struct kern_ipc_perm *);

/* must be called with ipcp locked */
int ipcperms(struct kern_ipc_perm *ipcp, short flg);

/* for rare, potentially huge allocations.
 * both function can sleep
 */
void* ipc_alloc(int size);
void ipc_free(void* ptr, int size);

/*
 * For allocation that need to be freed by RCU.
 * Objects are reference counted, they start with reference count 1.
 * getref increases the refcount, the putref call that reduces the recount
 * to 0 schedules the rcu destruction. Caller must guarantee locking.
 */
void* ipc_rcu_alloc(int size);
void ipc_rcu_getref(void *ptr);
void ipc_rcu_putref(void *ptr);

struct kern_ipc_perm *ipc_lock(struct ipc_ids *, int);

void kernel_to_ipc64_perm(struct kern_ipc_perm *in, struct ipc64_perm *out);
void ipc64_perm_to_ipc_perm(struct ipc64_perm *in, struct ipc_perm *out);
void ipc_update_perm(struct ipc64_perm *in, struct kern_ipc_perm *out);
struct kern_ipc_perm *ipcctl_pre_down(struct ipc_ids *ids, int id, int cmd,
				      struct ipc64_perm *perm, int extra_perm);

#if defined(__ia64__) || defined(__x86_64__) || defined(__hppa__) || defined(__XTENSA__)
  /* On IA-64, we always use the "64-bit version" of the IPC structures.  */ 
# define ipc_parse_version(cmd)	IPC_64
#else
int ipc_parse_version (int *cmd);
#endif

extern void free_msg(struct msg_msg *msg);
extern struct msg_msg *load_msg(const void __user *src, int len);
extern int store_msg(void __user *dest, struct msg_msg *msg, int len);

extern void recompute_msgmni(struct ipc_namespace *);

static inline int ipc_buildid(int id, int seq)
{
	return SEQ_MULTIPLIER * seq + id;
}

/*
 * Must be called with ipcp locked
 */
static inline int ipc_checkid(struct kern_ipc_perm *ipcp, int uid)
{
	if (uid / SEQ_MULTIPLIER != ipcp->seq)
		return 1;
	return 0;
}

static inline void ipc_lock_by_ptr(struct kern_ipc_perm *perm)
{
	rcu_read_lock();
	spin_lock(&perm->lock);
}

static inline void ipc_unlock(struct kern_ipc_perm *perm)
{
	spin_unlock(&perm->lock);
	rcu_read_unlock();
}

struct kern_ipc_perm *ipc_lock_check(struct ipc_ids *ids, int id);
int ipcget(struct ipc_namespace *ns, struct ipc_ids *ids,
			struct ipc_ops *ops, struct ipc_params *params);

#endif
