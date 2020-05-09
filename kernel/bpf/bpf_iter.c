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
	u32 btf_id;	/* cached value */
};

struct bpf_iter_link {
	struct bpf_link link;
	struct bpf_iter_target_info *tinfo;
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

static void cache_btf_id(struct bpf_iter_target_info *tinfo,
			 struct bpf_prog *prog)
{
	tinfo->btf_id = prog->aux->attach_btf_id;
}

bool bpf_iter_prog_supported(struct bpf_prog *prog)
{
	const char *attach_fname = prog->aux->attach_func_name;
	u32 prog_btf_id = prog->aux->attach_btf_id;
	const char *prefix = BPF_ITER_FUNC_PREFIX;
	struct bpf_iter_target_info *tinfo;
	int prefix_len = strlen(prefix);
	bool supported = false;

	if (strncmp(attach_fname, prefix, prefix_len))
		return false;

	mutex_lock(&targets_mutex);
	list_for_each_entry(tinfo, &targets, list) {
		if (tinfo->btf_id && tinfo->btf_id == prog_btf_id) {
			supported = true;
			break;
		}
		if (!strcmp(attach_fname + prefix_len, tinfo->target)) {
			cache_btf_id(tinfo, prog);
			supported = true;
			break;
		}
	}
	mutex_unlock(&targets_mutex);

	return supported;
}

static void bpf_iter_link_release(struct bpf_link *link)
{
}

static void bpf_iter_link_dealloc(struct bpf_link *link)
{
	struct bpf_iter_link *iter_link =
		container_of(link, struct bpf_iter_link, link);

	kfree(iter_link);
}

static const struct bpf_link_ops bpf_iter_link_lops = {
	.release = bpf_iter_link_release,
	.dealloc = bpf_iter_link_dealloc,
};

int bpf_iter_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_link_primer link_primer;
	struct bpf_iter_target_info *tinfo;
	struct bpf_iter_link *link;
	bool existed = false;
	u32 prog_btf_id;
	int err;

	if (attr->link_create.target_fd || attr->link_create.flags)
		return -EINVAL;

	prog_btf_id = prog->aux->attach_btf_id;
	mutex_lock(&targets_mutex);
	list_for_each_entry(tinfo, &targets, list) {
		if (tinfo->btf_id == prog_btf_id) {
			existed = true;
			break;
		}
	}
	mutex_unlock(&targets_mutex);
	if (!existed)
		return -ENOENT;

	link = kzalloc(sizeof(*link), GFP_USER | __GFP_NOWARN);
	if (!link)
		return -ENOMEM;

	bpf_link_init(&link->link, BPF_LINK_TYPE_ITER, &bpf_iter_link_lops, prog);
	link->tinfo = tinfo;

	err  = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		return err;
	}

	return bpf_link_settle(&link_primer);
}
