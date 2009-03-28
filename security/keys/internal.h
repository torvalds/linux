/* internal.h: authentication token and access key management internal defs
 *
 * Copyright (C) 2003-5, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _INTERNAL_H
#define _INTERNAL_H

#include <linux/sched.h>
#include <linux/key-type.h>

static inline __attribute__((format(printf, 1, 2)))
void no_printk(const char *fmt, ...)
{
}

#ifdef __KDEBUG
#define kenter(FMT, ...) \
	printk(KERN_DEBUG "==> %s("FMT")\n", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) \
	printk(KERN_DEBUG "<== %s()"FMT"\n", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) \
	printk(KERN_DEBUG "   "FMT"\n", ##__VA_ARGS__)
#else
#define kenter(FMT, ...) \
	no_printk(KERN_DEBUG "==> %s("FMT")\n", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) \
	no_printk(KERN_DEBUG "<== %s()"FMT"\n", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) \
	no_printk(KERN_DEBUG FMT"\n", ##__VA_ARGS__)
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
	struct mutex		cons_lock;	/* construction initiation lock */
	spinlock_t		lock;
	atomic_t		usage;		/* for accessing qnkeys & qnbytes */
	atomic_t		nkeys;		/* number of keys */
	atomic_t		nikeys;		/* number of instantiated keys */
	uid_t			uid;
	struct user_namespace	*user_ns;
	int			qnkeys;		/* number of keys allocated to this user */
	int			qnbytes;	/* number of bytes allocated to this user */
};

extern struct rb_root	key_user_tree;
extern spinlock_t	key_user_lock;
extern struct key_user	root_key_user;

extern struct key_user *key_user_lookup(uid_t uid,
					struct user_namespace *user_ns);
extern void key_user_put(struct key_user *user);

/*
 * key quota limits
 * - root has its own separate limits to everyone else
 */
extern unsigned key_quota_root_maxkeys;
extern unsigned key_quota_root_maxbytes;
extern unsigned key_quota_maxkeys;
extern unsigned key_quota_maxbytes;

#define KEYQUOTA_LINK_BYTES	4		/* a link in a keyring is worth 4 bytes */


extern struct rb_root key_serial_tree;
extern spinlock_t key_serial_lock;
extern struct mutex key_construction_mutex;
extern wait_queue_head_t request_key_conswq;


extern struct key_type *key_type_lookup(const char *type);
extern void key_type_put(struct key_type *ktype);

extern int __key_link(struct key *keyring, struct key *key);

extern key_ref_t __keyring_search_one(key_ref_t keyring_ref,
				      const struct key_type *type,
				      const char *description,
				      key_perm_t perm);

extern struct key *keyring_search_instkey(struct key *keyring,
					  key_serial_t target_id);

typedef int (*key_match_func_t)(const struct key *, const void *);

extern key_ref_t keyring_search_aux(key_ref_t keyring_ref,
				    const struct cred *cred,
				    struct key_type *type,
				    const void *description,
				    key_match_func_t match);

extern key_ref_t search_process_keyrings(struct key_type *type,
					 const void *description,
					 key_match_func_t match,
					 const struct cred *cred);

extern struct key *find_keyring_by_name(const char *name, bool skip_perm_check);

extern int install_user_keyrings(void);
extern int install_thread_keyring_to_cred(struct cred *);
extern int install_process_keyring_to_cred(struct cred *);

extern struct key *request_key_and_link(struct key_type *type,
					const char *description,
					const void *callout_info,
					size_t callout_len,
					void *aux,
					struct key *dest_keyring,
					unsigned long flags);

extern key_ref_t lookup_user_key(key_serial_t id, int create, int partial,
				 key_perm_t perm);

extern long join_session_keyring(const char *name);

/*
 * check to see whether permission is granted to use a key in the desired way
 */
extern int key_task_permission(const key_ref_t key_ref,
			       const struct cred *cred,
			       key_perm_t perm);

static inline int key_permission(const key_ref_t key_ref, key_perm_t perm)
{
	return key_task_permission(key_ref, current_cred(), perm);
}

/* required permissions */
#define	KEY_VIEW	0x01	/* require permission to view attributes */
#define	KEY_READ	0x02	/* require permission to read content */
#define	KEY_WRITE	0x04	/* require permission to update / modify */
#define	KEY_SEARCH	0x08	/* require permission to search (keyring) or find (key) */
#define	KEY_LINK	0x10	/* require permission to link */
#define	KEY_SETATTR	0x20	/* require permission to change attributes */
#define	KEY_ALL		0x3f	/* all the above permissions */

/*
 * request_key authorisation
 */
struct request_key_auth {
	struct key		*target_key;
	struct key		*dest_keyring;
	const struct cred	*cred;
	void			*callout_info;
	size_t			callout_len;
	pid_t			pid;
};

extern struct key_type key_type_request_key_auth;
extern struct key *request_key_auth_new(struct key *target,
					const void *callout_info,
					size_t callout_len,
					struct key *dest_keyring);

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
extern long keyctl_get_security(key_serial_t keyid, char __user *buffer,
				size_t buflen);

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
