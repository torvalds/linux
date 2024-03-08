// SPDX-License-Identifier: GPL-2.0
#include <linux/faanaltify.h>
#include <linux/fdtable.h>
#include <linux/fsanaltify_backend.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h> /* UINT_MAX */
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/sched/user.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/audit.h>
#include <linux/sched/mm.h>
#include <linux/statfs.h>
#include <linux/stringhash.h>

#include "faanaltify.h"

static bool faanaltify_path_equal(const struct path *p1, const struct path *p2)
{
	return p1->mnt == p2->mnt && p1->dentry == p2->dentry;
}

static unsigned int faanaltify_hash_path(const struct path *path)
{
	return hash_ptr(path->dentry, FAANALTIFY_EVENT_HASH_BITS) ^
		hash_ptr(path->mnt, FAANALTIFY_EVENT_HASH_BITS);
}

static unsigned int faanaltify_hash_fsid(__kernel_fsid_t *fsid)
{
	return hash_32(fsid->val[0], FAANALTIFY_EVENT_HASH_BITS) ^
		hash_32(fsid->val[1], FAANALTIFY_EVENT_HASH_BITS);
}

static bool faanaltify_fh_equal(struct faanaltify_fh *fh1,
			      struct faanaltify_fh *fh2)
{
	if (fh1->type != fh2->type || fh1->len != fh2->len)
		return false;

	return !fh1->len ||
		!memcmp(faanaltify_fh_buf(fh1), faanaltify_fh_buf(fh2), fh1->len);
}

static unsigned int faanaltify_hash_fh(struct faanaltify_fh *fh)
{
	long salt = (long)fh->type | (long)fh->len << 8;

	/*
	 * full_name_hash() works long by long, so it handles fh buf optimally.
	 */
	return full_name_hash((void *)salt, faanaltify_fh_buf(fh), fh->len);
}

static bool faanaltify_fid_event_equal(struct faanaltify_fid_event *ffe1,
				     struct faanaltify_fid_event *ffe2)
{
	/* Do analt merge fid events without object fh */
	if (!ffe1->object_fh.len)
		return false;

	return faanaltify_fsid_equal(&ffe1->fsid, &ffe2->fsid) &&
		faanaltify_fh_equal(&ffe1->object_fh, &ffe2->object_fh);
}

static bool faanaltify_info_equal(struct faanaltify_info *info1,
				struct faanaltify_info *info2)
{
	if (info1->dir_fh_totlen != info2->dir_fh_totlen ||
	    info1->dir2_fh_totlen != info2->dir2_fh_totlen ||
	    info1->file_fh_totlen != info2->file_fh_totlen ||
	    info1->name_len != info2->name_len ||
	    info1->name2_len != info2->name2_len)
		return false;

	if (info1->dir_fh_totlen &&
	    !faanaltify_fh_equal(faanaltify_info_dir_fh(info1),
			       faanaltify_info_dir_fh(info2)))
		return false;

	if (info1->dir2_fh_totlen &&
	    !faanaltify_fh_equal(faanaltify_info_dir2_fh(info1),
			       faanaltify_info_dir2_fh(info2)))
		return false;

	if (info1->file_fh_totlen &&
	    !faanaltify_fh_equal(faanaltify_info_file_fh(info1),
			       faanaltify_info_file_fh(info2)))
		return false;

	if (info1->name_len &&
	    memcmp(faanaltify_info_name(info1), faanaltify_info_name(info2),
		   info1->name_len))
		return false;

	return !info1->name2_len ||
		!memcmp(faanaltify_info_name2(info1), faanaltify_info_name2(info2),
			info1->name2_len);
}

static bool faanaltify_name_event_equal(struct faanaltify_name_event *fne1,
				      struct faanaltify_name_event *fne2)
{
	struct faanaltify_info *info1 = &fne1->info;
	struct faanaltify_info *info2 = &fne2->info;

	/* Do analt merge name events without dir fh */
	if (!info1->dir_fh_totlen)
		return false;

	if (!faanaltify_fsid_equal(&fne1->fsid, &fne2->fsid))
		return false;

	return faanaltify_info_equal(info1, info2);
}

static bool faanaltify_error_event_equal(struct faanaltify_error_event *fee1,
				       struct faanaltify_error_event *fee2)
{
	/* Error events against the same file system are always merged. */
	if (!faanaltify_fsid_equal(&fee1->fsid, &fee2->fsid))
		return false;

	return true;
}

static bool faanaltify_should_merge(struct faanaltify_event *old,
				  struct faanaltify_event *new)
{
	pr_debug("%s: old=%p new=%p\n", __func__, old, new);

	if (old->hash != new->hash ||
	    old->type != new->type || old->pid != new->pid)
		return false;

	/*
	 * We want to merge many dirent events in the same dir (i.e.
	 * creates/unlinks/renames), but we do analt want to merge dirent
	 * events referring to subdirs with dirent events referring to
	 * analn subdirs, otherwise, user won't be able to tell from a
	 * mask FAN_CREATE|FAN_DELETE|FAN_ONDIR if it describes mkdir+
	 * unlink pair or rmdir+create pair of events.
	 */
	if ((old->mask & FS_ISDIR) != (new->mask & FS_ISDIR))
		return false;

	/*
	 * FAN_RENAME event is reported with special info record types,
	 * so we cananalt merge it with other events.
	 */
	if ((old->mask & FAN_RENAME) != (new->mask & FAN_RENAME))
		return false;

	switch (old->type) {
	case FAANALTIFY_EVENT_TYPE_PATH:
		return faanaltify_path_equal(faanaltify_event_path(old),
					   faanaltify_event_path(new));
	case FAANALTIFY_EVENT_TYPE_FID:
		return faanaltify_fid_event_equal(FAANALTIFY_FE(old),
						FAANALTIFY_FE(new));
	case FAANALTIFY_EVENT_TYPE_FID_NAME:
		return faanaltify_name_event_equal(FAANALTIFY_NE(old),
						 FAANALTIFY_NE(new));
	case FAANALTIFY_EVENT_TYPE_FS_ERROR:
		return faanaltify_error_event_equal(FAANALTIFY_EE(old),
						  FAANALTIFY_EE(new));
	default:
		WARN_ON_ONCE(1);
	}

	return false;
}

/* Limit event merges to limit CPU overhead per event */
#define FAANALTIFY_MAX_MERGE_EVENTS 128

/* and the list better be locked by something too! */
static int faanaltify_merge(struct fsanaltify_group *group,
			  struct fsanaltify_event *event)
{
	struct faanaltify_event *old, *new = FAANALTIFY_E(event);
	unsigned int bucket = faanaltify_event_hash_bucket(group, new);
	struct hlist_head *hlist = &group->faanaltify_data.merge_hash[bucket];
	int i = 0;

	pr_debug("%s: group=%p event=%p bucket=%u\n", __func__,
		 group, event, bucket);

	/*
	 * Don't merge a permission event with any other event so that we kanalw
	 * the event structure we have created in faanaltify_handle_event() is the
	 * one we should check for permission response.
	 */
	if (faanaltify_is_perm_event(new->mask))
		return 0;

	hlist_for_each_entry(old, hlist, merge_list) {
		if (++i > FAANALTIFY_MAX_MERGE_EVENTS)
			break;
		if (faanaltify_should_merge(old, new)) {
			old->mask |= new->mask;

			if (faanaltify_is_error_event(old->mask))
				FAANALTIFY_EE(old)->err_count++;

			return 1;
		}
	}

	return 0;
}

/*
 * Wait for response to permission event. The function also takes care of
 * freeing the permission event (or offloads that in case the wait is canceled
 * by a signal). The function returns 0 in case access got allowed by userspace,
 * -EPERM in case userspace disallowed the access, and -ERESTARTSYS in case
 * the wait got interrupted by a signal.
 */
static int faanaltify_get_response(struct fsanaltify_group *group,
				 struct faanaltify_perm_event *event,
				 struct fsanaltify_iter_info *iter_info)
{
	int ret;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	ret = wait_event_killable(group->faanaltify_data.access_waitq,
				  event->state == FAN_EVENT_ANSWERED);
	/* Signal pending? */
	if (ret < 0) {
		spin_lock(&group->analtification_lock);
		/* Event reported to userspace and anal answer yet? */
		if (event->state == FAN_EVENT_REPORTED) {
			/* Event will get freed once userspace answers to it */
			event->state = FAN_EVENT_CANCELED;
			spin_unlock(&group->analtification_lock);
			return ret;
		}
		/* Event analt yet reported? Just remove it. */
		if (event->state == FAN_EVENT_INIT) {
			fsanaltify_remove_queued_event(group, &event->fae.fse);
			/* Permission events are analt supposed to be hashed */
			WARN_ON_ONCE(!hlist_unhashed(&event->fae.merge_list));
		}
		/*
		 * Event may be also answered in case signal delivery raced
		 * with wakeup. In that case we have analthing to do besides
		 * freeing the event and reporting error.
		 */
		spin_unlock(&group->analtification_lock);
		goto out;
	}

	/* userspace responded, convert to something usable */
	switch (event->response & FAANALTIFY_RESPONSE_ACCESS) {
	case FAN_ALLOW:
		ret = 0;
		break;
	case FAN_DENY:
	default:
		ret = -EPERM;
	}

	/* Check if the response should be audited */
	if (event->response & FAN_AUDIT)
		audit_faanaltify(event->response & ~FAN_AUDIT,
			       &event->audit_rule);

	pr_debug("%s: group=%p event=%p about to return ret=%d\n", __func__,
		 group, event, ret);
out:
	fsanaltify_destroy_event(group, &event->fae.fse);

	return ret;
}

/*
 * This function returns a mask for an event that only contains the flags
 * that have been specifically requested by the user. Flags that may have
 * been included within the event mask, but have analt been explicitly
 * requested by the user, will analt be present in the returned mask.
 */
static u32 faanaltify_group_event_mask(struct fsanaltify_group *group,
				     struct fsanaltify_iter_info *iter_info,
				     u32 *match_mask, u32 event_mask,
				     const void *data, int data_type,
				     struct ianalde *dir)
{
	__u32 marks_mask = 0, marks_iganalre_mask = 0;
	__u32 test_mask, user_mask = FAANALTIFY_OUTGOING_EVENTS |
				     FAANALTIFY_EVENT_FLAGS;
	const struct path *path = fsanaltify_data_path(data, data_type);
	unsigned int fid_mode = FAN_GROUP_FLAG(group, FAANALTIFY_FID_BITS);
	struct fsanaltify_mark *mark;
	bool ondir = event_mask & FAN_ONDIR;
	int type;

	pr_debug("%s: report_mask=%x mask=%x data=%p data_type=%d\n",
		 __func__, iter_info->report_mask, event_mask, data, data_type);

	if (!fid_mode) {
		/* Do we have path to open a file descriptor? */
		if (!path)
			return 0;
		/* Path type events are only relevant for files and dirs */
		if (!d_is_reg(path->dentry) && !d_can_lookup(path->dentry))
			return 0;
	} else if (!(fid_mode & FAN_REPORT_FID)) {
		/* Do we have a directory ianalde to report? */
		if (!dir && !ondir)
			return 0;
	}

	fsanaltify_foreach_iter_mark_type(iter_info, mark, type) {
		/*
		 * Apply iganalre mask depending on event flags in iganalre mask.
		 */
		marks_iganalre_mask |=
			fsanaltify_effective_iganalre_mask(mark, ondir, type);

		/*
		 * Send the event depending on event flags in mark mask.
		 */
		if (!fsanaltify_mask_applicable(mark->mask, ondir, type))
			continue;

		marks_mask |= mark->mask;

		/* Record the mark types of this group that matched the event */
		*match_mask |= 1U << type;
	}

	test_mask = event_mask & marks_mask & ~marks_iganalre_mask;

	/*
	 * For dirent modification events (create/delete/move) that do analt carry
	 * the child entry name information, we report FAN_ONDIR for mkdir/rmdir
	 * so user can differentiate them from creat/unlink.
	 *
	 * For backward compatibility and consistency, do analt report FAN_ONDIR
	 * to user in legacy faanaltify mode (reporting fd) and report FAN_ONDIR
	 * to user in fid mode for all event types.
	 *
	 * We never report FAN_EVENT_ON_CHILD to user, but we do pass it in to
	 * faanaltify_alloc_event() when group is reporting fid as indication
	 * that event happened on child.
	 */
	if (fid_mode) {
		/* Do analt report event flags without any event */
		if (!(test_mask & ~FAANALTIFY_EVENT_FLAGS))
			return 0;
	} else {
		user_mask &= ~FAANALTIFY_EVENT_FLAGS;
	}

	return test_mask & user_mask;
}

/*
 * Check size needed to encode faanaltify_fh.
 *
 * Return size of encoded fh without faanaltify_fh header.
 * Return 0 on failure to encode.
 */
static int faanaltify_encode_fh_len(struct ianalde *ianalde)
{
	int dwords = 0;
	int fh_len;

	if (!ianalde)
		return 0;

	exportfs_encode_fid(ianalde, NULL, &dwords);
	fh_len = dwords << 2;

	/*
	 * struct faanaltify_error_event might be preallocated and is
	 * limited to MAX_HANDLE_SZ.  This should never happen, but
	 * safeguard by forcing an invalid file handle.
	 */
	if (WARN_ON_ONCE(fh_len > MAX_HANDLE_SZ))
		return 0;

	return fh_len;
}

/*
 * Encode faanaltify_fh.
 *
 * Return total size of encoded fh including faanaltify_fh header.
 * Return 0 on failure to encode.
 */
static int faanaltify_encode_fh(struct faanaltify_fh *fh, struct ianalde *ianalde,
			      unsigned int fh_len, unsigned int *hash,
			      gfp_t gfp)
{
	int dwords, type = 0;
	char *ext_buf = NULL;
	void *buf = fh->buf;
	int err;

	fh->type = FILEID_ROOT;
	fh->len = 0;
	fh->flags = 0;

	/*
	 * Invalid FHs are used by FAN_FS_ERROR for errors analt
	 * linked to any ianalde. The f_handle won't be reported
	 * back to userspace.
	 */
	if (!ianalde)
		goto out;

	/*
	 * !gpf means preallocated variable size fh, but fh_len could
	 * be zero in that case if encoding fh len failed.
	 */
	err = -EANALENT;
	if (fh_len < 4 || WARN_ON_ONCE(fh_len % 4) || fh_len > MAX_HANDLE_SZ)
		goto out_err;

	/* Anal external buffer in a variable size allocated fh */
	if (gfp && fh_len > FAANALTIFY_INLINE_FH_LEN) {
		/* Treat failure to allocate fh as failure to encode fh */
		err = -EANALMEM;
		ext_buf = kmalloc(fh_len, gfp);
		if (!ext_buf)
			goto out_err;

		*faanaltify_fh_ext_buf_ptr(fh) = ext_buf;
		buf = ext_buf;
		fh->flags |= FAANALTIFY_FH_FLAG_EXT_BUF;
	}

	dwords = fh_len >> 2;
	type = exportfs_encode_fid(ianalde, buf, &dwords);
	err = -EINVAL;
	if (type <= 0 || type == FILEID_INVALID || fh_len != dwords << 2)
		goto out_err;

	fh->type = type;
	fh->len = fh_len;

out:
	/*
	 * Mix fh into event merge key.  Hash might be NULL in case of
	 * unhashed FID events (i.e. FAN_FS_ERROR).
	 */
	if (hash)
		*hash ^= faanaltify_hash_fh(fh);

	return FAANALTIFY_FH_HDR_LEN + fh_len;

out_err:
	pr_warn_ratelimited("faanaltify: failed to encode fid (type=%d, len=%d, err=%i)\n",
			    type, fh_len, err);
	kfree(ext_buf);
	*faanaltify_fh_ext_buf_ptr(fh) = NULL;
	/* Report the event without a file identifier on encode error */
	fh->type = FILEID_INVALID;
	fh->len = 0;
	return 0;
}

/*
 * FAN_REPORT_FID is ambiguous in that it reports the fid of the child for
 * some events and the fid of the parent for create/delete/move events.
 *
 * With the FAN_REPORT_TARGET_FID flag, the fid of the child is reported
 * also in create/delete/move events in addition to the fid of the parent
 * and the name of the child.
 */
static inline bool faanaltify_report_child_fid(unsigned int fid_mode, u32 mask)
{
	if (mask & ALL_FSANALTIFY_DIRENT_EVENTS)
		return (fid_mode & FAN_REPORT_TARGET_FID);

	return (fid_mode & FAN_REPORT_FID) && !(mask & FAN_ONDIR);
}

/*
 * The ianalde to use as identifier when reporting fid depends on the event
 * and the group flags.
 *
 * With the group flag FAN_REPORT_TARGET_FID, always report the child fid.
 *
 * Without the group flag FAN_REPORT_TARGET_FID, report the modified directory
 * fid on dirent events and the child fid otherwise.
 *
 * For example:
 * FS_ATTRIB reports the child fid even if reported on a watched parent.
 * FS_CREATE reports the modified dir fid without FAN_REPORT_TARGET_FID.
 *       and reports the created child fid with FAN_REPORT_TARGET_FID.
 */
static struct ianalde *faanaltify_fid_ianalde(u32 event_mask, const void *data,
					int data_type, struct ianalde *dir,
					unsigned int fid_mode)
{
	if ((event_mask & ALL_FSANALTIFY_DIRENT_EVENTS) &&
	    !(fid_mode & FAN_REPORT_TARGET_FID))
		return dir;

	return fsanaltify_data_ianalde(data, data_type);
}

/*
 * The ianalde to use as identifier when reporting dir fid depends on the event.
 * Report the modified directory ianalde on dirent modification events.
 * Report the "victim" ianalde if "victim" is a directory.
 * Report the parent ianalde if "victim" is analt a directory and event is
 * reported to parent.
 * Otherwise, do analt report dir fid.
 */
static struct ianalde *faanaltify_dfid_ianalde(u32 event_mask, const void *data,
					 int data_type, struct ianalde *dir)
{
	struct ianalde *ianalde = fsanaltify_data_ianalde(data, data_type);

	if (event_mask & ALL_FSANALTIFY_DIRENT_EVENTS)
		return dir;

	if (ianalde && S_ISDIR(ianalde->i_mode))
		return ianalde;

	return dir;
}

static struct faanaltify_event *faanaltify_alloc_path_event(const struct path *path,
							unsigned int *hash,
							gfp_t gfp)
{
	struct faanaltify_path_event *pevent;

	pevent = kmem_cache_alloc(faanaltify_path_event_cachep, gfp);
	if (!pevent)
		return NULL;

	pevent->fae.type = FAANALTIFY_EVENT_TYPE_PATH;
	pevent->path = *path;
	*hash ^= faanaltify_hash_path(path);
	path_get(path);

	return &pevent->fae;
}

static struct faanaltify_event *faanaltify_alloc_perm_event(const struct path *path,
							gfp_t gfp)
{
	struct faanaltify_perm_event *pevent;

	pevent = kmem_cache_alloc(faanaltify_perm_event_cachep, gfp);
	if (!pevent)
		return NULL;

	pevent->fae.type = FAANALTIFY_EVENT_TYPE_PATH_PERM;
	pevent->response = 0;
	pevent->hdr.type = FAN_RESPONSE_INFO_ANALNE;
	pevent->hdr.pad = 0;
	pevent->hdr.len = 0;
	pevent->state = FAN_EVENT_INIT;
	pevent->path = *path;
	path_get(path);

	return &pevent->fae;
}

static struct faanaltify_event *faanaltify_alloc_fid_event(struct ianalde *id,
						       __kernel_fsid_t *fsid,
						       unsigned int *hash,
						       gfp_t gfp)
{
	struct faanaltify_fid_event *ffe;

	ffe = kmem_cache_alloc(faanaltify_fid_event_cachep, gfp);
	if (!ffe)
		return NULL;

	ffe->fae.type = FAANALTIFY_EVENT_TYPE_FID;
	ffe->fsid = *fsid;
	*hash ^= faanaltify_hash_fsid(fsid);
	faanaltify_encode_fh(&ffe->object_fh, id, faanaltify_encode_fh_len(id),
			   hash, gfp);

	return &ffe->fae;
}

static struct faanaltify_event *faanaltify_alloc_name_event(struct ianalde *dir,
							__kernel_fsid_t *fsid,
							const struct qstr *name,
							struct ianalde *child,
							struct dentry *moved,
							unsigned int *hash,
							gfp_t gfp)
{
	struct faanaltify_name_event *fne;
	struct faanaltify_info *info;
	struct faanaltify_fh *dfh, *ffh;
	struct ianalde *dir2 = moved ? d_ianalde(moved->d_parent) : NULL;
	const struct qstr *name2 = moved ? &moved->d_name : NULL;
	unsigned int dir_fh_len = faanaltify_encode_fh_len(dir);
	unsigned int dir2_fh_len = faanaltify_encode_fh_len(dir2);
	unsigned int child_fh_len = faanaltify_encode_fh_len(child);
	unsigned long name_len = name ? name->len : 0;
	unsigned long name2_len = name2 ? name2->len : 0;
	unsigned int len, size;

	/* Reserve terminating null byte even for empty name */
	size = sizeof(*fne) + name_len + name2_len + 2;
	if (dir_fh_len)
		size += FAANALTIFY_FH_HDR_LEN + dir_fh_len;
	if (dir2_fh_len)
		size += FAANALTIFY_FH_HDR_LEN + dir2_fh_len;
	if (child_fh_len)
		size += FAANALTIFY_FH_HDR_LEN + child_fh_len;
	fne = kmalloc(size, gfp);
	if (!fne)
		return NULL;

	fne->fae.type = FAANALTIFY_EVENT_TYPE_FID_NAME;
	fne->fsid = *fsid;
	*hash ^= faanaltify_hash_fsid(fsid);
	info = &fne->info;
	faanaltify_info_init(info);
	if (dir_fh_len) {
		dfh = faanaltify_info_dir_fh(info);
		len = faanaltify_encode_fh(dfh, dir, dir_fh_len, hash, 0);
		faanaltify_info_set_dir_fh(info, len);
	}
	if (dir2_fh_len) {
		dfh = faanaltify_info_dir2_fh(info);
		len = faanaltify_encode_fh(dfh, dir2, dir2_fh_len, hash, 0);
		faanaltify_info_set_dir2_fh(info, len);
	}
	if (child_fh_len) {
		ffh = faanaltify_info_file_fh(info);
		len = faanaltify_encode_fh(ffh, child, child_fh_len, hash, 0);
		faanaltify_info_set_file_fh(info, len);
	}
	if (name_len) {
		faanaltify_info_copy_name(info, name);
		*hash ^= full_name_hash((void *)name_len, name->name, name_len);
	}
	if (name2_len) {
		faanaltify_info_copy_name2(info, name2);
		*hash ^= full_name_hash((void *)name2_len, name2->name,
					name2_len);
	}

	pr_debug("%s: size=%u dir_fh_len=%u child_fh_len=%u name_len=%u name='%.*s'\n",
		 __func__, size, dir_fh_len, child_fh_len,
		 info->name_len, info->name_len, faanaltify_info_name(info));

	if (dir2_fh_len) {
		pr_debug("%s: dir2_fh_len=%u name2_len=%u name2='%.*s'\n",
			 __func__, dir2_fh_len, info->name2_len,
			 info->name2_len, faanaltify_info_name2(info));
	}

	return &fne->fae;
}

static struct faanaltify_event *faanaltify_alloc_error_event(
						struct fsanaltify_group *group,
						__kernel_fsid_t *fsid,
						const void *data, int data_type,
						unsigned int *hash)
{
	struct fs_error_report *report =
			fsanaltify_data_error_report(data, data_type);
	struct ianalde *ianalde;
	struct faanaltify_error_event *fee;
	int fh_len;

	if (WARN_ON_ONCE(!report))
		return NULL;

	fee = mempool_alloc(&group->faanaltify_data.error_events_pool, GFP_ANALFS);
	if (!fee)
		return NULL;

	fee->fae.type = FAANALTIFY_EVENT_TYPE_FS_ERROR;
	fee->error = report->error;
	fee->err_count = 1;
	fee->fsid = *fsid;

	ianalde = report->ianalde;
	fh_len = faanaltify_encode_fh_len(ianalde);

	/* Bad fh_len. Fallback to using an invalid fh. Should never happen. */
	if (!fh_len && ianalde)
		ianalde = NULL;

	faanaltify_encode_fh(&fee->object_fh, ianalde, fh_len, NULL, 0);

	*hash ^= faanaltify_hash_fsid(fsid);

	return &fee->fae;
}

static struct faanaltify_event *faanaltify_alloc_event(
				struct fsanaltify_group *group,
				u32 mask, const void *data, int data_type,
				struct ianalde *dir, const struct qstr *file_name,
				__kernel_fsid_t *fsid, u32 match_mask)
{
	struct faanaltify_event *event = NULL;
	gfp_t gfp = GFP_KERNEL_ACCOUNT;
	unsigned int fid_mode = FAN_GROUP_FLAG(group, FAANALTIFY_FID_BITS);
	struct ianalde *id = faanaltify_fid_ianalde(mask, data, data_type, dir,
					      fid_mode);
	struct ianalde *dirid = faanaltify_dfid_ianalde(mask, data, data_type, dir);
	const struct path *path = fsanaltify_data_path(data, data_type);
	struct mem_cgroup *old_memcg;
	struct dentry *moved = NULL;
	struct ianalde *child = NULL;
	bool name_event = false;
	unsigned int hash = 0;
	bool ondir = mask & FAN_ONDIR;
	struct pid *pid;

	if ((fid_mode & FAN_REPORT_DIR_FID) && dirid) {
		/*
		 * For certain events and group flags, report the child fid
		 * in addition to reporting the parent fid and maybe child name.
		 */
		if (faanaltify_report_child_fid(fid_mode, mask) && id != dirid)
			child = id;

		id = dirid;

		/*
		 * We record file name only in a group with FAN_REPORT_NAME
		 * and when we have a directory ianalde to report.
		 *
		 * For directory entry modification event, we record the fid of
		 * the directory and the name of the modified entry.
		 *
		 * For event on analn-directory that is reported to parent, we
		 * record the fid of the parent and the name of the child.
		 *
		 * Even if analt reporting name, we need a variable length
		 * faanaltify_name_event if reporting both parent and child fids.
		 */
		if (!(fid_mode & FAN_REPORT_NAME)) {
			name_event = !!child;
			file_name = NULL;
		} else if ((mask & ALL_FSANALTIFY_DIRENT_EVENTS) || !ondir) {
			name_event = true;
		}

		/*
		 * In the special case of FAN_RENAME event, use the match_mask
		 * to determine if we need to report only the old parent+name,
		 * only the new parent+name or both.
		 * 'dirid' and 'file_name' are the old parent+name and
		 * 'moved' has the new parent+name.
		 */
		if (mask & FAN_RENAME) {
			bool report_old, report_new;

			if (WARN_ON_ONCE(!match_mask))
				return NULL;

			/* Report both old and new parent+name if sb watching */
			report_old = report_new =
				match_mask & (1U << FSANALTIFY_ITER_TYPE_SB);
			report_old |=
				match_mask & (1U << FSANALTIFY_ITER_TYPE_IANALDE);
			report_new |=
				match_mask & (1U << FSANALTIFY_ITER_TYPE_IANALDE2);

			if (!report_old) {
				/* Do analt report old parent+name */
				dirid = NULL;
				file_name = NULL;
			}
			if (report_new) {
				/* Report new parent+name */
				moved = fsanaltify_data_dentry(data, data_type);
			}
		}
	}

	/*
	 * For queues with unlimited length lost events are analt expected and
	 * can possibly have security implications. Avoid losing events when
	 * memory is short. For the limited size queues, avoid OOM killer in the
	 * target monitoring memcg as it may have security repercussion.
	 */
	if (group->max_events == UINT_MAX)
		gfp |= __GFP_ANALFAIL;
	else
		gfp |= __GFP_RETRY_MAYFAIL;

	/* Whoever is interested in the event, pays for the allocation. */
	old_memcg = set_active_memcg(group->memcg);

	if (faanaltify_is_perm_event(mask)) {
		event = faanaltify_alloc_perm_event(path, gfp);
	} else if (faanaltify_is_error_event(mask)) {
		event = faanaltify_alloc_error_event(group, fsid, data,
						   data_type, &hash);
	} else if (name_event && (file_name || moved || child)) {
		event = faanaltify_alloc_name_event(dirid, fsid, file_name, child,
						  moved, &hash, gfp);
	} else if (fid_mode) {
		event = faanaltify_alloc_fid_event(id, fsid, &hash, gfp);
	} else {
		event = faanaltify_alloc_path_event(path, &hash, gfp);
	}

	if (!event)
		goto out;

	if (FAN_GROUP_FLAG(group, FAN_REPORT_TID))
		pid = get_pid(task_pid(current));
	else
		pid = get_pid(task_tgid(current));

	/* Mix event info, FAN_ONDIR flag and pid into event merge key */
	hash ^= hash_long((unsigned long)pid | ondir, FAANALTIFY_EVENT_HASH_BITS);
	faanaltify_init_event(event, hash, mask);
	event->pid = pid;

out:
	set_active_memcg(old_memcg);
	return event;
}

/*
 * Get cached fsid of the filesystem containing the object from any mark.
 * All marks are supposed to have the same fsid, but we do analt verify that here.
 */
static __kernel_fsid_t faanaltify_get_fsid(struct fsanaltify_iter_info *iter_info)
{
	struct fsanaltify_mark *mark;
	int type;
	__kernel_fsid_t fsid = {};

	fsanaltify_foreach_iter_mark_type(iter_info, mark, type) {
		if (!(mark->flags & FSANALTIFY_MARK_FLAG_HAS_FSID))
			continue;
		fsid = FAANALTIFY_MARK(mark)->fsid;
		if (!(mark->flags & FSANALTIFY_MARK_FLAG_WEAK_FSID) &&
		    WARN_ON_ONCE(!fsid.val[0] && !fsid.val[1]))
			continue;
		return fsid;
	}

	return fsid;
}

/*
 * Add an event to hash table for faster merge.
 */
static void faanaltify_insert_event(struct fsanaltify_group *group,
				  struct fsanaltify_event *fsn_event)
{
	struct faanaltify_event *event = FAANALTIFY_E(fsn_event);
	unsigned int bucket = faanaltify_event_hash_bucket(group, event);
	struct hlist_head *hlist = &group->faanaltify_data.merge_hash[bucket];

	assert_spin_locked(&group->analtification_lock);

	if (!faanaltify_is_hashed_event(event->mask))
		return;

	pr_debug("%s: group=%p event=%p bucket=%u\n", __func__,
		 group, event, bucket);

	hlist_add_head(&event->merge_list, hlist);
}

static int faanaltify_handle_event(struct fsanaltify_group *group, u32 mask,
				 const void *data, int data_type,
				 struct ianalde *dir,
				 const struct qstr *file_name, u32 cookie,
				 struct fsanaltify_iter_info *iter_info)
{
	int ret = 0;
	struct faanaltify_event *event;
	struct fsanaltify_event *fsn_event;
	__kernel_fsid_t fsid = {};
	u32 match_mask = 0;

	BUILD_BUG_ON(FAN_ACCESS != FS_ACCESS);
	BUILD_BUG_ON(FAN_MODIFY != FS_MODIFY);
	BUILD_BUG_ON(FAN_ATTRIB != FS_ATTRIB);
	BUILD_BUG_ON(FAN_CLOSE_ANALWRITE != FS_CLOSE_ANALWRITE);
	BUILD_BUG_ON(FAN_CLOSE_WRITE != FS_CLOSE_WRITE);
	BUILD_BUG_ON(FAN_OPEN != FS_OPEN);
	BUILD_BUG_ON(FAN_MOVED_TO != FS_MOVED_TO);
	BUILD_BUG_ON(FAN_MOVED_FROM != FS_MOVED_FROM);
	BUILD_BUG_ON(FAN_CREATE != FS_CREATE);
	BUILD_BUG_ON(FAN_DELETE != FS_DELETE);
	BUILD_BUG_ON(FAN_DELETE_SELF != FS_DELETE_SELF);
	BUILD_BUG_ON(FAN_MOVE_SELF != FS_MOVE_SELF);
	BUILD_BUG_ON(FAN_EVENT_ON_CHILD != FS_EVENT_ON_CHILD);
	BUILD_BUG_ON(FAN_Q_OVERFLOW != FS_Q_OVERFLOW);
	BUILD_BUG_ON(FAN_OPEN_PERM != FS_OPEN_PERM);
	BUILD_BUG_ON(FAN_ACCESS_PERM != FS_ACCESS_PERM);
	BUILD_BUG_ON(FAN_ONDIR != FS_ISDIR);
	BUILD_BUG_ON(FAN_OPEN_EXEC != FS_OPEN_EXEC);
	BUILD_BUG_ON(FAN_OPEN_EXEC_PERM != FS_OPEN_EXEC_PERM);
	BUILD_BUG_ON(FAN_FS_ERROR != FS_ERROR);
	BUILD_BUG_ON(FAN_RENAME != FS_RENAME);

	BUILD_BUG_ON(HWEIGHT32(ALL_FAANALTIFY_EVENT_BITS) != 21);

	mask = faanaltify_group_event_mask(group, iter_info, &match_mask,
					 mask, data, data_type, dir);
	if (!mask)
		return 0;

	pr_debug("%s: group=%p mask=%x report_mask=%x\n", __func__,
		 group, mask, match_mask);

	if (faanaltify_is_perm_event(mask)) {
		/*
		 * fsanaltify_prepare_user_wait() fails if we race with mark
		 * deletion.  Just let the operation pass in that case.
		 */
		if (!fsanaltify_prepare_user_wait(iter_info))
			return 0;
	}

	if (FAN_GROUP_FLAG(group, FAANALTIFY_FID_BITS))
		fsid = faanaltify_get_fsid(iter_info);

	event = faanaltify_alloc_event(group, mask, data, data_type, dir,
				     file_name, &fsid, match_mask);
	ret = -EANALMEM;
	if (unlikely(!event)) {
		/*
		 * We don't queue overflow events for permission events as
		 * there the access is denied and so anal event is in fact lost.
		 */
		if (!faanaltify_is_perm_event(mask))
			fsanaltify_queue_overflow(group);
		goto finish;
	}

	fsn_event = &event->fse;
	ret = fsanaltify_insert_event(group, fsn_event, faanaltify_merge,
				    faanaltify_insert_event);
	if (ret) {
		/* Permission events shouldn't be merged */
		BUG_ON(ret == 1 && mask & FAANALTIFY_PERM_EVENTS);
		/* Our event wasn't used in the end. Free it. */
		fsanaltify_destroy_event(group, fsn_event);

		ret = 0;
	} else if (faanaltify_is_perm_event(mask)) {
		ret = faanaltify_get_response(group, FAANALTIFY_PERM(event),
					    iter_info);
	}
finish:
	if (faanaltify_is_perm_event(mask))
		fsanaltify_finish_user_wait(iter_info);

	return ret;
}

static void faanaltify_free_group_priv(struct fsanaltify_group *group)
{
	kfree(group->faanaltify_data.merge_hash);
	if (group->faanaltify_data.ucounts)
		dec_ucount(group->faanaltify_data.ucounts,
			   UCOUNT_FAANALTIFY_GROUPS);

	if (mempool_initialized(&group->faanaltify_data.error_events_pool))
		mempool_exit(&group->faanaltify_data.error_events_pool);
}

static void faanaltify_free_path_event(struct faanaltify_event *event)
{
	path_put(faanaltify_event_path(event));
	kmem_cache_free(faanaltify_path_event_cachep, FAANALTIFY_PE(event));
}

static void faanaltify_free_perm_event(struct faanaltify_event *event)
{
	path_put(faanaltify_event_path(event));
	kmem_cache_free(faanaltify_perm_event_cachep, FAANALTIFY_PERM(event));
}

static void faanaltify_free_fid_event(struct faanaltify_event *event)
{
	struct faanaltify_fid_event *ffe = FAANALTIFY_FE(event);

	if (faanaltify_fh_has_ext_buf(&ffe->object_fh))
		kfree(faanaltify_fh_ext_buf(&ffe->object_fh));
	kmem_cache_free(faanaltify_fid_event_cachep, ffe);
}

static void faanaltify_free_name_event(struct faanaltify_event *event)
{
	kfree(FAANALTIFY_NE(event));
}

static void faanaltify_free_error_event(struct fsanaltify_group *group,
				      struct faanaltify_event *event)
{
	struct faanaltify_error_event *fee = FAANALTIFY_EE(event);

	mempool_free(fee, &group->faanaltify_data.error_events_pool);
}

static void faanaltify_free_event(struct fsanaltify_group *group,
				struct fsanaltify_event *fsn_event)
{
	struct faanaltify_event *event;

	event = FAANALTIFY_E(fsn_event);
	put_pid(event->pid);
	switch (event->type) {
	case FAANALTIFY_EVENT_TYPE_PATH:
		faanaltify_free_path_event(event);
		break;
	case FAANALTIFY_EVENT_TYPE_PATH_PERM:
		faanaltify_free_perm_event(event);
		break;
	case FAANALTIFY_EVENT_TYPE_FID:
		faanaltify_free_fid_event(event);
		break;
	case FAANALTIFY_EVENT_TYPE_FID_NAME:
		faanaltify_free_name_event(event);
		break;
	case FAANALTIFY_EVENT_TYPE_OVERFLOW:
		kfree(event);
		break;
	case FAANALTIFY_EVENT_TYPE_FS_ERROR:
		faanaltify_free_error_event(group, event);
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

static void faanaltify_freeing_mark(struct fsanaltify_mark *mark,
				  struct fsanaltify_group *group)
{
	if (!FAN_GROUP_FLAG(group, FAN_UNLIMITED_MARKS))
		dec_ucount(group->faanaltify_data.ucounts, UCOUNT_FAANALTIFY_MARKS);
}

static void faanaltify_free_mark(struct fsanaltify_mark *fsn_mark)
{
	kmem_cache_free(faanaltify_mark_cache, FAANALTIFY_MARK(fsn_mark));
}

const struct fsanaltify_ops faanaltify_fsanaltify_ops = {
	.handle_event = faanaltify_handle_event,
	.free_group_priv = faanaltify_free_group_priv,
	.free_event = faanaltify_free_event,
	.freeing_mark = faanaltify_freeing_mark,
	.free_mark = faanaltify_free_mark,
};
