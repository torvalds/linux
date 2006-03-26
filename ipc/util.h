/*
 * linux/ipc/util.h
 * Copyright (C) 1999 Christoph Rohland
 *
 * ipc helper functions (c) 1999 Manfred Spraul <manfred@colorfullife.com>
 */

#ifndef _IPC_UTIL_H
#define _IPC_UTIL_H

#define USHRT_MAX 0xffff
#define SEQ_MULTIPLIER	(IPCMNI)

void sem_init (void);
void msg_init (void);
void shm_init (void);

struct ipc_id_ary {
	int size;
	struct kern_ipc_perm *p[0];
};

struct ipc_ids {
	int in_use;
	int max_id;
	unsigned short seq;
	unsigned short seq_max;
	struct mutex mutex;
	struct ipc_id_ary nullentry;
	struct ipc_id_ary* entries;
};

struct seq_file;
void __init ipc_init_ids(struct ipc_ids* ids, int size);
#ifdef CONFIG_PROC_FS
void __init ipc_init_proc_interface(const char *path, const char *header,
				    struct ipc_ids *ids,
				    int (*show)(struct seq_file *, void *));
#else
#define ipc_init_proc_interface(path, header, ids, show) do {} while (0)
#endif

/* must be called with ids->mutex acquired.*/
int ipc_findkey(struct ipc_ids* ids, key_t key);
int ipc_addid(struct ipc_ids* ids, struct kern_ipc_perm* new, int size);

/* must be called with both locks acquired. */
struct kern_ipc_perm* ipc_rmid(struct ipc_ids* ids, int id);

int ipcperms (struct kern_ipc_perm *ipcp, short flg);

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

struct kern_ipc_perm* ipc_get(struct ipc_ids* ids, int id);
struct kern_ipc_perm* ipc_lock(struct ipc_ids* ids, int id);
void ipc_lock_by_ptr(struct kern_ipc_perm *ipcp);
void ipc_unlock(struct kern_ipc_perm* perm);
int ipc_buildid(struct ipc_ids* ids, int id, int seq);
int ipc_checkid(struct ipc_ids* ids, struct kern_ipc_perm* ipcp, int uid);

void kernel_to_ipc64_perm(struct kern_ipc_perm *in, struct ipc64_perm *out);
void ipc64_perm_to_ipc_perm(struct ipc64_perm *in, struct ipc_perm *out);

#if defined(__ia64__) || defined(__x86_64__) || defined(__hppa__) || defined(__XTENSA__)
  /* On IA-64, we always use the "64-bit version" of the IPC structures.  */ 
# define ipc_parse_version(cmd)	IPC_64
#else
int ipc_parse_version (int *cmd);
#endif

extern void free_msg(struct msg_msg *msg);
extern struct msg_msg *load_msg(const void __user *src, int len);
extern int store_msg(void __user *dest, struct msg_msg *msg, int len);

#endif
