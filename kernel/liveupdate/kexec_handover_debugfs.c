// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec_handover_debugfs.c - kexec handover debugfs interfaces
 * Copyright (C) 2023 Alexander Graf <graf@amazon.com>
 * Copyright (C) 2025 Microsoft Corporation, Mike Rapoport <rppt@kernel.org>
 * Copyright (C) 2025 Google LLC, Changyuan Lyu <changyuanl@google.com>
 * Copyright (C) 2025 Google LLC, Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#define pr_fmt(fmt) "KHO: " fmt

#include <linux/init.h>
#include <linux/io.h>
#include <linux/libfdt.h>
#include <linux/mm.h>
#include "kexec_handover_internal.h"

static struct dentry *debugfs_root;

struct fdt_debugfs {
	struct list_head list;
	struct debugfs_blob_wrapper wrapper;
	struct dentry *file;
};

static int __kho_debugfs_fdt_add(struct list_head *list, struct dentry *dir,
				 const char *name, const void *fdt)
{
	struct fdt_debugfs *f;
	struct dentry *file;

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	f->wrapper.data = (void *)fdt;
	f->wrapper.size = fdt_totalsize(fdt);

	file = debugfs_create_blob(name, 0400, dir, &f->wrapper);
	if (IS_ERR(file)) {
		kfree(f);
		return PTR_ERR(file);
	}

	f->file = file;
	list_add(&f->list, list);

	return 0;
}

int kho_debugfs_fdt_add(struct kho_debugfs *dbg, const char *name,
			const void *fdt, bool root)
{
	struct dentry *dir;

	if (root)
		dir = dbg->dir;
	else
		dir = dbg->sub_fdt_dir;

	return __kho_debugfs_fdt_add(&dbg->fdt_list, dir, name, fdt);
}

void kho_debugfs_fdt_remove(struct kho_debugfs *dbg, void *fdt)
{
	struct fdt_debugfs *ff;

	list_for_each_entry(ff, &dbg->fdt_list, list) {
		if (ff->wrapper.data == fdt) {
			debugfs_remove(ff->file);
			list_del(&ff->list);
			kfree(ff);
			break;
		}
	}
}

static int kho_out_finalize_get(void *data, u64 *val)
{
	*val = kho_finalized();

	return 0;
}

static int kho_out_finalize_set(void *data, u64 val)
{
	if (val)
		return kho_finalize();
	else
		return -EINVAL;
}

DEFINE_DEBUGFS_ATTRIBUTE(kho_out_finalize_fops, kho_out_finalize_get,
			 kho_out_finalize_set, "%llu\n");

static int scratch_phys_show(struct seq_file *m, void *v)
{
	for (int i = 0; i < kho_scratch_cnt; i++)
		seq_printf(m, "0x%llx\n", kho_scratch[i].addr);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(scratch_phys);

static int scratch_len_show(struct seq_file *m, void *v)
{
	for (int i = 0; i < kho_scratch_cnt; i++)
		seq_printf(m, "0x%llx\n", kho_scratch[i].size);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(scratch_len);

__init void kho_in_debugfs_init(struct kho_debugfs *dbg, const void *fdt)
{
	struct dentry *dir, *sub_fdt_dir;
	int err, child;

	INIT_LIST_HEAD(&dbg->fdt_list);

	dir = debugfs_create_dir("in", debugfs_root);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err_out;
	}

	sub_fdt_dir = debugfs_create_dir("sub_fdts", dir);
	if (IS_ERR(sub_fdt_dir)) {
		err = PTR_ERR(sub_fdt_dir);
		goto err_rmdir;
	}

	err = __kho_debugfs_fdt_add(&dbg->fdt_list, dir, "fdt", fdt);
	if (err)
		goto err_rmdir;

	fdt_for_each_subnode(child, fdt, 0) {
		int len = 0;
		const char *name = fdt_get_name(fdt, child, NULL);
		const u64 *fdt_phys;

		fdt_phys = fdt_getprop(fdt, child, "fdt", &len);
		if (!fdt_phys)
			continue;
		if (len != sizeof(*fdt_phys)) {
			pr_warn("node %s prop fdt has invalid length: %d\n",
				name, len);
			continue;
		}
		err = __kho_debugfs_fdt_add(&dbg->fdt_list, sub_fdt_dir, name,
					    phys_to_virt(*fdt_phys));
		if (err) {
			pr_warn("failed to add fdt %s to debugfs: %pe\n", name,
				ERR_PTR(err));
			continue;
		}
	}

	dbg->dir = dir;
	dbg->sub_fdt_dir = sub_fdt_dir;

	return;
err_rmdir:
	debugfs_remove_recursive(dir);
err_out:
	/*
	 * Failure to create /sys/kernel/debug/kho/in does not prevent
	 * reviving state from KHO and setting up KHO for the next
	 * kexec.
	 */
	if (err) {
		pr_err("failed exposing handover FDT in debugfs: %pe\n",
		       ERR_PTR(err));
	}
}

__init int kho_out_debugfs_init(struct kho_debugfs *dbg)
{
	struct dentry *dir, *f, *sub_fdt_dir;

	INIT_LIST_HEAD(&dbg->fdt_list);

	dir = debugfs_create_dir("out", debugfs_root);
	if (IS_ERR(dir))
		return -ENOMEM;

	sub_fdt_dir = debugfs_create_dir("sub_fdts", dir);
	if (IS_ERR(sub_fdt_dir))
		goto err_rmdir;

	f = debugfs_create_file("scratch_phys", 0400, dir, NULL,
				&scratch_phys_fops);
	if (IS_ERR(f))
		goto err_rmdir;

	f = debugfs_create_file("scratch_len", 0400, dir, NULL,
				&scratch_len_fops);
	if (IS_ERR(f))
		goto err_rmdir;

	f = debugfs_create_file("finalize", 0600, dir, NULL,
				&kho_out_finalize_fops);
	if (IS_ERR(f))
		goto err_rmdir;

	dbg->dir = dir;
	dbg->sub_fdt_dir = sub_fdt_dir;
	return 0;

err_rmdir:
	debugfs_remove_recursive(dir);
	return -ENOENT;
}

__init int kho_debugfs_init(void)
{
	debugfs_root = debugfs_create_dir("kho", NULL);
	if (IS_ERR(debugfs_root))
		return -ENOENT;
	return 0;
}
