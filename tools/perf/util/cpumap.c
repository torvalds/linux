// SPDX-License-Identifier: GPL-2.0
#include <api/fs/fs.h>
#include "cpumap.h"
#include "debug.h"
#include "event.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/bitmap.h>
#include "asm/bug.h"

#include <linux/ctype.h>
#include <linux/zalloc.h>
#include <internal/cpumap.h>

static struct perf_cpu max_cpu_num;
static struct perf_cpu max_present_cpu_num;
static int max_node_num;
/**
 * The numa node X as read from /sys/devices/system/node/nodeX indexed by the
 * CPU number.
 */
static int *cpunode_map;

bool perf_record_cpu_map_data__test_bit(int i,
					const struct perf_record_cpu_map_data *data)
{
	int bit_word32 = i / 32;
	__u32 bit_mask32 = 1U << (i & 31);
	int bit_word64 = i / 64;
	__u64 bit_mask64 = ((__u64)1) << (i & 63);

	return (data->mask32_data.long_size == 4)
		? (bit_word32 < data->mask32_data.nr) &&
		(data->mask32_data.mask[bit_word32] & bit_mask32) != 0
		: (bit_word64 < data->mask64_data.nr) &&
		(data->mask64_data.mask[bit_word64] & bit_mask64) != 0;
}

/* Read ith mask value from data into the given 64-bit sized bitmap */
static void perf_record_cpu_map_data__read_one_mask(const struct perf_record_cpu_map_data *data,
						    int i, unsigned long *bitmap)
{
#if __SIZEOF_LONG__ == 8
	if (data->mask32_data.long_size == 4)
		bitmap[0] = data->mask32_data.mask[i];
	else
		bitmap[0] = data->mask64_data.mask[i];
#else
	if (data->mask32_data.long_size == 4) {
		bitmap[0] = data->mask32_data.mask[i];
		bitmap[1] = 0;
	} else {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		bitmap[0] = (unsigned long)(data->mask64_data.mask[i] >> 32);
		bitmap[1] = (unsigned long)data->mask64_data.mask[i];
#else
		bitmap[0] = (unsigned long)data->mask64_data.mask[i];
		bitmap[1] = (unsigned long)(data->mask64_data.mask[i] >> 32);
#endif
	}
#endif
}
static struct perf_cpu_map *cpu_map__from_entries(const struct perf_record_cpu_map_data *data)
{
	struct perf_cpu_map *map;

	map = perf_cpu_map__empty_new(data->cpus_data.nr);
	if (map) {
		unsigned i;

		for (i = 0; i < data->cpus_data.nr; i++) {
			/*
			 * Special treatment for -1, which is not real cpu number,
			 * and we need to use (int) -1 to initialize map[i],
			 * otherwise it would become 65535.
			 */
			if (data->cpus_data.cpu[i] == (u16) -1)
				RC_CHK_ACCESS(map)->map[i].cpu = -1;
			else
				RC_CHK_ACCESS(map)->map[i].cpu = (int) data->cpus_data.cpu[i];
		}
	}

	return map;
}

static struct perf_cpu_map *cpu_map__from_mask(const struct perf_record_cpu_map_data *data)
{
	DECLARE_BITMAP(local_copy, 64);
	int weight = 0, mask_nr = data->mask32_data.nr;
	struct perf_cpu_map *map;

	for (int i = 0; i < mask_nr; i++) {
		perf_record_cpu_map_data__read_one_mask(data, i, local_copy);
		weight += bitmap_weight(local_copy, 64);
	}

	map = perf_cpu_map__empty_new(weight);
	if (!map)
		return NULL;

	for (int i = 0, j = 0; i < mask_nr; i++) {
		int cpus_per_i = (i * data->mask32_data.long_size  * BITS_PER_BYTE);
		int cpu;

		perf_record_cpu_map_data__read_one_mask(data, i, local_copy);
		for_each_set_bit(cpu, local_copy, 64)
			RC_CHK_ACCESS(map)->map[j++].cpu = cpu + cpus_per_i;
	}
	return map;

}

static struct perf_cpu_map *cpu_map__from_range(const struct perf_record_cpu_map_data *data)
{
	struct perf_cpu_map *map;
	unsigned int i = 0;

	map = perf_cpu_map__empty_new(data->range_cpu_data.end_cpu -
				data->range_cpu_data.start_cpu + 1 + data->range_cpu_data.any_cpu);
	if (!map)
		return NULL;

	if (data->range_cpu_data.any_cpu)
		RC_CHK_ACCESS(map)->map[i++].cpu = -1;

	for (int cpu = data->range_cpu_data.start_cpu; cpu <= data->range_cpu_data.end_cpu;
	     i++, cpu++)
		RC_CHK_ACCESS(map)->map[i].cpu = cpu;

	return map;
}

struct perf_cpu_map *cpu_map__new_data(const struct perf_record_cpu_map_data *data)
{
	switch (data->type) {
	case PERF_CPU_MAP__CPUS:
		return cpu_map__from_entries(data);
	case PERF_CPU_MAP__MASK:
		return cpu_map__from_mask(data);
	case PERF_CPU_MAP__RANGE_CPUS:
		return cpu_map__from_range(data);
	default:
		pr_err("cpu_map__new_data unknown type %d\n", data->type);
		return NULL;
	}
}

size_t cpu_map__fprintf(struct perf_cpu_map *map, FILE *fp)
{
#define BUFSIZE 1024
	char buf[BUFSIZE];

	cpu_map__snprint(map, buf, sizeof(buf));
	return fprintf(fp, "%s\n", buf);
#undef BUFSIZE
}

struct perf_cpu_map *perf_cpu_map__empty_new(int nr)
{
	struct perf_cpu_map *cpus = perf_cpu_map__alloc(nr);

	if (cpus != NULL) {
		for (int i = 0; i < nr; i++)
			RC_CHK_ACCESS(cpus)->map[i].cpu = -1;
	}

	return cpus;
}

struct cpu_aggr_map *cpu_aggr_map__empty_new(int nr)
{
	struct cpu_aggr_map *cpus = malloc(sizeof(*cpus) + sizeof(struct aggr_cpu_id) * nr);

	if (cpus != NULL) {
		int i;

		cpus->nr = nr;
		for (i = 0; i < nr; i++)
			cpus->map[i] = aggr_cpu_id__empty();

		refcount_set(&cpus->refcnt, 1);
	}

	return cpus;
}

static int cpu__get_topology_int(int cpu, const char *name, int *value)
{
	char path[PATH_MAX];

	snprintf(path, PATH_MAX,
		"devices/system/cpu/cpu%d/topology/%s", cpu, name);

	return sysfs__read_int(path, value);
}

int cpu__get_socket_id(struct perf_cpu cpu)
{
	int value, ret = cpu__get_topology_int(cpu.cpu, "physical_package_id", &value);
	return ret ?: value;
}

struct aggr_cpu_id aggr_cpu_id__socket(struct perf_cpu cpu, void *data __maybe_unused)
{
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	id.socket = cpu__get_socket_id(cpu);
	return id;
}

static int aggr_cpu_id__cmp(const void *a_pointer, const void *b_pointer)
{
	struct aggr_cpu_id *a = (struct aggr_cpu_id *)a_pointer;
	struct aggr_cpu_id *b = (struct aggr_cpu_id *)b_pointer;

	if (a->node != b->node)
		return a->node - b->node;
	else if (a->socket != b->socket)
		return a->socket - b->socket;
	else if (a->die != b->die)
		return a->die - b->die;
	else if (a->core != b->core)
		return a->core - b->core;
	else
		return a->thread_idx - b->thread_idx;
}

struct cpu_aggr_map *cpu_aggr_map__new(const struct perf_cpu_map *cpus,
				       aggr_cpu_id_get_t get_id,
				       void *data, bool needs_sort)
{
	int idx;
	struct perf_cpu cpu;
	struct cpu_aggr_map *c = cpu_aggr_map__empty_new(perf_cpu_map__nr(cpus));

	if (!c)
		return NULL;

	/* Reset size as it may only be partially filled */
	c->nr = 0;

	perf_cpu_map__for_each_cpu(cpu, idx, cpus) {
		bool duplicate = false;
		struct aggr_cpu_id cpu_id = get_id(cpu, data);

		for (int j = 0; j < c->nr; j++) {
			if (aggr_cpu_id__equal(&cpu_id, &c->map[j])) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			c->map[c->nr] = cpu_id;
			c->nr++;
		}
	}
	/* Trim. */
	if (c->nr != perf_cpu_map__nr(cpus)) {
		struct cpu_aggr_map *trimmed_c =
			realloc(c,
				sizeof(struct cpu_aggr_map) + sizeof(struct aggr_cpu_id) * c->nr);

		if (trimmed_c)
			c = trimmed_c;
	}

	/* ensure we process id in increasing order */
	if (needs_sort)
		qsort(c->map, c->nr, sizeof(struct aggr_cpu_id), aggr_cpu_id__cmp);

	return c;

}

int cpu__get_die_id(struct perf_cpu cpu)
{
	int value, ret = cpu__get_topology_int(cpu.cpu, "die_id", &value);

	return ret ?: value;
}

struct aggr_cpu_id aggr_cpu_id__die(struct perf_cpu cpu, void *data)
{
	struct aggr_cpu_id id;
	int die;

	die = cpu__get_die_id(cpu);
	/* There is no die_id on legacy system. */
	if (die == -1)
		die = 0;

	/*
	 * die_id is relative to socket, so start
	 * with the socket ID and then add die to
	 * make a unique ID.
	 */
	id = aggr_cpu_id__socket(cpu, data);
	if (aggr_cpu_id__is_empty(&id))
		return id;

	id.die = die;
	return id;
}

int cpu__get_core_id(struct perf_cpu cpu)
{
	int value, ret = cpu__get_topology_int(cpu.cpu, "core_id", &value);
	return ret ?: value;
}

struct aggr_cpu_id aggr_cpu_id__core(struct perf_cpu cpu, void *data)
{
	struct aggr_cpu_id id;
	int core = cpu__get_core_id(cpu);

	/* aggr_cpu_id__die returns a struct with socket and die set. */
	id = aggr_cpu_id__die(cpu, data);
	if (aggr_cpu_id__is_empty(&id))
		return id;

	/*
	 * core_id is relative to socket and die, we need a global id.
	 * So we combine the result from cpu_map__get_die with the core id
	 */
	id.core = core;
	return id;

}

struct aggr_cpu_id aggr_cpu_id__cpu(struct perf_cpu cpu, void *data)
{
	struct aggr_cpu_id id;

	/* aggr_cpu_id__core returns a struct with socket, die and core set. */
	id = aggr_cpu_id__core(cpu, data);
	if (aggr_cpu_id__is_empty(&id))
		return id;

	id.cpu = cpu;
	return id;

}

struct aggr_cpu_id aggr_cpu_id__node(struct perf_cpu cpu, void *data __maybe_unused)
{
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	id.node = cpu__get_node(cpu);
	return id;
}

struct aggr_cpu_id aggr_cpu_id__global(struct perf_cpu cpu, void *data __maybe_unused)
{
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	/* it always aggregates to the cpu 0 */
	cpu.cpu = 0;
	id.cpu = cpu;
	return id;
}

/* setup simple routines to easily access node numbers given a cpu number */
static int get_max_num(char *path, int *max)
{
	size_t num;
	char *buf;
	int err = 0;

	if (filename__read_str(path, &buf, &num))
		return -1;

	buf[num] = '\0';

	/* start on the right, to find highest node num */
	while (--num) {
		if ((buf[num] == ',') || (buf[num] == '-')) {
			num++;
			break;
		}
	}
	if (sscanf(&buf[num], "%d", max) < 1) {
		err = -1;
		goto out;
	}

	/* convert from 0-based to 1-based */
	(*max)++;

out:
	free(buf);
	return err;
}

/* Determine highest possible cpu in the system for sparse allocation */
static void set_max_cpu_num(void)
{
	const char *mnt;
	char path[PATH_MAX];
	int ret = -1;

	/* set up default */
	max_cpu_num.cpu = 4096;
	max_present_cpu_num.cpu = 4096;

	mnt = sysfs__mountpoint();
	if (!mnt)
		goto out;

	/* get the highest possible cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/cpu/possible", mnt);
	if (ret >= PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_cpu_num.cpu);
	if (ret)
		goto out;

	/* get the highest present cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/cpu/present", mnt);
	if (ret >= PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_present_cpu_num.cpu);

out:
	if (ret)
		pr_err("Failed to read max cpus, using default of %d\n", max_cpu_num.cpu);
}

/* Determine highest possible node in the system for sparse allocation */
static void set_max_node_num(void)
{
	const char *mnt;
	char path[PATH_MAX];
	int ret = -1;

	/* set up default */
	max_node_num = 8;

	mnt = sysfs__mountpoint();
	if (!mnt)
		goto out;

	/* get the highest possible cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/node/possible", mnt);
	if (ret >= PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_node_num);

out:
	if (ret)
		pr_err("Failed to read max nodes, using default of %d\n", max_node_num);
}

int cpu__max_node(void)
{
	if (unlikely(!max_node_num))
		set_max_node_num();

	return max_node_num;
}

struct perf_cpu cpu__max_cpu(void)
{
	if (unlikely(!max_cpu_num.cpu))
		set_max_cpu_num();

	return max_cpu_num;
}

struct perf_cpu cpu__max_present_cpu(void)
{
	if (unlikely(!max_present_cpu_num.cpu))
		set_max_cpu_num();

	return max_present_cpu_num;
}


int cpu__get_node(struct perf_cpu cpu)
{
	if (unlikely(cpunode_map == NULL)) {
		pr_debug("cpu_map not initialized\n");
		return -1;
	}

	return cpunode_map[cpu.cpu];
}

static int init_cpunode_map(void)
{
	int i;

	set_max_cpu_num();
	set_max_node_num();

	cpunode_map = calloc(max_cpu_num.cpu, sizeof(int));
	if (!cpunode_map) {
		pr_err("%s: calloc failed\n", __func__);
		return -1;
	}

	for (i = 0; i < max_cpu_num.cpu; i++)
		cpunode_map[i] = -1;

	return 0;
}

int cpu__setup_cpunode_map(void)
{
	struct dirent *dent1, *dent2;
	DIR *dir1, *dir2;
	unsigned int cpu, mem;
	char buf[PATH_MAX];
	char path[PATH_MAX];
	const char *mnt;
	int n;

	/* initialize globals */
	if (init_cpunode_map())
		return -1;

	mnt = sysfs__mountpoint();
	if (!mnt)
		return 0;

	n = snprintf(path, PATH_MAX, "%s/devices/system/node", mnt);
	if (n >= PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		return -1;
	}

	dir1 = opendir(path);
	if (!dir1)
		return 0;

	/* walk tree and setup map */
	while ((dent1 = readdir(dir1)) != NULL) {
		if (dent1->d_type != DT_DIR || sscanf(dent1->d_name, "node%u", &mem) < 1)
			continue;

		n = snprintf(buf, PATH_MAX, "%s/%s", path, dent1->d_name);
		if (n >= PATH_MAX) {
			pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
			continue;
		}

		dir2 = opendir(buf);
		if (!dir2)
			continue;
		while ((dent2 = readdir(dir2)) != NULL) {
			if (dent2->d_type != DT_LNK || sscanf(dent2->d_name, "cpu%u", &cpu) < 1)
				continue;
			cpunode_map[cpu] = mem;
		}
		closedir(dir2);
	}
	closedir(dir1);
	return 0;
}

size_t cpu_map__snprint(struct perf_cpu_map *map, char *buf, size_t size)
{
	int i, start = -1;
	bool first = true;
	size_t ret = 0;

#define COMMA first ? "" : ","

	for (i = 0; i < perf_cpu_map__nr(map) + 1; i++) {
		struct perf_cpu cpu = { .cpu = INT_MAX };
		bool last = i == perf_cpu_map__nr(map);

		if (!last)
			cpu = perf_cpu_map__cpu(map, i);

		if (start == -1) {
			start = i;
			if (last) {
				ret += snprintf(buf + ret, size - ret,
						"%s%d", COMMA,
						perf_cpu_map__cpu(map, i).cpu);
			}
		} else if (((i - start) != (cpu.cpu - perf_cpu_map__cpu(map, start).cpu)) || last) {
			int end = i - 1;

			if (start == end) {
				ret += snprintf(buf + ret, size - ret,
						"%s%d", COMMA,
						perf_cpu_map__cpu(map, start).cpu);
			} else {
				ret += snprintf(buf + ret, size - ret,
						"%s%d-%d", COMMA,
						perf_cpu_map__cpu(map, start).cpu, perf_cpu_map__cpu(map, end).cpu);
			}
			first = false;
			start = i;
		}
	}

#undef COMMA

	pr_debug2("cpumask list: %s\n", buf);
	return ret;
}

static char hex_char(unsigned char val)
{
	if (val < 10)
		return val + '0';
	if (val < 16)
		return val - 10 + 'a';
	return '?';
}

size_t cpu_map__snprint_mask(struct perf_cpu_map *map, char *buf, size_t size)
{
	int i, cpu;
	char *ptr = buf;
	unsigned char *bitmap;
	struct perf_cpu last_cpu = perf_cpu_map__cpu(map, perf_cpu_map__nr(map) - 1);

	if (buf == NULL)
		return 0;

	bitmap = zalloc(last_cpu.cpu / 8 + 1);
	if (bitmap == NULL) {
		buf[0] = '\0';
		return 0;
	}

	for (i = 0; i < perf_cpu_map__nr(map); i++) {
		cpu = perf_cpu_map__cpu(map, i).cpu;
		bitmap[cpu / 8] |= 1 << (cpu % 8);
	}

	for (cpu = last_cpu.cpu / 4 * 4; cpu >= 0; cpu -= 4) {
		unsigned char bits = bitmap[cpu / 8];

		if (cpu % 8)
			bits >>= 4;
		else
			bits &= 0xf;

		*ptr++ = hex_char(bits);
		if ((cpu % 32) == 0 && cpu > 0)
			*ptr++ = ',';
	}
	*ptr = '\0';
	free(bitmap);

	buf[size - 1] = '\0';
	return ptr - buf;
}

const struct perf_cpu_map *cpu_map__online(void) /* thread unsafe */
{
	static const struct perf_cpu_map *online = NULL;

	if (!online)
		online = perf_cpu_map__new(NULL); /* from /sys/devices/system/cpu/online */

	return online;
}

bool aggr_cpu_id__equal(const struct aggr_cpu_id *a, const struct aggr_cpu_id *b)
{
	return a->thread_idx == b->thread_idx &&
		a->node == b->node &&
		a->socket == b->socket &&
		a->die == b->die &&
		a->core == b->core &&
		a->cpu.cpu == b->cpu.cpu;
}

bool aggr_cpu_id__is_empty(const struct aggr_cpu_id *a)
{
	return a->thread_idx == -1 &&
		a->node == -1 &&
		a->socket == -1 &&
		a->die == -1 &&
		a->core == -1 &&
		a->cpu.cpu == -1;
}

struct aggr_cpu_id aggr_cpu_id__empty(void)
{
	struct aggr_cpu_id ret = {
		.thread_idx = -1,
		.node = -1,
		.socket = -1,
		.die = -1,
		.core = -1,
		.cpu = (struct perf_cpu){ .cpu = -1 },
	};
	return ret;
}
