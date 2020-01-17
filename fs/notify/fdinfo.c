// SPDX-License-Identifier: GPL-2.0
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsyestify_backend.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/iyestify.h>
#include <linux/fayestify.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/exportfs.h>

#include "iyestify/iyestify.h"
#include "fdinfo.h"
#include "fsyestify.h"

#if defined(CONFIG_PROC_FS)

#if defined(CONFIG_INOTIFY_USER) || defined(CONFIG_FANOTIFY)

static void show_fdinfo(struct seq_file *m, struct file *f,
			void (*show)(struct seq_file *m,
				     struct fsyestify_mark *mark))
{
	struct fsyestify_group *group = f->private_data;
	struct fsyestify_mark *mark;

	mutex_lock(&group->mark_mutex);
	list_for_each_entry(mark, &group->marks_list, g_list) {
		show(m, mark);
		if (seq_has_overflowed(m))
			break;
	}
	mutex_unlock(&group->mark_mutex);
}

#if defined(CONFIG_EXPORTFS)
static void show_mark_fhandle(struct seq_file *m, struct iyesde *iyesde)
{
	struct {
		struct file_handle handle;
		u8 pad[MAX_HANDLE_SZ];
	} f;
	int size, ret, i;

	f.handle.handle_bytes = sizeof(f.pad);
	size = f.handle.handle_bytes >> 2;

	ret = exportfs_encode_iyesde_fh(iyesde, (struct fid *)f.handle.f_handle, &size, NULL);
	if ((ret == FILEID_INVALID) || (ret < 0)) {
		WARN_ONCE(1, "Can't encode file handler for iyestify: %d\n", ret);
		return;
	}

	f.handle.handle_type = ret;
	f.handle.handle_bytes = size * sizeof(u32);

	seq_printf(m, "fhandle-bytes:%x fhandle-type:%x f_handle:",
		   f.handle.handle_bytes, f.handle.handle_type);

	for (i = 0; i < f.handle.handle_bytes; i++)
		seq_printf(m, "%02x", (int)f.handle.f_handle[i]);
}
#else
static void show_mark_fhandle(struct seq_file *m, struct iyesde *iyesde)
{
}
#endif

#ifdef CONFIG_INOTIFY_USER

static void iyestify_fdinfo(struct seq_file *m, struct fsyestify_mark *mark)
{
	struct iyestify_iyesde_mark *iyesde_mark;
	struct iyesde *iyesde;

	if (mark->connector->type != FSNOTIFY_OBJ_TYPE_INODE)
		return;

	iyesde_mark = container_of(mark, struct iyestify_iyesde_mark, fsn_mark);
	iyesde = igrab(fsyestify_conn_iyesde(mark->connector));
	if (iyesde) {
		/*
		 * IN_ALL_EVENTS represents all of the mask bits
		 * that we expose to userspace.  There is at
		 * least one bit (FS_EVENT_ON_CHILD) which is
		 * used only internally to the kernel.
		 */
		u32 mask = mark->mask & IN_ALL_EVENTS;
		seq_printf(m, "iyestify wd:%x iyes:%lx sdev:%x mask:%x igyesred_mask:%x ",
			   iyesde_mark->wd, iyesde->i_iyes, iyesde->i_sb->s_dev,
			   mask, mark->igyesred_mask);
		show_mark_fhandle(m, iyesde);
		seq_putc(m, '\n');
		iput(iyesde);
	}
}

void iyestify_show_fdinfo(struct seq_file *m, struct file *f)
{
	show_fdinfo(m, f, iyestify_fdinfo);
}

#endif /* CONFIG_INOTIFY_USER */

#ifdef CONFIG_FANOTIFY

static void fayestify_fdinfo(struct seq_file *m, struct fsyestify_mark *mark)
{
	unsigned int mflags = 0;
	struct iyesde *iyesde;

	if (mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY)
		mflags |= FAN_MARK_IGNORED_SURV_MODIFY;

	if (mark->connector->type == FSNOTIFY_OBJ_TYPE_INODE) {
		iyesde = igrab(fsyestify_conn_iyesde(mark->connector));
		if (!iyesde)
			return;
		seq_printf(m, "fayestify iyes:%lx sdev:%x mflags:%x mask:%x igyesred_mask:%x ",
			   iyesde->i_iyes, iyesde->i_sb->s_dev,
			   mflags, mark->mask, mark->igyesred_mask);
		show_mark_fhandle(m, iyesde);
		seq_putc(m, '\n');
		iput(iyesde);
	} else if (mark->connector->type == FSNOTIFY_OBJ_TYPE_VFSMOUNT) {
		struct mount *mnt = fsyestify_conn_mount(mark->connector);

		seq_printf(m, "fayestify mnt_id:%x mflags:%x mask:%x igyesred_mask:%x\n",
			   mnt->mnt_id, mflags, mark->mask, mark->igyesred_mask);
	} else if (mark->connector->type == FSNOTIFY_OBJ_TYPE_SB) {
		struct super_block *sb = fsyestify_conn_sb(mark->connector);

		seq_printf(m, "fayestify sdev:%x mflags:%x mask:%x igyesred_mask:%x\n",
			   sb->s_dev, mflags, mark->mask, mark->igyesred_mask);
	}
}

void fayestify_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct fsyestify_group *group = f->private_data;

	seq_printf(m, "fayestify flags:%x event-flags:%x\n",
		   group->fayestify_data.flags, group->fayestify_data.f_flags);

	show_fdinfo(m, f, fayestify_fdinfo);
}

#endif /* CONFIG_FANOTIFY */

#endif /* CONFIG_INOTIFY_USER || CONFIG_FANOTIFY */

#endif /* CONFIG_PROC_FS */
