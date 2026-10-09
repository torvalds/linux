// SPDX-License-Identifier: GPL-2.0-only
#include <bpf/libbpf_internal.h>
#include "btf_colors.h"

/*
 * Functions in this file mainly exist to refer to functions from libbpf_internal.h,
 * which can't be included in main.c because of the u32 poison and pr_warn macro conflicts.
 */

void btf_sha256(const void *data, size_t len,
		__u8 out[BTF_SHA256_DIGEST_LENGTH])
{
	libbpf_sha256(data, len, out);
}

/*
 * Marks `root_id` and local types reachable from it with `color`.
 * `colors` is indexed by source ID; worklist has room for every local type.
 */
void btf_mark_reachable(struct btf *btf, __u32 root_id, enum btf_color color,
			__u8 *colors, __u32 *worklist)
{
	const struct btf *base = btf__base_btf(btf);
	__u32 start_id = base ? btf__type_cnt(base) : 1;
	__u32 pending = 0;

	if (root_id < start_id || (colors[root_id] & color) == color)
		return;
	colors[root_id] |= color;
	worklist[pending++] = root_id;
	while (pending) {
		const struct btf_type *t = btf__type_by_id(btf, worklist[--pending]);
		struct btf_field_iter it;
		__u32 *id;

		btf_field_iter_init(&it, (struct btf_type *)t, BTF_FIELD_ITER_IDS);
		while ((id = btf_field_iter_next(&it))) {
			if (*id < start_id || (colors[*id] & color) == color)
				continue;
			colors[*id] |= color;
			worklist[pending++] = *id;
		}
	}
}

static int cmp_loc(const void *a, const void *b)
{
	const struct btf_loc *la = a, *lb = b;

	if (la->func != lb->func)
		return la->func < lb->func ? -1 : 1;
	if (la->offset != lb->offset)
		return la->offset < lb->offset ? -1 : 1;
	if (la->loc_proto != lb->loc_proto)
		return la->loc_proto < lb->loc_proto ? -1 : 1;
	return 0;
}

static void remap(struct btf *btf, const __u32 *id_map, __u32 src_start_id)
{
	const struct btf *base = btf__base_btf(btf);
	__u32 start_id = base ? btf__type_cnt(base) : 1;
	__u32 i, type_cnt = btf__type_cnt(btf);

	for (i = start_id; i < type_cnt; i++) {
		struct btf_type *t = (struct btf_type *)btf__type_by_id(btf, i);
		struct btf_field_iter it;
		__u32 *id;

		btf_field_iter_init(&it, t, BTF_FIELD_ITER_IDS);
		while ((id = btf_field_iter_next(&it))) {
			/* Void and types in the original ancestor are unchanged. */
			if (*id >= src_start_id)
				*id = id_map[*id - src_start_id];
		}
		if (btf_is_locsec(t))
			qsort(btf_locsec_locs(t), btf_vlen(t), sizeof(struct btf_loc), cmp_loc);
	}
}

/*
 * Split the `src` into `main_out` base and `inline_out`,
 * according to `colors` array. Relative ordering remains
 * the same as in `src`.
 */
int btf_split_by_color(struct btf *src, const __u8 *colors,
		       struct btf **main_out, struct btf **inline_out)
{
	LIBBPF_OPTS(btf_new_opts, opts,
		    .base_btf = (struct btf *)btf__base_btf(src),
		    .add_layout = btf_header(src)->layout_len != 0,
	);
	struct btf *main_btf, *inline_btf = NULL;
	__u32 start_id = opts.base_btf ? btf__type_cnt(opts.base_btf) : 1;
	__u32 type_cnt = btf__type_cnt(src);
	__u32 *id_map, i;
	int err = -ENOMEM;

	*main_out = NULL;
	*inline_out = NULL;
	id_map = malloc((type_cnt - start_id ?: 1) * sizeof(*id_map));
	if (!id_map)
		return -ENOMEM;
	main_btf = btf__new_empty_opts(&opts);
	if (!main_btf)
		goto out;
	btf__set_endianness(main_btf, btf__endianness(src));
	/* Copy MAIN and SHARED marked types to `main_out`. */
	for (i = start_id; i < type_cnt; i++) {
		if (colors[i] == BTF_COLOR_LOC)
			continue;
		err = btf__add_type(main_btf, src, btf__type_by_id(src, i));
		if (err < 0)
			goto out;
		id_map[i - start_id] = err;
	}
	/*
	 * Copy LOC marked types to `inline_out`.
	 * `main_btf` types count is stable at this point.
	 */
	inline_btf = btf__new_empty_split(main_btf);
	if (!inline_btf) {
		err = -ENOMEM;
		goto out;
	}
	for (i = start_id; i < type_cnt; i++) {
		if (colors[i] != BTF_COLOR_LOC)
			continue;
		err = btf__add_type(inline_btf, src, btf__type_by_id(src, i));
		if (err < 0)
			goto out;
		id_map[i - start_id] = err;
	}
	remap(main_btf, id_map, start_id);
	remap(inline_btf, id_map, start_id);
	*main_out = main_btf;
	*inline_out = inline_btf;
	free(id_map);
	return 0;
out:
	btf__free(inline_btf);
	btf__free(main_btf);
	free(id_map);
	return err;
}
