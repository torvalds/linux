/* Authentication token and access key management
 *
 * Copyright (C) 2004, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * See Documentation/keys.txt for information on keys/keyrings.
 */

#ifndef _LINUX_KEY_H
#define _LINUX_KEY_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
#include <linux/sysctl.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>

#ifdef __KERNEL__

/* key handle serial number */
typedef int32_t key_serial_t;

/* key handle permissions mask */
typedef uint32_t key_perm_t;

struct key;

#ifdef CONFIG_KEYS

#undef KEY_DEBUGGING

#define KEY_POS_VIEW	0x01000000	/* possessor can view a key's attributes */
#define KEY_POS_READ	0x02000000	/* possessor can read key payload / view keyring */
#define KEY_POS_WRITE	0x04000000	/* possessor can update key payload / add link to keyring */
#define KEY_POS_SEARCH	0x08000000	/* possessor can find a key in search / search a keyring */
#define KEY_POS_LINK	0x10000000	/* possessor can create a link to a key/keyring */
#define KEY_POS_SETATTR	0x20000000	/* possessor can set key attributes */
#define KEY_POS_ALL	0x3f000000

#define KEY_USR_VIEW	0x00010000	/* user permissions... */
#define KEY_USR_READ	0x00020000
#define KEY_USR_WRITE	0x00040000
#define KEY_USR_SEARCH	0x00080000
#define KEY_USR_LINK	0x00100000
#define KEY_USR_SETATTR	0x00200000
#define KEY_USR_ALL	0x003f0000

#define KEY_GRP_VIEW	0x00000100	/* group permissions... */
#define KEY_GRP_READ	0x00000200
#define KEY_GRP_WRITE	0x00000400
#define KEY_GRP_SEARCH	0x00000800
#define KEY_GRP_LINK	0x00001000
#define KEY_GRP_SETATTR	0x00002000
#define KEY_GRP_ALL	0x00003f00

#define KEY_OTH_VIEW	0x00000001	/* third party permissions... */
#define KEY_OTH_READ	0x00000002
#define KEY_OTH_WRITE	0x00000004
#define KEY_OTH_SEARCH	0x00000008
#define KEY_OTH_LINK	0x00000010
#define KEY_OTH_SETATTR	0x00000020
#define KEY_OTH_ALL	0x0000003f

#define KEY_PERM_UNDEF	0xffffffff

struct seq_file;
struct user_struct;
struct signal_struct;
struct cred;

struct key_type;
struct key_owner;
struct keyring_list;
struct keyring_name;

/*****************************************************************************/
/*
 * key reference with possession attribute handling
 *
 * NOTE! key_ref_t is a typedef'd pointer to a type that is not actually
 * defined. This is because we abuse the bottom bit of the reference to carry a
 * flag to indicate whether the calling process possesses that key in one of
 * its keyrings.
 *
 * the key_ref_t has been made a separate type so that the compiler can reject
 * attempts to dereference it without proper conversion.
 *
 * the three functions are used to assemble and disassemble references
 */
typedef struct __key_reference_with_attributes *key_ref_t;

static inline key_ref_t make_key_ref(const struct key *key,
				     unsigned long possession)
{
	return (key_ref_t) ((unsigned long) key | possession);
}

static inline struct key *key_ref_to_ptr(const key_ref_t key_ref)
{
	return (struct key *) ((unsigned long) key_ref & ~1UL);
}

static inline unsigned long is_key_possessed(const key_ref_t key_ref)
{
	return (unsigned long) key_ref & 1UL;
}

/*****************************************************************************/
/*
 * authentication token / access credential / keyring
 * - types of key include:
 *   - keyrings
 *   - disk encryption IDs
 *   - Kerberos TGTs and tickets
 */
struct key {
	atomic_t		usage;		/* number of references */
	key_serial_t		serial;		/* key serial number */
	struct rb_node		serial_node;
	struct key_type		*type;		/* type of key */
	struct rw_semaphore	sem;		/* change vs change sem */
	struct key_user		*user;		/* owner of this key */
	void			*security;	/* security data for this key */
	union {
		time_t		expiry;		/* time at which key expires (or 0) */
		time_t		revoked_at;	/* time at which key was revoked */
	};
	uid_t			uid;
	gid_t			gid;
	key_perm_t		perm;		/* access permissions */
	unsigned short		quotalen;	/* length added to quota */
	unsigned short		datalen;	/* payload data length
						 * - may not match RCU dereferenced payload
						 * - payload should contain own length
						 */

#ifdef KEY_DEBUGGING
	unsigned		magic;
#define KEY_DEBUG_MAGIC		0x18273645u
#define KEY_DEBUG_MAGIC_X	0xf8e9dacbu
#endif

	unsigned long		flags;		/* status flags (change with bitops) */
#define KEY_FLAG_INSTANTIATED	0	/* set if key has been instantiated */
#define KEY_FLAG_DEAD		1	/* set if key type has been deleted */
#define KEY_FLAG_REVOKED	2	/* set if key had been revoked */
#define KEY_FLAG_IN_QUOTA	3	/* set if key consumes quota */
#define KEY_FLAG_USER_CONSTRUCT	4	/* set if key is being constructed in userspace */
#define KEY_FLAG_NEGATIVE	5	/* set if key is negative */

	/* the description string
	 * - this is used to match a key against search criteria
	 * - this should be a printable string
	 * - eg: for krb5 AFS, this might be "afs@REDHAT.COM"
	 */
	char			*description;

	/* type specific data
	 * - this is used by the keyring type to index the name
	 */
	union {
		struct list_head	link;
		unsigned long		x[2];
		void			*p[2];
	} type_data;

	/* key data
	 * - this is used to hold the data actually used in cryptography or
	 *   whatever
	 */
	union {
		unsigned long		value;
		void			*data;
		struct keyring_list	*subscriptions;
	} payload;
};

extern struct key *key_alloc(struct key_type *type,
			     const char *desc,
			     uid_t uid, gid_t gid,
			     const struct cred *cred,
			     key_perm_t perm,
			     unsigned long flags);


#define KEY_ALLOC_IN_QUOTA	0x0000	/* add to quota, reject if would overrun */
#define KEY_ALLOC_QUOTA_OVERRUN	0x0001	/* add to quota, permit even if overrun */
#define KEY_ALLOC_NOT_IN_QUOTA	0x0002	/* not in quota */

extern void key_revoke(struct key *key);
extern void key_put(struct key *key);

static inline struct key *key_get(struct key *key)
{
	if (key)
		atomic_inc(&key->usage);
	return key;
}

static inline void key_ref_put(key_ref_t key_ref)
{
	key_put(key_ref_to_ptr(key_ref));
}

extern struct key *request_key(struct key_type *type,
			       const char *description,
			       const char *callout_info);

extern struct key *request_key_with_auxdata(struct key_type *type,
					    const char *description,
					    const void *callout_info,
					    size_t callout_len,
					    void *aux);

extern struct key *request_key_async(struct key_type *type,
				     const char *description,
				     const void *callout_info,
				     size_t callout_len);

extern struct key *request_key_async_with_auxdata(struct key_type *type,
						  const char *description,
						  const void *callout_info,
						  size_t callout_len,
						  void *aux);

extern int wait_for_key_construction(struct key *key, bool intr);

extern int key_validate(struct key *key);

extern key_ref_t key_create_or_update(key_ref_t keyring,
				      const char *type,
				      const char *description,
				      const void *payload,
				      size_t plen,
				      key_perm_t perm,
				      unsigned long flags);

extern int key_update(key_ref_t key,
		      const void *payload,
		      size_t plen);

extern int key_link(struct key *keyring,
		    struct key *key);

extern int key_unlink(struct key *keyring,
		      struct key *key);

extern struct key *keyring_alloc(const char *description, uid_t uid, gid_t gid,
				 const struct cred *cred,
				 unsigned long flags,
				 struct key *dest);

extern int keyring_clear(struct key *keyring);

extern key_ref_t keyring_search(key_ref_t keyring,
				struct key_type *type,
				const char *description);

extern int keyring_add_key(struct key *keyring,
			   struct key *key);

extern struct key *key_lookup(key_serial_t id);

static inline key_serial_t key_serial(struct key *key)
{
	return key ? key->serial : 0;
}

#ifdef CONFIG_SYSCTL
extern ctl_table key_sysctls[];
#endif

extern void key_replace_session_keyring(void);

/*
 * the userspace interface
 */
extern int install_thread_keyring_to_cred(struct cred *cred);
extern void key_fsuid_changed(struct task_struct *tsk);
extern void key_fsgid_changed(struct task_struct *tsk);
extern void key_init(void);

#else /* CONFIG_KEYS */

#define key_validate(k)			0
#define key_serial(k)			0
#define key_get(k) 			({ NULL; })
#define key_revoke(k)			do { } while(0)
#define key_put(k)			do { } while(0)
#define key_ref_put(k)			do { } while(0)
#define make_key_ref(k, p)		NULL
#define key_ref_to_ptr(k)		NULL
#define is_key_possessed(k)		0
#define key_fsuid_changed(t)		do { } while(0)
#define key_fsgid_changed(t)		do { } while(0)
#define key_init()			do { } while(0)
#define key_replace_session_keyring()	do { } while(0)

#endif /* CONFIG_KEYS */
#endif /* __KERNEL__ */
#endif /* _LINUX_KEY_H */
