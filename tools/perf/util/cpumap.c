// SPDX-License-Identifier: GPL-2.0
#include "util.h"
#include <api/fs/fs.h>
#include "../perf.h"
#include "cpumap.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/bitmap.h>
#include "asm/bug.h"

#include "sane_ctype.h"

static int max_cpu_num;
static int max_present_cpu_num;
static int max_node_num;
static int *cpunode_map;

static struct cpu_map *cpu_map__default_new(void)
{
	struct cpu_map *cpus;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (nr_cpus < 0)
		return NULL;

	cpus = malloc(sizeof(*cpus) + nr_cpus * sizeof(int));
	if (cpus != NULL) {
		int i;
		for (i = 0; i < nr_cpus; ++i)
			cpus->map[i] = i;

		cpus->nr = nr_cpus;
		refcount_set(&cpus->refcnt, 1);
	}

	return cpus;
}

static struct cpu_map *cpu_map__trim_new(int nr_cpus, int *tmp_cpus)
{
	size_t payload_size = nr_cpus * sizeof(int);
	struct cpu_map *cpus = malloc(sizeof(*cpus) + payload_size);

	if (cpus != NULL) {
		cpus->nr = nr_cpus;
		memcpy(cpus->map, tmp_cpus, payload_size);
		refcount_set(&cpus->refcnt, 1);
	}

	return cpus;
}

struct cpu_map *cpu_map__read(FILE *file)
{
	struct cpu_map *cpus = NULL;
	int nr_cpus = 0;
	int *tmp_cpus = NULL, *tmp;
	int max_entries = 0;
	int n, cpu, prev;
	char sep;

	sep = 0;
	prev = -1;
	for (;;) {
		n = fscanf(file, "%u%c", &cpu, &sep);
		if (n <= 0)
			break;
		if (prev >= 0) {
			int new_max = nr_cpus + cpu - prev - 1;

			if (new_max >= max_entries) {
				max_entries = new_max + MAX_NR_CPUS / 2;
				tmp = realloc(tmp_cpus, max_entries * sizeof(int));
				if (tmp == NULL)
					goto out_free_tmp;
				tmp_cpus = tmp;
			}

			while (++prev < cpu)
				tmp_cpus[nr_cpus++] = prev;
		}
		if (nr_cpus == max_entries) {
			max_entries += MAX_NR_CPUS;
			tmp = realloc(tmp_cpus, max_entries * sizeof(int));
			if (tmp == NULL)
				goto out_free_tmp;
			tmp_cpus = tmp;
		}

		tmp_cpus[nr_cpus++] = cpu;
		if (n == 2 && sep == '-')
			prev = cpu;
		else
			prev = -1;
		if (n == 1 || sep == '\n')
			break;
	}

	if (nr_cpus > 0)
		cpus = cpu_map__trim_new(nr_cpus, tmp_cpus);
	else
		cpus = cpu_map__default_new();
out_free_tmp:
	free(tmp_cpus);
	return cpus;
}

static struct cpu_map *cpu_map__read_all_cpu_map(void)
{
	struct cpu_map *cpus = NULL;
	FILE *onlnf;

	onlnf = fopen("/sys/devices/system/cpu/online", "r");
	if (!onlnf)
		return cpu_map__default_new();

	cpus = cpu_map__read(onlnf);
	fclose(onlnf);
	return cpus;
}

struct cpu_map *cpu_map__new(const char *cpu_list)
{
	struct cpu_map *cpus = NULL;
	unsigned long start_cpu, end_cpu = 0;
	char *p = NULL;
	int i, nr_cpus = 0;
	int *tmp_cpus = NULL, *tmp;
	int max_entries = 0;

	if (!cpu_list)
		return cpu_map__read_all_cpu_map();

	/*
	 * must handle the case of empty cpumap to cover
	 * TOPOLOGY header for NUMA nodes with no CPU
	 * ( e.g., because of CPU hotplug)
	 */
	if (!isdigit(*cpu_list) && *cpu_list != '\0')
		goto out;

	while (isdigit(*cpu_list)) {
		p = NULL;
		start_cpu = strtoul(cpu_list, &p, 0);
		if (start_cpu >= INT_MAX
		    || (*p != '\0' && *p != ',' && *p != '-'))
			goto invalid;

		if (*p == '-') {
			cpu_list = ++p;
			p = NULL;
			end_cpu = strtoul(cpu_list, &p, 0);

			if (end_cpu >= INT_MAX || (*p != '\0' && *p != ','))
				goto invalid;

			if (end_cpu < start_cpu)
				goto invalid;
		} else {
			end_cpu = start_cpu;
		}

		for (; start_cpu <= end_cpu; start_cpu++) {
			/* check for duplicates */
			for (i = 0; i < nr_cpus; i++)
				if (tmp_cpus[i] == (int)start_cpu)
					goto invalid;

			if (nr_cpus == max_entries) {
				max_entries += MAX_NR_CPUS;
				tmp = realloc(tmp_cpus, max_entries * sizeof(int));
				if (tmp == NULL)
					goto invalid;
				tmp_cpus = tmp;
			}
			tmp_cpus[nr_cpus++] = (int)start_cpu;
		}
		if (*p)
			++p;

		cpu_list = p;
	}

	if (nr_cpus > 0)
		cpus = cpu_map__trim_new(nr_cpus, tmp_cpus);
	else if (*cpu_list != '\0')
		cpus = cpu_map__default_new();
	else
		cpus = cpu_map__dummy_new();
invalid:
	free(tmp_cpus);
out:
	return cpus;
}

static struct cpu_map *cpu_map__from_entries(struct cpu_map_entries *cpus)
{
	struct cpu_map *map;

	map = cpu_map__empty_new(cpus->nr);
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

static struct cpu_map *cpu_map__from_mask(struct cpu_map_mask *mask)
{
	struct cpu_map *map;
	int nr, nbits = mask->nr * mask->long_size * BITS_PER_BYTE;

	nr = bitmap_weight(mask->mask, nbits);

	map = cpu_map__empty_new(nr);
	if (map) {
		int cpu, i = 0;

		for_each_set_bit(cpu, mask->mask, nbits)
			map->map[i++] = cpu;
	}
	return map;

}

struct cpu_map *cpu_map__new_data(struct cpu_map_data *data)
{
	if (data->type == PERF_CPU_MAP__CPUS)
		return cpu_map__from_entries((struct cpu_map_entries *)data->data);
	else
		return cpu_map__from_mask((struct cpu_map_mask *)data->data);
}

size_t cpu_map__fprintf(struct cpu_map *map, FILE *fp)
{
#define BUFSIZE 1024
	char buf[BUFSIZE];

	cpu_map__snprint(map, buf, sizeof(buf));
	return fprintf(fp, "%s\n", buf);
#undef BUFSIZE
}

struct cpu_map *cpu_map__dummy_new(void)
{
	struct cpu_map *cpus = malloc(sizeof(*cpus) + sizeof(int));

	if (cpus != NULL) {
		cpus->nr = 1;
		cpus->map[0] = -1;
		refcount_set(&cpus->refcnt, 1);
	}

	return cpus;
}

struct cpu_map *cpu_map__empty_new(int nr)
{
	struct cpu_map *cpus = malloc(sizeof(*cpus) + sizeof(int) * nr);

	if (cpus != NULL) {
		int i;

		cpus->nr = nr;
		for (i = 0; i < nr; i++)
			cpus->map[i] = -1;

		refcount_set(&cpus->refcnt, 1);
	}

	return cpus;
}

static void cpu_map__delete(struct cpu_map *map)
{
	if (map) {
		WARN_ONCE(refcount_read(&map->refcnt) != 0,
			  "cpu_map refcnt unbalanced\n");
		free(map);
	}
}

struct cpu_map *cpu_map__get(struct cpu_map *map)
{
	if (map)
		refcount_inc(&map->refcnt);
	return map;
}

void cpu_map__put(struct cpu_map *map)
{
	if (map && refcount_dec_and_test(&map->refcnt))
		cpu_map__delete(map);
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

int cpu_map__get_socket(struct cpu_map *map, int idx, void *data __maybe_unused)
{
	int cpu;

	if (idx > map->nr)
		return -1;

	cpu = map->map[idx];

	return cpu_map__get_socket_id(cpu);
}

static int cmp_ids(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

int cpu_map__build_map(struct cpu_map *cpus, struct cpu_map **res,
		       int (*f)(struct cpu_map *map, int cpu, void *data),
		       void *data)
{
	struct cpu_map *c;
	int nr = cpus->nr;
	int cpu, s1, s2;

	/* allocate as much as possible */
	c = calloc(1, sizeof(*c) + nr * sizeof(int));
	if (!c)
		return -1;

	for (cpu = 0; cpu < nr; cpu++) {
		s1 = f(cpus, cpu, data);
		for (s2 = 0; s2 < c->nr; s2++) {
			if (s1 == c->map[s2])
				break;
		}
		if (s2 == c->nr) {
			c->map[c->nr] = s1;
			c->nr++;
		}
	}
	/* ensure we process id in increasing order */
	qsort(c->map, c->nr, sizeof(int), cmp_ids);

	refcount_set(&c->refcnt, 1);
	*res = c;
	return 0;
}

int cpu_map__get_core_id(int cpu)
{
	int value, ret = cpu__get_topology_int(cpu, "core_id", &value);
	return ret ?: value;
}

int cpu_map__get_core(struct cpu_map *map, int idx, void *data)
{
	int cpu, s;

	if (idx > map->nr)
		return -1;

	cpu = map->map[idx];

	cpu = cpu_map__get_core_id(cpu);

	s = cpu_map__get_socket(map, idx, data);
	if (s == -1)
		return -1;

	/*
	 * encode socket in upper 16 bits
	 * core_id is relative to socket, and
	 * we need a global id. So we combine
	 * socket+ core id
	 */
	return (s << 16) | (cpu & 0xffff);
}

int cpu_map__build_socket_map(struct cpu_map *cpus, struct cpu_map **sockp)
{
	return cpu_map__build_map(cpus, sockp, cpu_map__get_socket, NULL);
}

int cpu_map__build_core_map(struct cpu_map *cpus, struct cpu_map **corep)
{
	return cpu_map__build_map(cpus, corep, cpu_map__get_core, NULL);
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

bool cpu_map__has(struct cpu_map *cpus, int cpu)
{
	return cpu_map__idx(cpus, cpu) != -1;
}

int cpu_map__idx(struct cpu_map *cpus, int cpu)
{
	int i;

	for (i = 0; i < cpus->nr; ++i) {
		if (cpus->map[i] == cpu)
			return i;
	}

	return -1;
}

int cpu_map__cpu(struct cpu_map *cpus, int idx)
{
	return cpus->map[idx];
}

size_t cpu_map__snprint(struct cpu_map *map, char *buf, size_t size)
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

	pr_debug("cpumask list: %s\n", buf);
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

size_t cpu_map__snprint_mask(struct cpu_map *map, char *buf, size_t size)
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
