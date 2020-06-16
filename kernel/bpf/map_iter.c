// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <linux/bpf.h>
#include <linux/fs.h>
#include <linux/filter.h>
#include <linux/kernel.h>

struct bpf_iter_seq_map_info {
	u32 mid;
};

static void *bpf_map_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_map_info *info = seq->private;
	struct bpf_map *map;

	map = bpf_map_get_curr_or_next(&info->mid);
	if (!map)
		return NULL;

	++*pos;
	return map;
}

static void *bpf_map_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_map_info *info = seq->private;
	struct bpf_map *map;

	++*pos;
	++info->mid;
	bpf_map_put((struct bpf_map *)v);
	map = bpf_map_get_curr_or_next(&info->mid);
	if (!map)
		return NULL;

	return map;
}

struct bpf_iter__bpf_map {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct bpf_map *, map);
};

DEFINE_BPF_ITER_FUNC(bpf_map, struct bpf_iter_meta *meta, struct bpf_map *map)

static int __bpf_map_seq_show(struct seq_file *seq, void *v, bool in_stop)
{
	struct bpf_iter__bpf_map ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	int ret = 0;

	ctx.meta = &meta;
	ctx.map = v;
	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (prog)
		ret = bpf_iter_run_prog(prog, &ctx);

	return ret;
}

static int bpf_map_seq_show(struct seq_file *seq, void *v)
{
	return __bpf_map_seq_show(seq, v, false);
}

static void bpf_map_seq_stop(struct seq_file *seq, void *v)
{
	if (!v)
		(void)__bpf_map_seq_show(seq, v, true);
	else
		bpf_map_put((struct bpf_map *)v);
}

static const struct seq_operations bpf_map_seq_ops = {
	.start	= bpf_map_seq_start,
	.next	= bpf_map_seq_next,
	.stop	= bpf_map_seq_stop,
	.show	= bpf_map_seq_show,
};

static const struct bpf_iter_reg bpf_map_reg_info = {
	.target			= "bpf_map",
	.seq_ops		= &bpf_map_seq_ops,
	.init_seq_private	= NULL,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_map_info),
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__bpf_map, map),
		  PTR_TO_BTF_ID_OR_NULL },
	},
};

static int __init bpf_map_iter_init(void)
{
	return bpf_iter_reg_target(&bpf_map_reg_info);
}

late_initcall(bpf_map_iter_init);
