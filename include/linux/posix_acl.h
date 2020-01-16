/* SPDX-License-Identifier: GPL-2.0 */
/*
  File: linux/posix_acl.h

  (C) 2002 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/


#ifndef __LINUX_POSIX_ACL_H
#define __LINUX_POSIX_ACL_H

#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <uapi/linux/posix_acl.h>

struct posix_acl_entry {
	short			e_tag;
	unsigned short		e_perm;
	union {
		kuid_t		e_uid;
		kgid_t		e_gid;
	};
};

struct posix_acl {
	refcount_t		a_refcount;
	struct rcu_head		a_rcu;
	unsigned int		a_count;
	struct posix_acl_entry	a_entries[0];
};

#define FOREACH_ACL_ENTRY(pa, acl, pe) \
	for(pa=(acl)->a_entries, pe=pa+(acl)->a_count; pa<pe; pa++)


/*
 * Duplicate an ACL handle.
 */
static inline struct posix_acl *
posix_acl_dup(struct posix_acl *acl)
{
	if (acl)
		refcount_inc(&acl->a_refcount);
	return acl;
}

/*
 * Free an ACL handle.
 */
static inline void
posix_acl_release(struct posix_acl *acl)
{
	if (acl && refcount_dec_and_test(&acl->a_refcount))
		kfree_rcu(acl, a_rcu);
}


/* posix_acl.c */

extern void posix_acl_init(struct posix_acl *, int);
extern struct posix_acl *posix_acl_alloc(int, gfp_t);
extern int posix_acl_valid(struct user_namespace *, const struct posix_acl *);
extern int posix_acl_permission(struct iyesde *, const struct posix_acl *, int);
extern struct posix_acl *posix_acl_from_mode(umode_t, gfp_t);
extern int posix_acl_equiv_mode(const struct posix_acl *, umode_t *);
extern int __posix_acl_create(struct posix_acl **, gfp_t, umode_t *);
extern int __posix_acl_chmod(struct posix_acl **, gfp_t, umode_t);

extern struct posix_acl *get_posix_acl(struct iyesde *, int);
extern int set_posix_acl(struct iyesde *, int, struct posix_acl *);

#ifdef CONFIG_FS_POSIX_ACL
extern int posix_acl_chmod(struct iyesde *, umode_t);
extern int posix_acl_create(struct iyesde *, umode_t *, struct posix_acl **,
		struct posix_acl **);
extern int posix_acl_update_mode(struct iyesde *, umode_t *, struct posix_acl **);

extern int simple_set_acl(struct iyesde *, struct posix_acl *, int);
extern int simple_acl_create(struct iyesde *, struct iyesde *);

struct posix_acl *get_cached_acl(struct iyesde *iyesde, int type);
struct posix_acl *get_cached_acl_rcu(struct iyesde *iyesde, int type);
void set_cached_acl(struct iyesde *iyesde, int type, struct posix_acl *acl);
void forget_cached_acl(struct iyesde *iyesde, int type);
void forget_all_cached_acls(struct iyesde *iyesde);

static inline void cache_yes_acl(struct iyesde *iyesde)
{
	iyesde->i_acl = NULL;
	iyesde->i_default_acl = NULL;
}
#else
static inline int posix_acl_chmod(struct iyesde *iyesde, umode_t mode)
{
	return 0;
}

#define simple_set_acl		NULL

static inline int simple_acl_create(struct iyesde *dir, struct iyesde *iyesde)
{
	return 0;
}
static inline void cache_yes_acl(struct iyesde *iyesde)
{
}

static inline int posix_acl_create(struct iyesde *iyesde, umode_t *mode,
		struct posix_acl **default_acl, struct posix_acl **acl)
{
	*default_acl = *acl = NULL;
	return 0;
}

static inline void forget_all_cached_acls(struct iyesde *iyesde)
{
}
#endif /* CONFIG_FS_POSIX_ACL */

struct posix_acl *get_acl(struct iyesde *iyesde, int type);

#endif  /* __LINUX_POSIX_ACL_H */
