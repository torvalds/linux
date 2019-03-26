// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#include "util/bpf_map.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

static bool bpf_map_def__is_per_cpu(const struct bpf_map_def *def)
{
	return def->type == BPF_MAP_TYPE_PERCPU_HASH ||
	       def->type == BPF_MAP_TYPE_PERCPU_ARRAY ||
	       def->type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
	       def->type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE;
}

static void *bpf_map_def__alloc_value(const struct bpf_map_def *def)
{
	if (bpf_map_def__is_per_cpu(def))
		return malloc(round_up(def->value_size, 8) * sysconf(_SC_NPROCESSORS_CONF));

	return malloc(def->value_size);
}

int bpf_map__fprintf(struct bpf_map *map, FILE *fp)
{
	const struct bpf_map_def *def = bpf_map__def(map);
	void *prev_key = NULL, *key, *value;
	int fd = bpf_map__fd(map), err;
	int printed = 0;

	if (fd < 0)
		return fd;

	if (IS_ERR(def))
		return PTR_ERR(def);

	err = -ENOMEM;
	key = malloc(def->key_size);
	if (key == NULL)
		goto out;

	value = bpf_map_def__alloc_value(def);
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
