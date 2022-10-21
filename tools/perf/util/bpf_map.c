// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#include "util/bpf_map.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

static bool bpf_map__is_per_cpu(enum bpf_map_type type)
{
	return type == BPF_MAP_TYPE_PERCPU_HASH ||
	       type == BPF_MAP_TYPE_PERCPU_ARRAY ||
	       type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
	       type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE;
}

static void *bpf_map__alloc_value(const struct bpf_map *map)
{
	if (bpf_map__is_per_cpu(bpf_map__type(map)))
		return malloc(round_up(bpf_map__value_size(map), 8) *
			      sysconf(_SC_NPROCESSORS_CONF));

	return malloc(bpf_map__value_size(map));
}

int bpf_map__fprintf(struct bpf_map *map, FILE *fp)
{
	void *prev_key = NULL, *key, *value;
	int fd = bpf_map__fd(map), err;
	int printed = 0;

	if (fd < 0)
		return fd;

	if (!map)
		return PTR_ERR(map);

	err = -ENOMEM;
	key = malloc(bpf_map__key_size(map));
	if (key == NULL)
		goto out;

	value = bpf_map__alloc_value(map);
	if (value == NULL)
		goto out_free_key;

	while ((err = bpf_map_get_next_key(fd, prev_key, key) == 0)) {
		int intkey = *(int *)key;

		if (!bpf_map_lookup_elem(fd, key, value)) {
			bool boolval = *(bool *)value;
			if (boolval)
				printed += fprintf(fp, "[%d] = %d,\n", intkey, boolval);
		} else {
			printed += fprintf(fp, "[%d] = ERROR,\n", intkey);
		}

		prev_key = key;
	}

	if (err == ENOENT)
		err = printed;

	free(value);
out_free_key:
	free(key);
out:
	return err;
}
