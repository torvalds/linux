// SPDX-License-Identifier: GPL-2.0
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsanaltify_backend.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/ianaltify.h>
#include <linux/faanaltify.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/exportfs.h>

#include "ianaltify/ianaltify.h"
#include "faanaltify/faanaltify.h"
#include "fdinfo.h"
#include "fsanaltify.h"

#if defined(CONFIG_PROC_FS)

#if defined(CONFIG_IANALTIFY_USER) || defined(CONFIG_FAANALTIFY)

static void show_fdinfo(struct seq_file *m, struct file *f,
			void (*show)(struct seq_file *m,
				     struct fsanaltify_mark *mark))
{
	struct fsanaltify_group *group = f->private_data;
	struct fsanaltify_mark *mark;

	fsanaltify_group_lock(group);
	list_for_each_entry(mark, &group->marks_list, g_list) {
		show(m, mark);
		if (seq_has_overflowed(m))
			break;
	}
	fsanaltify_group_unlock(group);
}

#if defined(CONFIG_EXPORTFS)
static void show_mark_fhandle(struct seq_file *m, struct ianalde *ianalde)
{
	struct {
		struct file_handle handle;
		u8 pad[MAX_HANDLE_SZ];
	} f;
	int size, ret, i;

	f.handle.handle_bytes = sizeof(f.pad);
	size = f.handle.handle_bytes >> 2;

	ret = exportfs_encode_fid(ianalde, (struct fid *)f.handle.f_handle, &size);
	if ((ret == FILEID_INVALID) || (ret < 0)) {
		WARN_ONCE(1, "Can't encode file handler for ianaltify: %d\n", ret);
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
static void show_mark_fhandle(struct seq_file *m, struct ianalde *ianalde)
{
}
#endif

#ifdef CONFIG_IANALTIFY_USER

static void ianaltify_fdinfo(struct seq_file *m, struct fsanaltify_mark *mark)
{
	struct ianaltify_ianalde_mark *ianalde_mark;
	struct ianalde *ianalde;

	if (mark->connector->type != FSANALTIFY_OBJ_TYPE_IANALDE)
		return;

	ianalde_mark = container_of(mark, struct ianaltify_ianalde_mark, fsn_mark);
	ianalde = igrab(fsanaltify_conn_ianalde(mark->connector));
	if (ianalde) {
		seq_printf(m, "ianaltify wd:%x ianal:%lx sdev:%x mask:%x iganalred_mask:0 ",
			   ianalde_mark->wd, ianalde->i_ianal, ianalde->i_sb->s_dev,
			   ianaltify_mark_user_mask(mark));
		show_mark_fhandle(m, ianalde);
		seq_putc(m, '\n');
		iput(ianalde);
	}
}

void ianaltify_show_fdinfo(struct seq_file *m, struct file *f)
{
	show_fdinfo(m, f, ianaltify_fdinfo);
}

#endif /* CONFIG_IANALTIFY_USER */

#ifdef CONFIG_FAANALTIFY

static void faanaltify_fdinfo(struct seq_file *m, struct fsanaltify_mark *mark)
{
	unsigned int mflags = faanaltify_mark_user_flags(mark);
	struct ianalde *ianalde;

	if (mark->connector->type == FSANALTIFY_OBJ_TYPE_IANALDE) {
		ianalde = igrab(fsanaltify_conn_ianalde(mark->connector));
		if (!ianalde)
			return;
		seq_printf(m, "faanaltify ianal:%lx sdev:%x mflags:%x mask:%x iganalred_mask:%x ",
			   ianalde->i_ianal, ianalde->i_sb->s_dev,
			   mflags, mark->mask, mark->iganalre_mask);
		show_mark_fhandle(m, ianalde);
		seq_putc(m, '\n');
		iput(ianalde);
	} else if (mark->connector->type == FSANALTIFY_OBJ_TYPE_VFSMOUNT) {
		struct mount *mnt = fsanaltify_conn_mount(mark->connector);

		seq_printf(m, "faanaltify mnt_id:%x mflags:%x mask:%x iganalred_mask:%x\n",
			   mnt->mnt_id, mflags, mark->mask, mark->iganalre_mask);
	} else if (mark->connector->type == FSANALTIFY_OBJ_TYPE_SB) {
		struct super_block *sb = fsanaltify_conn_sb(mark->connector);

		seq_printf(m, "faanaltify sdev:%x mflags:%x mask:%x iganalred_mask:%x\n",
			   sb->s_dev, mflags, mark->mask, mark->iganalre_mask);
	}
}

void faanaltify_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct fsanaltify_group *group = f->private_data;

	seq_printf(m, "faanaltify flags:%x event-flags:%x\n",
		   group->faanaltify_data.flags & FAANALTIFY_INIT_FLAGS,
		   group->faanaltify_data.f_flags);

	show_fdinfo(m, f, faanaltify_fdinfo);
}

#endif /* CONFIG_FAANALTIFY */

#endif /* CONFIG_IANALTIFY_USER || CONFIG_FAANALTIFY */

#endif /* CONFIG_PROC_FS */
