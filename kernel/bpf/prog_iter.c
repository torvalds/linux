// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <linux/bpf.h>
#include <linux/fs.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/btf_ids.h>

struct bpf_iter_seq_prog_info {
	u32 prog_id;
};

static void *bpf_prog_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_prog_info *info = seq->private;
	struct bpf_prog *prog;

	prog = bpf_prog_get_curr_or_next(&info->prog_id);
	if (!prog)
		return NULL;

	if (*pos == 0)
		++*pos;
	return prog;
}

static void *bpf_prog_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_prog_info *info = seq->private;

	++*pos;
	++info->prog_id;
	bpf_prog_put((struct bpf_prog *)v);
	return bpf_prog_get_curr_or_next(&info->prog_id);
}

struct bpf_iter__bpf_prog {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct bpf_prog *, prog);
};

DEFINE_BPF_ITER_FUNC(bpf_prog, struct bpf_iter_meta *meta, struct bpf_prog *prog)

static int __bpf_prog_seq_show(struct seq_file *seq, void *v, bool in_stop)
{
	struct bpf_iter__bpf_prog ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	int ret = 0;

	ctx.meta = &meta;
	ctx.prog = v;
	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (prog)
		ret = bpf_iter_run_prog(prog, &ctx);

	return ret;
}

static int bpf_prog_seq_show(struct seq_file *seq, void *v)
{
	return __bpf_prog_seq_show(seq, v, false);
}

static void bpf_prog_seq_stop(struct seq_file *seq, void *v)
{
	if (!v)
		(void)__bpf_prog_seq_show(seq, v, true);
	else
		bpf_prog_put((struct bpf_prog *)v);
}

static const struct seq_operations bpf_prog_seq_ops = {
	.start	= bpf_prog_seq_start,
	.next	= bpf_prog_seq_next,
	.stop	= bpf_prog_seq_stop,
	.show	= bpf_prog_seq_show,
};

BTF_ID_LIST(btf_bpf_prog_id)
BTF_ID(struct, bpf_prog)

static const struct bpf_iter_seq_info bpf_prog_seq_info = {
	.seq_ops		= &bpf_prog_seq_ops,
	.init_seq_private	= NULL,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_prog_info),
};

static struct bpf_iter_reg bpf_prog_reg_info = {
	.target			= "bpf_prog",
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__bpf_prog, prog),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &bpf_prog_seq_info,
};

static int __init bpf_prog_iter_init(void)
{
	bpf_prog_reg_info.ctx_arg_info[0].btf_id = *btf_bpf_prog_id;
	return bpf_iter_reg_target(&bpf_prog_reg_info);
}

late_initcall(bpf_prog_iter_init);
