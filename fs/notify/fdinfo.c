#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify_backend.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/inotify.h>
#include <linux/fanotify.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/exportfs.h>

#include "inotify/inotify.h"
#include "../fs/mount.h"

#if defined(CONFIG_PROC_FS)

#if defined(CONFIG_INOTIFY_USER) || defined(CONFIG_FANOTIFY)

static int show_fdinfo(struct seq_file *m, struct file *f,
		       int (*show)(struct seq_file *m, struct fsnotify_mark *mark))
{
	struct fsnotify_group *group = f->private_data;
	struct fsnotify_mark *mark;
	int ret = 0;

	mutex_lock(&group->mark_mutex);
	list_for_each_entry(mark, &group->marks_list, g_list) {
		ret = show(m, mark);
		if (ret)
			break;
	}
	mutex_unlock(&group->mark_mutex);
	return ret;
}

#if defined(CONFIG_EXPORTFS)
static int show_mark_fhandle(struct seq_file *m, struct inode *inode)
{
	struct {
		struct file_handle handle;
		u8 pad[MAX_HANDLE_SZ];
	} f;
	int size, ret, i;

	f.handle.handle_bytes = sizeof(f.pad);
	size = f.handle.handle_bytes >> 2;

	ret = exportfs_encode_inode_fh(inode, (struct fid *)f.handle.f_handle, &size, 0);
	if ((ret == FILEID_INVALID) || (ret < 0)) {
		WARN_ONCE(1, "Can't encode file handler for inotify: %d\n", ret);
		return 0;
	}

	f.handle.handle_type = ret;
	f.handle.handle_bytes = size * sizeof(u32);

	ret = seq_printf(m, "fhandle-bytes:%x fhandle-type:%x f_handle:",
			 f.handle.handle_bytes, f.handle.handle_type);

	for (i = 0; i < f.handle.handle_bytes; i++)
		ret |= seq_printf(m, "%02x", (int)f.handle.f_handle[i]);

	return ret;
}
#else
static int show_mark_fhandle(struct seq_file *m, struct inode *inode)
{
	return 0;
}
#endif

#ifdef CONFIG_INOTIFY_USER

static int inotify_fdinfo(struct seq_file *m, struct fsnotify_mark *mark)
{
	struct inotify_inode_mark *inode_mark;
	struct inode *inode;
	int ret = 0;

	if (!(mark->flags & (FSNOTIFY_MARK_FLAG_ALIVE | FSNOTIFY_MARK_FLAG_INODE)))
		return 0;

	inode_mark = container_of(mark, struct inotify_inode_mark, fsn_mark);
	inode = igrab(mark->i.inode);
	if (inode) {
		ret = seq_printf(m, "inotify wd:%x ino:%lx sdev:%x "
				 "mask:%x ignored_mask:%x ",
				 inode_mark->wd, inode->i_ino,
				 inode->i_sb->s_dev,
				 mark->mask, mark->ignored_mask);
		ret |= show_mark_fhandle(m, inode);
		ret |= seq_putc(m, '\n');
		iput(inode);
	}

	return ret;
}

int inotify_show_fdinfo(struct seq_file *m, struct file *f)
{
	return show_fdinfo(m, f, inotify_fdinfo);
}

#endif /* CONFIG_INOTIFY_USER */

#ifdef CONFIG_FANOTIFY

static int fanotify_fdinfo(struct seq_file *m, struct fsnotify_mark *mark)
{
	unsigned int mflags = 0;
	struct inode *inode;
	int ret = 0;

	if (!(mark->flags & FSNOTIFY_MARK_FLAG_ALIVE))
		return 0;

	if (mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY)
		mflags |= FAN_MARK_IGNORED_SURV_MODIFY;

	if (mark->flags & FSNOTIFY_MARK_FLAG_INODE) {
		inode = igrab(mark->i.inode);
		if (!inode)
			goto out;
		ret = seq_printf(m, "fanotify ino:%lx sdev:%x "
				 "mflags:%x mask:%x ignored_mask:%x ",
				 inode->i_ino, inode->i_sb->s_dev,
				 mflags, mark->mask, mark->ignored_mask);
		ret |= show_mark_fhandle(m, inode);
		ret |= seq_putc(m, '\n');
		iput(inode);
	} else if (mark->flags & FSNOTIFY_MARK_FLAG_VFSMOUNT) {
		struct mount *mnt = real_mount(mark->m.mnt);

		ret = seq_printf(m, "fanotify mnt_id:%x mflags:%x mask:%x "
				 "ignored_mask:%x\n", mnt->mnt_id, mflags,
				 mark->mask, mark->ignored_mask);
	}
out:
	return ret;
}

int fanotify_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct fsnotify_group *group = f->private_data;
	unsigned int flags = 0;

	switch (group->priority) {
	case FS_PRIO_0:
		flags |= FAN_CLASS_NOTIF;
		break;
	case FS_PRIO_1:
		flags |= FAN_CLASS_CONTENT;
		break;
	case FS_PRIO_2:
		flags |= FAN_CLASS_PRE_CONTENT;
		break;
	}

	if (group->max_events == UINT_MAX)
		flags |= FAN_UNLIMITED_QUEUE;

	if (group->fanotify_data.max_marks == UINT_MAX)
		flags |= FAN_UNLIMITED_MARKS;

	seq_printf(m, "fanotify flags:%x event-flags:%x\n",
		   flags, group->fanotify_data.f_flags);

	return show_fdinfo(m, f, fanotify_fdinfo);
}

#endif /* CONFIG_FANOTIFY */

#endif /* CONFIG_INOTIFY_USER || CONFIG_FANOTIFY */

#endif /* CONFIG_PROC_FS */
