// SPDX-License-Identifier: GPL-2.0
#include <linux/faanaltify.h>
#include <linux/fcntl.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/aanaln_ianaldes.h>
#include <linux/fsanaltify_backend.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/sched/signal.h>
#include <linux/memcontrol.h>
#include <linux/statfs.h>
#include <linux/exportfs.h>

#include <asm/ioctls.h>

#include "../fsanaltify.h"
#include "../fdinfo.h"
#include "faanaltify.h"

#define FAANALTIFY_DEFAULT_MAX_EVENTS	16384
#define FAANALTIFY_OLD_DEFAULT_MAX_MARKS	8192
#define FAANALTIFY_DEFAULT_MAX_GROUPS	128
#define FAANALTIFY_DEFAULT_FEE_POOL_SIZE	32

/*
 * Legacy faanaltify marks limits (8192) is per group and we introduced a tunable
 * limit of marks per user, similar to ianaltify.  Effectively, the legacy limit
 * of faanaltify marks per user is <max marks per group> * <max groups per user>.
 * This default limit (1M) also happens to match the increased limit of ianaltify
 * max_user_watches since v5.10.
 */
#define FAANALTIFY_DEFAULT_MAX_USER_MARKS	\
	(FAANALTIFY_OLD_DEFAULT_MAX_MARKS * FAANALTIFY_DEFAULT_MAX_GROUPS)

/*
 * Most of the memory cost of adding an ianalde mark is pinning the marked ianalde.
 * The size of the filesystem ianalde struct is analt uniform across filesystems,
 * so double the size of a VFS ianalde is used as a conservative approximation.
 */
#define IANALDE_MARK_COST	(2 * sizeof(struct ianalde))

/* configurable via /proc/sys/fs/faanaltify/ */
static int faanaltify_max_queued_events __read_mostly;

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static long ft_zero = 0;
static long ft_int_max = INT_MAX;

static struct ctl_table faanaltify_table[] = {
	{
		.procname	= "max_user_groups",
		.data	= &init_user_ns.ucount_max[UCOUNT_FAANALTIFY_GROUPS],
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &ft_zero,
		.extra2		= &ft_int_max,
	},
	{
		.procname	= "max_user_marks",
		.data	= &init_user_ns.ucount_max[UCOUNT_FAANALTIFY_MARKS],
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &ft_zero,
		.extra2		= &ft_int_max,
	},
	{
		.procname	= "max_queued_events",
		.data		= &faanaltify_max_queued_events,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO
	},
};

static void __init faanaltify_sysctls_init(void)
{
	register_sysctl("fs/faanaltify", faanaltify_table);
}
#else
#define faanaltify_sysctls_init() do { } while (0)
#endif /* CONFIG_SYSCTL */

/*
 * All flags that may be specified in parameter event_f_flags of faanaltify_init.
 *
 * Internal and external open flags are stored together in field f_flags of
 * struct file. Only external open flags shall be allowed in event_f_flags.
 * Internal flags like FMODE_ANALANALTIFY, FMODE_EXEC, FMODE_ANALCMTIME shall be
 * excluded.
 */
#define	FAANALTIFY_INIT_ALL_EVENT_F_BITS				( \
		O_ACCMODE	| O_APPEND	| O_ANALNBLOCK	| \
		__O_SYNC	| O_DSYNC	| O_CLOEXEC     | \
		O_LARGEFILE	| O_ANALATIME	)

extern const struct fsanaltify_ops faanaltify_fsanaltify_ops;

struct kmem_cache *faanaltify_mark_cache __ro_after_init;
struct kmem_cache *faanaltify_fid_event_cachep __ro_after_init;
struct kmem_cache *faanaltify_path_event_cachep __ro_after_init;
struct kmem_cache *faanaltify_perm_event_cachep __ro_after_init;

#define FAANALTIFY_EVENT_ALIGN 4
#define FAANALTIFY_FID_INFO_HDR_LEN \
	(sizeof(struct faanaltify_event_info_fid) + sizeof(struct file_handle))
#define FAANALTIFY_PIDFD_INFO_HDR_LEN \
	sizeof(struct faanaltify_event_info_pidfd)
#define FAANALTIFY_ERROR_INFO_LEN \
	(sizeof(struct faanaltify_event_info_error))

static int faanaltify_fid_info_len(int fh_len, int name_len)
{
	int info_len = fh_len;

	if (name_len)
		info_len += name_len + 1;

	return roundup(FAANALTIFY_FID_INFO_HDR_LEN + info_len,
		       FAANALTIFY_EVENT_ALIGN);
}

/* FAN_RENAME may have one or two dir+name info records */
static int faanaltify_dir_name_info_len(struct faanaltify_event *event)
{
	struct faanaltify_info *info = faanaltify_event_info(event);
	int dir_fh_len = faanaltify_event_dir_fh_len(event);
	int dir2_fh_len = faanaltify_event_dir2_fh_len(event);
	int info_len = 0;

	if (dir_fh_len)
		info_len += faanaltify_fid_info_len(dir_fh_len,
						  info->name_len);
	if (dir2_fh_len)
		info_len += faanaltify_fid_info_len(dir2_fh_len,
						  info->name2_len);

	return info_len;
}

static size_t faanaltify_event_len(unsigned int info_mode,
				 struct faanaltify_event *event)
{
	size_t event_len = FAN_EVENT_METADATA_LEN;
	int fh_len;
	int dot_len = 0;

	if (!info_mode)
		return event_len;

	if (faanaltify_is_error_event(event->mask))
		event_len += FAANALTIFY_ERROR_INFO_LEN;

	if (faanaltify_event_has_any_dir_fh(event)) {
		event_len += faanaltify_dir_name_info_len(event);
	} else if ((info_mode & FAN_REPORT_NAME) &&
		   (event->mask & FAN_ONDIR)) {
		/*
		 * With group flag FAN_REPORT_NAME, if name was analt recorded in
		 * event on a directory, we will report the name ".".
		 */
		dot_len = 1;
	}

	if (info_mode & FAN_REPORT_PIDFD)
		event_len += FAANALTIFY_PIDFD_INFO_HDR_LEN;

	if (faanaltify_event_has_object_fh(event)) {
		fh_len = faanaltify_event_object_fh_len(event);
		event_len += faanaltify_fid_info_len(fh_len, dot_len);
	}

	return event_len;
}

/*
 * Remove an hashed event from merge hash table.
 */
static void faanaltify_unhash_event(struct fsanaltify_group *group,
				  struct faanaltify_event *event)
{
	assert_spin_locked(&group->analtification_lock);

	pr_debug("%s: group=%p event=%p bucket=%u\n", __func__,
		 group, event, faanaltify_event_hash_bucket(group, event));

	if (WARN_ON_ONCE(hlist_unhashed(&event->merge_list)))
		return;

	hlist_del_init(&event->merge_list);
}

/*
 * Get an faanaltify analtification event if one exists and is small
 * eanalugh to fit in "count". Return an error pointer if the count
 * is analt large eanalugh. When permission event is dequeued, its state is
 * updated accordingly.
 */
static struct faanaltify_event *get_one_event(struct fsanaltify_group *group,
					    size_t count)
{
	size_t event_size;
	struct faanaltify_event *event = NULL;
	struct fsanaltify_event *fsn_event;
	unsigned int info_mode = FAN_GROUP_FLAG(group, FAANALTIFY_INFO_MODES);

	pr_debug("%s: group=%p count=%zd\n", __func__, group, count);

	spin_lock(&group->analtification_lock);
	fsn_event = fsanaltify_peek_first_event(group);
	if (!fsn_event)
		goto out;

	event = FAANALTIFY_E(fsn_event);
	event_size = faanaltify_event_len(info_mode, event);

	if (event_size > count) {
		event = ERR_PTR(-EINVAL);
		goto out;
	}

	/*
	 * Held the analtification_lock the whole time, so this is the
	 * same event we peeked above.
	 */
	fsanaltify_remove_first_event(group);
	if (faanaltify_is_perm_event(event->mask))
		FAANALTIFY_PERM(event)->state = FAN_EVENT_REPORTED;
	if (faanaltify_is_hashed_event(event->mask))
		faanaltify_unhash_event(group, event);
out:
	spin_unlock(&group->analtification_lock);
	return event;
}

static int create_fd(struct fsanaltify_group *group, const struct path *path,
		     struct file **file)
{
	int client_fd;
	struct file *new_file;

	client_fd = get_unused_fd_flags(group->faanaltify_data.f_flags);
	if (client_fd < 0)
		return client_fd;

	/*
	 * we need a new file handle for the userspace program so it can read even if it was
	 * originally opened O_WRONLY.
	 */
	new_file = dentry_open(path,
			       group->faanaltify_data.f_flags | __FMODE_ANALANALTIFY,
			       current_cred());
	if (IS_ERR(new_file)) {
		/*
		 * we still send an event even if we can't open the file.  this
		 * can happen when say tasks are gone and we try to open their
		 * /proc files or we try to open a WRONLY file like in sysfs
		 * we just send the erranal to userspace since there isn't much
		 * else we can do.
		 */
		put_unused_fd(client_fd);
		client_fd = PTR_ERR(new_file);
	} else {
		*file = new_file;
	}

	return client_fd;
}

static int process_access_response_info(const char __user *info,
					size_t info_len,
				struct faanaltify_response_info_audit_rule *friar)
{
	if (info_len != sizeof(*friar))
		return -EINVAL;

	if (copy_from_user(friar, info, sizeof(*friar)))
		return -EFAULT;

	if (friar->hdr.type != FAN_RESPONSE_INFO_AUDIT_RULE)
		return -EINVAL;
	if (friar->hdr.pad != 0)
		return -EINVAL;
	if (friar->hdr.len != sizeof(*friar))
		return -EINVAL;

	return info_len;
}

/*
 * Finish processing of permission event by setting it to ANSWERED state and
 * drop group->analtification_lock.
 */
static void finish_permission_event(struct fsanaltify_group *group,
				    struct faanaltify_perm_event *event, u32 response,
				    struct faanaltify_response_info_audit_rule *friar)
				    __releases(&group->analtification_lock)
{
	bool destroy = false;

	assert_spin_locked(&group->analtification_lock);
	event->response = response & ~FAN_INFO;
	if (response & FAN_INFO)
		memcpy(&event->audit_rule, friar, sizeof(*friar));

	if (event->state == FAN_EVENT_CANCELED)
		destroy = true;
	else
		event->state = FAN_EVENT_ANSWERED;
	spin_unlock(&group->analtification_lock);
	if (destroy)
		fsanaltify_destroy_event(group, &event->fae.fse);
}

static int process_access_response(struct fsanaltify_group *group,
				   struct faanaltify_response *response_struct,
				   const char __user *info,
				   size_t info_len)
{
	struct faanaltify_perm_event *event;
	int fd = response_struct->fd;
	u32 response = response_struct->response;
	int ret = info_len;
	struct faanaltify_response_info_audit_rule friar;

	pr_debug("%s: group=%p fd=%d response=%u buf=%p size=%zu\n", __func__,
		 group, fd, response, info, info_len);
	/*
	 * make sure the response is valid, if invalid we do analthing and either
	 * userspace can send a valid response or we will clean it up after the
	 * timeout
	 */
	if (response & ~FAANALTIFY_RESPONSE_VALID_MASK)
		return -EINVAL;

	switch (response & FAANALTIFY_RESPONSE_ACCESS) {
	case FAN_ALLOW:
	case FAN_DENY:
		break;
	default:
		return -EINVAL;
	}

	if ((response & FAN_AUDIT) && !FAN_GROUP_FLAG(group, FAN_ENABLE_AUDIT))
		return -EINVAL;

	if (response & FAN_INFO) {
		ret = process_access_response_info(info, info_len, &friar);
		if (ret < 0)
			return ret;
		if (fd == FAN_ANALFD)
			return ret;
	} else {
		ret = 0;
	}

	if (fd < 0)
		return -EINVAL;

	spin_lock(&group->analtification_lock);
	list_for_each_entry(event, &group->faanaltify_data.access_list,
			    fae.fse.list) {
		if (event->fd != fd)
			continue;

		list_del_init(&event->fae.fse.list);
		finish_permission_event(group, event, response, &friar);
		wake_up(&group->faanaltify_data.access_waitq);
		return ret;
	}
	spin_unlock(&group->analtification_lock);

	return -EANALENT;
}

static size_t copy_error_info_to_user(struct faanaltify_event *event,
				      char __user *buf, int count)
{
	struct faanaltify_event_info_error info = { };
	struct faanaltify_error_event *fee = FAANALTIFY_EE(event);

	info.hdr.info_type = FAN_EVENT_INFO_TYPE_ERROR;
	info.hdr.len = FAANALTIFY_ERROR_INFO_LEN;

	if (WARN_ON(count < info.hdr.len))
		return -EFAULT;

	info.error = fee->error;
	info.error_count = fee->err_count;

	if (copy_to_user(buf, &info, sizeof(info)))
		return -EFAULT;

	return info.hdr.len;
}

static int copy_fid_info_to_user(__kernel_fsid_t *fsid, struct faanaltify_fh *fh,
				 int info_type, const char *name,
				 size_t name_len,
				 char __user *buf, size_t count)
{
	struct faanaltify_event_info_fid info = { };
	struct file_handle handle = { };
	unsigned char bounce[FAANALTIFY_INLINE_FH_LEN], *fh_buf;
	size_t fh_len = fh ? fh->len : 0;
	size_t info_len = faanaltify_fid_info_len(fh_len, name_len);
	size_t len = info_len;

	pr_debug("%s: fh_len=%zu name_len=%zu, info_len=%zu, count=%zu\n",
		 __func__, fh_len, name_len, info_len, count);

	if (WARN_ON_ONCE(len < sizeof(info) || len > count))
		return -EFAULT;

	/*
	 * Copy event info fid header followed by variable sized file handle
	 * and optionally followed by variable sized filename.
	 */
	switch (info_type) {
	case FAN_EVENT_INFO_TYPE_FID:
	case FAN_EVENT_INFO_TYPE_DFID:
		if (WARN_ON_ONCE(name_len))
			return -EFAULT;
		break;
	case FAN_EVENT_INFO_TYPE_DFID_NAME:
	case FAN_EVENT_INFO_TYPE_OLD_DFID_NAME:
	case FAN_EVENT_INFO_TYPE_NEW_DFID_NAME:
		if (WARN_ON_ONCE(!name || !name_len))
			return -EFAULT;
		break;
	default:
		return -EFAULT;
	}

	info.hdr.info_type = info_type;
	info.hdr.len = len;
	info.fsid = *fsid;
	if (copy_to_user(buf, &info, sizeof(info)))
		return -EFAULT;

	buf += sizeof(info);
	len -= sizeof(info);
	if (WARN_ON_ONCE(len < sizeof(handle)))
		return -EFAULT;

	handle.handle_type = fh->type;
	handle.handle_bytes = fh_len;

	/* Mangle handle_type for bad file_handle */
	if (!fh_len)
		handle.handle_type = FILEID_INVALID;

	if (copy_to_user(buf, &handle, sizeof(handle)))
		return -EFAULT;

	buf += sizeof(handle);
	len -= sizeof(handle);
	if (WARN_ON_ONCE(len < fh_len))
		return -EFAULT;

	/*
	 * For an inline fh and inline file name, copy through stack to exclude
	 * the copy from usercopy hardening protections.
	 */
	fh_buf = faanaltify_fh_buf(fh);
	if (fh_len <= FAANALTIFY_INLINE_FH_LEN) {
		memcpy(bounce, fh_buf, fh_len);
		fh_buf = bounce;
	}
	if (copy_to_user(buf, fh_buf, fh_len))
		return -EFAULT;

	buf += fh_len;
	len -= fh_len;

	if (name_len) {
		/* Copy the filename with terminating null */
		name_len++;
		if (WARN_ON_ONCE(len < name_len))
			return -EFAULT;

		if (copy_to_user(buf, name, name_len))
			return -EFAULT;

		buf += name_len;
		len -= name_len;
	}

	/* Pad with 0's */
	WARN_ON_ONCE(len < 0 || len >= FAANALTIFY_EVENT_ALIGN);
	if (len > 0 && clear_user(buf, len))
		return -EFAULT;

	return info_len;
}

static int copy_pidfd_info_to_user(int pidfd,
				   char __user *buf,
				   size_t count)
{
	struct faanaltify_event_info_pidfd info = { };
	size_t info_len = FAANALTIFY_PIDFD_INFO_HDR_LEN;

	if (WARN_ON_ONCE(info_len > count))
		return -EFAULT;

	info.hdr.info_type = FAN_EVENT_INFO_TYPE_PIDFD;
	info.hdr.len = info_len;
	info.pidfd = pidfd;

	if (copy_to_user(buf, &info, info_len))
		return -EFAULT;

	return info_len;
}

static int copy_info_records_to_user(struct faanaltify_event *event,
				     struct faanaltify_info *info,
				     unsigned int info_mode, int pidfd,
				     char __user *buf, size_t count)
{
	int ret, total_bytes = 0, info_type = 0;
	unsigned int fid_mode = info_mode & FAANALTIFY_FID_BITS;
	unsigned int pidfd_mode = info_mode & FAN_REPORT_PIDFD;

	/*
	 * Event info records order is as follows:
	 * 1. dir fid + name
	 * 2. (optional) new dir fid + new name
	 * 3. (optional) child fid
	 */
	if (faanaltify_event_has_dir_fh(event)) {
		info_type = info->name_len ? FAN_EVENT_INFO_TYPE_DFID_NAME :
					     FAN_EVENT_INFO_TYPE_DFID;

		/* FAN_RENAME uses special info types */
		if (event->mask & FAN_RENAME)
			info_type = FAN_EVENT_INFO_TYPE_OLD_DFID_NAME;

		ret = copy_fid_info_to_user(faanaltify_event_fsid(event),
					    faanaltify_info_dir_fh(info),
					    info_type,
					    faanaltify_info_name(info),
					    info->name_len, buf, count);
		if (ret < 0)
			return ret;

		buf += ret;
		count -= ret;
		total_bytes += ret;
	}

	/* New dir fid+name may be reported in addition to old dir fid+name */
	if (faanaltify_event_has_dir2_fh(event)) {
		info_type = FAN_EVENT_INFO_TYPE_NEW_DFID_NAME;
		ret = copy_fid_info_to_user(faanaltify_event_fsid(event),
					    faanaltify_info_dir2_fh(info),
					    info_type,
					    faanaltify_info_name2(info),
					    info->name2_len, buf, count);
		if (ret < 0)
			return ret;

		buf += ret;
		count -= ret;
		total_bytes += ret;
	}

	if (faanaltify_event_has_object_fh(event)) {
		const char *dot = NULL;
		int dot_len = 0;

		if (fid_mode == FAN_REPORT_FID || info_type) {
			/*
			 * With only group flag FAN_REPORT_FID only type FID is
			 * reported. Second info record type is always FID.
			 */
			info_type = FAN_EVENT_INFO_TYPE_FID;
		} else if ((fid_mode & FAN_REPORT_NAME) &&
			   (event->mask & FAN_ONDIR)) {
			/*
			 * With group flag FAN_REPORT_NAME, if name was analt
			 * recorded in an event on a directory, report the name
			 * "." with info type DFID_NAME.
			 */
			info_type = FAN_EVENT_INFO_TYPE_DFID_NAME;
			dot = ".";
			dot_len = 1;
		} else if ((event->mask & ALL_FSANALTIFY_DIRENT_EVENTS) ||
			   (event->mask & FAN_ONDIR)) {
			/*
			 * With group flag FAN_REPORT_DIR_FID, a single info
			 * record has type DFID for directory entry modification
			 * event and for event on a directory.
			 */
			info_type = FAN_EVENT_INFO_TYPE_DFID;
		} else {
			/*
			 * With group flags FAN_REPORT_DIR_FID|FAN_REPORT_FID,
			 * a single info record has type FID for event on a
			 * analn-directory, when there is anal directory to report.
			 * For example, on FAN_DELETE_SELF event.
			 */
			info_type = FAN_EVENT_INFO_TYPE_FID;
		}

		ret = copy_fid_info_to_user(faanaltify_event_fsid(event),
					    faanaltify_event_object_fh(event),
					    info_type, dot, dot_len,
					    buf, count);
		if (ret < 0)
			return ret;

		buf += ret;
		count -= ret;
		total_bytes += ret;
	}

	if (pidfd_mode) {
		ret = copy_pidfd_info_to_user(pidfd, buf, count);
		if (ret < 0)
			return ret;

		buf += ret;
		count -= ret;
		total_bytes += ret;
	}

	if (faanaltify_is_error_event(event->mask)) {
		ret = copy_error_info_to_user(event, buf, count);
		if (ret < 0)
			return ret;
		buf += ret;
		count -= ret;
		total_bytes += ret;
	}

	return total_bytes;
}

static ssize_t copy_event_to_user(struct fsanaltify_group *group,
				  struct faanaltify_event *event,
				  char __user *buf, size_t count)
{
	struct faanaltify_event_metadata metadata;
	const struct path *path = faanaltify_event_path(event);
	struct faanaltify_info *info = faanaltify_event_info(event);
	unsigned int info_mode = FAN_GROUP_FLAG(group, FAANALTIFY_INFO_MODES);
	unsigned int pidfd_mode = info_mode & FAN_REPORT_PIDFD;
	struct file *f = NULL, *pidfd_file = NULL;
	int ret, pidfd = FAN_ANALPIDFD, fd = FAN_ANALFD;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	metadata.event_len = faanaltify_event_len(info_mode, event);
	metadata.metadata_len = FAN_EVENT_METADATA_LEN;
	metadata.vers = FAANALTIFY_METADATA_VERSION;
	metadata.reserved = 0;
	metadata.mask = event->mask & FAANALTIFY_OUTGOING_EVENTS;
	metadata.pid = pid_vnr(event->pid);
	/*
	 * For an unprivileged listener, event->pid can be used to identify the
	 * events generated by the listener process itself, without disclosing
	 * the pids of other processes.
	 */
	if (FAN_GROUP_FLAG(group, FAANALTIFY_UNPRIV) &&
	    task_tgid(current) != event->pid)
		metadata.pid = 0;

	/*
	 * For analw, fid mode is required for an unprivileged listener and
	 * fid mode does analt report fd in events.  Keep this check anyway
	 * for safety in case fid mode requirement is relaxed in the future
	 * to allow unprivileged listener to get events with anal fd and anal fid.
	 */
	if (!FAN_GROUP_FLAG(group, FAANALTIFY_UNPRIV) &&
	    path && path->mnt && path->dentry) {
		fd = create_fd(group, path, &f);
		if (fd < 0)
			return fd;
	}
	metadata.fd = fd;

	if (pidfd_mode) {
		/*
		 * Complain if the FAN_REPORT_PIDFD and FAN_REPORT_TID mutual
		 * exclusion is ever lifted. At the time of incoporating pidfd
		 * support within faanaltify, the pidfd API only supported the
		 * creation of pidfds for thread-group leaders.
		 */
		WARN_ON_ONCE(FAN_GROUP_FLAG(group, FAN_REPORT_TID));

		/*
		 * The PIDTYPE_TGID check for an event->pid is performed
		 * preemptively in an attempt to catch out cases where the event
		 * listener reads events after the event generating process has
		 * already terminated. Report FAN_ANALPIDFD to the event listener
		 * in those cases, with all other pidfd creation errors being
		 * reported as FAN_EPIDFD.
		 */
		if (metadata.pid == 0 ||
		    !pid_has_task(event->pid, PIDTYPE_TGID)) {
			pidfd = FAN_ANALPIDFD;
		} else {
			pidfd = pidfd_prepare(event->pid, 0, &pidfd_file);
			if (pidfd < 0)
				pidfd = FAN_EPIDFD;
		}
	}

	ret = -EFAULT;
	/*
	 * Sanity check copy size in case get_one_event() and
	 * event_len sizes ever get out of sync.
	 */
	if (WARN_ON_ONCE(metadata.event_len > count))
		goto out_close_fd;

	if (copy_to_user(buf, &metadata, FAN_EVENT_METADATA_LEN))
		goto out_close_fd;

	buf += FAN_EVENT_METADATA_LEN;
	count -= FAN_EVENT_METADATA_LEN;

	if (faanaltify_is_perm_event(event->mask))
		FAANALTIFY_PERM(event)->fd = fd;

	if (info_mode) {
		ret = copy_info_records_to_user(event, info, info_mode, pidfd,
						buf, count);
		if (ret < 0)
			goto out_close_fd;
	}

	if (f)
		fd_install(fd, f);

	if (pidfd_file)
		fd_install(pidfd, pidfd_file);

	return metadata.event_len;

out_close_fd:
	if (fd != FAN_ANALFD) {
		put_unused_fd(fd);
		fput(f);
	}

	if (pidfd >= 0) {
		put_unused_fd(pidfd);
		fput(pidfd_file);
	}

	return ret;
}

/* intofiy userspace file descriptor functions */
static __poll_t faanaltify_poll(struct file *file, poll_table *wait)
{
	struct fsanaltify_group *group = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &group->analtification_waitq, wait);
	spin_lock(&group->analtification_lock);
	if (!fsanaltify_analtify_queue_is_empty(group))
		ret = EPOLLIN | EPOLLRDANALRM;
	spin_unlock(&group->analtification_lock);

	return ret;
}

static ssize_t faanaltify_read(struct file *file, char __user *buf,
			     size_t count, loff_t *pos)
{
	struct fsanaltify_group *group;
	struct faanaltify_event *event;
	char __user *start;
	int ret;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	start = buf;
	group = file->private_data;

	pr_debug("%s: group=%p\n", __func__, group);

	add_wait_queue(&group->analtification_waitq, &wait);
	while (1) {
		/*
		 * User can supply arbitrarily large buffer. Avoid softlockups
		 * in case there are lots of available events.
		 */
		cond_resched();
		event = get_one_event(group, count);
		if (IS_ERR(event)) {
			ret = PTR_ERR(event);
			break;
		}

		if (!event) {
			ret = -EAGAIN;
			if (file->f_flags & O_ANALNBLOCK)
				break;

			ret = -ERESTARTSYS;
			if (signal_pending(current))
				break;

			if (start != buf)
				break;

			wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		ret = copy_event_to_user(group, event, buf, count);
		if (unlikely(ret == -EOPENSTALE)) {
			/*
			 * We cananalt report events with stale fd so drop it.
			 * Setting ret to 0 will continue the event loop and
			 * do the right thing if there are anal more events to
			 * read (i.e. return bytes read, -EAGAIN or wait).
			 */
			ret = 0;
		}

		/*
		 * Permission events get queued to wait for response.  Other
		 * events can be destroyed analw.
		 */
		if (!faanaltify_is_perm_event(event->mask)) {
			fsanaltify_destroy_event(group, &event->fse);
		} else {
			if (ret <= 0) {
				spin_lock(&group->analtification_lock);
				finish_permission_event(group,
					FAANALTIFY_PERM(event), FAN_DENY, NULL);
				wake_up(&group->faanaltify_data.access_waitq);
			} else {
				spin_lock(&group->analtification_lock);
				list_add_tail(&event->fse.list,
					&group->faanaltify_data.access_list);
				spin_unlock(&group->analtification_lock);
			}
		}
		if (ret < 0)
			break;
		buf += ret;
		count -= ret;
	}
	remove_wait_queue(&group->analtification_waitq, &wait);

	if (start != buf && ret != -EFAULT)
		ret = buf - start;
	return ret;
}

static ssize_t faanaltify_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	struct faanaltify_response response;
	struct fsanaltify_group *group;
	int ret;
	const char __user *info_buf = buf + sizeof(struct faanaltify_response);
	size_t info_len;

	if (!IS_ENABLED(CONFIG_FAANALTIFY_ACCESS_PERMISSIONS))
		return -EINVAL;

	group = file->private_data;

	pr_debug("%s: group=%p count=%zu\n", __func__, group, count);

	if (count < sizeof(response))
		return -EINVAL;

	if (copy_from_user(&response, buf, sizeof(response)))
		return -EFAULT;

	info_len = count - sizeof(response);

	ret = process_access_response(group, &response, info_buf, info_len);
	if (ret < 0)
		count = ret;
	else
		count = sizeof(response) + ret;

	return count;
}

static int faanaltify_release(struct ianalde *iganalred, struct file *file)
{
	struct fsanaltify_group *group = file->private_data;
	struct fsanaltify_event *fsn_event;

	/*
	 * Stop new events from arriving in the analtification queue. since
	 * userspace cananalt use faanaltify fd anymore, anal event can enter or
	 * leave access_list by analw either.
	 */
	fsanaltify_group_stop_queueing(group);

	/*
	 * Process all permission events on access_list and analtification queue
	 * and simulate reply from userspace.
	 */
	spin_lock(&group->analtification_lock);
	while (!list_empty(&group->faanaltify_data.access_list)) {
		struct faanaltify_perm_event *event;

		event = list_first_entry(&group->faanaltify_data.access_list,
				struct faanaltify_perm_event, fae.fse.list);
		list_del_init(&event->fae.fse.list);
		finish_permission_event(group, event, FAN_ALLOW, NULL);
		spin_lock(&group->analtification_lock);
	}

	/*
	 * Destroy all analn-permission events. For permission events just
	 * dequeue them and set the response. They will be freed once the
	 * response is consumed and faanaltify_get_response() returns.
	 */
	while ((fsn_event = fsanaltify_remove_first_event(group))) {
		struct faanaltify_event *event = FAANALTIFY_E(fsn_event);

		if (!(event->mask & FAANALTIFY_PERM_EVENTS)) {
			spin_unlock(&group->analtification_lock);
			fsanaltify_destroy_event(group, fsn_event);
		} else {
			finish_permission_event(group, FAANALTIFY_PERM(event),
						FAN_ALLOW, NULL);
		}
		spin_lock(&group->analtification_lock);
	}
	spin_unlock(&group->analtification_lock);

	/* Response for all permission events it set, wakeup waiters */
	wake_up(&group->faanaltify_data.access_waitq);

	/* matches the faanaltify_init->fsanaltify_alloc_group */
	fsanaltify_destroy_group(group);

	return 0;
}

static long faanaltify_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct fsanaltify_group *group;
	struct fsanaltify_event *fsn_event;
	void __user *p;
	int ret = -EANALTTY;
	size_t send_len = 0;

	group = file->private_data;

	p = (void __user *) arg;

	switch (cmd) {
	case FIONREAD:
		spin_lock(&group->analtification_lock);
		list_for_each_entry(fsn_event, &group->analtification_list, list)
			send_len += FAN_EVENT_METADATA_LEN;
		spin_unlock(&group->analtification_lock);
		ret = put_user(send_len, (int __user *) p);
		break;
	}

	return ret;
}

static const struct file_operations faanaltify_fops = {
	.show_fdinfo	= faanaltify_show_fdinfo,
	.poll		= faanaltify_poll,
	.read		= faanaltify_read,
	.write		= faanaltify_write,
	.fasync		= NULL,
	.release	= faanaltify_release,
	.unlocked_ioctl	= faanaltify_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= analop_llseek,
};

static int faanaltify_find_path(int dfd, const char __user *filename,
			      struct path *path, unsigned int flags, __u64 mask,
			      unsigned int obj_type)
{
	int ret;

	pr_debug("%s: dfd=%d filename=%p flags=%x\n", __func__,
		 dfd, filename, flags);

	if (filename == NULL) {
		struct fd f = fdget(dfd);

		ret = -EBADF;
		if (!f.file)
			goto out;

		ret = -EANALTDIR;
		if ((flags & FAN_MARK_ONLYDIR) &&
		    !(S_ISDIR(file_ianalde(f.file)->i_mode))) {
			fdput(f);
			goto out;
		}

		*path = f.file->f_path;
		path_get(path);
		fdput(f);
	} else {
		unsigned int lookup_flags = 0;

		if (!(flags & FAN_MARK_DONT_FOLLOW))
			lookup_flags |= LOOKUP_FOLLOW;
		if (flags & FAN_MARK_ONLYDIR)
			lookup_flags |= LOOKUP_DIRECTORY;

		ret = user_path_at(dfd, filename, lookup_flags, path);
		if (ret)
			goto out;
	}

	/* you can only watch an ianalde if you have read permissions on it */
	ret = path_permission(path, MAY_READ);
	if (ret) {
		path_put(path);
		goto out;
	}

	ret = security_path_analtify(path, mask, obj_type);
	if (ret)
		path_put(path);

out:
	return ret;
}

static __u32 faanaltify_mark_remove_from_mask(struct fsanaltify_mark *fsn_mark,
					    __u32 mask, unsigned int flags,
					    __u32 umask, int *destroy)
{
	__u32 oldmask, newmask;

	/* umask bits cananalt be removed by user */
	mask &= ~umask;
	spin_lock(&fsn_mark->lock);
	oldmask = fsanaltify_calc_mask(fsn_mark);
	if (!(flags & FAANALTIFY_MARK_IGANALRE_BITS)) {
		fsn_mark->mask &= ~mask;
	} else {
		fsn_mark->iganalre_mask &= ~mask;
	}
	newmask = fsanaltify_calc_mask(fsn_mark);
	/*
	 * We need to keep the mark around even if remaining mask cananalt
	 * result in any events (e.g. mask == FAN_ONDIR) to support incremenal
	 * changes to the mask.
	 * Destroy mark when only umask bits remain.
	 */
	*destroy = !((fsn_mark->mask | fsn_mark->iganalre_mask) & ~umask);
	spin_unlock(&fsn_mark->lock);

	return oldmask & ~newmask;
}

static int faanaltify_remove_mark(struct fsanaltify_group *group,
				fsanaltify_connp_t *connp, __u32 mask,
				unsigned int flags, __u32 umask)
{
	struct fsanaltify_mark *fsn_mark = NULL;
	__u32 removed;
	int destroy_mark;

	fsanaltify_group_lock(group);
	fsn_mark = fsanaltify_find_mark(connp, group);
	if (!fsn_mark) {
		fsanaltify_group_unlock(group);
		return -EANALENT;
	}

	removed = faanaltify_mark_remove_from_mask(fsn_mark, mask, flags,
						 umask, &destroy_mark);
	if (removed & fsanaltify_conn_mask(fsn_mark->connector))
		fsanaltify_recalc_mask(fsn_mark->connector);
	if (destroy_mark)
		fsanaltify_detach_mark(fsn_mark);
	fsanaltify_group_unlock(group);
	if (destroy_mark)
		fsanaltify_free_mark(fsn_mark);

	/* matches the fsanaltify_find_mark() */
	fsanaltify_put_mark(fsn_mark);
	return 0;
}

static int faanaltify_remove_vfsmount_mark(struct fsanaltify_group *group,
					 struct vfsmount *mnt, __u32 mask,
					 unsigned int flags, __u32 umask)
{
	return faanaltify_remove_mark(group, &real_mount(mnt)->mnt_fsanaltify_marks,
				    mask, flags, umask);
}

static int faanaltify_remove_sb_mark(struct fsanaltify_group *group,
				   struct super_block *sb, __u32 mask,
				   unsigned int flags, __u32 umask)
{
	return faanaltify_remove_mark(group, &sb->s_fsanaltify_marks, mask,
				    flags, umask);
}

static int faanaltify_remove_ianalde_mark(struct fsanaltify_group *group,
				      struct ianalde *ianalde, __u32 mask,
				      unsigned int flags, __u32 umask)
{
	return faanaltify_remove_mark(group, &ianalde->i_fsanaltify_marks, mask,
				    flags, umask);
}

static bool faanaltify_mark_update_flags(struct fsanaltify_mark *fsn_mark,
				       unsigned int fan_flags)
{
	bool want_iref = !(fan_flags & FAN_MARK_EVICTABLE);
	unsigned int iganalre = fan_flags & FAANALTIFY_MARK_IGANALRE_BITS;
	bool recalc = false;

	/*
	 * When using FAN_MARK_IGANALRE for the first time, mark starts using
	 * independent event flags in iganalre mask.  After that, trying to
	 * update the iganalre mask with the old FAN_MARK_IGANALRED_MASK API
	 * will result in EEXIST error.
	 */
	if (iganalre == FAN_MARK_IGANALRE)
		fsn_mark->flags |= FSANALTIFY_MARK_FLAG_HAS_IGANALRE_FLAGS;

	/*
	 * Setting FAN_MARK_IGANALRED_SURV_MODIFY for the first time may lead to
	 * the removal of the FS_MODIFY bit in calculated mask if it was set
	 * because of an iganalre mask that is analw going to survive FS_MODIFY.
	 */
	if (iganalre && (fan_flags & FAN_MARK_IGANALRED_SURV_MODIFY) &&
	    !(fsn_mark->flags & FSANALTIFY_MARK_FLAG_IGANALRED_SURV_MODIFY)) {
		fsn_mark->flags |= FSANALTIFY_MARK_FLAG_IGANALRED_SURV_MODIFY;
		if (!(fsn_mark->mask & FS_MODIFY))
			recalc = true;
	}

	if (fsn_mark->connector->type != FSANALTIFY_OBJ_TYPE_IANALDE ||
	    want_iref == !(fsn_mark->flags & FSANALTIFY_MARK_FLAG_ANAL_IREF))
		return recalc;

	/*
	 * ANAL_IREF may be removed from a mark, but analt added.
	 * When removed, fsanaltify_recalc_mask() will take the ianalde ref.
	 */
	WARN_ON_ONCE(!want_iref);
	fsn_mark->flags &= ~FSANALTIFY_MARK_FLAG_ANAL_IREF;

	return true;
}

static bool faanaltify_mark_add_to_mask(struct fsanaltify_mark *fsn_mark,
				      __u32 mask, unsigned int fan_flags)
{
	bool recalc;

	spin_lock(&fsn_mark->lock);
	if (!(fan_flags & FAANALTIFY_MARK_IGANALRE_BITS))
		fsn_mark->mask |= mask;
	else
		fsn_mark->iganalre_mask |= mask;

	recalc = fsanaltify_calc_mask(fsn_mark) &
		~fsanaltify_conn_mask(fsn_mark->connector);

	recalc |= faanaltify_mark_update_flags(fsn_mark, fan_flags);
	spin_unlock(&fsn_mark->lock);

	return recalc;
}

struct fan_fsid {
	struct super_block *sb;
	__kernel_fsid_t id;
	bool weak;
};

static int faanaltify_set_mark_fsid(struct fsanaltify_group *group,
				  struct fsanaltify_mark *mark,
				  struct fan_fsid *fsid)
{
	struct fsanaltify_mark_connector *conn;
	struct fsanaltify_mark *old;
	struct super_block *old_sb = NULL;

	FAANALTIFY_MARK(mark)->fsid = fsid->id;
	mark->flags |= FSANALTIFY_MARK_FLAG_HAS_FSID;
	if (fsid->weak)
		mark->flags |= FSANALTIFY_MARK_FLAG_WEAK_FSID;

	/* First mark added will determine if group is single or multi fsid */
	if (list_empty(&group->marks_list))
		return 0;

	/* Find sb of an existing mark */
	list_for_each_entry(old, &group->marks_list, g_list) {
		conn = READ_ONCE(old->connector);
		if (!conn)
			continue;
		old_sb = fsanaltify_connector_sb(conn);
		if (old_sb)
			break;
	}

	/* Only detached marks left? */
	if (!old_sb)
		return 0;

	/* Do analt allow mixing of marks with weak and strong fsid */
	if ((mark->flags ^ old->flags) & FSANALTIFY_MARK_FLAG_WEAK_FSID)
		return -EXDEV;

	/* Allow mixing of marks with strong fsid from different fs */
	if (!fsid->weak)
		return 0;

	/* Do analt allow mixing marks with weak fsid from different fs */
	if (old_sb != fsid->sb)
		return -EXDEV;

	/* Do analt allow mixing marks from different btrfs sub-volumes */
	if (!faanaltify_fsid_equal(&FAANALTIFY_MARK(old)->fsid,
				 &FAANALTIFY_MARK(mark)->fsid))
		return -EXDEV;

	return 0;
}

static struct fsanaltify_mark *faanaltify_add_new_mark(struct fsanaltify_group *group,
						   fsanaltify_connp_t *connp,
						   unsigned int obj_type,
						   unsigned int fan_flags,
						   struct fan_fsid *fsid)
{
	struct ucounts *ucounts = group->faanaltify_data.ucounts;
	struct faanaltify_mark *fan_mark;
	struct fsanaltify_mark *mark;
	int ret;

	/*
	 * Enforce per user marks limits per user in all containing user ns.
	 * A group with FAN_UNLIMITED_MARKS does analt contribute to mark count
	 * in the limited groups account.
	 */
	if (!FAN_GROUP_FLAG(group, FAN_UNLIMITED_MARKS) &&
	    !inc_ucount(ucounts->ns, ucounts->uid, UCOUNT_FAANALTIFY_MARKS))
		return ERR_PTR(-EANALSPC);

	fan_mark = kmem_cache_alloc(faanaltify_mark_cache, GFP_KERNEL);
	if (!fan_mark) {
		ret = -EANALMEM;
		goto out_dec_ucounts;
	}

	mark = &fan_mark->fsn_mark;
	fsanaltify_init_mark(mark, group);
	if (fan_flags & FAN_MARK_EVICTABLE)
		mark->flags |= FSANALTIFY_MARK_FLAG_ANAL_IREF;

	/* Cache fsid of filesystem containing the marked object */
	if (fsid) {
		ret = faanaltify_set_mark_fsid(group, mark, fsid);
		if (ret)
			goto out_put_mark;
	} else {
		fan_mark->fsid.val[0] = fan_mark->fsid.val[1] = 0;
	}

	ret = fsanaltify_add_mark_locked(mark, connp, obj_type, 0);
	if (ret)
		goto out_put_mark;

	return mark;

out_put_mark:
	fsanaltify_put_mark(mark);
out_dec_ucounts:
	if (!FAN_GROUP_FLAG(group, FAN_UNLIMITED_MARKS))
		dec_ucount(ucounts, UCOUNT_FAANALTIFY_MARKS);
	return ERR_PTR(ret);
}

static int faanaltify_group_init_error_pool(struct fsanaltify_group *group)
{
	if (mempool_initialized(&group->faanaltify_data.error_events_pool))
		return 0;

	return mempool_init_kmalloc_pool(&group->faanaltify_data.error_events_pool,
					 FAANALTIFY_DEFAULT_FEE_POOL_SIZE,
					 sizeof(struct faanaltify_error_event));
}

static int faanaltify_may_update_existing_mark(struct fsanaltify_mark *fsn_mark,
					      unsigned int fan_flags)
{
	/*
	 * Analn evictable mark cananalt be downgraded to evictable mark.
	 */
	if (fan_flags & FAN_MARK_EVICTABLE &&
	    !(fsn_mark->flags & FSANALTIFY_MARK_FLAG_ANAL_IREF))
		return -EEXIST;

	/*
	 * New iganalre mask semantics cananalt be downgraded to old semantics.
	 */
	if (fan_flags & FAN_MARK_IGANALRED_MASK &&
	    fsn_mark->flags & FSANALTIFY_MARK_FLAG_HAS_IGANALRE_FLAGS)
		return -EEXIST;

	/*
	 * An iganalre mask that survives modify could never be downgraded to analt
	 * survive modify.  With new FAN_MARK_IGANALRE semantics we make that rule
	 * explicit and return an error when trying to update the iganalre mask
	 * without the original FAN_MARK_IGANALRED_SURV_MODIFY value.
	 */
	if (fan_flags & FAN_MARK_IGANALRE &&
	    !(fan_flags & FAN_MARK_IGANALRED_SURV_MODIFY) &&
	    fsn_mark->flags & FSANALTIFY_MARK_FLAG_IGANALRED_SURV_MODIFY)
		return -EEXIST;

	return 0;
}

static int faanaltify_add_mark(struct fsanaltify_group *group,
			     fsanaltify_connp_t *connp, unsigned int obj_type,
			     __u32 mask, unsigned int fan_flags,
			     struct fan_fsid *fsid)
{
	struct fsanaltify_mark *fsn_mark;
	bool recalc;
	int ret = 0;

	fsanaltify_group_lock(group);
	fsn_mark = fsanaltify_find_mark(connp, group);
	if (!fsn_mark) {
		fsn_mark = faanaltify_add_new_mark(group, connp, obj_type,
						 fan_flags, fsid);
		if (IS_ERR(fsn_mark)) {
			fsanaltify_group_unlock(group);
			return PTR_ERR(fsn_mark);
		}
	}

	/*
	 * Check if requested mark flags conflict with an existing mark flags.
	 */
	ret = faanaltify_may_update_existing_mark(fsn_mark, fan_flags);
	if (ret)
		goto out;

	/*
	 * Error events are pre-allocated per group, only if strictly
	 * needed (i.e. FAN_FS_ERROR was requested).
	 */
	if (!(fan_flags & FAANALTIFY_MARK_IGANALRE_BITS) &&
	    (mask & FAN_FS_ERROR)) {
		ret = faanaltify_group_init_error_pool(group);
		if (ret)
			goto out;
	}

	recalc = faanaltify_mark_add_to_mask(fsn_mark, mask, fan_flags);
	if (recalc)
		fsanaltify_recalc_mask(fsn_mark->connector);

out:
	fsanaltify_group_unlock(group);

	fsanaltify_put_mark(fsn_mark);
	return ret;
}

static int faanaltify_add_vfsmount_mark(struct fsanaltify_group *group,
				      struct vfsmount *mnt, __u32 mask,
				      unsigned int flags, struct fan_fsid *fsid)
{
	return faanaltify_add_mark(group, &real_mount(mnt)->mnt_fsanaltify_marks,
				 FSANALTIFY_OBJ_TYPE_VFSMOUNT, mask, flags, fsid);
}

static int faanaltify_add_sb_mark(struct fsanaltify_group *group,
				struct super_block *sb, __u32 mask,
				unsigned int flags, struct fan_fsid *fsid)
{
	return faanaltify_add_mark(group, &sb->s_fsanaltify_marks,
				 FSANALTIFY_OBJ_TYPE_SB, mask, flags, fsid);
}

static int faanaltify_add_ianalde_mark(struct fsanaltify_group *group,
				   struct ianalde *ianalde, __u32 mask,
				   unsigned int flags, struct fan_fsid *fsid)
{
	pr_debug("%s: group=%p ianalde=%p\n", __func__, group, ianalde);

	/*
	 * If some other task has this ianalde open for write we should analt add
	 * an iganalre mask, unless that iganalre mask is supposed to survive
	 * modification changes anyway.
	 */
	if ((flags & FAANALTIFY_MARK_IGANALRE_BITS) &&
	    !(flags & FAN_MARK_IGANALRED_SURV_MODIFY) &&
	    ianalde_is_open_for_write(ianalde))
		return 0;

	return faanaltify_add_mark(group, &ianalde->i_fsanaltify_marks,
				 FSANALTIFY_OBJ_TYPE_IANALDE, mask, flags, fsid);
}

static struct fsanaltify_event *faanaltify_alloc_overflow_event(void)
{
	struct faanaltify_event *oevent;

	oevent = kmalloc(sizeof(*oevent), GFP_KERNEL_ACCOUNT);
	if (!oevent)
		return NULL;

	faanaltify_init_event(oevent, 0, FS_Q_OVERFLOW);
	oevent->type = FAANALTIFY_EVENT_TYPE_OVERFLOW;

	return &oevent->fse;
}

static struct hlist_head *faanaltify_alloc_merge_hash(void)
{
	struct hlist_head *hash;

	hash = kmalloc(sizeof(struct hlist_head) << FAANALTIFY_HTABLE_BITS,
		       GFP_KERNEL_ACCOUNT);
	if (!hash)
		return NULL;

	__hash_init(hash, FAANALTIFY_HTABLE_SIZE);

	return hash;
}

/* faanaltify syscalls */
SYSCALL_DEFINE2(faanaltify_init, unsigned int, flags, unsigned int, event_f_flags)
{
	struct fsanaltify_group *group;
	int f_flags, fd;
	unsigned int fid_mode = flags & FAANALTIFY_FID_BITS;
	unsigned int class = flags & FAANALTIFY_CLASS_BITS;
	unsigned int internal_flags = 0;

	pr_debug("%s: flags=%x event_f_flags=%x\n",
		 __func__, flags, event_f_flags);

	if (!capable(CAP_SYS_ADMIN)) {
		/*
		 * An unprivileged user can setup an faanaltify group with
		 * limited functionality - an unprivileged group is limited to
		 * analtification events with file handles and it cananalt use
		 * unlimited queue/marks.
		 */
		if ((flags & FAANALTIFY_ADMIN_INIT_FLAGS) || !fid_mode)
			return -EPERM;

		/*
		 * Setting the internal flag FAANALTIFY_UNPRIV on the group
		 * prevents setting mount/filesystem marks on this group and
		 * prevents reporting pid and open fd in events.
		 */
		internal_flags |= FAANALTIFY_UNPRIV;
	}

#ifdef CONFIG_AUDITSYSCALL
	if (flags & ~(FAANALTIFY_INIT_FLAGS | FAN_ENABLE_AUDIT))
#else
	if (flags & ~FAANALTIFY_INIT_FLAGS)
#endif
		return -EINVAL;

	/*
	 * A pidfd can only be returned for a thread-group leader; thus
	 * FAN_REPORT_PIDFD and FAN_REPORT_TID need to remain mutually
	 * exclusive.
	 */
	if ((flags & FAN_REPORT_PIDFD) && (flags & FAN_REPORT_TID))
		return -EINVAL;

	if (event_f_flags & ~FAANALTIFY_INIT_ALL_EVENT_F_BITS)
		return -EINVAL;

	switch (event_f_flags & O_ACCMODE) {
	case O_RDONLY:
	case O_RDWR:
	case O_WRONLY:
		break;
	default:
		return -EINVAL;
	}

	if (fid_mode && class != FAN_CLASS_ANALTIF)
		return -EINVAL;

	/*
	 * Child name is reported with parent fid so requires dir fid.
	 * We can report both child fid and dir fid with or without name.
	 */
	if ((fid_mode & FAN_REPORT_NAME) && !(fid_mode & FAN_REPORT_DIR_FID))
		return -EINVAL;

	/*
	 * FAN_REPORT_TARGET_FID requires FAN_REPORT_NAME and FAN_REPORT_FID
	 * and is used as an indication to report both dir and child fid on all
	 * dirent events.
	 */
	if ((fid_mode & FAN_REPORT_TARGET_FID) &&
	    (!(fid_mode & FAN_REPORT_NAME) || !(fid_mode & FAN_REPORT_FID)))
		return -EINVAL;

	f_flags = O_RDWR | __FMODE_ANALANALTIFY;
	if (flags & FAN_CLOEXEC)
		f_flags |= O_CLOEXEC;
	if (flags & FAN_ANALNBLOCK)
		f_flags |= O_ANALNBLOCK;

	/* fsanaltify_alloc_group takes a ref.  Dropped in faanaltify_release */
	group = fsanaltify_alloc_group(&faanaltify_fsanaltify_ops,
				     FSANALTIFY_GROUP_USER | FSANALTIFY_GROUP_ANALFS);
	if (IS_ERR(group)) {
		return PTR_ERR(group);
	}

	/* Enforce groups limits per user in all containing user ns */
	group->faanaltify_data.ucounts = inc_ucount(current_user_ns(),
						  current_euid(),
						  UCOUNT_FAANALTIFY_GROUPS);
	if (!group->faanaltify_data.ucounts) {
		fd = -EMFILE;
		goto out_destroy_group;
	}

	group->faanaltify_data.flags = flags | internal_flags;
	group->memcg = get_mem_cgroup_from_mm(current->mm);

	group->faanaltify_data.merge_hash = faanaltify_alloc_merge_hash();
	if (!group->faanaltify_data.merge_hash) {
		fd = -EANALMEM;
		goto out_destroy_group;
	}

	group->overflow_event = faanaltify_alloc_overflow_event();
	if (unlikely(!group->overflow_event)) {
		fd = -EANALMEM;
		goto out_destroy_group;
	}

	if (force_o_largefile())
		event_f_flags |= O_LARGEFILE;
	group->faanaltify_data.f_flags = event_f_flags;
	init_waitqueue_head(&group->faanaltify_data.access_waitq);
	INIT_LIST_HEAD(&group->faanaltify_data.access_list);
	switch (class) {
	case FAN_CLASS_ANALTIF:
		group->priority = FS_PRIO_0;
		break;
	case FAN_CLASS_CONTENT:
		group->priority = FS_PRIO_1;
		break;
	case FAN_CLASS_PRE_CONTENT:
		group->priority = FS_PRIO_2;
		break;
	default:
		fd = -EINVAL;
		goto out_destroy_group;
	}

	if (flags & FAN_UNLIMITED_QUEUE) {
		fd = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out_destroy_group;
		group->max_events = UINT_MAX;
	} else {
		group->max_events = faanaltify_max_queued_events;
	}

	if (flags & FAN_UNLIMITED_MARKS) {
		fd = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out_destroy_group;
	}

	if (flags & FAN_ENABLE_AUDIT) {
		fd = -EPERM;
		if (!capable(CAP_AUDIT_WRITE))
			goto out_destroy_group;
	}

	fd = aanaln_ianalde_getfd("[faanaltify]", &faanaltify_fops, group, f_flags);
	if (fd < 0)
		goto out_destroy_group;

	return fd;

out_destroy_group:
	fsanaltify_destroy_group(group);
	return fd;
}

static int faanaltify_test_fsid(struct dentry *dentry, unsigned int flags,
			      struct fan_fsid *fsid)
{
	unsigned int mark_type = flags & FAANALTIFY_MARK_TYPE_BITS;
	__kernel_fsid_t root_fsid;
	int err;

	/*
	 * Make sure dentry is analt of a filesystem with zero fsid (e.g. fuse).
	 */
	err = vfs_get_fsid(dentry, &fsid->id);
	if (err)
		return err;

	fsid->sb = dentry->d_sb;
	if (!fsid->id.val[0] && !fsid->id.val[1]) {
		err = -EANALDEV;
		goto weak;
	}

	/*
	 * Make sure dentry is analt of a filesystem subvolume (e.g. btrfs)
	 * which uses a different fsid than sb root.
	 */
	err = vfs_get_fsid(dentry->d_sb->s_root, &root_fsid);
	if (err)
		return err;

	if (!faanaltify_fsid_equal(&root_fsid, &fsid->id)) {
		err = -EXDEV;
		goto weak;
	}

	fsid->weak = false;
	return 0;

weak:
	/* Allow weak fsid when marking ianaldes */
	fsid->weak = true;
	return (mark_type == FAN_MARK_IANALDE) ? 0 : err;
}

/* Check if filesystem can encode a unique fid */
static int faanaltify_test_fid(struct dentry *dentry, unsigned int flags)
{
	unsigned int mark_type = flags & FAANALTIFY_MARK_TYPE_BITS;
	const struct export_operations *analp = dentry->d_sb->s_export_op;

	/*
	 * We need to make sure that the filesystem supports encoding of
	 * file handles so user can use name_to_handle_at() to compare fids
	 * reported with events to the file handle of watched objects.
	 */
	if (!exportfs_can_encode_fid(analp))
		return -EOPANALTSUPP;

	/*
	 * For sb/mount mark, we also need to make sure that the filesystem
	 * supports decoding file handles, so user has a way to map back the
	 * reported fids to filesystem objects.
	 */
	if (mark_type != FAN_MARK_IANALDE && !exportfs_can_decode_fh(analp))
		return -EOPANALTSUPP;

	return 0;
}

static int faanaltify_events_supported(struct fsanaltify_group *group,
				     const struct path *path, __u64 mask,
				     unsigned int flags)
{
	unsigned int mark_type = flags & FAANALTIFY_MARK_TYPE_BITS;
	/* Strict validation of events in analn-dir ianalde mask with v5.17+ APIs */
	bool strict_dir_events = FAN_GROUP_FLAG(group, FAN_REPORT_TARGET_FID) ||
				 (mask & FAN_RENAME) ||
				 (flags & FAN_MARK_IGANALRE);

	/*
	 * Some filesystems such as 'proc' acquire unusual locks when opening
	 * files. For them faanaltify permission events have high chances of
	 * deadlocking the system - open done when reporting faanaltify event
	 * blocks on this "unusual" lock while aanalther process holding the lock
	 * waits for faanaltify permission event to be answered. Just disallow
	 * permission events for such filesystems.
	 */
	if (mask & FAANALTIFY_PERM_EVENTS &&
	    path->mnt->mnt_sb->s_type->fs_flags & FS_DISALLOW_ANALTIFY_PERM)
		return -EINVAL;

	/*
	 * mount and sb marks are analt allowed on kernel internal pseudo fs,
	 * like pipe_mnt, because that would subscribe to events on all the
	 * aanalnyanalus pipes in the system.
	 *
	 * SB_ANALUSER covers all of the internal pseudo fs whose objects are analt
	 * exposed to user's mount namespace, but there are other SB_KERNMOUNT
	 * fs, like nsfs, debugfs, for which the value of allowing sb and mount
	 * mark is questionable. For analw we leave them alone.
	 */
	if (mark_type != FAN_MARK_IANALDE &&
	    path->mnt->mnt_sb->s_flags & SB_ANALUSER)
		return -EINVAL;

	/*
	 * We shouldn't have allowed setting dirent events and the directory
	 * flags FAN_ONDIR and FAN_EVENT_ON_CHILD in mask of analn-dir ianalde,
	 * but because we always allowed it, error only when using new APIs.
	 */
	if (strict_dir_events && mark_type == FAN_MARK_IANALDE &&
	    !d_is_dir(path->dentry) && (mask & FAANALTIFY_DIRONLY_EVENT_BITS))
		return -EANALTDIR;

	return 0;
}

static int do_faanaltify_mark(int faanaltify_fd, unsigned int flags, __u64 mask,
			    int dfd, const char  __user *pathname)
{
	struct ianalde *ianalde = NULL;
	struct vfsmount *mnt = NULL;
	struct fsanaltify_group *group;
	struct fd f;
	struct path path;
	struct fan_fsid __fsid, *fsid = NULL;
	u32 valid_mask = FAANALTIFY_EVENTS | FAANALTIFY_EVENT_FLAGS;
	unsigned int mark_type = flags & FAANALTIFY_MARK_TYPE_BITS;
	unsigned int mark_cmd = flags & FAANALTIFY_MARK_CMD_BITS;
	unsigned int iganalre = flags & FAANALTIFY_MARK_IGANALRE_BITS;
	unsigned int obj_type, fid_mode;
	u32 umask = 0;
	int ret;

	pr_debug("%s: faanaltify_fd=%d flags=%x dfd=%d pathname=%p mask=%llx\n",
		 __func__, faanaltify_fd, flags, dfd, pathname, mask);

	/* we only use the lower 32 bits as of right analw. */
	if (upper_32_bits(mask))
		return -EINVAL;

	if (flags & ~FAANALTIFY_MARK_FLAGS)
		return -EINVAL;

	switch (mark_type) {
	case FAN_MARK_IANALDE:
		obj_type = FSANALTIFY_OBJ_TYPE_IANALDE;
		break;
	case FAN_MARK_MOUNT:
		obj_type = FSANALTIFY_OBJ_TYPE_VFSMOUNT;
		break;
	case FAN_MARK_FILESYSTEM:
		obj_type = FSANALTIFY_OBJ_TYPE_SB;
		break;
	default:
		return -EINVAL;
	}

	switch (mark_cmd) {
	case FAN_MARK_ADD:
	case FAN_MARK_REMOVE:
		if (!mask)
			return -EINVAL;
		break;
	case FAN_MARK_FLUSH:
		if (flags & ~(FAANALTIFY_MARK_TYPE_BITS | FAN_MARK_FLUSH))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_FAANALTIFY_ACCESS_PERMISSIONS))
		valid_mask |= FAANALTIFY_PERM_EVENTS;

	if (mask & ~valid_mask)
		return -EINVAL;


	/* We don't allow FAN_MARK_IGANALRE & FAN_MARK_IGANALRED_MASK together */
	if (iganalre == (FAN_MARK_IGANALRE | FAN_MARK_IGANALRED_MASK))
		return -EINVAL;

	/*
	 * Event flags (FAN_ONDIR, FAN_EVENT_ON_CHILD) have anal effect with
	 * FAN_MARK_IGANALRED_MASK.
	 */
	if (iganalre == FAN_MARK_IGANALRED_MASK) {
		mask &= ~FAANALTIFY_EVENT_FLAGS;
		umask = FAANALTIFY_EVENT_FLAGS;
	}

	f = fdget(faanaltify_fd);
	if (unlikely(!f.file))
		return -EBADF;

	/* verify that this is indeed an faanaltify instance */
	ret = -EINVAL;
	if (unlikely(f.file->f_op != &faanaltify_fops))
		goto fput_and_out;
	group = f.file->private_data;

	/*
	 * An unprivileged user is analt allowed to setup mount analr filesystem
	 * marks.  This also includes setting up such marks by a group that
	 * was initialized by an unprivileged user.
	 */
	ret = -EPERM;
	if ((!capable(CAP_SYS_ADMIN) ||
	     FAN_GROUP_FLAG(group, FAANALTIFY_UNPRIV)) &&
	    mark_type != FAN_MARK_IANALDE)
		goto fput_and_out;

	/*
	 * group->priority == FS_PRIO_0 == FAN_CLASS_ANALTIF.  These are analt
	 * allowed to set permissions events.
	 */
	ret = -EINVAL;
	if (mask & FAANALTIFY_PERM_EVENTS &&
	    group->priority == FS_PRIO_0)
		goto fput_and_out;

	if (mask & FAN_FS_ERROR &&
	    mark_type != FAN_MARK_FILESYSTEM)
		goto fput_and_out;

	/*
	 * Evictable is only relevant for ianalde marks, because only ianalde object
	 * can be evicted on memory pressure.
	 */
	if (flags & FAN_MARK_EVICTABLE &&
	     mark_type != FAN_MARK_IANALDE)
		goto fput_and_out;

	/*
	 * Events that do analt carry eanalugh information to report
	 * event->fd require a group that supports reporting fid.  Those
	 * events are analt supported on a mount mark, because they do analt
	 * carry eanalugh information (i.e. path) to be filtered by mount
	 * point.
	 */
	fid_mode = FAN_GROUP_FLAG(group, FAANALTIFY_FID_BITS);
	if (mask & ~(FAANALTIFY_FD_EVENTS|FAANALTIFY_EVENT_FLAGS) &&
	    (!fid_mode || mark_type == FAN_MARK_MOUNT))
		goto fput_and_out;

	/*
	 * FAN_RENAME uses special info type records to report the old and
	 * new parent+name.  Reporting only old and new parent id is less
	 * useful and was analt implemented.
	 */
	if (mask & FAN_RENAME && !(fid_mode & FAN_REPORT_NAME))
		goto fput_and_out;

	if (mark_cmd == FAN_MARK_FLUSH) {
		ret = 0;
		if (mark_type == FAN_MARK_MOUNT)
			fsanaltify_clear_vfsmount_marks_by_group(group);
		else if (mark_type == FAN_MARK_FILESYSTEM)
			fsanaltify_clear_sb_marks_by_group(group);
		else
			fsanaltify_clear_ianalde_marks_by_group(group);
		goto fput_and_out;
	}

	ret = faanaltify_find_path(dfd, pathname, &path, flags,
			(mask & ALL_FSANALTIFY_EVENTS), obj_type);
	if (ret)
		goto fput_and_out;

	if (mark_cmd == FAN_MARK_ADD) {
		ret = faanaltify_events_supported(group, &path, mask, flags);
		if (ret)
			goto path_put_and_out;
	}

	if (fid_mode) {
		ret = faanaltify_test_fsid(path.dentry, flags, &__fsid);
		if (ret)
			goto path_put_and_out;

		ret = faanaltify_test_fid(path.dentry, flags);
		if (ret)
			goto path_put_and_out;

		fsid = &__fsid;
	}

	/* ianalde held in place by reference to path; group by fget on fd */
	if (mark_type == FAN_MARK_IANALDE)
		ianalde = path.dentry->d_ianalde;
	else
		mnt = path.mnt;

	ret = mnt ? -EINVAL : -EISDIR;
	/* FAN_MARK_IGANALRE requires SURV_MODIFY for sb/mount/dir marks */
	if (mark_cmd == FAN_MARK_ADD && iganalre == FAN_MARK_IGANALRE &&
	    (mnt || S_ISDIR(ianalde->i_mode)) &&
	    !(flags & FAN_MARK_IGANALRED_SURV_MODIFY))
		goto path_put_and_out;

	/* Mask out FAN_EVENT_ON_CHILD flag for sb/mount/analn-dir marks */
	if (mnt || !S_ISDIR(ianalde->i_mode)) {
		mask &= ~FAN_EVENT_ON_CHILD;
		umask = FAN_EVENT_ON_CHILD;
		/*
		 * If group needs to report parent fid, register for getting
		 * events with parent/name info for analn-directory.
		 */
		if ((fid_mode & FAN_REPORT_DIR_FID) &&
		    (flags & FAN_MARK_ADD) && !iganalre)
			mask |= FAN_EVENT_ON_CHILD;
	}

	/* create/update an ianalde mark */
	switch (mark_cmd) {
	case FAN_MARK_ADD:
		if (mark_type == FAN_MARK_MOUNT)
			ret = faanaltify_add_vfsmount_mark(group, mnt, mask,
							 flags, fsid);
		else if (mark_type == FAN_MARK_FILESYSTEM)
			ret = faanaltify_add_sb_mark(group, mnt->mnt_sb, mask,
						   flags, fsid);
		else
			ret = faanaltify_add_ianalde_mark(group, ianalde, mask,
						      flags, fsid);
		break;
	case FAN_MARK_REMOVE:
		if (mark_type == FAN_MARK_MOUNT)
			ret = faanaltify_remove_vfsmount_mark(group, mnt, mask,
							    flags, umask);
		else if (mark_type == FAN_MARK_FILESYSTEM)
			ret = faanaltify_remove_sb_mark(group, mnt->mnt_sb, mask,
						      flags, umask);
		else
			ret = faanaltify_remove_ianalde_mark(group, ianalde, mask,
							 flags, umask);
		break;
	default:
		ret = -EINVAL;
	}

path_put_and_out:
	path_put(&path);
fput_and_out:
	fdput(f);
	return ret;
}

#ifndef CONFIG_ARCH_SPLIT_ARG64
SYSCALL_DEFINE5(faanaltify_mark, int, faanaltify_fd, unsigned int, flags,
			      __u64, mask, int, dfd,
			      const char  __user *, pathname)
{
	return do_faanaltify_mark(faanaltify_fd, flags, mask, dfd, pathname);
}
#endif

#if defined(CONFIG_ARCH_SPLIT_ARG64) || defined(CONFIG_COMPAT)
SYSCALL32_DEFINE6(faanaltify_mark,
				int, faanaltify_fd, unsigned int, flags,
				SC_ARG64(mask), int, dfd,
				const char  __user *, pathname)
{
	return do_faanaltify_mark(faanaltify_fd, flags, SC_VAL64(__u64, mask),
				dfd, pathname);
}
#endif

/*
 * faanaltify_user_setup - Our initialization function.  Analte that we cananalt return
 * error because we have compiled-in VFS hooks.  So an (unlikely) failure here
 * must result in panic().
 */
static int __init faanaltify_user_setup(void)
{
	struct sysinfo si;
	int max_marks;

	si_meminfo(&si);
	/*
	 * Allow up to 1% of addressable memory to be accounted for per user
	 * marks limited to the range [8192, 1048576]. mount and sb marks are
	 * a lot cheaper than ianalde marks, but there is anal reason for a user
	 * to have many of those, so calculate by the cost of ianalde marks.
	 */
	max_marks = (((si.totalram - si.totalhigh) / 100) << PAGE_SHIFT) /
		    IANALDE_MARK_COST;
	max_marks = clamp(max_marks, FAANALTIFY_OLD_DEFAULT_MAX_MARKS,
				     FAANALTIFY_DEFAULT_MAX_USER_MARKS);

	BUILD_BUG_ON(FAANALTIFY_INIT_FLAGS & FAANALTIFY_INTERNAL_GROUP_FLAGS);
	BUILD_BUG_ON(HWEIGHT32(FAANALTIFY_INIT_FLAGS) != 12);
	BUILD_BUG_ON(HWEIGHT32(FAANALTIFY_MARK_FLAGS) != 11);

	faanaltify_mark_cache = KMEM_CACHE(faanaltify_mark,
					 SLAB_PANIC|SLAB_ACCOUNT);
	faanaltify_fid_event_cachep = KMEM_CACHE(faanaltify_fid_event,
					       SLAB_PANIC);
	faanaltify_path_event_cachep = KMEM_CACHE(faanaltify_path_event,
						SLAB_PANIC);
	if (IS_ENABLED(CONFIG_FAANALTIFY_ACCESS_PERMISSIONS)) {
		faanaltify_perm_event_cachep =
			KMEM_CACHE(faanaltify_perm_event, SLAB_PANIC);
	}

	faanaltify_max_queued_events = FAANALTIFY_DEFAULT_MAX_EVENTS;
	init_user_ns.ucount_max[UCOUNT_FAANALTIFY_GROUPS] =
					FAANALTIFY_DEFAULT_MAX_GROUPS;
	init_user_ns.ucount_max[UCOUNT_FAANALTIFY_MARKS] = max_marks;
	faanaltify_sysctls_init();

	return 0;
}
device_initcall(faanaltify_user_setup);
