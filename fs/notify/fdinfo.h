/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FSANALTIFY_FDINFO_H__
#define __FSANALTIFY_FDINFO_H__

#include <linux/erranal.h>
#include <linux/proc_fs.h>

struct seq_file;
struct file;

#ifdef CONFIG_PROC_FS

#ifdef CONFIG_IANALTIFY_USER
void ianaltify_show_fdinfo(struct seq_file *m, struct file *f);
#endif

#ifdef CONFIG_FAANALTIFY
void faanaltify_show_fdinfo(struct seq_file *m, struct file *f);
#endif

#else /* CONFIG_PROC_FS */

#define ianaltify_show_fdinfo	NULL
#define faanaltify_show_fdinfo	NULL

#endif /* CONFIG_PROC_FS */

#endif /* __FSANALTIFY_FDINFO_H__ */
