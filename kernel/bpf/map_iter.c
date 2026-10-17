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

BTF_ID_LIST_GLOBAL_SINGLE(btf_bpf_map_id, struct, bpf_map)

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

/*
 * For an iterator that hands the live map value to the program as a writable
 * buffer: the verifier knows nothing of the map, so the special fields of the
 * value have to lie outside of what the program may write.
 */
int bpf_iter_map_check_value_write(const struct bpf_prog *prog,
				   const struct bpf_map *map)
{
	const struct btf_record *rec = map->record;
	const struct btf_field *field;
	u32 i;

	if (IS_ERR_OR_NULL(rec) || !prog->aux->max_rdwr_write)
		return 0;

	for (i = 0; i < rec->cnt; i++) {
		field = &rec->fields[i];
		if (field->offset < prog->aux->max_rdwr_write &&
		    field->offset + field->size > prog->aux->min_rdwr_write)
			return -EACCES;
	}

	return 0;
}

static int bpf_iter_map_check_prog(const struct bpf_prog *prog,
				   const struct bpf_iter_aux_info *aux)
{
	const struct bpf_map *map = aux->map;
	u32 value_size = map->value_size;
	bool is_percpu;

	is_percpu = map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
		    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
		    map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY;
	if (is_percpu)
		value_size = round_up(value_size, 8) * num_possible_cpus();

	if (prog->aux->max_rdonly_access > map->key_size ||
	    prog->aux->max_rdwr_access > value_size)
		return -EACCES;

	/* the program gets a copy of a per-cpu value */
	return is_percpu ? 0 : bpf_iter_map_check_value_write(prog, map);
}

static int bpf_iter_attach_map(struct bpf_prog *prog,
			       union bpf_iter_link_info *linfo,
			       struct bpf_iter_aux_info *aux)
{
	struct bpf_map *map;
	int err = -EINVAL;

	if (!linfo->map.map_fd)
		return -EBADF;

	map = bpf_map_get_with_uref(linfo->map.map_fd);
	if (IS_ERR(map))
		return PTR_ERR(map);
	if (map->excl_prog_sha) {
		err = -EPERM;
		goto put_map;
	}

	/* The value ctx arg is writable and aliases the live element. */
	if (btf_record_has_field(map->record, BPF_RCU_HEAD)) {
		err = -EOPNOTSUPP;
		goto put_map;
	}

	if (map->map_type != BPF_MAP_TYPE_PERCPU_HASH &&
	    map->map_type != BPF_MAP_TYPE_LRU_PERCPU_HASH &&
	    map->map_type != BPF_MAP_TYPE_PERCPU_ARRAY &&
	    map->map_type != BPF_MAP_TYPE_HASH &&
	    map->map_type != BPF_MAP_TYPE_LRU_HASH &&
	    map->map_type != BPF_MAP_TYPE_ARRAY &&
	    map->map_type != BPF_MAP_TYPE_RHASH)
		goto put_map;

	aux->map = map;
	err = bpf_iter_map_check_prog(prog, aux);
	if (err)
		goto put_map;

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
	.check_prog		= bpf_iter_map_check_prog,
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

__bpf_kfunc_start_defs();

__bpf_kfunc s64 bpf_map_sum_elem_count(const struct bpf_map *map)
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

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(bpf_map_iter_kfunc_ids)
BTF_ID_FLAGS(func, bpf_map_sum_elem_count)
BTF_KFUNCS_END(bpf_map_iter_kfunc_ids)

static const struct btf_kfunc_id_set bpf_map_iter_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &bpf_map_iter_kfunc_ids,
};

static int init_subsystem(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_UNSPEC, &bpf_map_iter_kfunc_set);
}
late_initcall(init_subsystem);
