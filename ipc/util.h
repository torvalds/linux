/* SPDX-License-Identifier: GPL-2.0 */
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

#include <linux/unistd.h>
#include <linux/err.h>
#include <linux/ipc_namespace.h>

#define IPCMNI 32768  /* <= MAX_INT limit for ipc arrays (including sysctl changes) */
#define SEQ_MULTIPLIER	(IPCMNI)

void sem_init(void);
void msg_init(void);
void shm_init(void);

struct ipc_namespace;
struct pid_namespace;

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
	int (*getnew)(struct ipc_namespace *, struct ipc_params *);
	int (*associate)(struct kern_ipc_perm *, int);
	int (*more_checks)(struct kern_ipc_perm *, struct ipc_params *);
};

struct seq_file;
struct ipc_ids;

void ipc_init_ids(struct ipc_ids *ids);
#ifdef CONFIG_PROC_FS
void __init ipc_init_proc_interface(const char *path, const char *header,
		int ids, int (*show)(struct seq_file *, void *));
struct pid_namespace *ipc_seq_pid_ns(struct seq_file *);
#else
#define ipc_init_proc_interface(path, header, ids, show) do {} while (0)
#endif

#define IPC_SEM_IDS	0
#define IPC_MSG_IDS	1
#define IPC_SHM_IDS	2

#define ipcid_to_idx(id) ((id) % SEQ_MULTIPLIER)
#define ipcid_to_seqx(id) ((id) / SEQ_MULTIPLIER)
#define IPCID_SEQ_MAX min_t(int, INT_MAX/SEQ_MULTIPLIER, USHRT_MAX)

/* must be called with ids->rwsem acquired for writing */
int ipc_addid(struct ipc_ids *, struct kern_ipc_perm *, int);

/* must be called with both locks acquired. */
void ipc_rmid(struct ipc_ids *, struct kern_ipc_perm *);

/* must be called with both locks acquired. */
void ipc_set_key_private(struct ipc_ids *, struct kern_ipc_perm *);

/* must be called with ipcp locked */
int ipcperms(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp, short flg);

/**
 * ipc_get_maxidx - get the highest assigned index
 * @ids: ipc identifier set
 *
 * Called with ipc_ids.rwsem held for reading.
 */
static inline int ipc_get_maxidx(struct ipc_ids *ids)
{
	if (ids->in_use == 0)
		return -1;

	if (ids->in_use == IPCMNI)
		return IPCMNI - 1;

	return ids->max_idx;
}

/*
 * For allocation that need to be freed by RCU.
 * Objects are reference counted, they start with reference count 1.
 * getref increases the refcount, the putref call that reduces the recount
 * to 0 schedules the rcu destruction. Caller must guarantee locking.
 *
 * refcount is initialized by ipc_addid(), before that point call_rcu()
 * must be used.
 */
bool ipc_rcu_getref(struct kern_ipc_perm *ptr);
void ipc_rcu_putref(struct kern_ipc_perm *ptr,
			void (*func)(struct rcu_head *head));

struct kern_ipc_perm *ipc_obtain_object_idr(struct ipc_ids *ids, int id);

void kernel_to_ipc64_perm(struct kern_ipc_perm *in, struct ipc64_perm *out);
void ipc64_perm_to_ipc_perm(struct ipc64_perm *in, struct ipc_perm *out);
int ipc_update_perm(struct ipc64_perm *in, struct kern_ipc_perm *out);
struct kern_ipc_perm *ipcctl_obtain_check(struct ipc_namespace *ns,
					     struct ipc_ids *ids, int id, int cmd,
					     struct ipc64_perm *perm, int extra_perm);

static inline void ipc_update_pid(struct pid **pos, struct pid *pid)
{
	struct pid *old = *pos;
	if (old != pid) {
		*pos = get_pid(pid);
		put_pid(old);
	}
}

#ifdef CONFIG_ARCH_WANT_IPC_PARSE_VERSION
int ipc_parse_version(int *cmd);
#endif

extern void free_msg(struct msg_msg *msg);
extern struct msg_msg *load_msg(const void __user *src, size_t len);
extern struct msg_msg *copy_msg(struct msg_msg *src, struct msg_msg *dst);
extern int store_msg(void __user *dest, struct msg_msg *msg, size_t len);

static inline int ipc_checkid(struct kern_ipc_perm *ipcp, int id)
{
	return ipcid_to_seqx(id) != ipcp->seq;
}

static inline void ipc_lock_object(struct kern_ipc_perm *perm)
{
	spin_lock(&perm->lock);
}

static inline void ipc_unlock_object(struct kern_ipc_perm *perm)
{
	spin_unlock(&perm->lock);
}

static inline void ipc_assert_locked_object(struct kern_ipc_perm *perm)
{
	assert_spin_locked(&perm->lock);
}

static inline void ipc_unlock(struct kern_ipc_perm *perm)
{
	ipc_unlock_object(perm);
	rcu_read_unlock();
}

/*
 * ipc_valid_object() - helper to sort out IPC_RMID races for codepaths
 * where the respective ipc_ids.rwsem is not being held down.
 * Checks whether the ipc object is still around or if it's gone already, as
 * ipc_rmid() may have already freed the ID while the ipc lock was spinning.
 * Needs to be called with kern_ipc_perm.lock held -- exception made for one
 * checkpoint case at sys_semtimedop() as noted in code commentary.
 */
static inline bool ipc_valid_object(struct kern_ipc_perm *perm)
{
	return !perm->deleted;
}

struct kern_ipc_perm *ipc_obtain_object_check(struct ipc_ids *ids, int id);
int ipcget(struct ipc_namespace *ns, struct ipc_ids *ids,
			const struct ipc_ops *ops, struct ipc_params *params);
void free_ipcs(struct ipc_namespace *ns, struct ipc_ids *ids,
		void (*free)(struct ipc_namespace *, struct kern_ipc_perm *));

static inline int sem_check_semmni(struct ipc_namespace *ns) {
	/*
	 * Check semmni range [0, IPCMNI]
	 * semmni is the last element of sem_ctls[4] array
	 */
	return ((ns->sem_ctls[3] < 0) || (ns->sem_ctls[3] > IPCMNI))
		? -ERANGE : 0;
}

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
struct compat_ipc_perm {
	key_t key;
	__compat_uid_t uid;
	__compat_gid_t gid;
	__compat_uid_t cuid;
	__compat_gid_t cgid;
	compat_mode_t mode;
	unsigned short seq;
};

void to_compat_ipc_perm(struct compat_ipc_perm *, struct ipc64_perm *);
void to_compat_ipc64_perm(struct compat_ipc64_perm *, struct ipc64_perm *);
int get_compat_ipc_perm(struct ipc64_perm *, struct compat_ipc_perm __user *);
int get_compat_ipc64_perm(struct ipc64_perm *,
			  struct compat_ipc64_perm __user *);

static inline int compat_ipc_parse_version(int *cmd)
{
	int version = *cmd & IPC_64;
	*cmd &= ~IPC_64;
	return version;
}
#endif

/* for __ARCH_WANT_SYS_IPC */
long ksys_semtimedop(int semid, struct sembuf __user *tsops,
		     unsigned int nsops,
		     const struct __kernel_timespec __user *timeout);
long ksys_semget(key_t key, int nsems, int semflg);
long ksys_old_semctl(int semid, int semnum, int cmd, unsigned long arg);
long ksys_msgget(key_t key, int msgflg);
long ksys_old_msgctl(int msqid, int cmd, struct msqid_ds __user *buf);
long ksys_msgrcv(int msqid, struct msgbuf __user *msgp, size_t msgsz,
		 long msgtyp, int msgflg);
long ksys_msgsnd(int msqid, struct msgbuf __user *msgp, size_t msgsz,
		 int msgflg);
long ksys_shmget(key_t key, size_t size, int shmflg);
long ksys_shmdt(char __user *shmaddr);
long ksys_old_shmctl(int shmid, int cmd, struct shmid_ds __user *buf);

/* for CONFIG_ARCH_WANT_OLD_COMPAT_IPC */
long compat_ksys_semtimedop(int semid, struct sembuf __user *tsems,
			    unsigned int nsops,
			    const struct old_timespec32 __user *timeout);
#ifdef CONFIG_COMPAT
long compat_ksys_old_semctl(int semid, int semnum, int cmd, int arg);
long compat_ksys_old_msgctl(int msqid, int cmd, void __user *uptr);
long compat_ksys_msgrcv(int msqid, compat_uptr_t msgp, compat_ssize_t msgsz,
			compat_long_t msgtyp, int msgflg);
long compat_ksys_msgsnd(int msqid, compat_uptr_t msgp,
		       compat_ssize_t msgsz, int msgflg);
long compat_ksys_old_shmctl(int shmid, int cmd, void __user *uptr);
#endif /* CONFIG_COMPAT */

#endif
