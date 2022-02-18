// SPDX-License-Identifier: GPL-2.0-only
#include <perf/cpumap.h>
#include <stdlib.h>
#include <linux/refcount.h>
#include <internal/cpumap.h>
#include <asm/bug.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>

static struct perf_cpu_map *perf_cpu_map__alloc(int nr_cpus)
{
	struct perf_cpu_map *cpus = malloc(sizeof(*cpus) + sizeof(struct perf_cpu) * nr_cpus);

	if (cpus != NULL) {
		cpus->nr = nr_cpus;
		refcount_set(&cpus->refcnt, 1);

	}
	return cpus;
}

struct perf_cpu_map *perf_cpu_map__dummy_new(void)
{
	struct perf_cpu_map *cpus = perf_cpu_map__alloc(1);

	if (cpus)
		cpus->map[0].cpu = -1;

	return cpus;
}

static void cpu_map__delete(struct perf_cpu_map *map)
{
	if (map) {
		WARN_ONCE(refcount_read(&map->refcnt) != 0,
			  "cpu_map refcnt unbalanced\n");
		free(map);
	}
}

struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map)
{
	if (map)
		refcount_inc(&map->refcnt);
	return map;
}

void perf_cpu_map__put(struct perf_cpu_map *map)
{
	if (map && refcount_dec_and_test(&map->refcnt))
		cpu_map__delete(map);
}

static struct perf_cpu_map *cpu_map__default_new(void)
{
	struct perf_cpu_map *cpus;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (nr_cpus < 0)
		return NULL;

	cpus = perf_cpu_map__alloc(nr_cpus);
	if (cpus != NULL) {
		int i;

		for (i = 0; i < nr_cpus; ++i)
			cpus->map[i].cpu = i;
	}

	return cpus;
}

struct perf_cpu_map *perf_cpu_map__default_new(void)
{
	return cpu_map__default_new();
}


static int cmp_cpu(const void *a, const void *b)
{
	const struct perf_cpu *cpu_a = a, *cpu_b = b;

	return cpu_a->cpu - cpu_b->cpu;
}

static struct perf_cpu_map *cpu_map__trim_new(int nr_cpus, const struct perf_cpu *tmp_cpus)
{
	size_t payload_size = nr_cpus * sizeof(struct perf_cpu);
	struct perf_cpu_map *cpus = perf_cpu_map__alloc(nr_cpus);
	int i, j;

	if (cpus != NULL) {
		memcpy(cpus->map, tmp_cpus, payload_size);
		qsort(cpus->map, nr_cpus, sizeof(struct perf_cpu), cmp_cpu);
		/* Remove dups */
		j = 0;
		for (i = 0; i < nr_cpus; i++) {
			if (i == 0 || cpus->map[i].cpu != cpus->map[i - 1].cpu)
				cpus->map[j++].cpu = cpus->map[i].cpu;
		}
		cpus->nr = j;
		assert(j <= nr_cpus);
	}
	return cpus;
}

struct perf_cpu_map *perf_cpu_map__read(FILE *file)
{
	struct perf_cpu_map *cpus = NULL;
	int nr_cpus = 0;
	struct perf_cpu *tmp_cpus = NULL, *tmp;
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

			WARN_ONCE(new_max >= MAX_NR_CPUS, "Perf can support %d CPUs. "
							  "Consider raising MAX_NR_CPUS\n", MAX_NR_CPUS);

			if (new_max >= max_entries) {
				max_entries = new_max + MAX_NR_CPUS / 2;
				tmp = realloc(tmp_cpus, max_entries * sizeof(struct perf_cpu));
				if (tmp == NULL)
					goto out_free_tmp;
				tmp_cpus = tmp;
			}

			while (++prev < cpu)
				tmp_cpus[nr_cpus++].cpu = prev;
		}
		if (nr_cpus == max_entries) {
			max_entries += MAX_NR_CPUS;
			tmp = realloc(tmp_cpus, max_entries * sizeof(struct perf_cpu));
			if (tmp == NULL)
				goto out_free_tmp;
			tmp_cpus = tmp;
		}

		tmp_cpus[nr_cpus++].cpu = cpu;
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

static struct perf_cpu_map *cpu_map__read_all_cpu_map(void)
{
	struct perf_cpu_map *cpus = NULL;
	FILE *onlnf;

	onlnf = fopen("/sys/devices/system/cpu/online", "r");
	if (!onlnf)
		return cpu_map__default_new();

	cpus = perf_cpu_map__read(onlnf);
	fclose(onlnf);
	return cpus;
}

struct perf_cpu_map *perf_cpu_map__new(const char *cpu_list)
{
	struct perf_cpu_map *cpus = NULL;
	unsigned long start_cpu, end_cpu = 0;
	char *p = NULL;
	int i, nr_cpus = 0;
	struct perf_cpu *tmp_cpus = NULL, *tmp;
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

		WARN_ONCE(end_cpu >= MAX_NR_CPUS, "Perf can support %d CPUs. "
						  "Consider raising MAX_NR_CPUS\n", MAX_NR_CPUS);

		for (; start_cpu <= end_cpu; start_cpu++) {
			/* check for duplicates */
			for (i = 0; i < nr_cpus; i++)
				if (tmp_cpus[i].cpu == (int)start_cpu)
					goto invalid;

			if (nr_cpus == max_entries) {
				max_entries += MAX_NR_CPUS;
				tmp = realloc(tmp_cpus, max_entries * sizeof(struct perf_cpu));
				if (tmp == NULL)
					goto invalid;
				tmp_cpus = tmp;
			}
			tmp_cpus[nr_cpus++].cpu = (int)start_cpu;
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
		cpus = perf_cpu_map__dummy_new();
invalid:
	free(tmp_cpus);
out:
	return cpus;
}

struct perf_cpu perf_cpu_map__cpu(const struct perf_cpu_map *cpus, int idx)
{
	struct perf_cpu result = {
		.cpu = -1
	};

	if (cpus && idx < cpus->nr)
		return cpus->map[idx];

	return result;
}

int perf_cpu_map__nr(const struct perf_cpu_map *cpus)
{
	return cpus ? cpus->nr : 1;
}

bool perf_cpu_map__empty(const struct perf_cpu_map *map)
{
	return map ? map->map[0].cpu == -1 : true;
}

int perf_cpu_map__idx(const struct perf_cpu_map *cpus, struct perf_cpu cpu)
{
	int low, high;

	if (!cpus)
		return -1;

	low = 0;
	high = cpus->nr;
	while (low < high) {
		int idx = (low + high) / 2;
		struct perf_cpu cpu_at_idx = cpus->map[idx];

		if (cpu_at_idx.cpu == cpu.cpu)
			return idx;

		if (cpu_at_idx.cpu > cpu.cpu)
			high = idx;
		else
			low = idx + 1;
	}

	return -1;
}

bool perf_cpu_map__has(const struct perf_cpu_map *cpus, struct perf_cpu cpu)
{
	return perf_cpu_map__idx(cpus, cpu) != -1;
}

struct perf_cpu perf_cpu_map__max(struct perf_cpu_map *map)
{
	struct perf_cpu result = {
		.cpu = -1
	};

	// cpu_map__trim_new() qsort()s it, cpu_map__default_new() sorts it as well.
	return map->nr > 0 ? map->map[map->nr - 1] : result;
}

/*
 * Merge two cpumaps
 *
 * orig either gets freed and replaced with a new map, or reused
 * with no reference count change (similar to "realloc")
 * other has its reference count increased.
 */

struct perf_cpu_map *perf_cpu_map__merge(struct perf_cpu_map *orig,
					 struct perf_cpu_map *other)
{
	struct perf_cpu *tmp_cpus;
	int tmp_len;
	int i, j, k;
	struct perf_cpu_map *merged;

	if (!orig && !other)
		return NULL;
	if (!orig) {
		perf_cpu_map__get(other);
		return other;
	}
	if (!other)
		return orig;
	if (orig->nr == other->nr &&
	    !memcmp(orig->map, other->map, orig->nr * sizeof(struct perf_cpu)))
		return orig;

	tmp_len = orig->nr + other->nr;
	tmp_cpus = malloc(tmp_len * sizeof(struct perf_cpu));
	if (!tmp_cpus)
		return NULL;

	/* Standard merge algorithm from wikipedia */
	i = j = k = 0;
	while (i < orig->nr && j < other->nr) {
		if (orig->map[i].cpu <= other->map[j].cpu) {
			if (orig->map[i].cpu == other->map[j].cpu)
				j++;
			tmp_cpus[k++] = orig->map[i++];
		} else
			tmp_cpus[k++] = other->map[j++];
	}

	while (i < orig->nr)
		tmp_cpus[k++] = orig->map[i++];

	while (j < other->nr)
		tmp_cpus[k++] = other->map[j++];
	assert(k <= tmp_len);

	merged = cpu_map__trim_new(k, tmp_cpus);
	free(tmp_cpus);
	perf_cpu_map__put(orig);
	return merged;
}
