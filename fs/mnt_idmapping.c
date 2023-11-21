// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Christian Brauner <brauner@kernel.org> */

#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/mnt_idmapping.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>

#include "internal.h"

struct mnt_idmap {
	struct user_namespace *owner;
	refcount_t count;
};

/*
 * Carries the initial idmapping of 0:0:4294967295 which is an identity
 * mapping. This means that {g,u}id 0 is mapped to {g,u}id 0, {g,u}id 1 is
 * mapped to {g,u}id 1, [...], {g,u}id 1000 to {g,u}id 1000, [...].
 */
struct mnt_idmap nop_mnt_idmap = {
	.owner	= &init_user_ns,
	.count	= REFCOUNT_INIT(1),
};
EXPORT_SYMBOL_GPL(nop_mnt_idmap);

/**
 * check_fsmapping - check whether an mount idmapping is allowed
 * @idmap: idmap of the relevent mount
 * @sb:    super block of the filesystem
 *
 * Return: true if @idmap is allowed, false if not.
 */
bool check_fsmapping(const struct mnt_idmap *idmap,
		     const struct super_block *sb)
{
	return idmap->owner != sb->s_user_ns;
}

/**
 * initial_idmapping - check whether this is the initial mapping
 * @ns: idmapping to check
 *
 * Check whether this is the initial mapping, mapping 0 to 0, 1 to 1,
 * [...], 1000 to 1000 [...].
 *
 * Return: true if this is the initial mapping, false if not.
 */
static inline bool initial_idmapping(const struct user_namespace *ns)
{
	return ns == &init_user_ns;
}

/**
 * no_idmapping - check whether we can skip remapping a kuid/gid
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 *
 * This function can be used to check whether a remapping between two
 * idmappings is required.
 * An idmapped mount is a mount that has an idmapping attached to it that
 * is different from the filsystem's idmapping and the initial idmapping.
 * If the initial mapping is used or the idmapping of the mount and the
 * filesystem are identical no remapping is required.
 *
 * Return: true if remapping can be skipped, false if not.
 */
static inline bool no_idmapping(const struct user_namespace *mnt_userns,
				const struct user_namespace *fs_userns)
{
	return initial_idmapping(mnt_userns) || mnt_userns == fs_userns;
}

/**
 * make_vfsuid - map a filesystem kuid according to an idmapping
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @kuid : kuid to be mapped
 *
 * Take a @kuid and remap it from @fs_userns into @idmap. Use this
 * function when preparing a @kuid to be reported to userspace.
 *
 * If no_idmapping() determines that this is not an idmapped mount we can
 * simply return @kuid unchanged.
 * If initial_idmapping() tells us that the filesystem is not mounted with an
 * idmapping we know the value of @kuid won't change when calling
 * from_kuid() so we can simply retrieve the value via __kuid_val()
 * directly.
 *
 * Return: @kuid mapped according to @idmap.
 * If @kuid has no mapping in either @idmap or @fs_userns INVALID_UID is
 * returned.
 */

vfsuid_t make_vfsuid(struct mnt_idmap *idmap,
				   struct user_namespace *fs_userns,
				   kuid_t kuid)
{
	uid_t uid;
	struct user_namespace *mnt_userns = idmap->owner;

	if (no_idmapping(mnt_userns, fs_userns))
		return VFSUIDT_INIT(kuid);
	if (initial_idmapping(fs_userns))
		uid = __kuid_val(kuid);
	else
		uid = from_kuid(fs_userns, kuid);
	if (uid == (uid_t)-1)
		return INVALID_VFSUID;
	return VFSUIDT_INIT(make_kuid(mnt_userns, uid));
}
EXPORT_SYMBOL_GPL(make_vfsuid);

/**
 * make_vfsgid - map a filesystem kgid according to an idmapping
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @kgid : kgid to be mapped
 *
 * Take a @kgid and remap it from @fs_userns into @idmap. Use this
 * function when preparing a @kgid to be reported to userspace.
 *
 * If no_idmapping() determines that this is not an idmapped mount we can
 * simply return @kgid unchanged.
 * If initial_idmapping() tells us that the filesystem is not mounted with an
 * idmapping we know the value of @kgid won't change when calling
 * from_kgid() so we can simply retrieve the value via __kgid_val()
 * directly.
 *
 * Return: @kgid mapped according to @idmap.
 * If @kgid has no mapping in either @idmap or @fs_userns INVALID_GID is
 * returned.
 */
vfsgid_t make_vfsgid(struct mnt_idmap *idmap,
		     struct user_namespace *fs_userns, kgid_t kgid)
{
	gid_t gid;
	struct user_namespace *mnt_userns = idmap->owner;

	if (no_idmapping(mnt_userns, fs_userns))
		return VFSGIDT_INIT(kgid);
	if (initial_idmapping(fs_userns))
		gid = __kgid_val(kgid);
	else
		gid = from_kgid(fs_userns, kgid);
	if (gid == (gid_t)-1)
		return INVALID_VFSGID;
	return VFSGIDT_INIT(make_kgid(mnt_userns, gid));
}
EXPORT_SYMBOL_GPL(make_vfsgid);

/**
 * from_vfsuid - map a vfsuid into the filesystem idmapping
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsuid : vfsuid to be mapped
 *
 * Map @vfsuid into the filesystem idmapping. This function has to be used in
 * order to e.g. write @vfsuid to inode->i_uid.
 *
 * Return: @vfsuid mapped into the filesystem idmapping
 */
kuid_t from_vfsuid(struct mnt_idmap *idmap,
		   struct user_namespace *fs_userns, vfsuid_t vfsuid)
{
	uid_t uid;
	struct user_namespace *mnt_userns = idmap->owner;

	if (no_idmapping(mnt_userns, fs_userns))
		return AS_KUIDT(vfsuid);
	uid = from_kuid(mnt_userns, AS_KUIDT(vfsuid));
	if (uid == (uid_t)-1)
		return INVALID_UID;
	if (initial_idmapping(fs_userns))
		return KUIDT_INIT(uid);
	return make_kuid(fs_userns, uid);
}
EXPORT_SYMBOL_GPL(from_vfsuid);

/**
 * from_vfsgid - map a vfsgid into the filesystem idmapping
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsgid : vfsgid to be mapped
 *
 * Map @vfsgid into the filesystem idmapping. This function has to be used in
 * order to e.g. write @vfsgid to inode->i_gid.
 *
 * Return: @vfsgid mapped into the filesystem idmapping
 */
kgid_t from_vfsgid(struct mnt_idmap *idmap,
		   struct user_namespace *fs_userns, vfsgid_t vfsgid)
{
	gid_t gid;
	struct user_namespace *mnt_userns = idmap->owner;

	if (no_idmapping(mnt_userns, fs_userns))
		return AS_KGIDT(vfsgid);
	gid = from_kgid(mnt_userns, AS_KGIDT(vfsgid));
	if (gid == (gid_t)-1)
		return INVALID_GID;
	if (initial_idmapping(fs_userns))
		return KGIDT_INIT(gid);
	return make_kgid(fs_userns, gid);
}
EXPORT_SYMBOL_GPL(from_vfsgid);

#ifdef CONFIG_MULTIUSER
/**
 * vfsgid_in_group_p() - check whether a vfsuid matches the caller's groups
 * @vfsgid: the mnt gid to match
 *
 * This function can be used to determine whether @vfsuid matches any of the
 * caller's groups.
 *
 * Return: 1 if vfsuid matches caller's groups, 0 if not.
 */
int vfsgid_in_group_p(vfsgid_t vfsgid)
{
	return in_group_p(AS_KGIDT(vfsgid));
}
#else
int vfsgid_in_group_p(vfsgid_t vfsgid)
{
	return 1;
}
#endif
EXPORT_SYMBOL_GPL(vfsgid_in_group_p);

struct mnt_idmap *alloc_mnt_idmap(struct user_namespace *mnt_userns)
{
	struct mnt_idmap *idmap;

	idmap = kzalloc(sizeof(struct mnt_idmap), GFP_KERNEL_ACCOUNT);
	if (!idmap)
		return ERR_PTR(-ENOMEM);

	idmap->owner = get_user_ns(mnt_userns);
	refcount_set(&idmap->count, 1);
	return idmap;
}

/**
 * mnt_idmap_get - get a reference to an idmapping
 * @idmap: the idmap to bump the reference on
 *
 * If @idmap is not the @nop_mnt_idmap bump the reference count.
 *
 * Return: @idmap with reference count bumped if @not_mnt_idmap isn't passed.
 */
struct mnt_idmap *mnt_idmap_get(struct mnt_idmap *idmap)
{
	if (idmap != &nop_mnt_idmap)
		refcount_inc(&idmap->count);

	return idmap;
}
EXPORT_SYMBOL_GPL(mnt_idmap_get);

/**
 * mnt_idmap_put - put a reference to an idmapping
 * @idmap: the idmap to put the reference on
 *
 * If this is a non-initial idmapping, put the reference count when a mount is
 * released and free it if we're the last user.
 */
void mnt_idmap_put(struct mnt_idmap *idmap)
{
	if (idmap != &nop_mnt_idmap && refcount_dec_and_test(&idmap->count)) {
		put_user_ns(idmap->owner);
		kfree(idmap);
	}
}
EXPORT_SYMBOL_GPL(mnt_idmap_put);
