// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Red Hat, Inc. */
#include <linux/bpf.h>
#include <linux/fs.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/btf_ids.h>

struct bpf_iter_seq_link_info {
	u32 link_id;
};

static void *bpf_link_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_link_info *info = seq->private;
	struct bpf_link *link;

	link = bpf_link_get_curr_or_next(&info->link_id);
	if (!link)
		return NULL;

	if (*pos == 0)
		++*pos;
	return link;
}

static void *bpf_link_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_link_info *info = seq->private;

	++*pos;
	++info->link_id;
	bpf_link_put((struct bpf_link *)v);
	return bpf_link_get_curr_or_next(&info->link_id);
}

struct bpf_iter__bpf_link {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct bpf_link *, link);
};

DEFINE_BPF_ITER_FUNC(bpf_link, struct bpf_iter_meta *meta, struct bpf_link *link)

static int __bpf_link_seq_show(struct seq_file *seq, void *v, bool in_stop)
{
	struct bpf_iter__bpf_link ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	int ret = 0;

	ctx.meta = &meta;
	ctx.link = v;
	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (prog)
		ret = bpf_iter_run_prog(prog, &ctx);

	return ret;
}

static int bpf_link_seq_show(struct seq_file *seq, void *v)
{
	return __bpf_link_seq_show(seq, v, false);
}

static void bpf_link_seq_stop(struct seq_file *seq, void *v)
{
	if (!v)
		(void)__bpf_link_seq_show(seq, v, true);
	else
		bpf_link_put((struct bpf_link *)v);
}

static const struct seq_operations bpf_link_seq_ops = {
	.start	= bpf_link_seq_start,
	.next	= bpf_link_seq_next,
	.stop	= bpf_link_seq_stop,
	.show	= bpf_link_seq_show,
};

BTF_ID_LIST(btf_bpf_link_id)
BTF_ID(struct, bpf_link)

static const struct bpf_iter_seq_info bpf_link_seq_info = {
	.seq_ops		= &bpf_link_seq_ops,
	.init_seq_private	= NULL,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_link_info),
};

static struct bpf_iter_reg bpf_link_reg_info = {
	.target			= "bpf_link",
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__bpf_link, link),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &bpf_link_seq_info,
};

static int __init bpf_link_iter_init(void)
{
	bpf_link_reg_info.ctx_arg_info[0].btf_id = *btf_bpf_link_id;
	return bpf_iter_reg_target(&bpf_link_reg_info);
}

late_initcall(bpf_link_iter_init);
