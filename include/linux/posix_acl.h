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

struct user_namespace;

struct posix_acl_entry {
	short			e_tag;
	unsigned short		e_perm;
	union {
		kuid_t		e_uid;
		kgid_t		e_gid;
	};
};

struct posix_acl {
	/* New members MUST be added within the struct_group() macro below. */
	struct_group_tagged(posix_acl_hdr, hdr,
		refcount_t		a_refcount;
		unsigned int		a_count;
		struct rcu_head		a_rcu;
	);
	struct posix_acl_entry	a_entries[] __counted_by(a_count);
};
static_assert(offsetof(struct posix_acl, a_entries) == sizeof(struct posix_acl_hdr),
	      "struct member likely outside of struct_group_tagged()");

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
extern struct posix_acl *posix_acl_alloc(unsigned int count, gfp_t flags);
extern struct posix_acl *posix_acl_from_mode(umode_t, gfp_t);
extern int posix_acl_equiv_mode(const struct posix_acl *, umode_t *);
extern int __posix_acl_create(struct posix_acl **, gfp_t, umode_t *);
extern int __posix_acl_chmod(struct posix_acl **, gfp_t, umode_t);

extern struct posix_acl *get_posix_acl(struct inode *, int);
int set_posix_acl(struct mnt_idmap *, struct dentry *, int,
		  struct posix_acl *);

struct posix_acl *get_cached_acl_rcu(struct inode *inode, int type);
struct posix_acl *posix_acl_clone(const struct posix_acl *acl, gfp_t flags);

#ifdef CONFIG_FS_POSIX_ACL
int posix_acl_chmod(struct mnt_idmap *, struct dentry *, umode_t);
extern int posix_acl_create(struct inode *, umode_t *, struct posix_acl **,
		struct posix_acl **);
int posix_acl_update_mode(struct mnt_idmap *, struct inode *, umode_t *,
			  struct posix_acl **);

int simple_set_acl(struct mnt_idmap *, struct dentry *,
		   struct posix_acl *, int);
extern int simple_acl_create(struct inode *, struct inode *);

struct posix_acl *get_cached_acl(struct inode *inode, int type);
void set_cached_acl(struct inode *inode, int type, struct posix_acl *acl);
void forget_cached_acl(struct inode *inode, int type);
void forget_all_cached_acls(struct inode *inode);
int posix_acl_valid(struct user_namespace *, const struct posix_acl *);
int posix_acl_permission(struct mnt_idmap *, struct inode *,
			 const struct posix_acl *, int);

static inline void cache_no_acl(struct inode *inode)
{
	inode->i_acl = NULL;
	inode->i_default_acl = NULL;
}

int vfs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		const char *acl_name, struct posix_acl *kacl);
struct posix_acl *vfs_get_acl(struct mnt_idmap *idmap,
			      struct dentry *dentry, const char *acl_name);
int vfs_remove_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		   const char *acl_name);
int posix_acl_listxattr(struct inode *inode, char **buffer,
			ssize_t *remaining_size);
#else
static inline int posix_acl_chmod(struct mnt_idmap *idmap,
				  struct dentry *dentry, umode_t mode)
{
	return 0;
}

#define simple_set_acl		NULL

static inline int simple_acl_create(struct inode *dir, struct inode *inode)
{
	return 0;
}
static inline void cache_no_acl(struct inode *inode)
{
}

static inline int posix_acl_create(struct inode *inode, umode_t *mode,
		struct posix_acl **default_acl, struct posix_acl **acl)
{
	*default_acl = *acl = NULL;
	return 0;
}

static inline void forget_all_cached_acls(struct inode *inode)
{
}

static inline int vfs_set_acl(struct mnt_idmap *idmap,
			      struct dentry *dentry, const char *name,
			      struct posix_acl *acl)
{
	return -EOPNOTSUPP;
}

static inline struct posix_acl *vfs_get_acl(struct mnt_idmap *idmap,
					    struct dentry *dentry,
					    const char *acl_name)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int vfs_remove_acl(struct mnt_idmap *idmap,
				 struct dentry *dentry, const char *acl_name)
{
	return -EOPNOTSUPP;
}
static inline int posix_acl_listxattr(struct inode *inode, char **buffer,
				      ssize_t *remaining_size)
{
	return 0;
}
#endif /* CONFIG_FS_POSIX_ACL */

struct posix_acl *get_inode_acl(struct inode *inode, int type);

#endif  /* __LINUX_POSIX_ACL_H */
