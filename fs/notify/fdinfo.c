// SPDX-License-Identifier: GPL-2.0
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
#include <linux/exportfs.h>

#include "inotify/inotify.h"
#include "fanotify/fanotify.h"
#include "fdinfo.h"
#include "fsnotify.h"

#if defined(CONFIG_PROC_FS)

#if defined(CONFIG_INOTIFY_USER) || defined(CONFIG_FANOTIFY)

static void show_fdinfo(struct seq_file *m, struct file *f,
			void (*show)(struct seq_file *m,
				     struct fsnotify_mark *mark))
{
	struct fsnotify_group *group = f->private_data;
	struct fsnotify_mark *mark;

	fsnotify_group_lock(group);
	list_for_each_entry(mark, &group->marks_list, g_list) {
		show(m, mark);
		if (seq_has_overflowed(m))
			break;
	}
	fsnotify_group_unlock(group);
}

#if defined(CONFIG_EXPORTFS)
static void show_mark_fhandle(struct seq_file *m, struct inode *inode)
{
	DEFINE_FLEX(struct file_handle, f, f_handle, handle_bytes, MAX_HANDLE_SZ);
	int size, ret, i;

	size = f->handle_bytes >> 2;

	ret = exportfs_encode_fid(inode, (struct fid *)f->f_handle, &size);
	if ((ret == FILEID_INVALID) || (ret < 0))
		return;

	f->handle_type = ret;
	f->handle_bytes = size * sizeof(u32);

	seq_printf(m, "fhandle-bytes:%x fhandle-type:%x f_handle:",
		   f->handle_bytes, f->handle_type);

	for (i = 0; i < f->handle_bytes; i++)
		seq_printf(m, "%02x", (int)f->f_handle[i]);
}
#else
static void show_mark_fhandle(struct seq_file *m, struct inode *inode)
{
}
#endif

#ifdef CONFIG_INOTIFY_USER

static void inotify_fdinfo(struct seq_file *m, struct fsnotify_mark *mark)
{
	struct inotify_inode_mark *inode_mark;
	struct inode *inode;

	if (mark->connector->type != FSNOTIFY_OBJ_TYPE_INODE)
		return;

	inode_mark = container_of(mark, struct inotify_inode_mark, fsn_mark);
	inode = igrab(fsnotify_conn_inode(mark->connector));
	if (inode) {
		seq_printf(m, "inotify wd:%x ino:%lx sdev:%x mask:%x ignored_mask:0 ",
			   inode_mark->wd, inode->i_ino, inode->i_sb->s_dev,
			   inotify_mark_user_mask(mark));
		show_mark_fhandle(m, inode);
		seq_putc(m, '\n');
		iput(inode);
	}
}

void inotify_show_fdinfo(struct seq_file *m, struct file *f)
{
	show_fdinfo(m, f, inotify_fdinfo);
}

#endif /* CONFIG_INOTIFY_USER */

#ifdef CONFIG_FANOTIFY

static void fanotify_fdinfo(struct seq_file *m, struct fsnotify_mark *mark)
{
	unsigned int mflags = fanotify_mark_user_flags(mark);
	struct inode *inode;

	if (mark->connector->type == FSNOTIFY_OBJ_TYPE_INODE) {
		inode = igrab(fsnotify_conn_inode(mark->connector));
		if (!inode)
			return;
		seq_printf(m, "fanotify ino:%lx sdev:%x mflags:%x mask:%x ignored_mask:%x ",
			   inode->i_ino, inode->i_sb->s_dev,
			   mflags, mark->mask, mark->ignore_mask);
		show_mark_fhandle(m, inode);
		seq_putc(m, '\n');
		iput(inode);
	} else if (mark->connector->type == FSNOTIFY_OBJ_TYPE_VFSMOUNT) {
		struct mount *mnt = fsnotify_conn_mount(mark->connector);

		seq_printf(m, "fanotify mnt_id:%x mflags:%x mask:%x ignored_mask:%x\n",
			   mnt->mnt_id, mflags, mark->mask, mark->ignore_mask);
	} else if (mark->connector->type == FSNOTIFY_OBJ_TYPE_SB) {
		struct super_block *sb = fsnotify_conn_sb(mark->connector);

		seq_printf(m, "fanotify sdev:%x mflags:%x mask:%x ignored_mask:%x\n",
			   sb->s_dev, mflags, mark->mask, mark->ignore_mask);
	}
}

void fanotify_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct fsnotify_group *group = f->private_data;

	seq_printf(m, "fanotify flags:%x event-flags:%x\n",
		   group->fanotify_data.flags & FANOTIFY_INIT_FLAGS,
		   group->fanotify_data.f_flags);

	show_fdinfo(m, f, fanotify_fdinfo);
}

#endif /* CONFIG_FANOTIFY */

#endif /* CONFIG_INOTIFY_USER || CONFIG_FANOTIFY */

#endif /* CONFIG_PROC_FS */
