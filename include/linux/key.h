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
 * See Documentation/security/keys/core.rst for information on keys/keyrings.
 */

#ifndef _LINUX_KEY_H
#define _LINUX_KEY_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
#include <linux/sysctl.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>
#include <linux/assoc_array.h>
#include <linux/refcount.h>
#include <linux/time64.h>

#ifdef __KERNEL__
#include <linux/uidgid.h>

/* key handle serial number */
typedef int32_t key_serial_t;

struct key;
struct net;

#ifdef CONFIG_KEYS

#include <linux/keyctl.h>

#undef KEY_DEBUGGING

struct seq_file;
struct user_struct;
struct signal_struct;
struct cred;

struct key_type;
struct key_owner;
struct key_tag;
struct keyring_list;
struct keyring_name;

struct key_tag {
	struct rcu_head		rcu;
	refcount_t		usage;
	bool			removed;	/* T when subject removed */
};

struct keyring_index_key {
	/* [!] If this structure is altered, the union in struct key must change too! */
	unsigned long		hash;			/* Hash value */
	union {
		struct {
#ifdef __LITTLE_ENDIAN /* Put desc_len at the LSB of x */
			u8	desc_len;
			char	desc[sizeof(long) - 1];	/* First few chars of description */
#else
			char	desc[sizeof(long) - 1];	/* First few chars of description */
			u8	desc_len;
#endif
		};
		unsigned long x;
	};
	struct key_type		*type;
	struct key_tag		*domain_tag;	/* Domain of operation */
	const char		*description;
};

union key_payload {
	void __rcu		*rcu_data0;
	void			*data[4];
};

struct key_ace {
	unsigned int		type;
	unsigned int		perm;
	union {
		kuid_t		uid;
		kgid_t		gid;
		unsigned int	subject_id;
	};
};

struct key_acl {
	refcount_t		usage;
	unsigned short		nr_ace;
	bool			possessor_viewable;
	struct rcu_head		rcu;
	struct key_ace		aces[];
};

#define KEY_POSSESSOR_ACE(perms) {			\
		.type = KEY_ACE_SUBJ_STANDARD,		\
		.perm = perms,				\
		.subject_id = KEY_ACE_POSSESSOR		\
	}

#define KEY_OWNER_ACE(perms) {				\
		.type = KEY_ACE_SUBJ_STANDARD,		\
		.perm = perms,				\
		.subject_id = KEY_ACE_OWNER		\
	}

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
				     bool possession)
{
	return (key_ref_t) ((unsigned long) key | possession);
}

static inline struct key *key_ref_to_ptr(const key_ref_t key_ref)
{
	return (struct key *) ((unsigned long) key_ref & ~1UL);
}

static inline bool is_key_possessed(const key_ref_t key_ref)
{
	return (unsigned long) key_ref & 1UL;
}

typedef int (*key_restrict_link_func_t)(struct key *dest_keyring,
					const struct key_type *type,
					const union key_payload *payload,
					struct key *restriction_key);

struct key_restriction {
	key_restrict_link_func_t check;
	struct key *key;
	struct key_type *keytype;
};

enum key_state {
	KEY_IS_UNINSTANTIATED,
	KEY_IS_POSITIVE,		/* Positively instantiated */
};

/*****************************************************************************/
/*
 * authentication token / access credential / keyring
 * - types of key include:
 *   - keyrings
 *   - disk encryption IDs
 *   - Kerberos TGTs and tickets
 */
struct key {
	refcount_t		usage;		/* number of references */
	key_serial_t		serial;		/* key serial number */
	union {
		struct list_head graveyard_link;
		struct rb_node	serial_node;
	};
	struct rw_semaphore	sem;		/* change vs change sem */
	struct key_user		*user;		/* owner of this key */
	void			*security;	/* security data for this key */
	struct key_acl		__rcu *acl;
	union {
		time64_t	expiry;		/* time at which key expires (or 0) */
		time64_t	revoked_at;	/* time at which key was revoked */
	};
	time64_t		last_used_at;	/* last time used for LRU keyring discard */
	kuid_t			uid;
	kgid_t			gid;
	unsigned short		quotalen;	/* length added to quota */
	unsigned short		datalen;	/* payload data length
						 * - may not match RCU dereferenced payload
						 * - payload should contain own length
						 */
	short			state;		/* Key state (+) or rejection error (-) */

#ifdef KEY_DEBUGGING
	unsigned		magic;
#define KEY_DEBUG_MAGIC		0x18273645u
#endif

	unsigned long		flags;		/* status flags (change with bitops) */
#define KEY_FLAG_DEAD		0	/* set if key type has been deleted */
#define KEY_FLAG_REVOKED	1	/* set if key had been revoked */
#define KEY_FLAG_IN_QUOTA	2	/* set if key consumes quota */
#define KEY_FLAG_USER_CONSTRUCT	3	/* set if key is being constructed in userspace */
#define KEY_FLAG_ROOT_CAN_CLEAR	4	/* set if key can be cleared by root without permission */
#define KEY_FLAG_INVALIDATED	5	/* set if key has been invalidated */
#define KEY_FLAG_BUILTIN	6	/* set if key is built in to the kernel */
#define KEY_FLAG_ROOT_CAN_INVAL	7	/* set if key can be invalidated by root without permission */
#define KEY_FLAG_KEEP		8	/* set if key should not be removed */
#define KEY_FLAG_UID_KEYRING	9	/* set if key is a user or user session keyring */
#define KEY_FLAG_HAS_ACL	10	/* Set if KEYCTL_SETACL called on key */

	/* the key type and key description string
	 * - the desc is used to match a key against search criteria
	 * - it should be a printable string
	 * - eg: for krb5 AFS, this might be "afs@REDHAT.COM"
	 */
	union {
		struct keyring_index_key index_key;
		struct {
			unsigned long	hash;
			unsigned long	len_desc;
			struct key_type	*type;		/* type of key */
			struct key_tag	*domain_tag;	/* Domain of operation */
			char		*description;
		};
	};

	/* key data
	 * - this is used to hold the data actually used in cryptography or
	 *   whatever
	 */
	union {
		union key_payload payload;
		struct {
			/* Keyring bits */
			struct list_head name_link;
			struct assoc_array keys;
		};
	};

	/* This is set on a keyring to restrict the addition of a link to a key
	 * to it.  If this structure isn't provided then it is assumed that the
	 * keyring is open to any addition.  It is ignored for non-keyring
	 * keys. Only set this value using keyring_restrict(), keyring_alloc(),
	 * or key_alloc().
	 *
	 * This is intended for use with rings of trusted keys whereby addition
	 * to the keyring needs to be controlled.  KEY_ALLOC_BYPASS_RESTRICTION
	 * overrides this, allowing the kernel to add extra keys without
	 * restriction.
	 */
	struct key_restriction *restrict_link;
};

extern struct key *key_alloc(struct key_type *type,
			     const char *desc,
			     kuid_t uid, kgid_t gid,
			     const struct cred *cred,
			     struct key_acl *acl,
			     unsigned long flags,
			     struct key_restriction *restrict_link);


#define KEY_ALLOC_IN_QUOTA		0x0000	/* add to quota, reject if would overrun */
#define KEY_ALLOC_QUOTA_OVERRUN		0x0001	/* add to quota, permit even if overrun */
#define KEY_ALLOC_NOT_IN_QUOTA		0x0002	/* not in quota */
#define KEY_ALLOC_BUILT_IN		0x0004	/* Key is built into kernel */
#define KEY_ALLOC_BYPASS_RESTRICTION	0x0008	/* Override the check on restricted keyrings */
#define KEY_ALLOC_UID_KEYRING		0x0010	/* allocating a user or user session keyring */

extern void key_revoke(struct key *key);
extern void key_invalidate(struct key *key);
extern void key_put(struct key *key);
extern bool key_put_tag(struct key_tag *tag);
extern void key_remove_domain(struct key_tag *domain_tag);

static inline struct key *__key_get(struct key *key)
{
	refcount_inc(&key->usage);
	return key;
}

static inline struct key *key_get(struct key *key)
{
	return key ? __key_get(key) : key;
}

static inline void key_ref_put(key_ref_t key_ref)
{
	key_put(key_ref_to_ptr(key_ref));
}

extern struct key *request_key_tag(struct key_type *type,
				   const char *description,
				   struct key_tag *domain_tag,
				   const char *callout_info,
				   struct key_acl *acl);

extern struct key *request_key_rcu(struct key_type *type,
				   const char *description,
				   struct key_tag *domain_tag);

extern struct key *request_key_with_auxdata(struct key_type *type,
					    const char *description,
					    struct key_tag *domain_tag,
					    const void *callout_info,
					    size_t callout_len,
					    void *aux,
					    struct key_acl *acl);

/**
 * request_key - Request a key and wait for construction
 * @type: Type of key.
 * @description: The searchable description of the key.
 * @callout_info: The data to pass to the instantiation upcall (or NULL).
 * @acl: The ACL to attach to a new key (or NULL).
 *
 * As for request_key_tag(), but with the default global domain tag.
 */
static inline struct key *request_key(struct key_type *type,
				      const char *description,
				      const char *callout_info,
				      struct key_acl *acl)
{
	return request_key_tag(type, description, NULL, callout_info, acl);
}

#ifdef CONFIG_NET
/*
 * request_key_net - Request a key for a net namespace and wait for construction
 * @type: Type of key.
 * @description: The searchable description of the key.
 * @net: The network namespace that is the key's domain of operation.
 * @callout_info: The data to pass to the instantiation upcall (or NULL).
 * @acl: The ACL to attach to a new key (or NULL).
 *
 * As for request_key() except that it does not add the returned key to a
 * keyring if found, new keys are always allocated in the user's quota, the
 * callout_info must be a NUL-terminated string and no auxiliary data can be
 * passed.  Only keys that operate the specified network namespace are used.
 *
 * Furthermore, it then works as wait_for_key_construction() to wait for the
 * completion of keys undergoing construction with a non-interruptible wait.
 */
#define request_key_net(type, description, net, callout_info, acl)	\
	request_key_tag(type, description, net->key_domain, callout_info, acl);
#endif /* CONFIG_NET */

extern int wait_for_key_construction(struct key *key, bool intr);

extern int key_validate(const struct key *key);

extern key_ref_t key_create_or_update(key_ref_t keyring,
				      const char *type,
				      const char *description,
				      const void *payload,
				      size_t plen,
				      struct key_acl *acl,
				      unsigned long flags);

extern int key_update(key_ref_t key,
		      const void *payload,
		      size_t plen);

extern int key_link(struct key *keyring,
		    struct key *key);

extern int key_move(struct key *key,
		    struct key *from_keyring,
		    struct key *to_keyring,
		    unsigned int flags);

extern int key_unlink(struct key *keyring,
		      struct key *key);

extern struct key *keyring_alloc(const char *description, kuid_t uid, kgid_t gid,
				 const struct cred *cred,
				 struct key_acl *acl,
				 unsigned long flags,
				 struct key_restriction *restrict_link,
				 struct key *dest);

extern int restrict_link_reject(struct key *keyring,
				const struct key_type *type,
				const union key_payload *payload,
				struct key *restriction_key);

extern int keyring_clear(struct key *keyring);

extern key_ref_t keyring_search(key_ref_t keyring,
				struct key_type *type,
				const char *description,
				bool recurse);

extern int keyring_add_key(struct key *keyring,
			   struct key *key);

extern int keyring_restrict(key_ref_t keyring, const char *type,
			    const char *restriction);

extern struct key *key_lookup(key_serial_t id);

static inline key_serial_t key_serial(const struct key *key)
{
	return key ? key->serial : 0;
}

extern void key_set_timeout(struct key *, unsigned);

extern key_ref_t lookup_user_key(key_serial_t id, unsigned long flags,
				 u32 desired_perm);
extern void key_free_user_ns(struct user_namespace *);

/*
 * The permissions required on a key that we're looking up.
 */
#define	KEY_NEED_VIEW	0x001	/* Require permission to view attributes */
#define	KEY_NEED_READ	0x002	/* Require permission to read content */
#define	KEY_NEED_WRITE	0x004	/* Require permission to update / modify */
#define	KEY_NEED_SEARCH	0x008	/* Require permission to search (keyring) or find (key) */
#define	KEY_NEED_LINK	0x010	/* Require permission to link */
#define	KEY_NEED_SETSEC	0x020	/* Require permission to set owner, group, ACL */
#define	KEY_NEED_INVAL	0x040	/* Require permission to invalidate key */
#define	KEY_NEED_REVOKE	0x080	/* Require permission to revoke key */
#define	KEY_NEED_JOIN	0x100	/* Require permission to join keyring as session */
#define	KEY_NEED_CLEAR	0x200	/* Require permission to clear a keyring */
#define KEY_NEED_ALL	0x3ff

#define OLD_KEY_NEED_SETATTR 0x20 /* Used to be Require permission to change attributes */

extern struct key_acl internal_key_acl;
extern struct key_acl internal_keyring_acl;
extern struct key_acl internal_writable_keyring_acl;

static inline short key_read_state(const struct key *key)
{
	/* Barrier versus mark_key_instantiated(). */
	return smp_load_acquire(&key->state);
}

/**
 * key_is_positive - Determine if a key has been positively instantiated
 * @key: The key to check.
 *
 * Return true if the specified key has been positively instantiated, false
 * otherwise.
 */
static inline bool key_is_positive(const struct key *key)
{
	return key_read_state(key) == KEY_IS_POSITIVE;
}

static inline bool key_is_negative(const struct key *key)
{
	return key_read_state(key) < 0;
}

#define dereference_key_rcu(KEY)					\
	(rcu_dereference((KEY)->payload.rcu_data0))

#define dereference_key_locked(KEY)					\
	(rcu_dereference_protected((KEY)->payload.rcu_data0,		\
				   rwsem_is_locked(&((struct key *)(KEY))->sem)))

#define rcu_assign_keypointer(KEY, PAYLOAD)				\
do {									\
	rcu_assign_pointer((KEY)->payload.rcu_data0, (PAYLOAD));	\
} while (0)

#ifdef CONFIG_SYSCTL
extern struct ctl_table key_sysctls[];
#endif
/*
 * the userspace interface
 */
extern int install_thread_keyring_to_cred(struct cred *cred);
extern void key_fsuid_changed(struct cred *new_cred);
extern void key_fsgid_changed(struct cred *new_cred);
extern void key_init(void);

#else /* CONFIG_KEYS */

#define key_validate(k)			0
#define key_serial(k)			0
#define key_get(k) 			({ NULL; })
#define key_revoke(k)			do { } while(0)
#define key_invalidate(k)		do { } while(0)
#define key_put(k)			do { } while(0)
#define key_ref_put(k)			do { } while(0)
#define make_key_ref(k, p)		NULL
#define key_ref_to_ptr(k)		NULL
#define is_key_possessed(k)		0
#define key_fsuid_changed(c)		do { } while(0)
#define key_fsgid_changed(c)		do { } while(0)
#define key_init()			do { } while(0)
#define key_free_user_ns(ns)		do { } while(0)
#define key_remove_domain(d)		do { } while(0)

#endif /* CONFIG_KEYS */
#endif /* __KERNEL__ */
#endif /* _LINUX_KEY_H */
