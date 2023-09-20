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

void perf_cpu_map__set_nr(struct perf_cpu_map *map, int nr_cpus)
{
	RC_CHK_ACCESS(map)->nr = nr_cpus;
}

struct perf_cpu_map *perf_cpu_map__alloc(int nr_cpus)
{
	RC_STRUCT(perf_cpu_map) *cpus = malloc(sizeof(*cpus) + sizeof(struct perf_cpu) * nr_cpus);
	struct perf_cpu_map *result;

	if (ADD_RC_CHK(result, cpus)) {
		cpus->nr = nr_cpus;
		refcount_set(&cpus->refcnt, 1);
	}
	return result;
}

struct perf_cpu_map *perf_cpu_map__dummy_new(void)
{
	struct perf_cpu_map *cpus = perf_cpu_map__alloc(1);

	if (cpus)
		RC_CHK_ACCESS(cpus)->map[0].cpu = -1;

	return cpus;
}

static void cpu_map__delete(struct perf_cpu_map *map)
{
	if (map) {
		WARN_ONCE(refcount_read(perf_cpu_map__refcnt(map)) != 0,
			  "cpu_map refcnt unbalanced\n");
		RC_CHK_FREE(map);
	}
}

struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map)
{
	struct perf_cpu_map *result;

	if (RC_CHK_GET(result, map))
		refcount_inc(perf_cpu_map__refcnt(map));

	return result;
}

void perf_cpu_map__put(struct perf_cpu_map *map)
{
	if (map) {
		if (refcount_dec_and_test(perf_cpu_map__refcnt(map)))
			cpu_map__delete(map);
		else
			RC_CHK_PUT(map);
	}
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
			RC_CHK_ACCESS(cpus)->map[i].cpu = i;
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

static struct perf_cpu __perf_cpu_map__cpu(const struct perf_cpu_map *cpus, int idx)
{
	return RC_CHK_ACCESS(cpus)->map[idx];
}

static struct perf_cpu_map *cpu_map__trim_new(int nr_cpus, const struct perf_cpu *tmp_cpus)
{
	size_t payload_size = nr_cpus * sizeof(struct perf_cpu);
	struct perf_cpu_map *cpus = perf_cpu_map__alloc(nr_cpus);
	int i, j;

	if (cpus != NULL) {
		memcpy(RC_CHK_ACCESS(cpus)->map, tmp_cpus, payload_size);
		qsort(RC_CHK_ACCESS(cpus)->map, nr_cpus, sizeof(struct perf_cpu), cmp_cpu);
		/* Remove dups */
		j = 0;
		for (i = 0; i < nr_cpus; i++) {
			if (i == 0 ||
			    __perf_cpu_map__cpu(cpus, i).cpu !=
			    __perf_cpu_map__cpu(cpus, i - 1).cpu) {
				RC_CHK_ACCESS(cpus)->map[j++].cpu =
					__perf_cpu_map__cpu(cpus, i).cpu;
			}
		}
		perf_cpu_map__set_nr(cpus, j);
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

static int __perf_cpu_map__nr(const struct perf_cpu_map *cpus)
{
	return RC_CHK_ACCESS(cpus)->nr;
}

struct perf_cpu perf_cpu_map__cpu(const struct perf_cpu_map *cpus, int idx)
{
	struct perf_cpu result = {
		.cpu = -1
	};

	if (cpus && idx < __perf_cpu_map__nr(cpus))
		return __perf_cpu_map__cpu(cpus, idx);

	return result;
}

int perf_cpu_map__nr(const struct perf_cpu_map *cpus)
{
	return cpus ? __perf_cpu_map__nr(cpus) : 1;
}

bool perf_cpu_map__empty(const struct perf_cpu_map *map)
{
	return map ? __perf_cpu_map__cpu(map, 0).cpu == -1 : true;
}

int perf_cpu_map__idx(const struct perf_cpu_map *cpus, struct perf_cpu cpu)
{
	int low, high;

	if (!cpus)
		return -1;

	low = 0;
	high = __perf_cpu_map__nr(cpus);
	while (low < high) {
		int idx = (low + high) / 2;
		struct perf_cpu cpu_at_idx = __perf_cpu_map__cpu(cpus, idx);

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

bool perf_cpu_map__equal(const struct perf_cpu_map *lhs, const struct perf_cpu_map *rhs)
{
	int nr;

	if (lhs == rhs)
		return true;

	if (!lhs || !rhs)
		return false;

	nr = __perf_cpu_map__nr(lhs);
	if (nr != __perf_cpu_map__nr(rhs))
		return false;

	for (int idx = 0; idx < nr; idx++) {
		if (__perf_cpu_map__cpu(lhs, idx).cpu != __perf_cpu_map__cpu(rhs, idx).cpu)
			return false;
	}
	return true;
}

bool perf_cpu_map__has_any_cpu(const struct perf_cpu_map *map)
{
	return map && __perf_cpu_map__cpu(map, 0).cpu == -1;
}

struct perf_cpu perf_cpu_map__max(const struct perf_cpu_map *map)
{
	struct perf_cpu result = {
		.cpu = -1
	};

	// cpu_map__trim_new() qsort()s it, cpu_map__default_new() sorts it as well.
	return __perf_cpu_map__nr(map) > 0
		? __perf_cpu_map__cpu(map, __perf_cpu_map__nr(map) - 1)
		: result;
}

/** Is 'b' a subset of 'a'. */
bool perf_cpu_map__is_subset(const struct perf_cpu_map *a, const struct perf_cpu_map *b)
{
	if (a == b || !b)
		return true;
	if (!a || __perf_cpu_map__nr(b) > __perf_cpu_map__nr(a))
		return false;

	for (int i = 0, j = 0; i < __perf_cpu_map__nr(a); i++) {
		if (__perf_cpu_map__cpu(a, i).cpu > __perf_cpu_map__cpu(b, j).cpu)
			return false;
		if (__perf_cpu_map__cpu(a, i).cpu == __perf_cpu_map__cpu(b, j).cpu) {
			j++;
			if (j == __perf_cpu_map__nr(b))
				return true;
		}
	}
	return false;
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

	if (perf_cpu_map__is_subset(orig, other))
		return orig;
	if (perf_cpu_map__is_subset(other, orig)) {
		perf_cpu_map__put(orig);
		return perf_cpu_map__get(other);
	}

	tmp_len = __perf_cpu_map__nr(orig) + __perf_cpu_map__nr(other);
	tmp_cpus = malloc(tmp_len * sizeof(struct perf_cpu));
	if (!tmp_cpus)
		return NULL;

	/* Standard merge algorithm from wikipedia */
	i = j = k = 0;
	while (i < __perf_cpu_map__nr(orig) && j < __perf_cpu_map__nr(other)) {
		if (__perf_cpu_map__cpu(orig, i).cpu <= __perf_cpu_map__cpu(other, j).cpu) {
			if (__perf_cpu_map__cpu(orig, i).cpu == __perf_cpu_map__cpu(other, j).cpu)
				j++;
			tmp_cpus[k++] = __perf_cpu_map__cpu(orig, i++);
		} else
			tmp_cpus[k++] = __perf_cpu_map__cpu(other, j++);
	}

	while (i < __perf_cpu_map__nr(orig))
		tmp_cpus[k++] = __perf_cpu_map__cpu(orig, i++);

	while (j < __perf_cpu_map__nr(other))
		tmp_cpus[k++] = __perf_cpu_map__cpu(other, j++);
	assert(k <= tmp_len);

	merged = cpu_map__trim_new(k, tmp_cpus);
	free(tmp_cpus);
	perf_cpu_map__put(orig);
	return merged;
}

struct perf_cpu_map *perf_cpu_map__intersect(struct perf_cpu_map *orig,
					     struct perf_cpu_map *other)
{
	struct perf_cpu *tmp_cpus;
	int tmp_len;
	int i, j, k;
	struct perf_cpu_map *merged = NULL;

	if (perf_cpu_map__is_subset(other, orig))
		return perf_cpu_map__get(orig);
	if (perf_cpu_map__is_subset(orig, other))
		return perf_cpu_map__get(other);

	tmp_len = max(__perf_cpu_map__nr(orig), __perf_cpu_map__nr(other));
	tmp_cpus = malloc(tmp_len * sizeof(struct perf_cpu));
	if (!tmp_cpus)
		return NULL;

	i = j = k = 0;
	while (i < __perf_cpu_map__nr(orig) && j < __perf_cpu_map__nr(other)) {
		if (__perf_cpu_map__cpu(orig, i).cpu < __perf_cpu_map__cpu(other, j).cpu)
			i++;
		else if (__perf_cpu_map__cpu(orig, i).cpu > __perf_cpu_map__cpu(other, j).cpu)
			j++;
		else {
			j++;
			tmp_cpus[k++] = __perf_cpu_map__cpu(orig, i++);
		}
	}
	if (k)
		merged = cpu_map__trim_new(k, tmp_cpus);
	free(tmp_cpus);
	return merged;
}
