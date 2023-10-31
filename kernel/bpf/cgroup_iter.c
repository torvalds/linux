// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Google */
#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <linux/cgroup.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>

#include "../cgroup/cgroup-internal.h"  /* cgroup_mutex and cgroup_is_dead */

/* cgroup_iter provides four modes of traversal to the cgroup hierarchy.
 *
 *  1. Walk the descendants of a cgroup in pre-order.
 *  2. Walk the descendants of a cgroup in post-order.
 *  3. Walk the ancestors of a cgroup.
 *  4. Show the given cgroup only.
 *
 * For walking descendants, cgroup_iter can walk in either pre-order or
 * post-order. For walking ancestors, the iter walks up from a cgroup to
 * the root.
 *
 * The iter program can terminate the walk early by returning 1. Walk
 * continues if prog returns 0.
 *
 * The prog can check (seq->num == 0) to determine whether this is
 * the first element. The prog may also be passed a NULL cgroup,
 * which means the walk has completed and the prog has a chance to
 * do post-processing, such as outputting an epilogue.
 *
 * Note: the iter_prog is called with cgroup_mutex held.
 *
 * Currently only one session is supported, which means, depending on the
 * volume of data bpf program intends to send to user space, the number
 * of cgroups that can be walked is limited. For example, given the current
 * buffer size is 8 * PAGE_SIZE, if the program sends 64B data for each
 * cgroup, assuming PAGE_SIZE is 4kb, the total number of cgroups that can
 * be walked is 512. This is a limitation of cgroup_iter. If the output data
 * is larger than the kernel buffer size, after all data in the kernel buffer
 * is consumed by user space, the subsequent read() syscall will signal
 * EOPNOTSUPP. In order to work around, the user may have to update their
 * program to reduce the volume of data sent to output. For example, skip
 * some uninteresting cgroups.
 */

struct bpf_iter__cgroup {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct cgroup *, cgroup);
};

struct cgroup_iter_priv {
	struct cgroup_subsys_state *start_css;
	bool visited_all;
	bool terminate;
	int order;
};

static void *cgroup_iter_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct cgroup_iter_priv *p = seq->private;

	cgroup_lock();

	/* cgroup_iter doesn't support read across multiple sessions. */
	if (*pos > 0) {
		if (p->visited_all)
			return NULL;

		/* Haven't visited all, but because cgroup_mutex has dropped,
		 * return -EOPNOTSUPP to indicate incomplete iteration.
		 */
		return ERR_PTR(-EOPNOTSUPP);
	}

	++*pos;
	p->terminate = false;
	p->visited_all = false;
	if (p->order == BPF_CGROUP_ITER_DESCENDANTS_PRE)
		return css_next_descendant_pre(NULL, p->start_css);
	else if (p->order == BPF_CGROUP_ITER_DESCENDANTS_POST)
		return css_next_descendant_post(NULL, p->start_css);
	else /* BPF_CGROUP_ITER_SELF_ONLY and BPF_CGROUP_ITER_ANCESTORS_UP */
		return p->start_css;
}

static int __cgroup_iter_seq_show(struct seq_file *seq,
				  struct cgroup_subsys_state *css, int in_stop);

static void cgroup_iter_seq_stop(struct seq_file *seq, void *v)
{
	struct cgroup_iter_priv *p = seq->private;

	cgroup_unlock();

	/* pass NULL to the prog for post-processing */
	if (!v) {
		__cgroup_iter_seq_show(seq, NULL, true);
		p->visited_all = true;
	}
}

static void *cgroup_iter_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct cgroup_subsys_state *curr = (struct cgroup_subsys_state *)v;
	struct cgroup_iter_priv *p = seq->private;

	++*pos;
	if (p->terminate)
		return NULL;

	if (p->order == BPF_CGROUP_ITER_DESCENDANTS_PRE)
		return css_next_descendant_pre(curr, p->start_css);
	else if (p->order == BPF_CGROUP_ITER_DESCENDANTS_POST)
		return css_next_descendant_post(curr, p->start_css);
	else if (p->order == BPF_CGROUP_ITER_ANCESTORS_UP)
		return curr->parent;
	else  /* BPF_CGROUP_ITER_SELF_ONLY */
		return NULL;
}

static int __cgroup_iter_seq_show(struct seq_file *seq,
				  struct cgroup_subsys_state *css, int in_stop)
{
	struct cgroup_iter_priv *p = seq->private;
	struct bpf_iter__cgroup ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	int ret = 0;

	/* cgroup is dead, skip this element */
	if (css && cgroup_is_dead(css->cgroup))
		return 0;

	ctx.meta = &meta;
	ctx.cgroup = css ? css->cgroup : NULL;
	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (prog)
		ret = bpf_iter_run_prog(prog, &ctx);

	/* if prog returns > 0, terminate after this element. */
	if (ret != 0)
		p->terminate = true;

	return 0;
}

static int cgroup_iter_seq_show(struct seq_file *seq, void *v)
{
	return __cgroup_iter_seq_show(seq, (struct cgroup_subsys_state *)v,
				      false);
}

static const struct seq_operations cgroup_iter_seq_ops = {
	.start  = cgroup_iter_seq_start,
	.next   = cgroup_iter_seq_next,
	.stop   = cgroup_iter_seq_stop,
	.show   = cgroup_iter_seq_show,
};

BTF_ID_LIST_GLOBAL_SINGLE(bpf_cgroup_btf_id, struct, cgroup)

static int cgroup_iter_seq_init(void *priv, struct bpf_iter_aux_info *aux)
{
	struct cgroup_iter_priv *p = (struct cgroup_iter_priv *)priv;
	struct cgroup *cgrp = aux->cgroup.start;

	/* bpf_iter_attach_cgroup() has already acquired an extra reference
	 * for the start cgroup, but the reference may be released after
	 * cgroup_iter_seq_init(), so acquire another reference for the
	 * start cgroup.
	 */
	p->start_css = &cgrp->self;
	css_get(p->start_css);
	p->terminate = false;
	p->visited_all = false;
	p->order = aux->cgroup.order;
	return 0;
}

static void cgroup_iter_seq_fini(void *priv)
{
	struct cgroup_iter_priv *p = (struct cgroup_iter_priv *)priv;

	css_put(p->start_css);
}

static const struct bpf_iter_seq_info cgroup_iter_seq_info = {
	.seq_ops		= &cgroup_iter_seq_ops,
	.init_seq_private	= cgroup_iter_seq_init,
	.fini_seq_private	= cgroup_iter_seq_fini,
	.seq_priv_size		= sizeof(struct cgroup_iter_priv),
};

static int bpf_iter_attach_cgroup(struct bpf_prog *prog,
				  union bpf_iter_link_info *linfo,
				  struct bpf_iter_aux_info *aux)
{
	int fd = linfo->cgroup.cgroup_fd;
	u64 id = linfo->cgroup.cgroup_id;
	int order = linfo->cgroup.order;
	struct cgroup *cgrp;

	if (order != BPF_CGROUP_ITER_DESCENDANTS_PRE &&
	    order != BPF_CGROUP_ITER_DESCENDANTS_POST &&
	    order != BPF_CGROUP_ITER_ANCESTORS_UP &&
	    order != BPF_CGROUP_ITER_SELF_ONLY)
		return -EINVAL;

	if (fd && id)
		return -EINVAL;

	if (fd)
		cgrp = cgroup_v1v2_get_from_fd(fd);
	else if (id)
		cgrp = cgroup_get_from_id(id);
	else /* walk the entire hierarchy by default. */
		cgrp = cgroup_get_from_path("/");

	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	aux->cgroup.start = cgrp;
	aux->cgroup.order = order;
	return 0;
}

static void bpf_iter_detach_cgroup(struct bpf_iter_aux_info *aux)
{
	cgroup_put(aux->cgroup.start);
}

static void bpf_iter_cgroup_show_fdinfo(const struct bpf_iter_aux_info *aux,
					struct seq_file *seq)
{
	char *buf;

	buf = kzalloc(PATH_MAX, GFP_KERNEL);
	if (!buf) {
		seq_puts(seq, "cgroup_path:\t<unknown>\n");
		goto show_order;
	}

	/* If cgroup_path_ns() fails, buf will be an empty string, cgroup_path
	 * will print nothing.
	 *
	 * Path is in the calling process's cgroup namespace.
	 */
	cgroup_path_ns(aux->cgroup.start, buf, PATH_MAX,
		       current->nsproxy->cgroup_ns);
	seq_printf(seq, "cgroup_path:\t%s\n", buf);
	kfree(buf);

show_order:
	if (aux->cgroup.order == BPF_CGROUP_ITER_DESCENDANTS_PRE)
		seq_puts(seq, "order: descendants_pre\n");
	else if (aux->cgroup.order == BPF_CGROUP_ITER_DESCENDANTS_POST)
		seq_puts(seq, "order: descendants_post\n");
	else if (aux->cgroup.order == BPF_CGROUP_ITER_ANCESTORS_UP)
		seq_puts(seq, "order: ancestors_up\n");
	else /* BPF_CGROUP_ITER_SELF_ONLY */
		seq_puts(seq, "order: self_only\n");
}

static int bpf_iter_cgroup_fill_link_info(const struct bpf_iter_aux_info *aux,
					  struct bpf_link_info *info)
{
	info->iter.cgroup.order = aux->cgroup.order;
	info->iter.cgroup.cgroup_id = cgroup_id(aux->cgroup.start);
	return 0;
}

DEFINE_BPF_ITER_FUNC(cgroup, struct bpf_iter_meta *meta,
		     struct cgroup *cgroup)

static struct bpf_iter_reg bpf_cgroup_reg_info = {
	.target			= "cgroup",
	.feature		= BPF_ITER_RESCHED,
	.attach_target		= bpf_iter_attach_cgroup,
	.detach_target		= bpf_iter_detach_cgroup,
	.show_fdinfo		= bpf_iter_cgroup_show_fdinfo,
	.fill_link_info		= bpf_iter_cgroup_fill_link_info,
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__cgroup, cgroup),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &cgroup_iter_seq_info,
};

static int __init bpf_cgroup_iter_init(void)
{
	bpf_cgroup_reg_info.ctx_arg_info[0].btf_id = bpf_cgroup_btf_id[0];
	return bpf_iter_reg_target(&bpf_cgroup_reg_info);
}

late_initcall(bpf_cgroup_iter_init);

struct bpf_iter_css {
	__u64 __opaque[3];
} __attribute__((aligned(8)));

struct bpf_iter_css_kern {
	struct cgroup_subsys_state *start;
	struct cgroup_subsys_state *pos;
	unsigned int flags;
} __attribute__((aligned(8)));

__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		"Global functions as their definitions will be in vmlinux BTF");

__bpf_kfunc int bpf_iter_css_new(struct bpf_iter_css *it,
		struct cgroup_subsys_state *start, unsigned int flags)
{
	struct bpf_iter_css_kern *kit = (void *)it;

	BUILD_BUG_ON(sizeof(struct bpf_iter_css_kern) > sizeof(struct bpf_iter_css));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_css_kern) != __alignof__(struct bpf_iter_css));

	kit->start = NULL;
	switch (flags) {
	case BPF_CGROUP_ITER_DESCENDANTS_PRE:
	case BPF_CGROUP_ITER_DESCENDANTS_POST:
	case BPF_CGROUP_ITER_ANCESTORS_UP:
		break;
	default:
		return -EINVAL;
	}

	kit->start = start;
	kit->pos = NULL;
	kit->flags = flags;
	return 0;
}

__bpf_kfunc struct cgroup_subsys_state *bpf_iter_css_next(struct bpf_iter_css *it)
{
	struct bpf_iter_css_kern *kit = (void *)it;

	if (!kit->start)
		return NULL;

	switch (kit->flags) {
	case BPF_CGROUP_ITER_DESCENDANTS_PRE:
		kit->pos = css_next_descendant_pre(kit->pos, kit->start);
		break;
	case BPF_CGROUP_ITER_DESCENDANTS_POST:
		kit->pos = css_next_descendant_post(kit->pos, kit->start);
		break;
	case BPF_CGROUP_ITER_ANCESTORS_UP:
		kit->pos = kit->pos ? kit->pos->parent : kit->start;
	}

	return kit->pos;
}

__bpf_kfunc void bpf_iter_css_destroy(struct bpf_iter_css *it)
{
}

__diag_pop();