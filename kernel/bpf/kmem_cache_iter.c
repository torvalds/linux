// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Google */
#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>

#include "../../mm/slab.h" /* kmem_cache, slab_caches and slab_mutex */

/* open-coded version */
struct bpf_iter_kmem_cache {
	__u64 __opaque[1];
} __attribute__((aligned(8)));

struct bpf_iter_kmem_cache_kern {
	struct kmem_cache *pos;
} __attribute__((aligned(8)));

#define KMEM_CACHE_POS_START  ((void *)1L)

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_iter_kmem_cache_new(struct bpf_iter_kmem_cache *it)
{
	struct bpf_iter_kmem_cache_kern *kit = (void *)it;

	BUILD_BUG_ON(sizeof(*kit) > sizeof(*it));
	BUILD_BUG_ON(__alignof__(*kit) != __alignof__(*it));

	kit->pos = KMEM_CACHE_POS_START;
	return 0;
}

__bpf_kfunc struct kmem_cache *bpf_iter_kmem_cache_next(struct bpf_iter_kmem_cache *it)
{
	struct bpf_iter_kmem_cache_kern *kit = (void *)it;
	struct kmem_cache *prev = kit->pos;
	struct kmem_cache *next;
	bool destroy = false;

	if (!prev)
		return NULL;

	mutex_lock(&slab_mutex);

	if (list_empty(&slab_caches)) {
		mutex_unlock(&slab_mutex);
		return NULL;
	}

	if (prev == KMEM_CACHE_POS_START)
		next = list_first_entry(&slab_caches, struct kmem_cache, list);
	else if (list_last_entry(&slab_caches, struct kmem_cache, list) == prev)
		next = NULL;
	else
		next = list_next_entry(prev, list);

	/* boot_caches have negative refcount, don't touch them */
	if (next && next->refcount > 0)
		next->refcount++;

	/* Skip kmem_cache_destroy() for active entries */
	if (prev && prev != KMEM_CACHE_POS_START) {
		if (prev->refcount > 1)
			prev->refcount--;
		else if (prev->refcount == 1)
			destroy = true;
	}

	mutex_unlock(&slab_mutex);

	if (destroy)
		kmem_cache_destroy(prev);

	kit->pos = next;
	return next;
}

__bpf_kfunc void bpf_iter_kmem_cache_destroy(struct bpf_iter_kmem_cache *it)
{
	struct bpf_iter_kmem_cache_kern *kit = (void *)it;
	struct kmem_cache *s = kit->pos;
	bool destroy = false;

	if (s == NULL || s == KMEM_CACHE_POS_START)
		return;

	mutex_lock(&slab_mutex);

	/* Skip kmem_cache_destroy() for active entries */
	if (s->refcount > 1)
		s->refcount--;
	else if (s->refcount == 1)
		destroy = true;

	mutex_unlock(&slab_mutex);

	if (destroy)
		kmem_cache_destroy(s);
}

__bpf_kfunc_end_defs();

struct bpf_iter__kmem_cache {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct kmem_cache *, s);
};

union kmem_cache_iter_priv {
	struct bpf_iter_kmem_cache it;
	struct bpf_iter_kmem_cache_kern kit;
};

static void *kmem_cache_iter_seq_start(struct seq_file *seq, loff_t *pos)
{
	loff_t cnt = 0;
	bool found = false;
	struct kmem_cache *s;
	union kmem_cache_iter_priv *p = seq->private;

	mutex_lock(&slab_mutex);

	/* Find an entry at the given position in the slab_caches list instead
	 * of keeping a reference (of the last visited entry, if any) out of
	 * slab_mutex. It might miss something if one is deleted in the middle
	 * while it releases the lock.  But it should be rare and there's not
	 * much we can do about it.
	 */
	list_for_each_entry(s, &slab_caches, list) {
		if (cnt == *pos) {
			/* Make sure this entry remains in the list by getting
			 * a new reference count.  Note that boot_cache entries
			 * have a negative refcount, so don't touch them.
			 */
			if (s->refcount > 0)
				s->refcount++;
			found = true;
			break;
		}
		cnt++;
	}
	mutex_unlock(&slab_mutex);

	if (!found)
		s = NULL;

	p->kit.pos = s;
	return s;
}

static void kmem_cache_iter_seq_stop(struct seq_file *seq, void *v)
{
	struct bpf_iter_meta meta;
	struct bpf_iter__kmem_cache ctx = {
		.meta = &meta,
		.s = v,
	};
	union kmem_cache_iter_priv *p = seq->private;
	struct bpf_prog *prog;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, true);
	if (prog && !ctx.s)
		bpf_iter_run_prog(prog, &ctx);

	bpf_iter_kmem_cache_destroy(&p->it);
}

static void *kmem_cache_iter_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	union kmem_cache_iter_priv *p = seq->private;

	++*pos;

	return bpf_iter_kmem_cache_next(&p->it);
}

static int kmem_cache_iter_seq_show(struct seq_file *seq, void *v)
{
	struct bpf_iter_meta meta;
	struct bpf_iter__kmem_cache ctx = {
		.meta = &meta,
		.s = v,
	};
	struct bpf_prog *prog;
	int ret = 0;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, false);
	if (prog)
		ret = bpf_iter_run_prog(prog, &ctx);

	return ret;
}

static const struct seq_operations kmem_cache_iter_seq_ops = {
	.start  = kmem_cache_iter_seq_start,
	.next   = kmem_cache_iter_seq_next,
	.stop   = kmem_cache_iter_seq_stop,
	.show   = kmem_cache_iter_seq_show,
};

BTF_ID_LIST_GLOBAL_SINGLE(bpf_kmem_cache_btf_id, struct, kmem_cache)

static const struct bpf_iter_seq_info kmem_cache_iter_seq_info = {
	.seq_ops		= &kmem_cache_iter_seq_ops,
	.seq_priv_size		= sizeof(union kmem_cache_iter_priv),
};

static void bpf_iter_kmem_cache_show_fdinfo(const struct bpf_iter_aux_info *aux,
					    struct seq_file *seq)
{
	seq_puts(seq, "kmem_cache iter\n");
}

DEFINE_BPF_ITER_FUNC(kmem_cache, struct bpf_iter_meta *meta,
		     struct kmem_cache *s)

static struct bpf_iter_reg bpf_kmem_cache_reg_info = {
	.target			= "kmem_cache",
	.feature		= BPF_ITER_RESCHED,
	.show_fdinfo		= bpf_iter_kmem_cache_show_fdinfo,
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__kmem_cache, s),
		  PTR_TO_BTF_ID_OR_NULL | PTR_TRUSTED },
	},
	.seq_info		= &kmem_cache_iter_seq_info,
};

static int __init bpf_kmem_cache_iter_init(void)
{
	bpf_kmem_cache_reg_info.ctx_arg_info[0].btf_id = bpf_kmem_cache_btf_id[0];
	return bpf_iter_reg_target(&bpf_kmem_cache_reg_info);
}

late_initcall(bpf_kmem_cache_iter_init);
