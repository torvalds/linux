#include "util.h"
#include <api/fs/fs.h>
#include "../perf.h"
#include "cpumap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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

	if (!isdigit(*cpu_list))
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
	else
		cpus = cpu_map__default_new();
invalid:
	free(tmp_cpus);
out:
	return cpus;
}

size_t cpu_map__fprintf(struct cpu_map *map, FILE *fp)
{
	int i;
	size_t printed = fprintf(fp, "%d cpu%s: ",
				 map->nr, map->nr > 1 ? "s" : "");
	for (i = 0; i < map->nr; ++i)
		printed += fprintf(fp, "%s%d", i ? ", " : "", map->map[i]);

	return printed + fprintf(fp, "\n");
}

struct cpu_map *cpu_map__dummy_new(void)
{
	struct cpu_map *cpus = malloc(sizeof(*cpus) + sizeof(int));

	if (cpus != NULL) {
		cpus->nr = 1;
		cpus->map[0] = -1;
	}

	return cpus;
}

void cpu_map__delete(struct cpu_map *map)
{
	free(map);
}

int cpu_map__get_socket(struct cpu_map *map, int idx)
{
	FILE *fp;
	const char *mnt;
	char path[PATH_MAX];
	int cpu, ret;

	if (idx > map->nr)
		return -1;

	cpu = map->map[idx];

	mnt = sysfs__mountpoint();
	if (!mnt)
		return -1;

	snprintf(path, PATH_MAX,
		"%s/devices/system/cpu/cpu%d/topology/physical_package_id",
		mnt, cpu);

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	ret = fscanf(fp, "%d", &cpu);
	fclose(fp);
	return ret == 1 ? cpu : -1;
}

static int cmp_ids(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static int cpu_map__build_map(struct cpu_map *cpus, struct cpu_map **res,
			      int (*f)(struct cpu_map *map, int cpu))
{
	struct cpu_map *c;
	int nr = cpus->nr;
	int cpu, s1, s2;

	/* allocate as much as possible */
	c = calloc(1, sizeof(*c) + nr * sizeof(int));
	if (!c)
		return -1;

	for (cpu = 0; cpu < nr; cpu++) {
		s1 = f(cpus, cpu);
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

	*res = c;
	return 0;
}

int cpu_map__get_core(struct cpu_map *map, int idx)
{
	FILE *fp;
	const char *mnt;
	char path[PATH_MAX];
	int cpu, ret, s;

	if (idx > map->nr)
		return -1;

	cpu = map->map[idx];

	mnt = sysfs__mountpoint();
	if (!mnt)
		return -1;

	snprintf(path, PATH_MAX,
		"%s/devices/system/cpu/cpu%d/topology/core_id",
		mnt, cpu);

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	ret = fscanf(fp, "%d", &cpu);
	fclose(fp);
	if (ret != 1)
		return -1;

	s = cpu_map__get_socket(map, idx);
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
	return cpu_map__build_map(cpus, sockp, cpu_map__get_socket);
}

int cpu_map__build_core_map(struct cpu_map *cpus, struct cpu_map **corep)
{
	return cpu_map__build_map(cpus, corep, cpu_map__get_core);
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

	mnt = sysfs__mountpoint();
	if (!mnt)
		goto out;

	/* get the highest possible cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/cpu/possible", mnt);
	if (ret == PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_cpu_num);

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
	if (ret == PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_node_num);

out:
	if (ret)
		pr_err("Failed to read max nodes, using default of %d\n", max_node_num);
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
	if (n == PATH_MAX) {
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
		if (n == PATH_MAX) {
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
