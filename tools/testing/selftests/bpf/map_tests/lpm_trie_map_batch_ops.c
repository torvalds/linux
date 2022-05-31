// SPDX-License-Identifier: GPL-2.0

#include <arpa/inet.h>
#include <linux/bpf.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <test_maps.h>

struct test_lpm_key {
	__u32 prefix;
	struct in_addr ipv4;
};

static void map_batch_update(int map_fd, __u32 max_entries,
			     struct test_lpm_key *keys, int *values)
{
	__u32 i;
	int err;
	char buff[16] = { 0 };
	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	for (i = 0; i < max_entries; i++) {
		keys[i].prefix = 32;
		snprintf(buff, 16, "192.168.1.%d", i + 1);
		inet_pton(AF_INET, buff, &keys[i].ipv4);
		values[i] = i + 1;
	}

	err = bpf_map_update_batch(map_fd, keys, values, &max_entries, &opts);
	CHECK(err, "bpf_map_update_batch()", "error:%s\n", strerror(errno));
}

static void map_batch_verify(int *visited, __u32 max_entries,
			     struct test_lpm_key *keys, int *values)
{
	char buff[16] = { 0 };
	int lower_byte = 0;
	__u32 i;

	memset(visited, 0, max_entries * sizeof(*visited));
	for (i = 0; i < max_entries; i++) {
		inet_ntop(AF_INET, &keys[i].ipv4, buff, 32);
		CHECK(sscanf(buff, "192.168.1.%d", &lower_byte) == EOF,
		      "sscanf()", "error: i %d\n", i);
		CHECK(lower_byte != values[i], "key/value checking",
		      "error: i %d key %s value %d\n", i, buff, values[i]);
		visited[i] = 1;
	}
	for (i = 0; i < max_entries; i++) {
		CHECK(visited[i] != 1, "visited checking",
		      "error: keys array at index %d missing\n", i);
	}
}

void test_lpm_trie_map_batch_ops(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, create_opts, .map_flags = BPF_F_NO_PREALLOC);
	struct test_lpm_key *keys, key;
	int map_fd, *values, *visited;
	__u32 step, count, total, total_success;
	const __u32 max_entries = 10;
	__u64 batch = 0;
	int err;
	DECLARE_LIBBPF_OPTS(bpf_map_batch_opts, opts,
		.elem_flags = 0,
		.flags = 0,
	);

	map_fd = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, "lpm_trie_map",
				sizeof(struct test_lpm_key), sizeof(int),
				max_entries, &create_opts);
	CHECK(map_fd == -1, "bpf_map_create()", "error:%s\n",
	      strerror(errno));

	keys = malloc(max_entries * sizeof(struct test_lpm_key));
	values = malloc(max_entries * sizeof(int));
	visited = malloc(max_entries * sizeof(int));
	CHECK(!keys || !values || !visited, "malloc()", "error:%s\n",
	      strerror(errno));

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
				keys + total, values + total, &count, &opts);

			CHECK((err && errno != ENOENT), "lookup with steps",
			      "error: %s\n", strerror(errno));

			total += count;
			if (err)
				break;
		}

		CHECK(total != max_entries, "lookup with steps",
		      "total = %u, max_entries = %u\n", total, max_entries);

		map_batch_verify(visited, max_entries, keys, values);

		total = 0;
		count = step;
		while (total < max_entries) {
			if (max_entries - total < step)
				count = max_entries - total;
			err = bpf_map_delete_batch(map_fd, keys + total, &count,
						   &opts);
			CHECK((err && errno != ENOENT), "delete batch",
			      "error: %s\n", strerror(errno));
			total += count;
			if (err)
				break;
		}
		CHECK(total != max_entries, "delete with steps",
		      "total = %u, max_entries = %u\n", total, max_entries);

		/* check map is empty, errono == ENOENT */
		err = bpf_map_get_next_key(map_fd, NULL, &key);
		CHECK(!err || errno != ENOENT, "bpf_map_get_next_key()",
		      "error: %s\n", strerror(errno));

		total_success++;
	}

	CHECK(total_success == 0, "check total_success",
	      "unexpected failure\n");

	printf("%s:PASS\n", __func__);

	free(keys);
	free(values);
	free(visited);
}
