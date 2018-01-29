/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IVERSION_H
#define _LINUX_IVERSION_H

#include <linux/fs.h>

/*
 * The change attribute (i_version) is mandated by NFSv4 and is mostly for
 * knfsd, but is also used for other purposes (e.g. IMA). The i_version must
 * appear different to observers if there was a change to the inode's data or
 * metadata since it was last queried.
 *
 * Observers see the i_version as a 64-bit number that never decreases. If it
 * remains the same since it was last checked, then nothing has changed in the
 * inode. If it's different then something has changed. Observers cannot infer
 * anything about the nature or magnitude of the changes from the value, only
 * that the inode has changed in some fashion.
 *
 * Not all filesystems properly implement the i_version counter. Subsystems that
 * want to use i_version field on an inode should first check whether the
 * filesystem sets the SB_I_VERSION flag (usually via the IS_I_VERSION macro).
 *
 * Those that set SB_I_VERSION will automatically have their i_version counter
 * incremented on writes to normal files. If the SB_I_VERSION is not set, then
 * the VFS will not touch it on writes, and the filesystem can use it how it
 * wishes. Note that the filesystem is always responsible for updating the
 * i_version on namespace changes in directories (mkdir, rmdir, unlink, etc.).
 * We consider these sorts of filesystems to have a kernel-managed i_version.
 *
 * It may be impractical for filesystems to keep i_version updates atomic with
 * respect to the changes that cause them.  They should, however, guarantee
 * that i_version updates are never visible before the changes that caused
 * them.  Also, i_version updates should never be delayed longer than it takes
 * the original change to reach disk.
 *
 * Note that some filesystems (e.g. NFS and AFS) just use the field to store
 * a server-provided value (for the most part). For that reason, those
 * filesystems do not set SB_I_VERSION. These filesystems are considered to
 * have a self-managed i_version.
 */

/**
 * inode_set_iversion_raw - set i_version to the specified raw value
 * @inode: inode to set
 * @new: new i_version value to set
 *
 * Set @inode's i_version field to @new. This function is for use by
 * filesystems that self-manage the i_version.
 *
 * For example, the NFS client stores its NFSv4 change attribute in this way,
 * and the AFS client stores the data_version from the server here.
 */
static inline void
inode_set_iversion_raw(struct inode *inode, u64 new)
{
	inode->i_version = new;
}

/**
 * inode_set_iversion - set i_version to a particular value
 * @inode: inode to set
 * @new: new i_version value to set
 *
 * Set @inode's i_version field to @new. This function is for filesystems with
 * a kernel-managed i_version.
 *
 * For now, this just does the same thing as the _raw variant.
 */
static inline void
inode_set_iversion(struct inode *inode, u64 new)
{
	inode_set_iversion_raw(inode, new);
}

/**
 * inode_set_iversion_queried - set i_version to a particular value and set
 *                              flag to indicate that it has been viewed
 * @inode: inode to set
 * @new: new i_version value to set
 *
 * When loading in an i_version value from a backing store, we typically don't
 * know whether it was previously viewed before being stored or not. Thus, we
 * must assume that it was, to ensure that any changes will result in the
 * value changing.
 *
 * This function will set the inode's i_version, and possibly flag the value
 * as if it has already been viewed at least once.
 *
 * For now, this just does what inode_set_iversion does.
 */
static inline void
inode_set_iversion_queried(struct inode *inode, u64 new)
{
	inode_set_iversion(inode, new);
}

/**
 * inode_maybe_inc_iversion - increments i_version
 * @inode: inode with the i_version that should be updated
 * @force: increment the counter even if it's not necessary
 *
 * Every time the inode is modified, the i_version field must be seen to have
 * changed by any observer.
 *
 * In this implementation, we always increment it after taking the i_lock to
 * ensure that we don't race with other incrementors.
 *
 * Returns true if counter was bumped, and false if it wasn't.
 */
static inline bool
inode_maybe_inc_iversion(struct inode *inode, bool force)
{
	spin_lock(&inode->i_lock);
	inode->i_version++;
	spin_unlock(&inode->i_lock);
	return true;
}

/**
 * inode_inc_iversion - forcibly increment i_version
 * @inode: inode that needs to be updated
 *
 * Forcbily increment the i_version field. This always results in a change to
 * the observable value.
 */
static inline void
inode_inc_iversion(struct inode *inode)
{
	inode_maybe_inc_iversion(inode, true);
}

/**
 * inode_iversion_need_inc - is the i_version in need of being incremented?
 * @inode: inode to check
 *
 * Returns whether the inode->i_version counter needs incrementing on the next
 * change.
 *
 * For now, we assume that it always does.
 */
static inline bool
inode_iversion_need_inc(struct inode *inode)
{
	return true;
}

/**
 * inode_peek_iversion_raw - grab a "raw" iversion value
 * @inode: inode from which i_version should be read
 *
 * Grab a "raw" inode->i_version value and return it. The i_version is not
 * flagged or converted in any way. This is mostly used to access a self-managed
 * i_version.
 *
 * With those filesystems, we want to treat the i_version as an entirely
 * opaque value.
 */
static inline u64
inode_peek_iversion_raw(const struct inode *inode)
{
	return inode->i_version;
}

/**
 * inode_inc_iversion_raw - forcibly increment raw i_version
 * @inode: inode that needs to be updated
 *
 * Forcbily increment the raw i_version field. This always results in a change
 * to the raw value.
 *
 * NFS will use the i_version field to store the value from the server. It
 * mostly treats it as opaque, but in the case where it holds a write
 * delegation, it must increment the value itself. This function does that.
 */
static inline void
inode_inc_iversion_raw(struct inode *inode)
{
	inode_inc_iversion(inode);
}

/**
 * inode_peek_iversion - read i_version without flagging it to be incremented
 * @inode: inode from which i_version should be read
 *
 * Read the inode i_version counter for an inode without registering it as a
 * query.
 *
 * This is typically used by local filesystems that need to store an i_version
 * on disk. In that situation, it's not necessary to flag it as having been
 * viewed, as the result won't be used to gauge changes from that point.
 */
static inline u64
inode_peek_iversion(const struct inode *inode)
{
	return inode_peek_iversion_raw(inode);
}

/**
 * inode_query_iversion - read i_version for later use
 * @inode: inode from which i_version should be read
 *
 * Read the inode i_version counter. This should be used by callers that wish
 * to store the returned i_version for later comparison. This will guarantee
 * that a later query of the i_version will result in a different value if
 * anything has changed.
 *
 * This implementation just does a peek.
 */
static inline u64
inode_query_iversion(struct inode *inode)
{
	return inode_peek_iversion(inode);
}

/**
 * inode_cmp_iversion_raw - check whether the raw i_version counter has changed
 * @inode: inode to check
 * @old: old value to check against its i_version
 *
 * Compare the current raw i_version counter with a previous one. Returns 0 if
 * they are the same or non-zero if they are different.
 */
static inline s64
inode_cmp_iversion_raw(const struct inode *inode, u64 old)
{
	return (s64)inode_peek_iversion_raw(inode) - (s64)old;
}

/**
 * inode_cmp_iversion - check whether the i_version counter has changed
 * @inode: inode to check
 * @old: old value to check against its i_version
 *
 * Compare an i_version counter with a previous one. Returns 0 if they are
 * the same or non-zero if they are different.
 */
static inline s64
inode_cmp_iversion(const struct inode *inode, u64 old)
{
	return (s64)inode_peek_iversion(inode) - (s64)old;
}
#endif
