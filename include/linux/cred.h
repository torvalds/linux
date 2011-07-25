/* Credentials management - see Documentation/security/credentials.txt
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_CRED_H
#define _LINUX_CRED_H

#include <linux/capability.h>
#include <linux/init.h>
#include <linux/key.h>
#include <linux/selinux.h>
#include <asm/atomic.h>

struct user_struct;
struct cred;
struct inode;

/*
 * COW Supplementary groups list
 */
#define NGROUPS_SMALL		32
#define NGROUPS_PER_BLOCK	((unsigned int)(PAGE_SIZE / sizeof(gid_t)))

struct group_info {
	atomic_t	usage;
	int		ngroups;
	int		nblocks;
	gid_t		small_block[NGROUPS_SMALL];
	gid_t		*blocks[0];
};

/**
 * get_group_info - Get a reference to a group info structure
 * @group_info: The group info to reference
 *
 * This gets a reference to a set of supplementary groups.
 *
 * If the caller is accessing a task's credentials, they must hold the RCU read
 * lock when reading.
 */
static inline struct group_info *get_group_info(struct group_info *gi)
{
	atomic_inc(&gi->usage);
	return gi;
}

/**
 * put_group_info - Release a reference to a group info structure
 * @group_info: The group info to release
 */
#define put_group_info(group_info)			\
do {							\
	if (atomic_dec_and_test(&(group_info)->usage))	\
		groups_free(group_info);		\
} while (0)

extern struct group_info *groups_alloc(int);
extern struct group_info init_groups;
extern void groups_free(struct group_info *);
extern int set_current_groups(struct group_info *);
extern int set_groups(struct cred *, struct group_info *);
extern int groups_search(const struct group_info *, gid_t);

/* access the groups "array" with this macro */
#define GROUP_AT(gi, i) \
	((gi)->blocks[(i) / NGROUPS_PER_BLOCK][(i) % NGROUPS_PER_BLOCK])

extern int in_group_p(gid_t);
extern int in_egroup_p(gid_t);

/*
 * The common credentials for a thread group
 * - shared by CLONE_THREAD
 */
#ifdef CONFIG_KEYS
struct thread_group_cred {
	atomic_t	usage;
	pid_t		tgid;			/* thread group process ID */
	spinlock_t	lock;
	struct key __rcu *session_keyring;	/* keyring inherited over fork */
	struct key	*process_keyring;	/* keyring private to this process */
	struct rcu_head	rcu;			/* RCU deletion hook */
};
#endif

/*
 * The security context of a task
 *
 * The parts of the context break down into two categories:
 *
 *  (1) The objective context of a task.  These parts are used when some other
 *	task is attempting to affect this one.
 *
 *  (2) The subjective context.  These details are used when the task is acting
 *	upon another object, be that a file, a task, a key or whatever.
 *
 * Note that some members of this structure belong to both categories - the
 * LSM security pointer for instance.
 *
 * A task has two security pointers.  task->real_cred points to the objective
 * context that defines that task's actual details.  The objective part of this
 * context is used whenever that task is acted upon.
 *
 * task->cred points to the subjective context that defines the details of how
 * that task is going to act upon another object.  This may be overridden
 * temporarily to point to another security context, but normally points to the
 * same context as task->real_cred.
 */
struct cred {
	atomic_t	usage;
#ifdef CONFIG_DEBUG_CREDENTIALS
	atomic_t	subscribers;	/* number of processes subscribed */
	void		*put_addr;
	unsigned	magic;
#define CRED_MAGIC	0x43736564
#define CRED_MAGIC_DEAD	0x44656144
#endif
	uid_t		uid;		/* real UID of the task */
	gid_t		gid;		/* real GID of the task */
	uid_t		suid;		/* saved UID of the task */
	gid_t		sgid;		/* saved GID of the task */
	uid_t		euid;		/* effective UID of the task */
	gid_t		egid;		/* effective GID of the task */
	uid_t		fsuid;		/* UID for VFS ops */
	gid_t		fsgid;		/* GID for VFS ops */
	unsigned	securebits;	/* SUID-less security management */
	kernel_cap_t	cap_inheritable; /* caps our children can inherit */
	kernel_cap_t	cap_permitted;	/* caps we're permitted */
	kernel_cap_t	cap_effective;	/* caps we can actually use */
	kernel_cap_t	cap_bset;	/* capability bounding set */
#ifdef CONFIG_KEYS
	unsigned char	jit_keyring;	/* default keyring to attach requested
					 * keys to */
	struct key	*thread_keyring; /* keyring private to this thread */
	struct key	*request_key_auth; /* assumed request_key authority */
	struct thread_group_cred *tgcred; /* thread-group shared credentials */
#endif
#ifdef CONFIG_SECURITY
	void		*security;	/* subjective LSM security */
#endif
	struct user_struct *user;	/* real user ID subscription */
	struct user_namespace *user_ns; /* cached user->user_ns */
	struct group_info *group_info;	/* supplementary groups for euid/fsgid */
	struct rcu_head	rcu;		/* RCU deletion hook */
};

extern void __put_cred(struct cred *);
extern void exit_creds(struct task_struct *);
extern int copy_creds(struct task_struct *, unsigned long);
extern const struct cred *get_task_cred(struct task_struct *);
extern struct cred *cred_alloc_blank(void);
extern struct cred *prepare_creds(void);
extern struct cred *prepare_exec_creds(void);
extern int commit_creds(struct cred *);
extern void abort_creds(struct cred *);
extern const struct cred *override_creds(const struct cred *);
extern void revert_creds(const struct cred *);
extern struct cred *prepare_kernel_cred(struct task_struct *);
extern int change_create_files_as(struct cred *, struct inode *);
extern int set_security_override(struct cred *, u32);
extern int set_security_override_from_ctx(struct cred *, const char *);
extern int set_create_files_as(struct cred *, struct inode *);
extern void __init cred_init(void);

/*
 * check for validity of credentials
 */
#ifdef CONFIG_DEBUG_CREDENTIALS
extern void __invalid_creds(const struct cred *, const char *, unsigned);
extern void __validate_process_creds(struct task_struct *,
				     const char *, unsigned);

extern bool creds_are_invalid(const struct cred *cred);

static inline void __validate_creds(const struct cred *cred,
				    const char *file, unsigned line)
{
	if (unlikely(creds_are_invalid(cred)))
		__invalid_creds(cred, file, line);
}

#define validate_creds(cred)				\
do {							\
	__validate_creds((cred), __FILE__, __LINE__);	\
} while(0)

#define validate_process_creds()				\
do {								\
	__validate_process_creds(current, __FILE__, __LINE__);	\
} while(0)

extern void validate_creds_for_do_exit(struct task_struct *);
#else
static inline void validate_creds(const struct cred *cred)
{
}
static inline void validate_creds_for_do_exit(struct task_struct *tsk)
{
}
static inline void validate_process_creds(void)
{
}
#endif

/**
 * get_new_cred - Get a reference on a new set of credentials
 * @cred: The new credentials to reference
 *
 * Get a reference on the specified set of new credentials.  The caller must
 * release the reference.
 */
static inline struct cred *get_new_cred(struct cred *cred)
{
	atomic_inc(&cred->usage);
	return cred;
}

/**
 * get_cred - Get a reference on a set of credentials
 * @cred: The credentials to reference
 *
 * Get a reference on the specified set of credentials.  The caller must
 * release the reference.
 *
 * This is used to deal with a committed set of credentials.  Although the
 * pointer is const, this will temporarily discard the const and increment the
 * usage count.  The purpose of this is to attempt to catch at compile time the
 * accidental alteration of a set of credentials that should be considered
 * immutable.
 */
static inline const struct cred *get_cred(const struct cred *cred)
{
	struct cred *nonconst_cred = (struct cred *) cred;
	validate_creds(cred);
	return get_new_cred(nonconst_cred);
}

/**
 * put_cred - Release a reference to a set of credentials
 * @cred: The credentials to release
 *
 * Release a reference to a set of credentials, deleting them when the last ref
 * is released.
 *
 * This takes a const pointer to a set of credentials because the credentials
 * on task_struct are attached by const pointers to prevent accidental
 * alteration of otherwise immutable credential sets.
 */
static inline void put_cred(const struct cred *_cred)
{
	struct cred *cred = (struct cred *) _cred;

	validate_creds(cred);
	if (atomic_dec_and_test(&(cred)->usage))
		__put_cred(cred);
}

/**
 * current_cred - Access the current task's subjective credentials
 *
 * Access the subjective credentials of the current task.
 */
#define current_cred() \
	(current->cred)

/**
 * __task_cred - Access a task's objective credentials
 * @task: The task to query
 *
 * Access the objective credentials of a task.  The caller must hold the RCU
 * readlock or the task must be dead and unable to change its own credentials.
 *
 * The result of this function should not be passed directly to get_cred();
 * rather get_task_cred() should be used instead.
 */
#define __task_cred(task)						\
	({								\
		const struct task_struct *__t = (task);			\
		rcu_dereference_check(__t->real_cred,			\
				      task_is_dead(__t));		\
	})

/**
 * get_current_cred - Get the current task's subjective credentials
 *
 * Get the subjective credentials of the current task, pinning them so that
 * they can't go away.  Accessing the current task's credentials directly is
 * not permitted.
 */
#define get_current_cred()				\
	(get_cred(current_cred()))

/**
 * get_current_user - Get the current task's user_struct
 *
 * Get the user record of the current task, pinning it so that it can't go
 * away.
 */
#define get_current_user()				\
({							\
	struct user_struct *__u;			\
	struct cred *__cred;				\
	__cred = (struct cred *) current_cred();	\
	__u = get_uid(__cred->user);			\
	__u;						\
})

/**
 * get_current_groups - Get the current task's supplementary group list
 *
 * Get the supplementary group list of the current task, pinning it so that it
 * can't go away.
 */
#define get_current_groups()				\
({							\
	struct group_info *__groups;			\
	struct cred *__cred;				\
	__cred = (struct cred *) current_cred();	\
	__groups = get_group_info(__cred->group_info);	\
	__groups;					\
})

#define task_cred_xxx(task, xxx)			\
({							\
	__typeof__(((struct cred *)NULL)->xxx) ___val;	\
	rcu_read_lock();				\
	___val = __task_cred((task))->xxx;		\
	rcu_read_unlock();				\
	___val;						\
})

#define task_uid(task)		(task_cred_xxx((task), uid))
#define task_euid(task)		(task_cred_xxx((task), euid))

#define current_cred_xxx(xxx)			\
({						\
	current->cred->xxx;			\
})

#define current_uid()		(current_cred_xxx(uid))
#define current_gid()		(current_cred_xxx(gid))
#define current_euid()		(current_cred_xxx(euid))
#define current_egid()		(current_cred_xxx(egid))
#define current_suid()		(current_cred_xxx(suid))
#define current_sgid()		(current_cred_xxx(sgid))
#define current_fsuid() 	(current_cred_xxx(fsuid))
#define current_fsgid() 	(current_cred_xxx(fsgid))
#define current_cap()		(current_cred_xxx(cap_effective))
#define current_user()		(current_cred_xxx(user))
#define current_security()	(current_cred_xxx(security))

#ifdef CONFIG_USER_NS
#define current_user_ns() (current_cred_xxx(user_ns))
#else
extern struct user_namespace init_user_ns;
#define current_user_ns() (&init_user_ns)
#endif


#define current_uid_gid(_uid, _gid)		\
do {						\
	const struct cred *__cred;		\
	__cred = current_cred();		\
	*(_uid) = __cred->uid;			\
	*(_gid) = __cred->gid;			\
} while(0)

#define current_euid_egid(_euid, _egid)		\
do {						\
	const struct cred *__cred;		\
	__cred = current_cred();		\
	*(_euid) = __cred->euid;		\
	*(_egid) = __cred->egid;		\
} while(0)

#define current_fsuid_fsgid(_fsuid, _fsgid)	\
do {						\
	const struct cred *__cred;		\
	__cred = current_cred();		\
	*(_fsuid) = __cred->fsuid;		\
	*(_fsgid) = __cred->fsgid;		\
} while(0)

#endif /* _LINUX_CRED_H */
