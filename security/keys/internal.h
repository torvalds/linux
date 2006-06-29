/* internal.h: authentication token and access key management internal defs
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _INTERNAL_H
#define _INTERNAL_H

#include <linux/key.h>
#include <linux/key-ui.h>

#if 0
#define kenter(FMT, a...)	printk("==> %s("FMT")\n",__FUNCTION__ , ## a)
#define kleave(FMT, a...)	printk("<== %s()"FMT"\n",__FUNCTION__ , ## a)
#define kdebug(FMT, a...)	printk(FMT"\n" , ## a)
#else
#define kenter(FMT, a...)	do {} while(0)
#define kleave(FMT, a...)	do {} while(0)
#define kdebug(FMT, a...)	do {} while(0)
#endif

extern struct key_type key_type_user;

/*****************************************************************************/
/*
 * keep track of keys for a user
 * - this needs to be separate to user_struct to avoid a refcount-loop
 *   (user_struct pins some keyrings which pin this struct)
 * - this also keeps track of keys under request from userspace for this UID
 */
struct key_user {
	struct rb_node		node;
	struct list_head	consq;		/* construction queue */
	spinlock_t		lock;
	atomic_t		usage;		/* for accessing qnkeys & qnbytes */
	atomic_t		nkeys;		/* number of keys */
	atomic_t		nikeys;		/* number of instantiated keys */
	uid_t			uid;
	int			qnkeys;		/* number of keys allocated to this user */
	int			qnbytes;	/* number of bytes allocated to this user */
};

#define KEYQUOTA_MAX_KEYS	100
#define KEYQUOTA_MAX_BYTES	10000
#define KEYQUOTA_LINK_BYTES	4		/* a link in a keyring is worth 4 bytes */

extern struct rb_root	key_user_tree;
extern spinlock_t	key_user_lock;
extern struct key_user	root_key_user;

extern struct key_user *key_user_lookup(uid_t uid);
extern void key_user_put(struct key_user *user);



extern struct rb_root key_serial_tree;
extern spinlock_t key_serial_lock;
extern struct semaphore key_alloc_sem;
extern struct rw_semaphore key_construction_sem;
extern wait_queue_head_t request_key_conswq;


extern void keyring_publish_name(struct key *keyring);

extern int __key_link(struct key *keyring, struct key *key);

extern key_ref_t __keyring_search_one(key_ref_t keyring_ref,
				      const struct key_type *type,
				      const char *description,
				      key_perm_t perm);

extern struct key *keyring_search_instkey(struct key *keyring,
					  key_serial_t target_id);

typedef int (*key_match_func_t)(const struct key *, const void *);

extern key_ref_t keyring_search_aux(key_ref_t keyring_ref,
				    struct task_struct *tsk,
				    struct key_type *type,
				    const void *description,
				    key_match_func_t match);

extern key_ref_t search_process_keyrings(struct key_type *type,
					 const void *description,
					 key_match_func_t match,
					 struct task_struct *tsk);

extern struct key *find_keyring_by_name(const char *name, key_serial_t bound);

extern int install_thread_keyring(struct task_struct *tsk);
extern int install_process_keyring(struct task_struct *tsk);

extern struct key *request_key_and_link(struct key_type *type,
					const char *description,
					const char *callout_info,
					void *aux,
					struct key *dest_keyring,
					unsigned long flags);

/*
 * request_key authorisation
 */
struct request_key_auth {
	struct key		*target_key;
	struct task_struct	*context;
	const char		*callout_info;
	pid_t			pid;
};

extern struct key_type key_type_request_key_auth;
extern struct key *request_key_auth_new(struct key *target,
					const char *callout_info);

extern struct key *key_get_instantiation_authkey(key_serial_t target_id);

/*
 * keyctl functions
 */
extern long keyctl_get_keyring_ID(key_serial_t, int);
extern long keyctl_join_session_keyring(const char __user *);
extern long keyctl_update_key(key_serial_t, const void __user *, size_t);
extern long keyctl_revoke_key(key_serial_t);
extern long keyctl_keyring_clear(key_serial_t);
extern long keyctl_keyring_link(key_serial_t, key_serial_t);
extern long keyctl_keyring_unlink(key_serial_t, key_serial_t);
extern long keyctl_describe_key(key_serial_t, char __user *, size_t);
extern long keyctl_keyring_search(key_serial_t, const char __user *,
				  const char __user *, key_serial_t);
extern long keyctl_read_key(key_serial_t, char __user *, size_t);
extern long keyctl_chown_key(key_serial_t, uid_t, gid_t);
extern long keyctl_setperm_key(key_serial_t, key_perm_t);
extern long keyctl_instantiate_key(key_serial_t, const void __user *,
				   size_t, key_serial_t);
extern long keyctl_negate_key(key_serial_t, unsigned, key_serial_t);
extern long keyctl_set_reqkey_keyring(int);
extern long keyctl_set_timeout(key_serial_t, unsigned);
extern long keyctl_assume_authority(key_serial_t);


/*
 * debugging key validation
 */
#ifdef KEY_DEBUGGING
extern void __key_check(const struct key *);

static inline void key_check(const struct key *key)
{
	if (key && (IS_ERR(key) || key->magic != KEY_DEBUG_MAGIC))
		__key_check(key);
}

#else

#define key_check(key) do {} while(0)

#endif

#endif /* _INTERNAL_H */
