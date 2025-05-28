/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MNT_IDMAPPING_H
#define _LINUX_MNT_IDMAPPING_H

#include <linux/types.h>
#include <linux/uidgid.h>

struct mnt_idmap;
struct user_namespace;

extern struct mnt_idmap nop_mnt_idmap;
extern struct mnt_idmap invalid_mnt_idmap;
extern struct user_namespace init_user_ns;

typedef struct {
	uid_t val;
} vfsuid_t;

typedef struct {
	gid_t val;
} vfsgid_t;

static_assert(sizeof(vfsuid_t) == sizeof(kuid_t));
static_assert(sizeof(vfsgid_t) == sizeof(kgid_t));
static_assert(offsetof(vfsuid_t, val) == offsetof(kuid_t, val));
static_assert(offsetof(vfsgid_t, val) == offsetof(kgid_t, val));

static inline bool is_valid_mnt_idmap(const struct mnt_idmap *idmap)
{
	return idmap != &nop_mnt_idmap && idmap != &invalid_mnt_idmap;
}

#ifdef CONFIG_MULTIUSER
static inline uid_t __vfsuid_val(vfsuid_t uid)
{
	return uid.val;
}

static inline gid_t __vfsgid_val(vfsgid_t gid)
{
	return gid.val;
}
#else
static inline uid_t __vfsuid_val(vfsuid_t uid)
{
	return 0;
}

static inline gid_t __vfsgid_val(vfsgid_t gid)
{
	return 0;
}
#endif

static inline bool vfsuid_valid(vfsuid_t uid)
{
	return __vfsuid_val(uid) != (uid_t)-1;
}

static inline bool vfsgid_valid(vfsgid_t gid)
{
	return __vfsgid_val(gid) != (gid_t)-1;
}

static inline bool vfsuid_eq(vfsuid_t left, vfsuid_t right)
{
	return vfsuid_valid(left) && __vfsuid_val(left) == __vfsuid_val(right);
}

static inline bool vfsgid_eq(vfsgid_t left, vfsgid_t right)
{
	return vfsgid_valid(left) && __vfsgid_val(left) == __vfsgid_val(right);
}

/**
 * vfsuid_eq_kuid - check whether kuid and vfsuid have the same value
 * @vfsuid: the vfsuid to compare
 * @kuid: the kuid to compare
 *
 * Check whether @vfsuid and @kuid have the same values.
 *
 * Return: true if @vfsuid and @kuid have the same value, false if not.
 * Comparison between two invalid uids returns false.
 */
static inline bool vfsuid_eq_kuid(vfsuid_t vfsuid, kuid_t kuid)
{
	return vfsuid_valid(vfsuid) && __vfsuid_val(vfsuid) == __kuid_val(kuid);
}

/**
 * vfsgid_eq_kgid - check whether kgid and vfsgid have the same value
 * @vfsgid: the vfsgid to compare
 * @kgid: the kgid to compare
 *
 * Check whether @vfsgid and @kgid have the same values.
 *
 * Return: true if @vfsgid and @kgid have the same value, false if not.
 * Comparison between two invalid gids returns false.
 */
static inline bool vfsgid_eq_kgid(vfsgid_t vfsgid, kgid_t kgid)
{
	return vfsgid_valid(vfsgid) && __vfsgid_val(vfsgid) == __kgid_val(kgid);
}

/*
 * vfs{g,u}ids are created from k{g,u}ids.
 * We don't allow them to be created from regular {u,g}id.
 */
#define VFSUIDT_INIT(val) (vfsuid_t){ __kuid_val(val) }
#define VFSGIDT_INIT(val) (vfsgid_t){ __kgid_val(val) }

#define INVALID_VFSUID VFSUIDT_INIT(INVALID_UID)
#define INVALID_VFSGID VFSGIDT_INIT(INVALID_GID)

/*
 * Allow a vfs{g,u}id to be used as a k{g,u}id where we want to compare
 * whether the mapped value is identical to value of a k{g,u}id.
 */
#define AS_KUIDT(val) (kuid_t){ __vfsuid_val(val) }
#define AS_KGIDT(val) (kgid_t){ __vfsgid_val(val) }

int vfsgid_in_group_p(vfsgid_t vfsgid);

struct mnt_idmap *mnt_idmap_get(struct mnt_idmap *idmap);
void mnt_idmap_put(struct mnt_idmap *idmap);

vfsuid_t make_vfsuid(struct mnt_idmap *idmap,
		     struct user_namespace *fs_userns, kuid_t kuid);

vfsgid_t make_vfsgid(struct mnt_idmap *idmap,
		     struct user_namespace *fs_userns, kgid_t kgid);

kuid_t from_vfsuid(struct mnt_idmap *idmap,
		   struct user_namespace *fs_userns, vfsuid_t vfsuid);

kgid_t from_vfsgid(struct mnt_idmap *idmap,
		   struct user_namespace *fs_userns, vfsgid_t vfsgid);

/**
 * vfsuid_has_fsmapping - check whether a vfsuid maps into the filesystem
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsuid: vfsuid to be mapped
 *
 * Check whether @vfsuid has a mapping in the filesystem idmapping. Use this
 * function to check whether the filesystem idmapping has a mapping for
 * @vfsuid.
 *
 * Return: true if @vfsuid has a mapping in the filesystem, false if not.
 */
static inline bool vfsuid_has_fsmapping(struct mnt_idmap *idmap,
					struct user_namespace *fs_userns,
					vfsuid_t vfsuid)
{
	return uid_valid(from_vfsuid(idmap, fs_userns, vfsuid));
}

static inline bool vfsuid_has_mapping(struct user_namespace *userns,
				      vfsuid_t vfsuid)
{
	return from_kuid(userns, AS_KUIDT(vfsuid)) != (uid_t)-1;
}

/**
 * vfsuid_into_kuid - convert vfsuid into kuid
 * @vfsuid: the vfsuid to convert
 *
 * This can be used when a vfsuid is committed as a kuid.
 *
 * Return: a kuid with the value of @vfsuid
 */
static inline kuid_t vfsuid_into_kuid(vfsuid_t vfsuid)
{
	return AS_KUIDT(vfsuid);
}

/**
 * vfsgid_has_fsmapping - check whether a vfsgid maps into the filesystem
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsgid: vfsgid to be mapped
 *
 * Check whether @vfsgid has a mapping in the filesystem idmapping. Use this
 * function to check whether the filesystem idmapping has a mapping for
 * @vfsgid.
 *
 * Return: true if @vfsgid has a mapping in the filesystem, false if not.
 */
static inline bool vfsgid_has_fsmapping(struct mnt_idmap *idmap,
					struct user_namespace *fs_userns,
					vfsgid_t vfsgid)
{
	return gid_valid(from_vfsgid(idmap, fs_userns, vfsgid));
}

static inline bool vfsgid_has_mapping(struct user_namespace *userns,
				      vfsgid_t vfsgid)
{
	return from_kgid(userns, AS_KGIDT(vfsgid)) != (gid_t)-1;
}

/**
 * vfsgid_into_kgid - convert vfsgid into kgid
 * @vfsgid: the vfsgid to convert
 *
 * This can be used when a vfsgid is committed as a kgid.
 *
 * Return: a kgid with the value of @vfsgid
 */
static inline kgid_t vfsgid_into_kgid(vfsgid_t vfsgid)
{
	return AS_KGIDT(vfsgid);
}

/**
 * mapped_fsuid - return caller's fsuid mapped according to an idmapping
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 *
 * Use this helper to initialize a new vfs or filesystem object based on
 * the caller's fsuid. A common example is initializing the i_uid field of
 * a newly allocated inode triggered by a creation event such as mkdir or
 * O_CREAT. Other examples include the allocation of quotas for a specific
 * user.
 *
 * Return: the caller's current fsuid mapped up according to @idmap.
 */
static inline kuid_t mapped_fsuid(struct mnt_idmap *idmap,
				  struct user_namespace *fs_userns)
{
	return from_vfsuid(idmap, fs_userns, VFSUIDT_INIT(current_fsuid()));
}

/**
 * mapped_fsgid - return caller's fsgid mapped according to an idmapping
 * @idmap: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 *
 * Use this helper to initialize a new vfs or filesystem object based on
 * the caller's fsgid. A common example is initializing the i_gid field of
 * a newly allocated inode triggered by a creation event such as mkdir or
 * O_CREAT. Other examples include the allocation of quotas for a specific
 * user.
 *
 * Return: the caller's current fsgid mapped up according to @idmap.
 */
static inline kgid_t mapped_fsgid(struct mnt_idmap *idmap,
				  struct user_namespace *fs_userns)
{
	return from_vfsgid(idmap, fs_userns, VFSGIDT_INIT(current_fsgid()));
}

#endif /* _LINUX_MNT_IDMAPPING_H */
