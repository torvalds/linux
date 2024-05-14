/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MNT_IDMAPPING_H
#define _LINUX_MNT_IDMAPPING_H

#include <linux/types.h>
#include <linux/uidgid.h>

struct user_namespace;
/*
 * Carries the initial idmapping of 0:0:4294967295 which is an identity
 * mapping. This means that {g,u}id 0 is mapped to {g,u}id 0, {g,u}id 1 is
 * mapped to {g,u}id 1, [...], {g,u}id 1000 to {g,u}id 1000, [...].
 */
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
static inline int vfsgid_in_group_p(vfsgid_t vfsgid)
{
	return in_group_p(AS_KGIDT(vfsgid));
}
#else
static inline int vfsgid_in_group_p(vfsgid_t vfsgid)
{
	return 1;
}
#endif

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
 * make_vfsuid - map a filesystem kuid into a mnt_userns
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @kuid : kuid to be mapped
 *
 * Take a @kuid and remap it from @fs_userns into @mnt_userns. Use this
 * function when preparing a @kuid to be reported to userspace.
 *
 * If no_idmapping() determines that this is not an idmapped mount we can
 * simply return @kuid unchanged.
 * If initial_idmapping() tells us that the filesystem is not mounted with an
 * idmapping we know the value of @kuid won't change when calling
 * from_kuid() so we can simply retrieve the value via __kuid_val()
 * directly.
 *
 * Return: @kuid mapped according to @mnt_userns.
 * If @kuid has no mapping in either @mnt_userns or @fs_userns INVALID_UID is
 * returned.
 */

static inline vfsuid_t make_vfsuid(struct user_namespace *mnt_userns,
				   struct user_namespace *fs_userns,
				   kuid_t kuid)
{
	uid_t uid;

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

static inline kuid_t mapped_kuid_fs(struct user_namespace *mnt_userns,
				    struct user_namespace *fs_userns,
				    kuid_t kuid)
{
	return AS_KUIDT(make_vfsuid(mnt_userns, fs_userns, kuid));
}

/**
 * make_vfsgid - map a filesystem kgid into a mnt_userns
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @kgid : kgid to be mapped
 *
 * Take a @kgid and remap it from @fs_userns into @mnt_userns. Use this
 * function when preparing a @kgid to be reported to userspace.
 *
 * If no_idmapping() determines that this is not an idmapped mount we can
 * simply return @kgid unchanged.
 * If initial_idmapping() tells us that the filesystem is not mounted with an
 * idmapping we know the value of @kgid won't change when calling
 * from_kgid() so we can simply retrieve the value via __kgid_val()
 * directly.
 *
 * Return: @kgid mapped according to @mnt_userns.
 * If @kgid has no mapping in either @mnt_userns or @fs_userns INVALID_GID is
 * returned.
 */

static inline vfsgid_t make_vfsgid(struct user_namespace *mnt_userns,
				   struct user_namespace *fs_userns,
				   kgid_t kgid)
{
	gid_t gid;

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

static inline kgid_t mapped_kgid_fs(struct user_namespace *mnt_userns,
				    struct user_namespace *fs_userns,
				    kgid_t kgid)
{
	return AS_KGIDT(make_vfsgid(mnt_userns, fs_userns, kgid));
}

/**
 * from_vfsuid - map a vfsuid into the filesystem idmapping
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsuid : vfsuid to be mapped
 *
 * Map @vfsuid into the filesystem idmapping. This function has to be used in
 * order to e.g. write @vfsuid to inode->i_uid.
 *
 * Return: @vfsuid mapped into the filesystem idmapping
 */
static inline kuid_t from_vfsuid(struct user_namespace *mnt_userns,
				 struct user_namespace *fs_userns,
				 vfsuid_t vfsuid)
{
	uid_t uid;

	if (no_idmapping(mnt_userns, fs_userns))
		return AS_KUIDT(vfsuid);
	uid = from_kuid(mnt_userns, AS_KUIDT(vfsuid));
	if (uid == (uid_t)-1)
		return INVALID_UID;
	if (initial_idmapping(fs_userns))
		return KUIDT_INIT(uid);
	return make_kuid(fs_userns, uid);
}

/**
 * mapped_kuid_user - map a user kuid into a mnt_userns
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @kuid : kuid to be mapped
 *
 * Use the idmapping of @mnt_userns to remap a @kuid into @fs_userns. Use this
 * function when preparing a @kuid to be written to disk or inode.
 *
 * If no_idmapping() determines that this is not an idmapped mount we can
 * simply return @kuid unchanged.
 * If initial_idmapping() tells us that the filesystem is not mounted with an
 * idmapping we know the value of @kuid won't change when calling
 * make_kuid() so we can simply retrieve the value via KUIDT_INIT()
 * directly.
 *
 * Return: @kuid mapped according to @mnt_userns.
 * If @kuid has no mapping in either @mnt_userns or @fs_userns INVALID_UID is
 * returned.
 */
static inline kuid_t mapped_kuid_user(struct user_namespace *mnt_userns,
				      struct user_namespace *fs_userns,
				      kuid_t kuid)
{
	return from_vfsuid(mnt_userns, fs_userns, VFSUIDT_INIT(kuid));
}

/**
 * vfsuid_has_fsmapping - check whether a vfsuid maps into the filesystem
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsuid: vfsuid to be mapped
 *
 * Check whether @vfsuid has a mapping in the filesystem idmapping. Use this
 * function to check whether the filesystem idmapping has a mapping for
 * @vfsuid.
 *
 * Return: true if @vfsuid has a mapping in the filesystem, false if not.
 */
static inline bool vfsuid_has_fsmapping(struct user_namespace *mnt_userns,
					struct user_namespace *fs_userns,
					vfsuid_t vfsuid)
{
	return uid_valid(from_vfsuid(mnt_userns, fs_userns, vfsuid));
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
 * from_vfsgid - map a vfsgid into the filesystem idmapping
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsgid : vfsgid to be mapped
 *
 * Map @vfsgid into the filesystem idmapping. This function has to be used in
 * order to e.g. write @vfsgid to inode->i_gid.
 *
 * Return: @vfsgid mapped into the filesystem idmapping
 */
static inline kgid_t from_vfsgid(struct user_namespace *mnt_userns,
				 struct user_namespace *fs_userns,
				 vfsgid_t vfsgid)
{
	gid_t gid;

	if (no_idmapping(mnt_userns, fs_userns))
		return AS_KGIDT(vfsgid);
	gid = from_kgid(mnt_userns, AS_KGIDT(vfsgid));
	if (gid == (gid_t)-1)
		return INVALID_GID;
	if (initial_idmapping(fs_userns))
		return KGIDT_INIT(gid);
	return make_kgid(fs_userns, gid);
}

/**
 * mapped_kgid_user - map a user kgid into a mnt_userns
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @kgid : kgid to be mapped
 *
 * Use the idmapping of @mnt_userns to remap a @kgid into @fs_userns. Use this
 * function when preparing a @kgid to be written to disk or inode.
 *
 * If no_idmapping() determines that this is not an idmapped mount we can
 * simply return @kgid unchanged.
 * If initial_idmapping() tells us that the filesystem is not mounted with an
 * idmapping we know the value of @kgid won't change when calling
 * make_kgid() so we can simply retrieve the value via KGIDT_INIT()
 * directly.
 *
 * Return: @kgid mapped according to @mnt_userns.
 * If @kgid has no mapping in either @mnt_userns or @fs_userns INVALID_GID is
 * returned.
 */
static inline kgid_t mapped_kgid_user(struct user_namespace *mnt_userns,
				      struct user_namespace *fs_userns,
				      kgid_t kgid)
{
	return from_vfsgid(mnt_userns, fs_userns, VFSGIDT_INIT(kgid));
}

/**
 * vfsgid_has_fsmapping - check whether a vfsgid maps into the filesystem
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 * @vfsgid: vfsgid to be mapped
 *
 * Check whether @vfsgid has a mapping in the filesystem idmapping. Use this
 * function to check whether the filesystem idmapping has a mapping for
 * @vfsgid.
 *
 * Return: true if @vfsgid has a mapping in the filesystem, false if not.
 */
static inline bool vfsgid_has_fsmapping(struct user_namespace *mnt_userns,
					struct user_namespace *fs_userns,
					vfsgid_t vfsgid)
{
	return gid_valid(from_vfsgid(mnt_userns, fs_userns, vfsgid));
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
 * mapped_fsuid - return caller's fsuid mapped up into a mnt_userns
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 *
 * Use this helper to initialize a new vfs or filesystem object based on
 * the caller's fsuid. A common example is initializing the i_uid field of
 * a newly allocated inode triggered by a creation event such as mkdir or
 * O_CREAT. Other examples include the allocation of quotas for a specific
 * user.
 *
 * Return: the caller's current fsuid mapped up according to @mnt_userns.
 */
static inline kuid_t mapped_fsuid(struct user_namespace *mnt_userns,
				  struct user_namespace *fs_userns)
{
	return from_vfsuid(mnt_userns, fs_userns,
			   VFSUIDT_INIT(current_fsuid()));
}

/**
 * mapped_fsgid - return caller's fsgid mapped up into a mnt_userns
 * @mnt_userns: the mount's idmapping
 * @fs_userns: the filesystem's idmapping
 *
 * Use this helper to initialize a new vfs or filesystem object based on
 * the caller's fsgid. A common example is initializing the i_gid field of
 * a newly allocated inode triggered by a creation event such as mkdir or
 * O_CREAT. Other examples include the allocation of quotas for a specific
 * user.
 *
 * Return: the caller's current fsgid mapped up according to @mnt_userns.
 */
static inline kgid_t mapped_fsgid(struct user_namespace *mnt_userns,
				  struct user_namespace *fs_userns)
{
	return from_vfsgid(mnt_userns, fs_userns,
			   VFSGIDT_INIT(current_fsgid()));
}

#endif /* _LINUX_MNT_IDMAPPING_H */
