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

/* protect bpf_iter_link changes */
static DEFINE_MUTEX(link_mutex);

/* bpf_seq_read, a customized and simpler version for bpf iterator.
 * no_llseek is assumed for this file.
 * The following are differences from seq_read():
 *  . fixed buffer size (PAGE_SIZE)
 *  . assuming no_llseek
 *  . stop() may call bpf program, handling potential overflow there
 */
static ssize_t bpf_seq_read(struct file *file, char __user *buf, size_t size,
			    loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	size_t n, offs, copied = 0;
	int err = 0;
	void *p;

	mutex_lock(&seq->lock);

	if (!seq->buf) {
		seq->size = PAGE_SIZE;
		seq->buf = kmalloc(seq->size, GFP_KERNEL);
		if (!seq->buf) {
			err = -ENOMEM;
			goto done;
		}
	}

	if (seq->count) {
		n = min(seq->count, size);
		err = copy_to_user(buf, seq->buf + seq->from, n);
		if (err) {
			err = -EFAULT;
			goto done;
		}
		seq->count -= n;
		seq->from += n;
		copied = n;
		goto done;
	}

	seq->from = 0;
	p = seq->op->start(seq, &seq->index);
	if (!p)
		goto stop;
	if (IS_ERR(p)) {
		err = PTR_ERR(p);
		seq->op->stop(seq, p);
		seq->count = 0;
		goto done;
	}

	err = seq->op->show(seq, p);
	if (err > 0) {
		seq->count = 0;
	} else if (err < 0 || seq_has_overflowed(seq)) {
		if (!err)
			err = -E2BIG;
		seq->op->stop(seq, p);
		seq->count = 0;
		goto done;
	}

	while (1) {
		loff_t pos = seq->index;

		offs = seq->count;
		p = seq->op->next(seq, p, &seq->index);
		if (pos == seq->index) {
			pr_info_ratelimited("buggy seq_file .next function %ps "
				"did not updated position index\n",
				seq->op->next);
			seq->index++;
		}

		if (IS_ERR_OR_NULL(p))
			break;

		if (seq->count >= size)
			break;

		err = seq->op->show(seq, p);
		if (err > 0) {
			seq->count = offs;
		} else if (err < 0 || seq_has_overflowed(seq)) {
			seq->count = offs;
			if (offs == 0) {
				if (!err)
					err = -E2BIG;
				seq->op->stop(seq, p);
				goto done;
			}
			break;
		}
	}
stop:
	offs = seq->count;
	/* bpf program called if !p */
	seq->op->stop(seq, p);
	if (!p && seq_has_overflowed(seq)) {
		seq->count = offs;
		if (offs == 0) {
			err = -E2BIG;
			goto done;
		}
	}

	n = min(seq->count, size);
	err = copy_to_user(buf, seq->buf, n);
	if (err) {
		err = -EFAULT;
		goto done;
	}
	copied = n;
	seq->count -= n;
	seq->from = n;
done:
	if (!copied)
		copied = err;
	else
		*ppos += copied;
	mutex_unlock(&seq->lock);
	return copied;
}

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

static int bpf_iter_link_replace(struct bpf_link *link,
				 struct bpf_prog *new_prog,
				 struct bpf_prog *old_prog)
{
	int ret = 0;

	mutex_lock(&link_mutex);
	if (old_prog && link->prog != old_prog) {
		ret = -EPERM;
		goto out_unlock;
	}

	if (link->prog->type != new_prog->type ||
	    link->prog->expected_attach_type != new_prog->expected_attach_type ||
	    link->prog->aux->attach_btf_id != new_prog->aux->attach_btf_id) {
		ret = -EINVAL;
		goto out_unlock;
	}

	old_prog = xchg(&link->prog, new_prog);
	bpf_prog_put(old_prog);

out_unlock:
	mutex_unlock(&link_mutex);
	return ret;
}

static const struct bpf_link_ops bpf_iter_link_lops = {
	.release = bpf_iter_link_release,
	.dealloc = bpf_iter_link_dealloc,
	.update_prog = bpf_iter_link_replace,
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
