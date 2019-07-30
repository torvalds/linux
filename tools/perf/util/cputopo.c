// SPDX-License-Identifier: GPL-2.0
#include <sys/param.h>
#include <inttypes.h>
#include <api/fs/fs.h>

#include "cputopo.h"
#include "cpumap.h"
#include "util.h"
#include "env.h"


#define CORE_SIB_FMT \
	"%s/devices/system/cpu/cpu%d/topology/core_siblings_list"
#define THRD_SIB_FMT \
	"%s/devices/system/cpu/cpu%d/topology/thread_siblings_list"
#define NODE_ONLINE_FMT \
	"%s/devices/system/node/online"
#define NODE_MEMINFO_FMT \
	"%s/devices/system/node/node%d/meminfo"
#define NODE_CPULIST_FMT \
	"%s/devices/system/node/node%d/cpulist"

static int build_cpu_topology(struct cpu_topology *tp, int cpu)
{
	FILE *fp;
	char filename[MAXPATHLEN];
	char *buf = NULL, *p;
	size_t len = 0;
	ssize_t sret;
	u32 i = 0;
	int ret = -1;

	scnprintf(filename, MAXPATHLEN, CORE_SIB_FMT,
		  sysfs__mountpoint(), cpu);
	fp = fopen(filename, "r");
	if (!fp)
		goto try_threads;

	sret = getline(&buf, &len, fp);
	fclose(fp);
	if (sret <= 0)
		goto try_threads;

	p = strchr(buf, '\n');
	if (p)
		*p = '\0';

	for (i = 0; i < tp->core_sib; i++) {
		if (!strcmp(buf, tp->core_siblings[i]))
			break;
	}
	if (i == tp->core_sib) {
		tp->core_siblings[i] = buf;
		tp->core_sib++;
		buf = NULL;
		len = 0;
	}
	ret = 0;

try_threads:
	scnprintf(filename, MAXPATHLEN, THRD_SIB_FMT,
		  sysfs__mountpoint(), cpu);
	fp = fopen(filename, "r");
	if (!fp)
		goto done;

	if (getline(&buf, &len, fp) <= 0)
		goto done;

	p = strchr(buf, '\n');
	if (p)
		*p = '\0';

	for (i = 0; i < tp->thread_sib; i++) {
		if (!strcmp(buf, tp->thread_siblings[i]))
			break;
	}
	if (i == tp->thread_sib) {
		tp->thread_siblings[i] = buf;
		tp->thread_sib++;
		buf = NULL;
	}
	ret = 0;
done:
	if (fp)
		fclose(fp);
	free(buf);
	return ret;
}

void cpu_topology__delete(struct cpu_topology *tp)
{
	u32 i;

	if (!tp)
		return;

	for (i = 0 ; i < tp->core_sib; i++)
		zfree(&tp->core_siblings[i]);

	for (i = 0 ; i < tp->thread_sib; i++)
		zfree(&tp->thread_siblings[i]);

	free(tp);
}

struct cpu_topology *cpu_topology__new(void)
{
	struct cpu_topology *tp = NULL;
	void *addr;
	u32 nr, i;
	size_t sz;
	long ncpus;
	int ret = -1;
	struct cpu_map *map;

	ncpus = cpu__max_present_cpu();

	/* build online CPU map */
	map = cpu_map__new(NULL);
	if (map == NULL) {
		pr_debug("failed to get system cpumap\n");
		return NULL;
	}

	nr = (u32)(ncpus & UINT_MAX);

	sz = nr * sizeof(char *);
	addr = calloc(1, sizeof(*tp) + 2 * sz);
	if (!addr)
		goto out_free;

	tp = addr;
	addr += sizeof(*tp);
	tp->core_siblings = addr;
	addr += sz;
	tp->thread_siblings = addr;

	for (i = 0; i < nr; i++) {
		if (!cpu_map__has(map, i))
			continue;

		ret = build_cpu_topology(tp, i);
		if (ret < 0)
			break;
	}

out_free:
	cpu_map__put(map);
	if (ret) {
		cpu_topology__delete(tp);
		tp = NULL;
	}
	return tp;
}

static int load_numa_node(struct numa_topology_node *node, int nr)
{
	char str[MAXPATHLEN];
	char field[32];
	char *buf = NULL, *p;
	size_t len = 0;
	int ret = -1;
	FILE *fp;
	u64 mem;

	node->node = (u32) nr;

	scnprintf(str, MAXPATHLEN, NODE_MEMINFO_FMT,
		  sysfs__mountpoint(), nr);
	fp = fopen(str, "r");
	if (!fp)
		return -1;

	while (getline(&buf, &len, fp) > 0) {
		/* skip over invalid lines */
		if (!strchr(buf, ':'))
			continue;
		if (sscanf(buf, "%*s %*d %31s %"PRIu64, field, &mem) != 2)
			goto err;
		if (!strcmp(field, "MemTotal:"))
			node->mem_total = mem;
		if (!strcmp(field, "MemFree:"))
			node->mem_free = mem;
		if (node->mem_total && node->mem_free)
			break;
	}

	fclose(fp);
	fp = NULL;

	scnprintf(str, MAXPATHLEN, NODE_CPULIST_FMT,
		  sysfs__mountpoint(), nr);

	fp = fopen(str, "r");
	if (!fp)
		return -1;

	if (getline(&buf, &len, fp) <= 0)
		goto err;

	p = strchr(buf, '\n');
	if (p)
		*p = '\0';

	node->cpus = buf;
	fclose(fp);
	return 0;

err:
	free(buf);
	if (fp)
		fclose(fp);
	return ret;
}

struct numa_topology *numa_topology__new(void)
{
	struct cpu_map *node_map = NULL;
	struct numa_topology *tp = NULL;
	char path[MAXPATHLEN];
	char *buf = NULL;
	size_t len = 0;
	u32 nr, i;
	FILE *fp;
	char *c;

	scnprintf(path, MAXPATHLEN, NODE_ONLINE_FMT,
		  sysfs__mountpoint());

	fp = fopen(path, "r");
	if (!fp)
		return NULL;

	if (getline(&buf, &len, fp) <= 0)
		goto out;

	c = strchr(buf, '\n');
	if (c)
		*c = '\0';

	node_map = cpu_map__new(buf);
	if (!node_map)
		goto out;

	nr = (u32) node_map->nr;

	tp = zalloc(sizeof(*tp) + sizeof(tp->nodes[0])*nr);
	if (!tp)
		goto out;

	tp->nr = nr;

	for (i = 0; i < nr; i++) {
		if (load_numa_node(&tp->nodes[i], node_map->map[i])) {
			numa_topology__delete(tp);
			tp = NULL;
			break;
		}
	}

out:
	free(buf);
	fclose(fp);
	cpu_map__put(node_map);
	return tp;
}

void numa_topology__delete(struct numa_topology *tp)
{
	u32 i;

	for (i = 0; i < tp->nr; i++)
		free(tp->nodes[i].cpus);

	free(tp);
}
