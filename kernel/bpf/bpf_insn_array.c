// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Isovalent */

#include <linux/bpf.h>

struct bpf_insn_array {
	struct bpf_map map;
	atomic_t used;
	long *ips;
	DECLARE_FLEX_ARRAY(struct bpf_insn_array_value, values);
};

#define cast_insn_array(MAP_PTR) \
	container_of((MAP_PTR), struct bpf_insn_array, map)

#define INSN_DELETED ((u32)-1)

static inline u64 insn_array_alloc_size(u32 max_entries)
{
	const u64 base_size = sizeof(struct bpf_insn_array);
	const u64 entry_size = sizeof(struct bpf_insn_array_value);

	return base_size + max_entries * (entry_size + sizeof(long));
}

static int insn_array_alloc_check(union bpf_attr *attr)
{
	u32 value_size = sizeof(struct bpf_insn_array_value);

	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size != value_size || attr->map_flags != 0)
		return -EINVAL;

	return 0;
}

static void insn_array_free(struct bpf_map *map)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);

	bpf_map_area_free(insn_array);
}

static struct bpf_map *insn_array_alloc(union bpf_attr *attr)
{
	u64 size = insn_array_alloc_size(attr->max_entries);
	struct bpf_insn_array *insn_array;

	insn_array = bpf_map_area_alloc(size, NUMA_NO_NODE);
	if (!insn_array)
		return ERR_PTR(-ENOMEM);

	/* ips are allocated right after the insn_array->values[] array */
	insn_array->ips = (void *)&insn_array->values[attr->max_entries];

	bpf_map_init_from_attr(&insn_array->map, attr);

	/* BPF programs aren't allowed to write to the map */
	insn_array->map.map_flags |= BPF_F_RDONLY_PROG;

	return &insn_array->map;
}

static void *insn_array_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);
	u32 index = *(u32 *)key;

	if (unlikely(index >= insn_array->map.max_entries))
		return NULL;

	return &insn_array->values[index];
}

static long insn_array_update_elem(struct bpf_map *map, void *key, void *value, u64 map_flags)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);
	u32 index = *(u32 *)key;
	struct bpf_insn_array_value val = {};

	if (unlikely(index >= insn_array->map.max_entries))
		return -E2BIG;

	if (unlikely(map_flags & BPF_NOEXIST))
		return -EEXIST;

	copy_map_value(map, &val, value);
	if (val.jitted_off || val.xlated_off)
		return -EINVAL;

	insn_array->values[index].orig_off = val.orig_off;

	return 0;
}

static long insn_array_delete_elem(struct bpf_map *map, void *key)
{
	return -EINVAL;
}

static int insn_array_check_btf(const struct bpf_map *map,
			      const struct btf *btf,
			      const struct btf_type *key_type,
			      const struct btf_type *value_type)
{
	if (!btf_type_is_i32(key_type))
		return -EINVAL;

	if (!btf_type_is_i64(value_type))
		return -EINVAL;

	return 0;
}

static u64 insn_array_mem_usage(const struct bpf_map *map)
{
	return insn_array_alloc_size(map->max_entries);
}

static int insn_array_map_direct_value_addr(const struct bpf_map *map, u64 *imm, u32 off)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);

	if ((off % sizeof(long)) != 0 ||
	    (off / sizeof(long)) >= map->max_entries)
		return -EACCES;

	/* from BPF's point of view, this map is a jump table */
	*imm = (unsigned long)insn_array->ips;

	return 0;
}

BTF_ID_LIST_SINGLE(insn_array_btf_ids, struct, bpf_insn_array)

const struct bpf_map_ops insn_array_map_ops = {
	.map_alloc_check = insn_array_alloc_check,
	.map_alloc = insn_array_alloc,
	.map_free = insn_array_free,
	.map_get_next_key = bpf_array_get_next_key,
	.map_lookup_elem = insn_array_lookup_elem,
	.map_update_elem = insn_array_update_elem,
	.map_delete_elem = insn_array_delete_elem,
	.map_check_btf = insn_array_check_btf,
	.map_mem_usage = insn_array_mem_usage,
	.map_direct_value_addr = insn_array_map_direct_value_addr,
	.map_btf_id = &insn_array_btf_ids[0],
};

static inline bool is_frozen(struct bpf_map *map)
{
	guard(mutex)(&map->freeze_mutex);

	return map->frozen;
}

static bool is_insn_array(const struct bpf_map *map)
{
	return map->map_type == BPF_MAP_TYPE_INSN_ARRAY;
}

static inline bool valid_offsets(const struct bpf_insn_array *insn_array,
				 const struct bpf_prog *prog)
{
	u32 off;
	int i;

	for (i = 0; i < insn_array->map.max_entries; i++) {
		off = insn_array->values[i].orig_off;

		if (off >= prog->len)
			return false;

		if (off > 0) {
			if (prog->insnsi[off-1].code == (BPF_LD | BPF_DW | BPF_IMM))
				return false;
		}
	}

	return true;
}

int bpf_insn_array_init(struct bpf_map *map, const struct bpf_prog *prog)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);
	struct bpf_insn_array_value *values = insn_array->values;
	int i;

	if (!is_frozen(map))
		return -EINVAL;

	if (!valid_offsets(insn_array, prog))
		return -EINVAL;

	/*
	 * There can be only one program using the map
	 */
	if (atomic_xchg(&insn_array->used, 1))
		return -EBUSY;

	/*
	 * Reset all the map indexes to the original values.  This is needed,
	 * e.g., when a replay of verification with different log level should
	 * be performed.
	 */
	for (i = 0; i < map->max_entries; i++)
		values[i].xlated_off = values[i].orig_off;

	return 0;
}

int bpf_insn_array_ready(struct bpf_map *map)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);
	int i;

	for (i = 0; i < map->max_entries; i++) {
		if (insn_array->values[i].xlated_off == INSN_DELETED)
			continue;
		if (!insn_array->ips[i])
			return -EFAULT;
	}

	return 0;
}

void bpf_insn_array_release(struct bpf_map *map)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);

	atomic_set(&insn_array->used, 0);
}

void bpf_insn_array_adjust(struct bpf_map *map, u32 off, u32 len)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);
	int i;

	if (len <= 1)
		return;

	for (i = 0; i < map->max_entries; i++) {
		if (insn_array->values[i].xlated_off <= off)
			continue;
		if (insn_array->values[i].xlated_off == INSN_DELETED)
			continue;
		insn_array->values[i].xlated_off += len - 1;
	}
}

void bpf_insn_array_adjust_after_remove(struct bpf_map *map, u32 off, u32 len)
{
	struct bpf_insn_array *insn_array = cast_insn_array(map);
	int i;

	for (i = 0; i < map->max_entries; i++) {
		if (insn_array->values[i].xlated_off < off)
			continue;
		if (insn_array->values[i].xlated_off == INSN_DELETED)
			continue;
		if (insn_array->values[i].xlated_off < off + len)
			insn_array->values[i].xlated_off = INSN_DELETED;
		else
			insn_array->values[i].xlated_off -= len;
	}
}

/*
 * This function is called by JITs. The image is the real program
 * image, the offsets array set up the xlated -> jitted mapping.
 * The offsets[xlated] offset should point to the beginning of
 * the jitted instruction.
 */
void bpf_prog_update_insn_ptrs(struct bpf_prog *prog, u32 *offsets, void *image)
{
	struct bpf_insn_array *insn_array;
	struct bpf_map *map;
	u32 xlated_off;
	int i, j;

	if (!offsets || !image)
		return;

	for (i = 0; i < prog->aux->used_map_cnt; i++) {
		map = prog->aux->used_maps[i];
		if (!is_insn_array(map))
			continue;

		insn_array = cast_insn_array(map);
		for (j = 0; j < map->max_entries; j++) {
			xlated_off = insn_array->values[j].xlated_off;
			if (xlated_off == INSN_DELETED)
				continue;
			if (xlated_off < prog->aux->subprog_start)
				continue;
			xlated_off -= prog->aux->subprog_start;
			if (xlated_off >= prog->len)
				continue;

			insn_array->values[j].jitted_off = offsets[xlated_off];
			insn_array->ips[j] = (long)(image + offsets[xlated_off]);
		}
	}
}
