// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <test_maps.h>

static void map_batch_update(int map_fd, __u32 max_entries, int *keys,
			     int *values)
{
	int i, err;
	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	for (i = 0; i < max_entries; i++) {
		keys[i] = i;
		values[i] = i + 1;
	}

	err = bpf_map_update_batch(map_fd, keys, values, &max_entries, &opts);
	CHECK(err, "bpf_map_update_batch()", "error:%s\n", strerror(errno));
}

static void map_batch_verify(int *visited, __u32 max_entries,
			     int *keys, int *values)
{
	int i;

	memset(visited, 0, max_entries * sizeof(*visited));
	for (i = 0; i < max_entries; i++) {
		CHECK(keys[i] + 1 != values[i], "key/value checking",
		      "error: i %d key %d value %d\n", i, keys[i], values[i]);
		visited[i] = 1;
	}
	for (i = 0; i < max_entries; i++) {
		CHECK(visited[i] != 1, "visited checking",
		      "error: keys array at index %d missing\n", i);
	}
}

void test_array_map_batch_ops(void)
{
	struct bpf_create_map_attr xattr = {
		.name = "array_map",
		.map_type = BPF_MAP_TYPE_ARRAY,
		.key_size = sizeof(int),
		.value_size = sizeof(int),
	};
	int map_fd, *keys, *values, *visited;
	__u32 count, total, total_success;
	const __u32 max_entries = 10;
	__u64 batch = 0;
	int err, step;
	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	xattr.max_entries = max_entries;
	map_fd = bpf_create_map_xattr(&xattr);
	CHECK(map_fd == -1,
	      "bpf_create_map_xattr()", "error:%s\n", strerror(errno));

	keys = malloc(max_entries * sizeof(int));
	values = malloc(max_entries * sizeof(int));
	visited = malloc(max_entries * sizeof(int));
	CHECK(!keys || !values || !visited, "malloc()", "error:%s\n",
	      strerror(errno));

	/* populate elements to the map */
	map_batch_update(map_fd, max_entries, keys, values);

	/* test 1: lookup in a loop with various steps. */
	total_success = 0;
	for (step = 1; step < max_entries; step++) {
		map_batch_update(map_fd, max_entries, keys, values);
		map_batch_verify(visited, max_entries, keys, values);
		memset(keys, 0, max_entries * sizeof(*keys));
		memset(values, 0, max_entries * sizeof(*values));
		batch = 0;
		total = 0;
		/* iteratively lookup/delete elements with 'step'
		 * elements each.
		 */
		count = step;
		while (true) {
			err = bpf_map_lookup_batch(map_fd,
						total ? &batch : NULL, &batch,
						keys + total,
						values + total,
						&count, &opts);

			CHECK((err && errno != ENOENT), "lookup with steps",
			      "error: %s\n", strerror(errno));

			total += count;
			if (err)
				break;

		}

		CHECK(total != max_entries, "lookup with steps",
		      "total = %u, max_entries = %u\n", total, max_entries);

		map_batch_verify(visited, max_entries, keys, values);

		total_success++;
	}

	CHECK(total_success == 0, "check total_success",
	      "unexpected failure\n");

	printf("%s:PASS\n", __func__);

	free(keys);
	free(values);
	free(visited);
}
