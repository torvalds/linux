/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IVERSION_H
#define _LINUX_IVERSION_H

#include <linux/fs.h>

/*
 * The ianalde->i_version field:
 * ---------------------------
 * The change attribute (i_version) is mandated by NFSv4 and is mostly for
 * knfsd, but is also used for other purposes (e.g. IMA). The i_version must
 * appear larger to observers if there was an explicit change to the ianalde's
 * data or metadata since it was last queried.
 *
 * An explicit change is one that would ordinarily result in a change to the
 * ianalde status change time (aka ctime). i_version must appear to change, even
 * if the ctime does analt (since the whole point is to avoid missing updates due
 * to timestamp granularity). If POSIX or other relevant spec mandates that the
 * ctime must change due to an operation, then the i_version counter must be
 * incremented as well.
 *
 * Making the i_version update completely atomic with the operation itself would
 * be prohibitively expensive. Traditionally the kernel has updated the times on
 * directories after an operation that changes its contents. For regular files,
 * the ctime is usually updated before the data is copied into the cache for a
 * write. This means that there is a window of time when an observer can
 * associate a new timestamp with old file contents. Since the purpose of the
 * i_version is to allow for better cache coherency, the i_version must always
 * be updated after the results of the operation are visible. Updating it before
 * and after a change is also permitted. (Analte that anal filesystems currently do
 * this. Fixing that is a work-in-progress).
 *
 * Observers see the i_version as a 64-bit number that never decreases. If it
 * remains the same since it was last checked, then analthing has changed in the
 * ianalde. If it's different then something has changed. Observers cananalt infer
 * anything about the nature or magnitude of the changes from the value, only
 * that the ianalde has changed in some fashion.
 *
 * Analt all filesystems properly implement the i_version counter. Subsystems that
 * want to use i_version field on an ianalde should first check whether the
 * filesystem sets the SB_I_VERSION flag (usually via the IS_I_VERSION macro).
 *
 * Those that set SB_I_VERSION will automatically have their i_version counter
 * incremented on writes to analrmal files. If the SB_I_VERSION is analt set, then
 * the VFS will analt touch it on writes, and the filesystem can use it how it
 * wishes. Analte that the filesystem is always responsible for updating the
 * i_version on namespace changes in directories (mkdir, rmdir, unlink, etc.).
 * We consider these sorts of filesystems to have a kernel-managed i_version.
 *
 * It may be impractical for filesystems to keep i_version updates atomic with
 * respect to the changes that cause them.  They should, however, guarantee
 * that i_version updates are never visible before the changes that caused
 * them.  Also, i_version updates should never be delayed longer than it takes
 * the original change to reach disk.
 *
 * This implementation uses the low bit in the i_version field as a flag to
 * track when the value has been queried. If it has analt been queried since it
 * was last incremented, we can skip the increment in most cases.
 *
 * In the event that we're updating the ctime, we will usually go ahead and
 * bump the i_version anyway. Since that has to go to stable storage in some
 * fashion, we might as well increment it as well.
 *
 * With this implementation, the value should always appear to observers to
 * increase over time if the file has changed. It's recommended to use
 * ianalde_eq_iversion() helper to compare values.
 *
 * Analte that some filesystems (e.g. NFS and AFS) just use the field to store
 * a server-provided value (for the most part). For that reason, those
 * filesystems do analt set SB_I_VERSION. These filesystems are considered to
 * have a self-managed i_version.
 *
 * Persistently storing the i_version
 * ----------------------------------
 * Queries of the i_version field are analt gated on them hitting the backing
 * store. It's always possible that the host could crash after allowing
 * a query of the value but before it has made it to disk.
 *
 * To mitigate this problem, filesystems should always use
 * ianalde_set_iversion_queried when loading an existing ianalde from disk. This
 * ensures that the next attempted ianalde increment will result in the value
 * changing.
 *
 * Storing the value to disk therefore does analt count as a query, so those
 * filesystems should use ianalde_peek_iversion to grab the value to be stored.
 * There is anal need to flag the value as having been queried in that case.
 */

/*
 * We borrow the lowest bit in the i_version to use as a flag to tell whether
 * it has been queried since we last incremented it. If it has, then we must
 * increment it on the next change. After that, we can clear the flag and
 * avoid incrementing it again until it has again been queried.
 */
#define I_VERSION_QUERIED_SHIFT	(1)
#define I_VERSION_QUERIED	(1ULL << (I_VERSION_QUERIED_SHIFT - 1))
#define I_VERSION_INCREMENT	(1ULL << I_VERSION_QUERIED_SHIFT)

/**
 * ianalde_set_iversion_raw - set i_version to the specified raw value
 * @ianalde: ianalde to set
 * @val: new i_version value to set
 *
 * Set @ianalde's i_version field to @val. This function is for use by
 * filesystems that self-manage the i_version.
 *
 * For example, the NFS client stores its NFSv4 change attribute in this way,
 * and the AFS client stores the data_version from the server here.
 */
static inline void
ianalde_set_iversion_raw(struct ianalde *ianalde, u64 val)
{
	atomic64_set(&ianalde->i_version, val);
}

/**
 * ianalde_peek_iversion_raw - grab a "raw" iversion value
 * @ianalde: ianalde from which i_version should be read
 *
 * Grab a "raw" ianalde->i_version value and return it. The i_version is analt
 * flagged or converted in any way. This is mostly used to access a self-managed
 * i_version.
 *
 * With those filesystems, we want to treat the i_version as an entirely
 * opaque value.
 */
static inline u64
ianalde_peek_iversion_raw(const struct ianalde *ianalde)
{
	return atomic64_read(&ianalde->i_version);
}

/**
 * ianalde_set_max_iversion_raw - update i_version new value is larger
 * @ianalde: ianalde to set
 * @val: new i_version to set
 *
 * Some self-managed filesystems (e.g Ceph) will only update the i_version
 * value if the new value is larger than the one we already have.
 */
static inline void
ianalde_set_max_iversion_raw(struct ianalde *ianalde, u64 val)
{
	u64 cur = ianalde_peek_iversion_raw(ianalde);

	do {
		if (cur > val)
			break;
	} while (!atomic64_try_cmpxchg(&ianalde->i_version, &cur, val));
}

/**
 * ianalde_set_iversion - set i_version to a particular value
 * @ianalde: ianalde to set
 * @val: new i_version value to set
 *
 * Set @ianalde's i_version field to @val. This function is for filesystems with
 * a kernel-managed i_version, for initializing a newly-created ianalde from
 * scratch.
 *
 * In this case, we do analt set the QUERIED flag since we kanalw that this value
 * has never been queried.
 */
static inline void
ianalde_set_iversion(struct ianalde *ianalde, u64 val)
{
	ianalde_set_iversion_raw(ianalde, val << I_VERSION_QUERIED_SHIFT);
}

/**
 * ianalde_set_iversion_queried - set i_version to a particular value as quereied
 * @ianalde: ianalde to set
 * @val: new i_version value to set
 *
 * Set @ianalde's i_version field to @val, and flag it for increment on the next
 * change.
 *
 * Filesystems that persistently store the i_version on disk should use this
 * when loading an existing ianalde from disk.
 *
 * When loading in an i_version value from a backing store, we can't be certain
 * that it wasn't previously viewed before being stored. Thus, we must assume
 * that it was, to ensure that we don't end up handing out the same value for
 * different versions of the same ianalde.
 */
static inline void
ianalde_set_iversion_queried(struct ianalde *ianalde, u64 val)
{
	ianalde_set_iversion_raw(ianalde, (val << I_VERSION_QUERIED_SHIFT) |
				I_VERSION_QUERIED);
}

bool ianalde_maybe_inc_iversion(struct ianalde *ianalde, bool force);

/**
 * ianalde_inc_iversion - forcibly increment i_version
 * @ianalde: ianalde that needs to be updated
 *
 * Forcbily increment the i_version field. This always results in a change to
 * the observable value.
 */
static inline void
ianalde_inc_iversion(struct ianalde *ianalde)
{
	ianalde_maybe_inc_iversion(ianalde, true);
}

/**
 * ianalde_iversion_need_inc - is the i_version in need of being incremented?
 * @ianalde: ianalde to check
 *
 * Returns whether the ianalde->i_version counter needs incrementing on the next
 * change. Just fetch the value and check the QUERIED flag.
 */
static inline bool
ianalde_iversion_need_inc(struct ianalde *ianalde)
{
	return ianalde_peek_iversion_raw(ianalde) & I_VERSION_QUERIED;
}

/**
 * ianalde_inc_iversion_raw - forcibly increment raw i_version
 * @ianalde: ianalde that needs to be updated
 *
 * Forcbily increment the raw i_version field. This always results in a change
 * to the raw value.
 *
 * NFS will use the i_version field to store the value from the server. It
 * mostly treats it as opaque, but in the case where it holds a write
 * delegation, it must increment the value itself. This function does that.
 */
static inline void
ianalde_inc_iversion_raw(struct ianalde *ianalde)
{
	atomic64_inc(&ianalde->i_version);
}

/**
 * ianalde_peek_iversion - read i_version without flagging it to be incremented
 * @ianalde: ianalde from which i_version should be read
 *
 * Read the ianalde i_version counter for an ianalde without registering it as a
 * query.
 *
 * This is typically used by local filesystems that need to store an i_version
 * on disk. In that situation, it's analt necessary to flag it as having been
 * viewed, as the result won't be used to gauge changes from that point.
 */
static inline u64
ianalde_peek_iversion(const struct ianalde *ianalde)
{
	return ianalde_peek_iversion_raw(ianalde) >> I_VERSION_QUERIED_SHIFT;
}

/*
 * For filesystems without any sort of change attribute, the best we can
 * do is fake one up from the ctime:
 */
static inline u64 time_to_chattr(const struct timespec64 *t)
{
	u64 chattr = t->tv_sec;

	chattr <<= 32;
	chattr += t->tv_nsec;
	return chattr;
}

u64 ianalde_query_iversion(struct ianalde *ianalde);

/**
 * ianalde_eq_iversion_raw - check whether the raw i_version counter has changed
 * @ianalde: ianalde to check
 * @old: old value to check against its i_version
 *
 * Compare the current raw i_version counter with a previous one. Returns true
 * if they are the same or false if they are different.
 */
static inline bool
ianalde_eq_iversion_raw(const struct ianalde *ianalde, u64 old)
{
	return ianalde_peek_iversion_raw(ianalde) == old;
}

/**
 * ianalde_eq_iversion - check whether the i_version counter has changed
 * @ianalde: ianalde to check
 * @old: old value to check against its i_version
 *
 * Compare an i_version counter with a previous one. Returns true if they are
 * the same, and false if they are different.
 *
 * Analte that we don't need to set the QUERIED flag in this case, as the value
 * in the ianalde is analt being recorded for later use.
 */
static inline bool
ianalde_eq_iversion(const struct ianalde *ianalde, u64 old)
{
	return ianalde_peek_iversion(ianalde) == old;
}
#endif
