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

static int max_cpu_num;
static int max_present_cpu_num;
static int max_node_num;
static int *cpunode_map;

static struct perf_cpu_map *cpu_map__from_entries(struct cpu_map_entries *cpus)
{
	struct perf_cpu_map *map;

	map = perf_cpu_map__empty_new(cpus->nr);
	if (map) {
		unsigned i;

		for (i = 0; i < cpus->nr; i++) {
			/*
			 * Special treatment for -1, which is not real cpu number,
			 * and we need to use (int) -1 to initialize map[i],
			 * otherwise it would become 65535.
			 */
			if (cpus->cpu[i] == (u16) -1)
				map->map[i] = -1;
			else
				map->map[i] = (int) cpus->cpu[i];
		}
	}

	return map;
}

static struct perf_cpu_map *cpu_map__from_mask(struct perf_record_record_cpu_map *mask)
{
	struct perf_cpu_map *map;
	int nr, nbits = mask->nr * mask->long_size * BITS_PER_BYTE;

	nr = bitmap_weight(mask->mask, nbits);

	map = perf_cpu_map__empty_new(nr);
	if (map) {
		int cpu, i = 0;

		for_each_set_bit(cpu, mask->mask, nbits)
			map->map[i++] = cpu;
	}
	return map;

}

struct perf_cpu_map *cpu_map__new_data(struct perf_record_cpu_map_data *data)
{
	if (data->type == PERF_CPU_MAP__CPUS)
		return cpu_map__from_entries((struct cpu_map_entries *)data->data);
	else
		return cpu_map__from_mask((struct perf_record_record_cpu_map *)data->data);
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
	struct perf_cpu_map *cpus = malloc(sizeof(*cpus) + sizeof(int) * nr);

	if (cpus != NULL) {
		int i;

		cpus->nr = nr;
		for (i = 0; i < nr; i++)
			cpus->map[i] = -1;

		refcount_set(&cpus->refcnt, 1);
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
			cpus->map[i] = cpu_map__empty_aggr_cpu_id();

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

int cpu_map__get_socket_id(int cpu)
{
	int value, ret = cpu__get_topology_int(cpu, "physical_package_id", &value);
	return ret ?: value;
}

struct aggr_cpu_id cpu_map__get_socket(struct perf_cpu_map *map, int idx,
					void *data __maybe_unused)
{
	int cpu;
	struct aggr_cpu_id id = cpu_map__empty_aggr_cpu_id();

	if (idx > map->nr)
		return id;

	cpu = map->map[idx];

	id.socket = cpu_map__get_socket_id(cpu);
	return id;
}

static int cmp_aggr_cpu_id(const void *a_pointer, const void *b_pointer)
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
		return a->thread - b->thread;
}

int cpu_map__build_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **res,
		       struct aggr_cpu_id (*f)(struct perf_cpu_map *map, int cpu, void *data),
		       void *data)
{
	int nr = cpus->nr;
	struct cpu_aggr_map *c = cpu_aggr_map__empty_new(nr);
	int cpu, s2;
	struct aggr_cpu_id s1;

	if (!c)
		return -1;

	/* Reset size as it may only be partially filled */
	c->nr = 0;

	for (cpu = 0; cpu < nr; cpu++) {
		s1 = f(cpus, cpu, data);
		for (s2 = 0; s2 < c->nr; s2++) {
			if (cpu_map__compare_aggr_cpu_id(s1, c->map[s2]))
				break;
		}
		if (s2 == c->nr) {
			c->map[c->nr] = s1;
			c->nr++;
		}
	}
	/* ensure we process id in increasing order */
	qsort(c->map, c->nr, sizeof(struct aggr_cpu_id), cmp_aggr_cpu_id);

	*res = c;
	return 0;
}

int cpu_map__get_die_id(int cpu)
{
	int value, ret = cpu__get_topology_int(cpu, "die_id", &value);

	return ret ?: value;
}

struct aggr_cpu_id cpu_map__get_die(struct perf_cpu_map *map, int idx, void *data)
{
	int cpu, die;
	struct aggr_cpu_id id = cpu_map__empty_aggr_cpu_id();

	if (idx > map->nr)
		return id;

	cpu = map->map[idx];

	die = cpu_map__get_die_id(cpu);
	/* There is no die_id on legacy system. */
	if (die == -1)
		die = 0;

	/*
	 * die_id is relative to socket, so start
	 * with the socket ID and then add die to
	 * make a unique ID.
	 */
	id = cpu_map__get_socket(map, idx, data);
	if (cpu_map__aggr_cpu_id_is_empty(id))
		return id;

	id.die = die;
	return id;
}

int cpu_map__get_core_id(int cpu)
{
	int value, ret = cpu__get_topology_int(cpu, "core_id", &value);
	return ret ?: value;
}

int cpu_map__get_node_id(int cpu)
{
	return cpu__get_node(cpu);
}

struct aggr_cpu_id cpu_map__get_core(struct perf_cpu_map *map, int idx, void *data)
{
	int cpu;
	struct aggr_cpu_id id = cpu_map__empty_aggr_cpu_id();

	if (idx > map->nr)
		return id;

	cpu = map->map[idx];

	cpu = cpu_map__get_core_id(cpu);

	/* cpu_map__get_die returns a struct with socket and die set*/
	id = cpu_map__get_die(map, idx, data);
	if (cpu_map__aggr_cpu_id_is_empty(id))
		return id;

	/*
	 * core_id is relative to socket and die, we need a global id.
	 * So we combine the result from cpu_map__get_die with the core id
	 */
	id.core = cpu;
	return id;
}

struct aggr_cpu_id cpu_map__get_node(struct perf_cpu_map *map, int idx, void *data __maybe_unused)
{
	struct aggr_cpu_id id = cpu_map__empty_aggr_cpu_id();

	if (idx < 0 || idx >= map->nr)
		return id;

	id.node = cpu_map__get_node_id(map->map[idx]);
	return id;
}

int cpu_map__build_socket_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **sockp)
{
	return cpu_map__build_map(cpus, sockp, cpu_map__get_socket, NULL);
}

int cpu_map__build_die_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **diep)
{
	return cpu_map__build_map(cpus, diep, cpu_map__get_die, NULL);
}

int cpu_map__build_core_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **corep)
{
	return cpu_map__build_map(cpus, corep, cpu_map__get_core, NULL);
}

int cpu_map__build_node_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **numap)
{
	return cpu_map__build_map(cpus, numap, cpu_map__get_node, NULL);
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
	max_cpu_num = 4096;
	max_present_cpu_num = 4096;

	mnt = sysfs__mountpoint();
	if (!mnt)
		goto out;

	/* get the highest possible cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/cpu/possible", mnt);
	if (ret >= PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_cpu_num);
	if (ret)
		goto out;

	/* get the highest present cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/cpu/present", mnt);
	if (ret >= PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_present_cpu_num);

out:
	if (ret)
		pr_err("Failed to read max cpus, using default of %d\n", max_cpu_num);
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

int cpu__max_cpu(void)
{
	if (unlikely(!max_cpu_num))
		set_max_cpu_num();

	return max_cpu_num;
}

int cpu__max_present_cpu(void)
{
	if (unlikely(!max_present_cpu_num))
		set_max_cpu_num();

	return max_present_cpu_num;
}


int cpu__get_node(int cpu)
{
	if (unlikely(cpunode_map == NULL)) {
		pr_debug("cpu_map not initialized\n");
		return -1;
	}

	return cpunode_map[cpu];
}

static int init_cpunode_map(void)
{
	int i;

	set_max_cpu_num();
	set_max_node_num();

	cpunode_map = calloc(max_cpu_num, sizeof(int));
	if (!cpunode_map) {
		pr_err("%s: calloc failed\n", __func__);
		return -1;
	}

	for (i = 0; i < max_cpu_num; i++)
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

bool cpu_map__has(struct perf_cpu_map *cpus, int cpu)
{
	return perf_cpu_map__idx(cpus, cpu) != -1;
}

int cpu_map__cpu(struct perf_cpu_map *cpus, int idx)
{
	return cpus->map[idx];
}

size_t cpu_map__snprint(struct perf_cpu_map *map, char *buf, size_t size)
{
	int i, cpu, start = -1;
	bool first = true;
	size_t ret = 0;

#define COMMA first ? "" : ","

	for (i = 0; i < map->nr + 1; i++) {
		bool last = i == map->nr;

		cpu = last ? INT_MAX : map->map[i];

		if (start == -1) {
			start = i;
			if (last) {
				ret += snprintf(buf + ret, size - ret,
						"%s%d", COMMA,
						map->map[i]);
			}
		} else if (((i - start) != (cpu - map->map[start])) || last) {
			int end = i - 1;

			if (start == end) {
				ret += snprintf(buf + ret, size - ret,
						"%s%d", COMMA,
						map->map[start]);
			} else {
				ret += snprintf(buf + ret, size - ret,
						"%s%d-%d", COMMA,
						map->map[start], map->map[end]);
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
	int last_cpu = cpu_map__cpu(map, map->nr - 1);

	if (buf == NULL)
		return 0;

	bitmap = zalloc(last_cpu / 8 + 1);
	if (bitmap == NULL) {
		buf[0] = '\0';
		return 0;
	}

	for (i = 0; i < map->nr; i++) {
		cpu = cpu_map__cpu(map, i);
		bitmap[cpu / 8] |= 1 << (cpu % 8);
	}

	for (cpu = last_cpu / 4 * 4; cpu >= 0; cpu -= 4) {
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

bool cpu_map__compare_aggr_cpu_id(struct aggr_cpu_id a, struct aggr_cpu_id b)
{
	return a.thread == b.thread &&
		a.node == b.node &&
		a.socket == b.socket &&
		a.die == b.die &&
		a.core == b.core;
}

bool cpu_map__aggr_cpu_id_is_empty(struct aggr_cpu_id a)
{
	return a.thread == -1 &&
		a.node == -1 &&
		a.socket == -1 &&
		a.die == -1 &&
		a.core == -1;
}

struct aggr_cpu_id cpu_map__empty_aggr_cpu_id(void)
{
	struct aggr_cpu_id ret = {
		.thread = -1,
		.node = -1,
		.socket = -1,
		.die = -1,
		.core = -1
	};
	return ret;
}
