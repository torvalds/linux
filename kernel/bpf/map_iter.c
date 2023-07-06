// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <linux/bpf.h>
#include <linux/fs.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/btf_ids.h>

struct bpf_iter_seq_map_info {
	u32 map_id;
};

static void *bpf_map_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_map_info *info = seq->private;
	struct bpf_map *map;

	map = bpf_map_get_curr_or_next(&info->map_id);
	if (!map)
		return NULL;

	if (*pos == 0)
		++*pos;
	return map;
}

static void *bpf_map_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_map_info *info = seq->private;

	++*pos;
	++info->map_id;
	bpf_map_put((struct bpf_map *)v);
	return bpf_map_get_curr_or_next(&info->map_id);
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

BTF_ID_LIST(btf_bpf_map_id)
BTF_ID(struct, bpf_map)

static const struct bpf_iter_seq_info bpf_map_seq_info = {
	.seq_ops		= &bpf_map_seq_ops,
	.init_seq_private	= NULL,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_map_info),
};

static struct bpf_iter_reg bpf_map_reg_info = {
	.target			= "bpf_map",
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__bpf_map, map),
		  PTR_TO_BTF_ID_OR_NULL | PTR_TRUSTED },
	},
	.seq_info		= &bpf_map_seq_info,
};

static int bpf_iter_attach_map(struct bpf_prog *prog,
			       union bpf_iter_link_info *linfo,
			       struct bpf_iter_aux_info *aux)
{
	u32 key_acc_size, value_acc_size, key_size, value_size;
	struct bpf_map *map;
	bool is_percpu = false;
	int err = -EINVAL;

	if (!linfo->map.map_fd)
		return -EBADF;

	map = bpf_map_get_with_uref(linfo->map.map_fd);
	if (IS_ERR(map))
		return PTR_ERR(map);

	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY)
		is_percpu = true;
	else if (map->map_type != BPF_MAP_TYPE_HASH &&
		 map->map_type != BPF_MAP_TYPE_LRU_HASH &&
		 map->map_type != BPF_MAP_TYPE_ARRAY)
		goto put_map;

	key_acc_size = prog->aux->max_rdonly_access;
	value_acc_size = prog->aux->max_rdwr_access;
	key_size = map->key_size;
	if (!is_percpu)
		value_size = map->value_size;
	else
		value_size = round_up(map->value_size, 8) * num_possible_cpus();

	if (key_acc_size > key_size || value_acc_size > value_size) {
		err = -EACCES;
		goto put_map;
	}

	aux->map = map;
	return 0;

put_map:
	bpf_map_put_with_uref(map);
	return err;
}

static void bpf_iter_detach_map(struct bpf_iter_aux_info *aux)
{
	bpf_map_put_with_uref(aux->map);
}

void bpf_iter_map_show_fdinfo(const struct bpf_iter_aux_info *aux,
			      struct seq_file *seq)
{
	seq_printf(seq, "map_id:\t%u\n", aux->map->id);
}

int bpf_iter_map_fill_link_info(const struct bpf_iter_aux_info *aux,
				struct bpf_link_info *info)
{
	info->iter.map.map_id = aux->map->id;
	return 0;
}

DEFINE_BPF_ITER_FUNC(bpf_map_elem, struct bpf_iter_meta *meta,
		     struct bpf_map *map, void *key, void *value)

static const struct bpf_iter_reg bpf_map_elem_reg_info = {
	.target			= "bpf_map_elem",
	.attach_target		= bpf_iter_attach_map,
	.detach_target		= bpf_iter_detach_map,
	.show_fdinfo		= bpf_iter_map_show_fdinfo,
	.fill_link_info		= bpf_iter_map_fill_link_info,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__bpf_map_elem, key),
		  PTR_TO_BUF | PTR_MAYBE_NULL | MEM_RDONLY },
		{ offsetof(struct bpf_iter__bpf_map_elem, value),
		  PTR_TO_BUF | PTR_MAYBE_NULL },
	},
};

static int __init bpf_map_iter_init(void)
{
	int ret;

	bpf_map_reg_info.ctx_arg_info[0].btf_id = *btf_bpf_map_id;
	ret = bpf_iter_reg_target(&bpf_map_reg_info);
	if (ret)
		return ret;

	return bpf_iter_reg_target(&bpf_map_elem_reg_info);
}

late_initcall(bpf_map_iter_init);

__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "Global functions as their definitions will be in vmlinux BTF");

__bpf_kfunc s64 bpf_map_sum_elem_count(struct bpf_map *map)
{
	s64 *pcount;
	s64 ret = 0;
	int cpu;

	if (!map || !map->elem_count)
		return 0;

	for_each_possible_cpu(cpu) {
		pcount = per_cpu_ptr(map->elem_count, cpu);
		ret += READ_ONCE(*pcount);
	}
	return ret;
}

__diag_pop();

BTF_SET8_START(bpf_map_iter_kfunc_ids)
BTF_ID_FLAGS(func, bpf_map_sum_elem_count, KF_TRUSTED_ARGS)
BTF_SET8_END(bpf_map_iter_kfunc_ids)

static const struct btf_kfunc_id_set bpf_map_iter_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &bpf_map_iter_kfunc_ids,
};

static int init_subsystem(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &bpf_map_iter_kfunc_set);
}
late_initcall(init_subsystem);
