// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */

#include <linux/fs.h>
#include <linux/filter.h>
#include <linux/bpf.h>

struct bpf_iter_target_info {
	struct list_head list;
	const char *target;
	const struct seq_operations *seq_ops;
	bpf_iter_init_seq_priv_t init_seq_private;
	bpf_iter_fini_seq_priv_t fini_seq_private;
	u32 seq_priv_size;
};

static struct list_head targets = LIST_HEAD_INIT(targets);
static DEFINE_MUTEX(targets_mutex);

int bpf_iter_reg_target(struct bpf_iter_reg *reg_info)
{
	struct bpf_iter_target_info *tinfo;

	tinfo = kmalloc(sizeof(*tinfo), GFP_KERNEL);
	if (!tinfo)
		return -ENOMEM;

	tinfo->target = reg_info->target;
	tinfo->seq_ops = reg_info->seq_ops;
	tinfo->init_seq_private = reg_info->init_seq_private;
	tinfo->fini_seq_private = reg_info->fini_seq_private;
	tinfo->seq_priv_size = reg_info->seq_priv_size;
	INIT_LIST_HEAD(&tinfo->list);

	mutex_lock(&targets_mutex);
	list_add(&tinfo->list, &targets);
	mutex_unlock(&targets_mutex);

	return 0;
}

void bpf_iter_unreg_target(const char *target)
{
	struct bpf_iter_target_info *tinfo;
	bool found = false;

	mutex_lock(&targets_mutex);
	list_for_each_entry(tinfo, &targets, list) {
		if (!strcmp(target, tinfo->target)) {
			list_del(&tinfo->list);
			kfree(tinfo);
			found = true;
			break;
		}
	}
	mutex_unlock(&targets_mutex);

	WARN_ON(found == false);
}
