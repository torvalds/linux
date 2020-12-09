/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Authentication token and access key management internal defs
 *
 * Copyright (C) 2003-5, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _INTERNAL_H
#define _INTERNAL_H

#include <linux/sched.h>
#include <linux/wait_bit.h>
#include <linux/cred.h>
#include <linux/key-type.h>
#include <linux/task_work.h>
#include <linux/keyctl.h>
#include <linux/refcount.h>
#include <linux/watch_queue.h>
#include <linux/compat.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

struct iovec;

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

extern struct key_type key_type_dead;
extern struct key_type key_type_user;
extern struct key_type key_type_logon;

/*****************************************************************************/
/*
 * Keep track of keys for a user.
 *
 * This needs to be separate to user_struct to avoid a refcount-loop
 * (user_struct pins some keyrings which pin this struct).
 *
 * We also keep track of keys under request from userspace for this UID here.
 */
struct key_user {
	struct rb_node		node;
	struct mutex		cons_lock;	/* construction initiation lock */
	spinlock_t		lock;
	refcount_t		usage;		/* for accessing qnkeys & qnbytes */
	atomic_t		nkeys;		/* number of keys */
	atomic_t		nikeys;		/* number of instantiated keys */
	kuid_t			uid;
	int			qnkeys;		/* number of keys allocated to this user */
	int			qnbytes;	/* number of bytes allocated to this user */
};

extern struct rb_root	key_user_tree;
extern spinlock_t	key_user_lock;
extern struct key_user	root_key_user;

extern struct key_user *key_user_lookup(kuid_t uid);
extern void key_user_put(struct key_user *user);

/*
 * Key quota limits.
 * - root has its own separate limits to everyone else
 */
extern unsigned key_quota_root_maxkeys;
extern unsigned key_quota_root_maxbytes;
extern unsigned key_quota_maxkeys;
extern unsigned key_quota_maxbytes;

#define KEYQUOTA_LINK_BYTES	4		/* a link in a keyring is worth 4 bytes */


extern struct kmem_cache *key_jar;
extern struct rb_root key_serial_tree;
extern spinlock_t key_serial_lock;
extern struct mutex key_construction_mutex;
extern wait_queue_head_t request_key_conswq;

extern void key_set_index_key(struct keyring_index_key *index_key);
extern struct key_type *key_type_lookup(const char *type);
extern void key_type_put(struct key_type *ktype);

extern int __key_link_lock(struct key *keyring,
			   const struct keyring_index_key *index_key);
extern int __key_move_lock(struct key *l_keyring, struct key *u_keyring,
			   const struct keyring_index_key *index_key);
extern int __key_link_begin(struct key *keyring,
			    const struct keyring_index_key *index_key,
			    struct assoc_array_edit **_edit);
extern int __key_link_check_live_key(struct key *keyring, struct key *key);
extern void __key_link(struct key *keyring, struct key *key,
		       struct assoc_array_edit **_edit);
extern void __key_link_end(struct key *keyring,
			   const struct keyring_index_key *index_key,
			   struct assoc_array_edit *edit);

extern key_ref_t find_key_to_update(key_ref_t keyring_ref,
				    const struct keyring_index_key *index_key);

extern struct key *keyring_search_instkey(struct key *keyring,
					  key_serial_t target_id);

extern int iterate_over_keyring(const struct key *keyring,
				int (*func)(const struct key *key, void *data),
				void *data);

struct keyring_search_context {
	struct keyring_index_key index_key;
	const struct cred	*cred;
	struct key_match_data	match_data;
	unsigned		flags;
#define KEYRING_SEARCH_NO_STATE_CHECK	0x0001	/* Skip state checks */
#define KEYRING_SEARCH_DO_STATE_CHECK	0x0002	/* Override NO_STATE_CHECK */
#define KEYRING_SEARCH_NO_UPDATE_TIME	0x0004	/* Don't update times */
#define KEYRING_SEARCH_NO_CHECK_PERM	0x0008	/* Don't check permissions */
#define KEYRING_SEARCH_DETECT_TOO_DEEP	0x0010	/* Give an error on excessive depth */
#define KEYRING_SEARCH_SKIP_EXPIRED	0x0020	/* Ignore expired keys (intention to replace) */
#define KEYRING_SEARCH_RECURSE		0x0040	/* Search child keyrings also */

	int (*iterator)(const void *object, void *iterator_data);

	/* Internal stuff */
	int			skipped_ret;
	bool			possessed;
	key_ref_t		result;
	time64_t		now;
};

extern bool key_default_cmp(const struct key *key,
			    const struct key_match_data *match_data);
extern key_ref_t keyring_search_rcu(key_ref_t keyring_ref,
				    struct keyring_search_context *ctx);

extern key_ref_t search_cred_keyrings_rcu(struct keyring_search_context *ctx);
extern key_ref_t search_process_keyrings_rcu(struct keyring_search_context *ctx);

extern struct key *find_keyring_by_name(const char *name, bool uid_keyring);

extern int look_up_user_keyrings(struct key **, struct key **);
extern struct key *get_user_session_keyring_rcu(const struct cred *);
extern int install_thread_keyring_to_cred(struct cred *);
extern int install_process_keyring_to_cred(struct cred *);
extern int install_session_keyring_to_cred(struct cred *, struct key *);

extern struct key *request_key_and_link(struct key_type *type,
					const char *description,
					struct key_tag *domain_tag,
					const void *callout_info,
					size_t callout_len,
					void *aux,
					struct key *dest_keyring,
					unsigned long flags);

extern bool lookup_user_key_possessed(const struct key *key,
				      const struct key_match_data *match_data);
#define KEY_LOOKUP_CREATE	0x01
#define KEY_LOOKUP_PARTIAL	0x02

extern long join_session_keyring(const char *name);
extern void key_change_session_keyring(struct callback_head *twork);

extern struct work_struct key_gc_work;
extern unsigned key_gc_delay;
extern void keyring_gc(struct key *keyring, time64_t limit);
extern void keyring_restriction_gc(struct key *keyring,
				   struct key_type *dead_type);
extern void key_schedule_gc(time64_t gc_at);
extern void key_schedule_gc_links(void);
extern void key_gc_keytype(struct key_type *ktype);

extern int key_task_permission(const key_ref_t key_ref,
			       const struct cred *cred,
			       enum key_need_perm need_perm);

static inline void notify_key(struct key *key,
			      enum key_notification_subtype subtype, u32 aux)
{
#ifdef CONFIG_KEY_NOTIFICATIONS
	struct key_notification n = {
		.watch.type	= WATCH_TYPE_KEY_NOTIFY,
		.watch.subtype	= subtype,
		.watch.info	= watch_sizeof(n),
		.key_id		= key_serial(key),
		.aux		= aux,
	};

	post_watch_notification(key->watchers, &n.watch, current_cred(),
				n.key_id);
#endif
}

/*
 * Check to see whether permission is granted to use a key in the desired way.
 */
static inline int key_permission(const key_ref_t key_ref,
				 enum key_need_perm need_perm)
{
	return key_task_permission(key_ref, current_cred(), need_perm);
}

extern struct key_type key_type_request_key_auth;
extern struct key *request_key_auth_new(struct key *target,
					const char *op,
					const void *callout_info,
					size_t callout_len,
					struct key *dest_keyring);

extern struct key *key_get_instantiation_authkey(key_serial_t target_id);

/*
 * Determine whether a key is dead.
 */
static inline bool key_is_dead(const struct key *key, time64_t limit)
{
	return
		key->flags & ((1 << KEY_FLAG_DEAD) |
			      (1 << KEY_FLAG_INVALIDATED)) ||
		(key->expiry > 0 && key->expiry <= limit) ||
		key->domain_tag->removed;
}

/*
 * keyctl() functions
 */
extern long keyctl_get_keyring_ID(key_serial_t, int);
extern long keyctl_join_session_keyring(const char __user *);
extern long keyctl_update_key(key_serial_t, const void __user *, size_t);
extern long keyctl_revoke_key(key_serial_t);
extern long keyctl_keyring_clear(key_serial_t);
extern long keyctl_keyring_link(key_serial_t, key_serial_t);
extern long keyctl_keyring_move(key_serial_t, key_serial_t, key_serial_t, unsigned int);
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
extern long keyctl_session_to_parent(void);
extern long keyctl_reject_key(key_serial_t, unsigned, unsigned, key_serial_t);
extern long keyctl_instantiate_key_iov(key_serial_t,
				       const struct iovec __user *,
				       unsigned, key_serial_t);
extern long keyctl_invalidate_key(key_serial_t);
extern long keyctl_restrict_keyring(key_serial_t id,
				    const char __user *_type,
				    const char __user *_restriction);
#ifdef CONFIG_PERSISTENT_KEYRINGS
extern long keyctl_get_persistent(uid_t, key_serial_t);
extern unsigned persistent_keyring_expiry;
#else
static inline long keyctl_get_persistent(uid_t uid, key_serial_t destring)
{
	return -EOPNOTSUPP;
}
#endif

#ifdef CONFIG_KEY_DH_OPERATIONS
extern long keyctl_dh_compute(struct keyctl_dh_params __user *, char __user *,
			      size_t, struct keyctl_kdf_params __user *);
extern long __keyctl_dh_compute(struct keyctl_dh_params __user *, char __user *,
				size_t, struct keyctl_kdf_params *);
#ifdef CONFIG_COMPAT
extern long compat_keyctl_dh_compute(struct keyctl_dh_params __user *params,
				char __user *buffer, size_t buflen,
				struct compat_keyctl_kdf_params __user *kdf);
#endif
#define KEYCTL_KDF_MAX_OUTPUT_LEN	1024	/* max length of KDF output */
#define KEYCTL_KDF_MAX_OI_LEN		64	/* max length of otherinfo */
#else
static inline long keyctl_dh_compute(struct keyctl_dh_params __user *params,
				     char __user *buffer, size_t buflen,
				     struct keyctl_kdf_params __user *kdf)
{
	return -EOPNOTSUPP;
}

#ifdef CONFIG_COMPAT
static inline long compat_keyctl_dh_compute(
				struct keyctl_dh_params __user *params,
				char __user *buffer, size_t buflen,
				struct keyctl_kdf_params __user *kdf)
{
	return -EOPNOTSUPP;
}
#endif
#endif

#ifdef CONFIG_ASYMMETRIC_KEY_TYPE
extern long keyctl_pkey_query(key_serial_t,
			      const char __user *,
			      struct keyctl_pkey_query __user *);

extern long keyctl_pkey_verify(const struct keyctl_pkey_params __user *,
			       const char __user *,
			       const void __user *, const void __user *);

extern long keyctl_pkey_e_d_s(int,
			      const struct keyctl_pkey_params __user *,
			      const char __user *,
			      const void __user *, void __user *);
#else
static inline long keyctl_pkey_query(key_serial_t id,
				     const char __user *_info,
				     struct keyctl_pkey_query __user *_res)
{
	return -EOPNOTSUPP;
}

static inline long keyctl_pkey_verify(const struct keyctl_pkey_params __user *params,
				      const char __user *_info,
				      const void __user *_in,
				      const void __user *_in2)
{
	return -EOPNOTSUPP;
}

static inline long keyctl_pkey_e_d_s(int op,
				     const struct keyctl_pkey_params __user *params,
				     const char __user *_info,
				     const void __user *_in,
				     void __user *_out)
{
	return -EOPNOTSUPP;
}
#endif

extern long keyctl_capabilities(unsigned char __user *_buffer, size_t buflen);

#ifdef CONFIG_KEY_NOTIFICATIONS
extern long keyctl_watch_key(key_serial_t, int, int);
#else
static inline long keyctl_watch_key(key_serial_t key_id, int watch_fd, int watch_id)
{
	return -EOPNOTSUPP;
}
#endif

/*
 * Debugging key validation
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
