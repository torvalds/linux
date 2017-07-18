#include <errno.h>
#include <inttypes.h>
#include "util.h"
#include "string2.h"
#include <sys/param.h>
#include <sys/types.h>
#include <byteswap.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "evlist.h"
#include "evsel.h"
#include "header.h"
#include "memswap.h"
#include "../perf.h"
#include "trace-event.h"
#include "session.h"
#include "symbol.h"
#include "debug.h"
#include "cpumap.h"
#include "pmu.h"
#include "vdso.h"
#include "strbuf.h"
#include "build-id.h"
#include "data.h"
#include <api/fs/fs.h>
#include "asm/bug.h"

#include "sane_ctype.h"

/*
 * magic2 = "PERFILE2"
 * must be a numerical value to let the endianness
 * determine the memory layout. That way we are able
 * to detect endianness when reading the perf.data file
 * back.
 *
 * we check for legacy (PERFFILE) format.
 */
static const char *__perf_magic1 = "PERFFILE";
static const u64 __perf_magic2    = 0x32454c4946524550ULL;
static const u64 __perf_magic2_sw = 0x50455246494c4532ULL;

#define PERF_MAGIC	__perf_magic2

const char perf_version_string[] = PERF_VERSION;

struct perf_file_attr {
	struct perf_event_attr	attr;
	struct perf_file_section	ids;
};

struct feat_fd {
	struct perf_header	*ph;
	int			fd;
	ssize_t			offset;
	size_t			size;
};

void perf_header__set_feat(struct perf_header *header, int feat)
{
	set_bit(feat, header->adds_features);
}

void perf_header__clear_feat(struct perf_header *header, int feat)
{
	clear_bit(feat, header->adds_features);
}

bool perf_header__has_feat(const struct perf_header *header, int feat)
{
	return test_bit(feat, header->adds_features);
}

/* Return: 0 if succeded, -ERR if failed. */
int do_write(struct feat_fd *ff, const void *buf, size_t size)
{
	ssize_t ret;

	ret  = writen(ff->fd, buf, size);
	if (ret != (ssize_t)size)
		return ret < 0 ? (int)ret : -1;

	return 0;
}

/* Return: 0 if succeded, -ERR if failed. */
int write_padded(struct feat_fd *ff, const void *bf,
		 size_t count, size_t count_aligned)
{
	static const char zero_buf[NAME_ALIGN];
	int err = do_write(ff, bf, count);

	if (!err)
		err = do_write(ff, zero_buf, count_aligned - count);

	return err;
}

#define string_size(str)						\
	(PERF_ALIGN((strlen(str) + 1), NAME_ALIGN) + sizeof(u32))

/* Return: 0 if succeded, -ERR if failed. */
static int do_write_string(struct feat_fd *ff, const char *str)
{
	u32 len, olen;
	int ret;

	olen = strlen(str) + 1;
	len = PERF_ALIGN(olen, NAME_ALIGN);

	/* write len, incl. \0 */
	ret = do_write(ff, &len, sizeof(len));
	if (ret < 0)
		return ret;

	return write_padded(ff, str, olen, len);
}

static int __do_read(struct feat_fd *ff, void *addr, ssize_t size)
{
	ssize_t ret = readn(ff->fd, addr, size);

	if (ret != size)
		return ret < 0 ? (int)ret : -1;
	return 0;
}

static int do_read_u32(struct feat_fd *ff, u32 *addr)
{
	int ret;

	ret = __do_read(ff, addr, sizeof(*addr));
	if (ret)
		return ret;

	if (ff->ph->needs_swap)
		*addr = bswap_32(*addr);
	return 0;
}

static int do_read_u64(struct feat_fd *ff, u64 *addr)
{
	int ret;

	ret = __do_read(ff, addr, sizeof(*addr));
	if (ret)
		return ret;

	if (ff->ph->needs_swap)
		*addr = bswap_64(*addr);
	return 0;
}

static char *do_read_string(struct feat_fd *ff)
{
	u32 len;
	char *buf;

	if (do_read_u32(ff, &len))
		return NULL;

	buf = malloc(len);
	if (!buf)
		return NULL;

	if (!__do_read(ff, buf, len)) {
		/*
		 * strings are padded by zeroes
		 * thus the actual strlen of buf
		 * may be less than len
		 */
		return buf;
	}

	free(buf);
	return NULL;
}

static int write_tracing_data(struct feat_fd *ff,
			      struct perf_evlist *evlist)
{
	return read_tracing_data(ff->fd, &evlist->entries);
}

static int write_build_id(struct feat_fd *ff,
			  struct perf_evlist *evlist __maybe_unused)
{
	struct perf_session *session;
	int err;

	session = container_of(ff->ph, struct perf_session, header);

	if (!perf_session__read_build_ids(session, true))
		return -1;

	err = perf_session__write_buildid_table(session, ff);
	if (err < 0) {
		pr_debug("failed to write buildid table\n");
		return err;
	}
	perf_session__cache_build_ids(session);

	return 0;
}

static int write_hostname(struct feat_fd *ff,
			  struct perf_evlist *evlist __maybe_unused)
{
	struct utsname uts;
	int ret;

	ret = uname(&uts);
	if (ret < 0)
		return -1;

	return do_write_string(ff, uts.nodename);
}

static int write_osrelease(struct feat_fd *ff,
			   struct perf_evlist *evlist __maybe_unused)
{
	struct utsname uts;
	int ret;

	ret = uname(&uts);
	if (ret < 0)
		return -1;

	return do_write_string(ff, uts.release);
}

static int write_arch(struct feat_fd *ff,
		      struct perf_evlist *evlist __maybe_unused)
{
	struct utsname uts;
	int ret;

	ret = uname(&uts);
	if (ret < 0)
		return -1;

	return do_write_string(ff, uts.machine);
}

static int write_version(struct feat_fd *ff,
			 struct perf_evlist *evlist __maybe_unused)
{
	return do_write_string(ff, perf_version_string);
}

static int __write_cpudesc(struct feat_fd *ff, const char *cpuinfo_proc)
{
	FILE *file;
	char *buf = NULL;
	char *s, *p;
	const char *search = cpuinfo_proc;
	size_t len = 0;
	int ret = -1;

	if (!search)
		return -1;

	file = fopen("/proc/cpuinfo", "r");
	if (!file)
		return -1;

	while (getline(&buf, &len, file) > 0) {
		ret = strncmp(buf, search, strlen(search));
		if (!ret)
			break;
	}

	if (ret) {
		ret = -1;
		goto done;
	}

	s = buf;

	p = strchr(buf, ':');
	if (p && *(p+1) == ' ' && *(p+2))
		s = p + 2;
	p = strchr(s, '\n');
	if (p)
		*p = '\0';

	/* squash extra space characters (branding string) */
	p = s;
	while (*p) {
		if (isspace(*p)) {
			char *r = p + 1;
			char *q = r;
			*p = ' ';
			while (*q && isspace(*q))
				q++;
			if (q != (p+1))
				while ((*r++ = *q++));
		}
		p++;
	}
	ret = do_write_string(ff, s);
done:
	free(buf);
	fclose(file);
	return ret;
}

static int write_cpudesc(struct feat_fd *ff,
		       struct perf_evlist *evlist __maybe_unused)
{
#ifndef CPUINFO_PROC
#define CPUINFO_PROC {"model name", }
#endif
	const char *cpuinfo_procs[] = CPUINFO_PROC;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cpuinfo_procs); i++) {
		int ret;
		ret = __write_cpudesc(ff, cpuinfo_procs[i]);
		if (ret >= 0)
			return ret;
	}
	return -1;
}


static int write_nrcpus(struct feat_fd *ff,
			struct perf_evlist *evlist __maybe_unused)
{
	long nr;
	u32 nrc, nra;
	int ret;

	nrc = cpu__max_present_cpu();

	nr = sysconf(_SC_NPROCESSORS_ONLN);
	if (nr < 0)
		return -1;

	nra = (u32)(nr & UINT_MAX);

	ret = do_write(ff, &nrc, sizeof(nrc));
	if (ret < 0)
		return ret;

	return do_write(ff, &nra, sizeof(nra));
}

static int write_event_desc(struct feat_fd *ff,
			    struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	u32 nre, nri, sz;
	int ret;

	nre = evlist->nr_entries;

	/*
	 * write number of events
	 */
	ret = do_write(ff, &nre, sizeof(nre));
	if (ret < 0)
		return ret;

	/*
	 * size of perf_event_attr struct
	 */
	sz = (u32)sizeof(evsel->attr);
	ret = do_write(ff, &sz, sizeof(sz));
	if (ret < 0)
		return ret;

	evlist__for_each_entry(evlist, evsel) {
		ret = do_write(ff, &evsel->attr, sz);
		if (ret < 0)
			return ret;
		/*
		 * write number of unique id per event
		 * there is one id per instance of an event
		 *
		 * copy into an nri to be independent of the
		 * type of ids,
		 */
		nri = evsel->ids;
		ret = do_write(ff, &nri, sizeof(nri));
		if (ret < 0)
			return ret;

		/*
		 * write event string as passed on cmdline
		 */
		ret = do_write_string(ff, perf_evsel__name(evsel));
		if (ret < 0)
			return ret;
		/*
		 * write unique ids for this event
		 */
		ret = do_write(ff, evsel->id, evsel->ids * sizeof(u64));
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int write_cmdline(struct feat_fd *ff,
			 struct perf_evlist *evlist __maybe_unused)
{
	char buf[MAXPATHLEN];
	u32 n;
	int i, ret;

	/* actual path to perf binary */
	ret = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (ret <= 0)
		return -1;

	/* readlink() does not add null termination */
	buf[ret] = '\0';

	/* account for binary path */
	n = perf_env.nr_cmdline + 1;

	ret = do_write(ff, &n, sizeof(n));
	if (ret < 0)
		return ret;

	ret = do_write_string(ff, buf);
	if (ret < 0)
		return ret;

	for (i = 0 ; i < perf_env.nr_cmdline; i++) {
		ret = do_write_string(ff, perf_env.cmdline_argv[i]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

#define CORE_SIB_FMT \
	"/sys/devices/system/cpu/cpu%d/topology/core_siblings_list"
#define THRD_SIB_FMT \
	"/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list"

struct cpu_topo {
	u32 cpu_nr;
	u32 core_sib;
	u32 thread_sib;
	char **core_siblings;
	char **thread_siblings;
};

static int build_cpu_topo(struct cpu_topo *tp, int cpu)
{
	FILE *fp;
	char filename[MAXPATHLEN];
	char *buf = NULL, *p;
	size_t len = 0;
	ssize_t sret;
	u32 i = 0;
	int ret = -1;

	sprintf(filename, CORE_SIB_FMT, cpu);
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
	sprintf(filename, THRD_SIB_FMT, cpu);
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
	if(fp)
		fclose(fp);
	free(buf);
	return ret;
}

static void free_cpu_topo(struct cpu_topo *tp)
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

static struct cpu_topo *build_cpu_topology(void)
{
	struct cpu_topo *tp = NULL;
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
	tp->cpu_nr = nr;
	addr += sizeof(*tp);
	tp->core_siblings = addr;
	addr += sz;
	tp->thread_siblings = addr;

	for (i = 0; i < nr; i++) {
		if (!cpu_map__has(map, i))
			continue;

		ret = build_cpu_topo(tp, i);
		if (ret < 0)
			break;
	}

out_free:
	cpu_map__put(map);
	if (ret) {
		free_cpu_topo(tp);
		tp = NULL;
	}
	return tp;
}

static int write_cpu_topology(struct feat_fd *ff,
			      struct perf_evlist *evlist __maybe_unused)
{
	struct cpu_topo *tp;
	u32 i;
	int ret, j;

	tp = build_cpu_topology();
	if (!tp)
		return -1;

	ret = do_write(ff, &tp->core_sib, sizeof(tp->core_sib));
	if (ret < 0)
		goto done;

	for (i = 0; i < tp->core_sib; i++) {
		ret = do_write_string(ff, tp->core_siblings[i]);
		if (ret < 0)
			goto done;
	}
	ret = do_write(ff, &tp->thread_sib, sizeof(tp->thread_sib));
	if (ret < 0)
		goto done;

	for (i = 0; i < tp->thread_sib; i++) {
		ret = do_write_string(ff, tp->thread_siblings[i]);
		if (ret < 0)
			break;
	}

	ret = perf_env__read_cpu_topology_map(&perf_env);
	if (ret < 0)
		goto done;

	for (j = 0; j < perf_env.nr_cpus_avail; j++) {
		ret = do_write(ff, &perf_env.cpu[j].core_id,
			       sizeof(perf_env.cpu[j].core_id));
		if (ret < 0)
			return ret;
		ret = do_write(ff, &perf_env.cpu[j].socket_id,
			       sizeof(perf_env.cpu[j].socket_id));
		if (ret < 0)
			return ret;
	}
done:
	free_cpu_topo(tp);
	return ret;
}



static int write_total_mem(struct feat_fd *ff,
			   struct perf_evlist *evlist __maybe_unused)
{
	char *buf = NULL;
	FILE *fp;
	size_t len = 0;
	int ret = -1, n;
	uint64_t mem;

	fp = fopen("/proc/meminfo", "r");
	if (!fp)
		return -1;

	while (getline(&buf, &len, fp) > 0) {
		ret = strncmp(buf, "MemTotal:", 9);
		if (!ret)
			break;
	}
	if (!ret) {
		n = sscanf(buf, "%*s %"PRIu64, &mem);
		if (n == 1)
			ret = do_write(ff, &mem, sizeof(mem));
	} else
		ret = -1;
	free(buf);
	fclose(fp);
	return ret;
}

static int write_topo_node(struct feat_fd *ff, int node)
{
	char str[MAXPATHLEN];
	char field[32];
	char *buf = NULL, *p;
	size_t len = 0;
	FILE *fp;
	u64 mem_total, mem_free, mem;
	int ret = -1;

	sprintf(str, "/sys/devices/system/node/node%d/meminfo", node);
	fp = fopen(str, "r");
	if (!fp)
		return -1;

	while (getline(&buf, &len, fp) > 0) {
		/* skip over invalid lines */
		if (!strchr(buf, ':'))
			continue;
		if (sscanf(buf, "%*s %*d %31s %"PRIu64, field, &mem) != 2)
			goto done;
		if (!strcmp(field, "MemTotal:"))
			mem_total = mem;
		if (!strcmp(field, "MemFree:"))
			mem_free = mem;
	}

	fclose(fp);
	fp = NULL;

	ret = do_write(ff, &mem_total, sizeof(u64));
	if (ret)
		goto done;

	ret = do_write(ff, &mem_free, sizeof(u64));
	if (ret)
		goto done;

	ret = -1;
	sprintf(str, "/sys/devices/system/node/node%d/cpulist", node);

	fp = fopen(str, "r");
	if (!fp)
		goto done;

	if (getline(&buf, &len, fp) <= 0)
		goto done;

	p = strchr(buf, '\n');
	if (p)
		*p = '\0';

	ret = do_write_string(ff, buf);
done:
	free(buf);
	if (fp)
		fclose(fp);
	return ret;
}

static int write_numa_topology(struct feat_fd *ff,
			       struct perf_evlist *evlist __maybe_unused)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *fp;
	struct cpu_map *node_map = NULL;
	char *c;
	u32 nr, i, j;
	int ret = -1;

	fp = fopen("/sys/devices/system/node/online", "r");
	if (!fp)
		return -1;

	if (getline(&buf, &len, fp) <= 0)
		goto done;

	c = strchr(buf, '\n');
	if (c)
		*c = '\0';

	node_map = cpu_map__new(buf);
	if (!node_map)
		goto done;

	nr = (u32)node_map->nr;

	ret = do_write(ff, &nr, sizeof(nr));
	if (ret < 0)
		goto done;

	for (i = 0; i < nr; i++) {
		j = (u32)node_map->map[i];
		ret = do_write(ff, &j, sizeof(j));
		if (ret < 0)
			break;

		ret = write_topo_node(ff, i);
		if (ret < 0)
			break;
	}
done:
	free(buf);
	fclose(fp);
	cpu_map__put(node_map);
	return ret;
}

/*
 * File format:
 *
 * struct pmu_mappings {
 *	u32	pmu_num;
 *	struct pmu_map {
 *		u32	type;
 *		char	name[];
 *	}[pmu_num];
 * };
 */

static int write_pmu_mappings(struct feat_fd *ff,
			      struct perf_evlist *evlist __maybe_unused)
{
	struct perf_pmu *pmu = NULL;
	off_t offset = lseek(ff->fd, 0, SEEK_CUR);
	__u32 pmu_num = 0;
	int ret;

	/* write real pmu_num later */
	ret = do_write(ff, &pmu_num, sizeof(pmu_num));
	if (ret < 0)
		return ret;

	while ((pmu = perf_pmu__scan(pmu))) {
		if (!pmu->name)
			continue;
		pmu_num++;

		ret = do_write(ff, &pmu->type, sizeof(pmu->type));
		if (ret < 0)
			return ret;

		ret = do_write_string(ff, pmu->name);
		if (ret < 0)
			return ret;
	}

	if (pwrite(ff->fd, &pmu_num, sizeof(pmu_num), offset) != sizeof(pmu_num)) {
		/* discard all */
		lseek(ff->fd, offset, SEEK_SET);
		return -1;
	}

	return 0;
}

/*
 * File format:
 *
 * struct group_descs {
 *	u32	nr_groups;
 *	struct group_desc {
 *		char	name[];
 *		u32	leader_idx;
 *		u32	nr_members;
 *	}[nr_groups];
 * };
 */
static int write_group_desc(struct feat_fd *ff,
			    struct perf_evlist *evlist)
{
	u32 nr_groups = evlist->nr_groups;
	struct perf_evsel *evsel;
	int ret;

	ret = do_write(ff, &nr_groups, sizeof(nr_groups));
	if (ret < 0)
		return ret;

	evlist__for_each_entry(evlist, evsel) {
		if (perf_evsel__is_group_leader(evsel) &&
		    evsel->nr_members > 1) {
			const char *name = evsel->group_name ?: "{anon_group}";
			u32 leader_idx = evsel->idx;
			u32 nr_members = evsel->nr_members;

			ret = do_write_string(ff, name);
			if (ret < 0)
				return ret;

			ret = do_write(ff, &leader_idx, sizeof(leader_idx));
			if (ret < 0)
				return ret;

			ret = do_write(ff, &nr_members, sizeof(nr_members));
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}

/*
 * default get_cpuid(): nothing gets recorded
 * actual implementation must be in arch/$(SRCARCH)/util/header.c
 */
int __weak get_cpuid(char *buffer __maybe_unused, size_t sz __maybe_unused)
{
	return -1;
}

static int write_cpuid(struct feat_fd *ff,
		       struct perf_evlist *evlist __maybe_unused)
{
	char buffer[64];
	int ret;

	ret = get_cpuid(buffer, sizeof(buffer));
	if (!ret)
		goto write_it;

	return -1;
write_it:
	return do_write_string(ff, buffer);
}

static int write_branch_stack(struct feat_fd *ff __maybe_unused,
			      struct perf_evlist *evlist __maybe_unused)
{
	return 0;
}

static int write_auxtrace(struct feat_fd *ff,
			  struct perf_evlist *evlist __maybe_unused)
{
	struct perf_session *session;
	int err;

	session = container_of(ff->ph, struct perf_session, header);

	err = auxtrace_index__write(ff->fd, &session->auxtrace_index);
	if (err < 0)
		pr_err("Failed to write auxtrace index\n");
	return err;
}

static int cpu_cache_level__sort(const void *a, const void *b)
{
	struct cpu_cache_level *cache_a = (struct cpu_cache_level *)a;
	struct cpu_cache_level *cache_b = (struct cpu_cache_level *)b;

	return cache_a->level - cache_b->level;
}

static bool cpu_cache_level__cmp(struct cpu_cache_level *a, struct cpu_cache_level *b)
{
	if (a->level != b->level)
		return false;

	if (a->line_size != b->line_size)
		return false;

	if (a->sets != b->sets)
		return false;

	if (a->ways != b->ways)
		return false;

	if (strcmp(a->type, b->type))
		return false;

	if (strcmp(a->size, b->size))
		return false;

	if (strcmp(a->map, b->map))
		return false;

	return true;
}

static int cpu_cache_level__read(struct cpu_cache_level *cache, u32 cpu, u16 level)
{
	char path[PATH_MAX], file[PATH_MAX];
	struct stat st;
	size_t len;

	scnprintf(path, PATH_MAX, "devices/system/cpu/cpu%d/cache/index%d/", cpu, level);
	scnprintf(file, PATH_MAX, "%s/%s", sysfs__mountpoint(), path);

	if (stat(file, &st))
		return 1;

	scnprintf(file, PATH_MAX, "%s/level", path);
	if (sysfs__read_int(file, (int *) &cache->level))
		return -1;

	scnprintf(file, PATH_MAX, "%s/coherency_line_size", path);
	if (sysfs__read_int(file, (int *) &cache->line_size))
		return -1;

	scnprintf(file, PATH_MAX, "%s/number_of_sets", path);
	if (sysfs__read_int(file, (int *) &cache->sets))
		return -1;

	scnprintf(file, PATH_MAX, "%s/ways_of_associativity", path);
	if (sysfs__read_int(file, (int *) &cache->ways))
		return -1;

	scnprintf(file, PATH_MAX, "%s/type", path);
	if (sysfs__read_str(file, &cache->type, &len))
		return -1;

	cache->type[len] = 0;
	cache->type = rtrim(cache->type);

	scnprintf(file, PATH_MAX, "%s/size", path);
	if (sysfs__read_str(file, &cache->size, &len)) {
		free(cache->type);
		return -1;
	}

	cache->size[len] = 0;
	cache->size = rtrim(cache->size);

	scnprintf(file, PATH_MAX, "%s/shared_cpu_list", path);
	if (sysfs__read_str(file, &cache->map, &len)) {
		free(cache->map);
		free(cache->type);
		return -1;
	}

	cache->map[len] = 0;
	cache->map = rtrim(cache->map);
	return 0;
}

static void cpu_cache_level__fprintf(FILE *out, struct cpu_cache_level *c)
{
	fprintf(out, "L%d %-15s %8s [%s]\n", c->level, c->type, c->size, c->map);
}

static int build_caches(struct cpu_cache_level caches[], u32 size, u32 *cntp)
{
	u32 i, cnt = 0;
	long ncpus;
	u32 nr, cpu;
	u16 level;

	ncpus = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpus < 0)
		return -1;

	nr = (u32)(ncpus & UINT_MAX);

	for (cpu = 0; cpu < nr; cpu++) {
		for (level = 0; level < 10; level++) {
			struct cpu_cache_level c;
			int err;

			err = cpu_cache_level__read(&c, cpu, level);
			if (err < 0)
				return err;

			if (err == 1)
				break;

			for (i = 0; i < cnt; i++) {
				if (cpu_cache_level__cmp(&c, &caches[i]))
					break;
			}

			if (i == cnt)
				caches[cnt++] = c;
			else
				cpu_cache_level__free(&c);

			if (WARN_ONCE(cnt == size, "way too many cpu caches.."))
				goto out;
		}
	}
 out:
	*cntp = cnt;
	return 0;
}

#define MAX_CACHES 2000

static int write_cache(struct feat_fd *ff,
		       struct perf_evlist *evlist __maybe_unused)
{
	struct cpu_cache_level caches[MAX_CACHES];
	u32 cnt = 0, i, version = 1;
	int ret;

	ret = build_caches(caches, MAX_CACHES, &cnt);
	if (ret)
		goto out;

	qsort(&caches, cnt, sizeof(struct cpu_cache_level), cpu_cache_level__sort);

	ret = do_write(ff, &version, sizeof(u32));
	if (ret < 0)
		goto out;

	ret = do_write(ff, &cnt, sizeof(u32));
	if (ret < 0)
		goto out;

	for (i = 0; i < cnt; i++) {
		struct cpu_cache_level *c = &caches[i];

		#define _W(v)					\
			ret = do_write(ff, &c->v, sizeof(u32));	\
			if (ret < 0)				\
				goto out;

		_W(level)
		_W(line_size)
		_W(sets)
		_W(ways)
		#undef _W

		#define _W(v)						\
			ret = do_write_string(ff, (const char *) c->v);	\
			if (ret < 0)					\
				goto out;

		_W(type)
		_W(size)
		_W(map)
		#undef _W
	}

out:
	for (i = 0; i < cnt; i++)
		cpu_cache_level__free(&caches[i]);
	return ret;
}

static int write_stat(struct feat_fd *ff __maybe_unused,
		      struct perf_evlist *evlist __maybe_unused)
{
	return 0;
}

static void print_hostname(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# hostname : %s\n", ff->ph->env.hostname);
}

static void print_osrelease(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# os release : %s\n", ff->ph->env.os_release);
}

static void print_arch(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# arch : %s\n", ff->ph->env.arch);
}

static void print_cpudesc(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# cpudesc : %s\n", ff->ph->env.cpu_desc);
}

static void print_nrcpus(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# nrcpus online : %u\n", ff->ph->env.nr_cpus_online);
	fprintf(fp, "# nrcpus avail : %u\n", ff->ph->env.nr_cpus_avail);
}

static void print_version(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# perf version : %s\n", ff->ph->env.version);
}

static void print_cmdline(struct feat_fd *ff, FILE *fp)
{
	int nr, i;

	nr = ff->ph->env.nr_cmdline;

	fprintf(fp, "# cmdline : ");

	for (i = 0; i < nr; i++)
		fprintf(fp, "%s ", ff->ph->env.cmdline_argv[i]);
	fputc('\n', fp);
}

static void print_cpu_topology(struct feat_fd *ff, FILE *fp)
{
	struct perf_header *ph = ff->ph;
	int cpu_nr = ph->env.nr_cpus_avail;
	int nr, i;
	char *str;

	nr = ph->env.nr_sibling_cores;
	str = ph->env.sibling_cores;

	for (i = 0; i < nr; i++) {
		fprintf(fp, "# sibling cores   : %s\n", str);
		str += strlen(str) + 1;
	}

	nr = ph->env.nr_sibling_threads;
	str = ph->env.sibling_threads;

	for (i = 0; i < nr; i++) {
		fprintf(fp, "# sibling threads : %s\n", str);
		str += strlen(str) + 1;
	}

	if (ph->env.cpu != NULL) {
		for (i = 0; i < cpu_nr; i++)
			fprintf(fp, "# CPU %d: Core ID %d, Socket ID %d\n", i,
				ph->env.cpu[i].core_id, ph->env.cpu[i].socket_id);
	} else
		fprintf(fp, "# Core ID and Socket ID information is not available\n");
}

static void free_event_desc(struct perf_evsel *events)
{
	struct perf_evsel *evsel;

	if (!events)
		return;

	for (evsel = events; evsel->attr.size; evsel++) {
		zfree(&evsel->name);
		zfree(&evsel->id);
	}

	free(events);
}

static struct perf_evsel *read_event_desc(struct feat_fd *ff)
{
	struct perf_evsel *evsel, *events = NULL;
	u64 *id;
	void *buf = NULL;
	u32 nre, sz, nr, i, j;
	size_t msz;

	/* number of events */
	if (do_read_u32(ff, &nre))
		goto error;

	if (do_read_u32(ff, &sz))
		goto error;

	/* buffer to hold on file attr struct */
	buf = malloc(sz);
	if (!buf)
		goto error;

	/* the last event terminates with evsel->attr.size == 0: */
	events = calloc(nre + 1, sizeof(*events));
	if (!events)
		goto error;

	msz = sizeof(evsel->attr);
	if (sz < msz)
		msz = sz;

	for (i = 0, evsel = events; i < nre; evsel++, i++) {
		evsel->idx = i;

		/*
		 * must read entire on-file attr struct to
		 * sync up with layout.
		 */
		if (__do_read(ff, buf, sz))
			goto error;

		if (ff->ph->needs_swap)
			perf_event__attr_swap(buf);

		memcpy(&evsel->attr, buf, msz);

		if (do_read_u32(ff, &nr))
			goto error;

		if (ff->ph->needs_swap)
			evsel->needs_swap = true;

		evsel->name = do_read_string(ff);
		if (!evsel->name)
			goto error;

		if (!nr)
			continue;

		id = calloc(nr, sizeof(*id));
		if (!id)
			goto error;
		evsel->ids = nr;
		evsel->id = id;

		for (j = 0 ; j < nr; j++) {
			if (do_read_u64(ff, id))
				goto error;
			id++;
		}
	}
out:
	free(buf);
	return events;
error:
	free_event_desc(events);
	events = NULL;
	goto out;
}

static int __desc_attr__fprintf(FILE *fp, const char *name, const char *val,
				void *priv __maybe_unused)
{
	return fprintf(fp, ", %s = %s", name, val);
}

static void print_event_desc(struct feat_fd *ff, FILE *fp)
{
	struct perf_evsel *evsel, *events = read_event_desc(ff);
	u32 j;
	u64 *id;

	if (!events) {
		fprintf(fp, "# event desc: not available or unable to read\n");
		return;
	}

	for (evsel = events; evsel->attr.size; evsel++) {
		fprintf(fp, "# event : name = %s, ", evsel->name);

		if (evsel->ids) {
			fprintf(fp, ", id = {");
			for (j = 0, id = evsel->id; j < evsel->ids; j++, id++) {
				if (j)
					fputc(',', fp);
				fprintf(fp, " %"PRIu64, *id);
			}
			fprintf(fp, " }");
		}

		perf_event_attr__fprintf(fp, &evsel->attr, __desc_attr__fprintf, NULL);

		fputc('\n', fp);
	}

	free_event_desc(events);
}

static void print_total_mem(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# total memory : %llu kB\n", ff->ph->env.total_mem);
}

static void print_numa_topology(struct feat_fd *ff, FILE *fp)
{
	int i;
	struct numa_node *n;

	for (i = 0; i < ff->ph->env.nr_numa_nodes; i++) {
		n = &ff->ph->env.numa_nodes[i];

		fprintf(fp, "# node%u meminfo  : total = %"PRIu64" kB,"
			    " free = %"PRIu64" kB\n",
			n->node, n->mem_total, n->mem_free);

		fprintf(fp, "# node%u cpu list : ", n->node);
		cpu_map__fprintf(n->map, fp);
	}
}

static void print_cpuid(struct feat_fd *ff, FILE *fp)
{
	fprintf(fp, "# cpuid : %s\n", ff->ph->env.cpuid);
}

static void print_branch_stack(struct feat_fd *ff __maybe_unused, FILE *fp)
{
	fprintf(fp, "# contains samples with branch stack\n");
}

static void print_auxtrace(struct feat_fd *ff __maybe_unused, FILE *fp)
{
	fprintf(fp, "# contains AUX area data (e.g. instruction trace)\n");
}

static void print_stat(struct feat_fd *ff __maybe_unused, FILE *fp)
{
	fprintf(fp, "# contains stat data\n");
}

static void print_cache(struct feat_fd *ff, FILE *fp __maybe_unused)
{
	int i;

	fprintf(fp, "# CPU cache info:\n");
	for (i = 0; i < ff->ph->env.caches_cnt; i++) {
		fprintf(fp, "#  ");
		cpu_cache_level__fprintf(fp, &ff->ph->env.caches[i]);
	}
}

static void print_pmu_mappings(struct feat_fd *ff, FILE *fp)
{
	const char *delimiter = "# pmu mappings: ";
	char *str, *tmp;
	u32 pmu_num;
	u32 type;

	pmu_num = ff->ph->env.nr_pmu_mappings;
	if (!pmu_num) {
		fprintf(fp, "# pmu mappings: not available\n");
		return;
	}

	str = ff->ph->env.pmu_mappings;

	while (pmu_num) {
		type = strtoul(str, &tmp, 0);
		if (*tmp != ':')
			goto error;

		str = tmp + 1;
		fprintf(fp, "%s%s = %" PRIu32, delimiter, str, type);

		delimiter = ", ";
		str += strlen(str) + 1;
		pmu_num--;
	}

	fprintf(fp, "\n");

	if (!pmu_num)
		return;
error:
	fprintf(fp, "# pmu mappings: unable to read\n");
}

static void print_group_desc(struct feat_fd *ff, FILE *fp)
{
	struct perf_session *session;
	struct perf_evsel *evsel;
	u32 nr = 0;

	session = container_of(ff->ph, struct perf_session, header);

	evlist__for_each_entry(session->evlist, evsel) {
		if (perf_evsel__is_group_leader(evsel) &&
		    evsel->nr_members > 1) {
			fprintf(fp, "# group: %s{%s", evsel->group_name ?: "",
				perf_evsel__name(evsel));

			nr = evsel->nr_members - 1;
		} else if (nr) {
			fprintf(fp, ",%s", perf_evsel__name(evsel));

			if (--nr == 0)
				fprintf(fp, "}\n");
		}
	}
}

static int __event_process_build_id(struct build_id_event *bev,
				    char *filename,
				    struct perf_session *session)
{
	int err = -1;
	struct machine *machine;
	u16 cpumode;
	struct dso *dso;
	enum dso_kernel_type dso_type;

	machine = perf_session__findnew_machine(session, bev->pid);
	if (!machine)
		goto out;

	cpumode = bev->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	switch (cpumode) {
	case PERF_RECORD_MISC_KERNEL:
		dso_type = DSO_TYPE_KERNEL;
		break;
	case PERF_RECORD_MISC_GUEST_KERNEL:
		dso_type = DSO_TYPE_GUEST_KERNEL;
		break;
	case PERF_RECORD_MISC_USER:
	case PERF_RECORD_MISC_GUEST_USER:
		dso_type = DSO_TYPE_USER;
		break;
	default:
		goto out;
	}

	dso = machine__findnew_dso(machine, filename);
	if (dso != NULL) {
		char sbuild_id[SBUILD_ID_SIZE];

		dso__set_build_id(dso, &bev->build_id);

		if (dso_type != DSO_TYPE_USER) {
			struct kmod_path m = { .name = NULL, };

			if (!kmod_path__parse_name(&m, filename) && m.kmod)
				dso__set_module_info(dso, &m, machine);
			else
				dso->kernel = dso_type;

			free(m.name);
		}

		build_id__sprintf(dso->build_id, sizeof(dso->build_id),
				  sbuild_id);
		pr_debug("build id event received for %s: %s\n",
			 dso->long_name, sbuild_id);
		dso__put(dso);
	}

	err = 0;
out:
	return err;
}

static int perf_header__read_build_ids_abi_quirk(struct perf_header *header,
						 int input, u64 offset, u64 size)
{
	struct perf_session *session = container_of(header, struct perf_session, header);
	struct {
		struct perf_event_header   header;
		u8			   build_id[PERF_ALIGN(BUILD_ID_SIZE, sizeof(u64))];
		char			   filename[0];
	} old_bev;
	struct build_id_event bev;
	char filename[PATH_MAX];
	u64 limit = offset + size;

	while (offset < limit) {
		ssize_t len;

		if (readn(input, &old_bev, sizeof(old_bev)) != sizeof(old_bev))
			return -1;

		if (header->needs_swap)
			perf_event_header__bswap(&old_bev.header);

		len = old_bev.header.size - sizeof(old_bev);
		if (readn(input, filename, len) != len)
			return -1;

		bev.header = old_bev.header;

		/*
		 * As the pid is the missing value, we need to fill
		 * it properly. The header.misc value give us nice hint.
		 */
		bev.pid	= HOST_KERNEL_ID;
		if (bev.header.misc == PERF_RECORD_MISC_GUEST_USER ||
		    bev.header.misc == PERF_RECORD_MISC_GUEST_KERNEL)
			bev.pid	= DEFAULT_GUEST_KERNEL_ID;

		memcpy(bev.build_id, old_bev.build_id, sizeof(bev.build_id));
		__event_process_build_id(&bev, filename, session);

		offset += bev.header.size;
	}

	return 0;
}

static int perf_header__read_build_ids(struct perf_header *header,
				       int input, u64 offset, u64 size)
{
	struct perf_session *session = container_of(header, struct perf_session, header);
	struct build_id_event bev;
	char filename[PATH_MAX];
	u64 limit = offset + size, orig_offset = offset;
	int err = -1;

	while (offset < limit) {
		ssize_t len;

		if (readn(input, &bev, sizeof(bev)) != sizeof(bev))
			goto out;

		if (header->needs_swap)
			perf_event_header__bswap(&bev.header);

		len = bev.header.size - sizeof(bev);
		if (readn(input, filename, len) != len)
			goto out;
		/*
		 * The a1645ce1 changeset:
		 *
		 * "perf: 'perf kvm' tool for monitoring guest performance from host"
		 *
		 * Added a field to struct build_id_event that broke the file
		 * format.
		 *
		 * Since the kernel build-id is the first entry, process the
		 * table using the old format if the well known
		 * '[kernel.kallsyms]' string for the kernel build-id has the
		 * first 4 characters chopped off (where the pid_t sits).
		 */
		if (memcmp(filename, "nel.kallsyms]", 13) == 0) {
			if (lseek(input, orig_offset, SEEK_SET) == (off_t)-1)
				return -1;
			return perf_header__read_build_ids_abi_quirk(header, input, offset, size);
		}

		__event_process_build_id(&bev, filename, session);

		offset += bev.header.size;
	}
	err = 0;
out:
	return err;
}

/* Macro for features that simply need to read and store a string. */
#define FEAT_PROCESS_STR_FUN(__feat, __feat_env) \
static int process_##__feat(struct feat_fd *ff, void *data __maybe_unused) \
{\
	ff->ph->env.__feat_env = do_read_string(ff); \
	return ff->ph->env.__feat_env ? 0 : -ENOMEM; \
}

FEAT_PROCESS_STR_FUN(hostname, hostname);
FEAT_PROCESS_STR_FUN(osrelease, os_release);
FEAT_PROCESS_STR_FUN(version, version);
FEAT_PROCESS_STR_FUN(arch, arch);
FEAT_PROCESS_STR_FUN(cpudesc, cpu_desc);
FEAT_PROCESS_STR_FUN(cpuid, cpuid);

static int process_tracing_data(struct feat_fd *ff, void *data)
{
	ssize_t ret = trace_report(ff->fd, data, false);

	return ret < 0 ? -1 : 0;
}

static int process_build_id(struct feat_fd *ff, void *data __maybe_unused)
{
	if (perf_header__read_build_ids(ff->ph, ff->fd, ff->offset, ff->size))
		pr_debug("Failed to read buildids, continuing...\n");
	return 0;
}

static int process_nrcpus(struct feat_fd *ff, void *data __maybe_unused)
{
	int ret;
	u32 nr_cpus_avail, nr_cpus_online;

	ret = do_read_u32(ff, &nr_cpus_avail);
	if (ret)
		return ret;

	ret = do_read_u32(ff, &nr_cpus_online);
	if (ret)
		return ret;
	ff->ph->env.nr_cpus_avail = (int)nr_cpus_avail;
	ff->ph->env.nr_cpus_online = (int)nr_cpus_online;
	return 0;
}

static int process_total_mem(struct feat_fd *ff, void *data __maybe_unused)
{
	u64 total_mem;
	int ret;

	ret = do_read_u64(ff, &total_mem);
	if (ret)
		return -1;
	ff->ph->env.total_mem = (unsigned long long)total_mem;
	return 0;
}

static struct perf_evsel *
perf_evlist__find_by_index(struct perf_evlist *evlist, int idx)
{
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->idx == idx)
			return evsel;
	}

	return NULL;
}

static void
perf_evlist__set_event_name(struct perf_evlist *evlist,
			    struct perf_evsel *event)
{
	struct perf_evsel *evsel;

	if (!event->name)
		return;

	evsel = perf_evlist__find_by_index(evlist, event->idx);
	if (!evsel)
		return;

	if (evsel->name)
		return;

	evsel->name = strdup(event->name);
}

static int
process_event_desc(struct feat_fd *ff, void *data __maybe_unused)
{
	struct perf_session *session;
	struct perf_evsel *evsel, *events = read_event_desc(ff);

	if (!events)
		return 0;

	session = container_of(ff->ph, struct perf_session, header);
	for (evsel = events; evsel->attr.size; evsel++)
		perf_evlist__set_event_name(session->evlist, evsel);

	free_event_desc(events);

	return 0;
}

static int process_cmdline(struct feat_fd *ff, void *data __maybe_unused)
{
	char *str, *cmdline = NULL, **argv = NULL;
	u32 nr, i, len = 0;

	if (do_read_u32(ff, &nr))
		return -1;

	ff->ph->env.nr_cmdline = nr;

	cmdline = zalloc(ff->size + nr + 1);
	if (!cmdline)
		return -1;

	argv = zalloc(sizeof(char *) * (nr + 1));
	if (!argv)
		goto error;

	for (i = 0; i < nr; i++) {
		str = do_read_string(ff);
		if (!str)
			goto error;

		argv[i] = cmdline + len;
		memcpy(argv[i], str, strlen(str) + 1);
		len += strlen(str) + 1;
		free(str);
	}
	ff->ph->env.cmdline = cmdline;
	ff->ph->env.cmdline_argv = (const char **) argv;
	return 0;

error:
	free(argv);
	free(cmdline);
	return -1;
}

static int process_cpu_topology(struct feat_fd *ff, void *data __maybe_unused)
{
	u32 nr, i;
	char *str;
	struct strbuf sb;
	int cpu_nr = ff->ph->env.nr_cpus_avail;
	u64 size = 0;
	struct perf_header *ph = ff->ph;

	ph->env.cpu = calloc(cpu_nr, sizeof(*ph->env.cpu));
	if (!ph->env.cpu)
		return -1;

	if (do_read_u32(ff, &nr))
		goto free_cpu;

	ph->env.nr_sibling_cores = nr;
	size += sizeof(u32);
	if (strbuf_init(&sb, 128) < 0)
		goto free_cpu;

	for (i = 0; i < nr; i++) {
		str = do_read_string(ff);
		if (!str)
			goto error;

		/* include a NULL character at the end */
		if (strbuf_add(&sb, str, strlen(str) + 1) < 0)
			goto error;
		size += string_size(str);
		free(str);
	}
	ph->env.sibling_cores = strbuf_detach(&sb, NULL);

	if (do_read_u32(ff, &nr))
		return -1;

	ph->env.nr_sibling_threads = nr;
	size += sizeof(u32);

	for (i = 0; i < nr; i++) {
		str = do_read_string(ff);
		if (!str)
			goto error;

		/* include a NULL character at the end */
		if (strbuf_add(&sb, str, strlen(str) + 1) < 0)
			goto error;
		size += string_size(str);
		free(str);
	}
	ph->env.sibling_threads = strbuf_detach(&sb, NULL);

	/*
	 * The header may be from old perf,
	 * which doesn't include core id and socket id information.
	 */
	if (ff->size <= size) {
		zfree(&ph->env.cpu);
		return 0;
	}

	for (i = 0; i < (u32)cpu_nr; i++) {
		if (do_read_u32(ff, &nr))
			goto free_cpu;

		ph->env.cpu[i].core_id = nr;

		if (do_read_u32(ff, &nr))
			goto free_cpu;

		if (nr != (u32)-1 && nr > (u32)cpu_nr) {
			pr_debug("socket_id number is too big."
				 "You may need to upgrade the perf tool.\n");
			goto free_cpu;
		}

		ph->env.cpu[i].socket_id = nr;
	}

	return 0;

error:
	strbuf_release(&sb);
free_cpu:
	zfree(&ph->env.cpu);
	return -1;
}

static int process_numa_topology(struct feat_fd *ff, void *data __maybe_unused)
{
	struct numa_node *nodes, *n;
	u32 nr, i;
	char *str;

	/* nr nodes */
	if (do_read_u32(ff, &nr))
		return -1;

	nodes = zalloc(sizeof(*nodes) * nr);
	if (!nodes)
		return -ENOMEM;

	for (i = 0; i < nr; i++) {
		n = &nodes[i];

		/* node number */
		if (do_read_u32(ff, &n->node))
			goto error;

		if (do_read_u64(ff, &n->mem_total))
			goto error;

		if (do_read_u64(ff, &n->mem_free))
			goto error;

		str = do_read_string(ff);
		if (!str)
			goto error;

		n->map = cpu_map__new(str);
		if (!n->map)
			goto error;

		free(str);
	}
	ff->ph->env.nr_numa_nodes = nr;
	ff->ph->env.numa_nodes = nodes;
	return 0;

error:
	free(nodes);
	return -1;
}

static int process_pmu_mappings(struct feat_fd *ff, void *data __maybe_unused)
{
	char *name;
	u32 pmu_num;
	u32 type;
	struct strbuf sb;

	if (do_read_u32(ff, &pmu_num))
		return -1;

	if (!pmu_num) {
		pr_debug("pmu mappings not available\n");
		return 0;
	}

	ff->ph->env.nr_pmu_mappings = pmu_num;
	if (strbuf_init(&sb, 128) < 0)
		return -1;

	while (pmu_num) {
		if (do_read_u32(ff, &type))
			goto error;

		name = do_read_string(ff);
		if (!name)
			goto error;

		if (strbuf_addf(&sb, "%u:%s", type, name) < 0)
			goto error;
		/* include a NULL character at the end */
		if (strbuf_add(&sb, "", 1) < 0)
			goto error;

		if (!strcmp(name, "msr"))
			ff->ph->env.msr_pmu_type = type;

		free(name);
		pmu_num--;
	}
	ff->ph->env.pmu_mappings = strbuf_detach(&sb, NULL);
	return 0;

error:
	strbuf_release(&sb);
	return -1;
}

static int process_group_desc(struct feat_fd *ff, void *data __maybe_unused)
{
	size_t ret = -1;
	u32 i, nr, nr_groups;
	struct perf_session *session;
	struct perf_evsel *evsel, *leader = NULL;
	struct group_desc {
		char *name;
		u32 leader_idx;
		u32 nr_members;
	} *desc;

	if (do_read_u32(ff, &nr_groups))
		return -1;

	ff->ph->env.nr_groups = nr_groups;
	if (!nr_groups) {
		pr_debug("group desc not available\n");
		return 0;
	}

	desc = calloc(nr_groups, sizeof(*desc));
	if (!desc)
		return -1;

	for (i = 0; i < nr_groups; i++) {
		desc[i].name = do_read_string(ff);
		if (!desc[i].name)
			goto out_free;

		if (do_read_u32(ff, &desc[i].leader_idx))
			goto out_free;

		if (do_read_u32(ff, &desc[i].nr_members))
			goto out_free;
	}

	/*
	 * Rebuild group relationship based on the group_desc
	 */
	session = container_of(ff->ph, struct perf_session, header);
	session->evlist->nr_groups = nr_groups;

	i = nr = 0;
	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->idx == (int) desc[i].leader_idx) {
			evsel->leader = evsel;
			/* {anon_group} is a dummy name */
			if (strcmp(desc[i].name, "{anon_group}")) {
				evsel->group_name = desc[i].name;
				desc[i].name = NULL;
			}
			evsel->nr_members = desc[i].nr_members;

			if (i >= nr_groups || nr > 0) {
				pr_debug("invalid group desc\n");
				goto out_free;
			}

			leader = evsel;
			nr = evsel->nr_members - 1;
			i++;
		} else if (nr) {
			/* This is a group member */
			evsel->leader = leader;

			nr--;
		}
	}

	if (i != nr_groups || nr != 0) {
		pr_debug("invalid group desc\n");
		goto out_free;
	}

	ret = 0;
out_free:
	for (i = 0; i < nr_groups; i++)
		zfree(&desc[i].name);
	free(desc);

	return ret;
}

static int process_auxtrace(struct feat_fd *ff, void *data __maybe_unused)
{
	struct perf_session *session;
	int err;

	session = container_of(ff->ph, struct perf_session, header);

	err = auxtrace_index__process(ff->fd, ff->size, session,
				      ff->ph->needs_swap);
	if (err < 0)
		pr_err("Failed to process auxtrace index\n");
	return err;
}

static int process_cache(struct feat_fd *ff, void *data __maybe_unused)
{
	struct cpu_cache_level *caches;
	u32 cnt, i, version;

	if (do_read_u32(ff, &version))
		return -1;

	if (version != 1)
		return -1;

	if (do_read_u32(ff, &cnt))
		return -1;

	caches = zalloc(sizeof(*caches) * cnt);
	if (!caches)
		return -1;

	for (i = 0; i < cnt; i++) {
		struct cpu_cache_level c;

		#define _R(v)						\
			if (do_read_u32(ff, &c.v))\
				goto out_free_caches;			\

		_R(level)
		_R(line_size)
		_R(sets)
		_R(ways)
		#undef _R

		#define _R(v)					\
			c.v = do_read_string(ff);		\
			if (!c.v)				\
				goto out_free_caches;

		_R(type)
		_R(size)
		_R(map)
		#undef _R

		caches[i] = c;
	}

	ff->ph->env.caches = caches;
	ff->ph->env.caches_cnt = cnt;
	return 0;
out_free_caches:
	free(caches);
	return -1;
}

struct feature_ops {
	int (*write)(struct feat_fd *ff, struct perf_evlist *evlist);
	void (*print)(struct feat_fd *ff, FILE *fp);
	int (*process)(struct feat_fd *ff, void *data);
	const char *name;
	bool full_only;
};

#define FEAT_OPA(n, func) \
	[n] = { .name = #n, .write = write_##func, .print = print_##func }
#define FEAT_OPP(n, func) \
	[n] = { .name = #n, .write = write_##func, .print = print_##func, \
		.process = process_##func }
#define FEAT_OPF(n, func) \
	[n] = { .name = #n, .write = write_##func, .print = print_##func, \
		.process = process_##func, .full_only = true }

/* feature_ops not implemented: */
#define print_tracing_data	NULL
#define print_build_id		NULL

static const struct feature_ops feat_ops[HEADER_LAST_FEATURE] = {
	FEAT_OPP(HEADER_TRACING_DATA,	tracing_data),
	FEAT_OPP(HEADER_BUILD_ID,	build_id),
	FEAT_OPP(HEADER_HOSTNAME,	hostname),
	FEAT_OPP(HEADER_OSRELEASE,	osrelease),
	FEAT_OPP(HEADER_VERSION,	version),
	FEAT_OPP(HEADER_ARCH,		arch),
	FEAT_OPP(HEADER_NRCPUS,		nrcpus),
	FEAT_OPP(HEADER_CPUDESC,	cpudesc),
	FEAT_OPP(HEADER_CPUID,		cpuid),
	FEAT_OPP(HEADER_TOTAL_MEM,	total_mem),
	FEAT_OPP(HEADER_EVENT_DESC,	event_desc),
	FEAT_OPP(HEADER_CMDLINE,	cmdline),
	FEAT_OPF(HEADER_CPU_TOPOLOGY,	cpu_topology),
	FEAT_OPF(HEADER_NUMA_TOPOLOGY,	numa_topology),
	FEAT_OPA(HEADER_BRANCH_STACK,	branch_stack),
	FEAT_OPP(HEADER_PMU_MAPPINGS,	pmu_mappings),
	FEAT_OPP(HEADER_GROUP_DESC,	group_desc),
	FEAT_OPP(HEADER_AUXTRACE,	auxtrace),
	FEAT_OPA(HEADER_STAT,		stat),
	FEAT_OPF(HEADER_CACHE,		cache),
};

struct header_print_data {
	FILE *fp;
	bool full; /* extended list of headers */
};

static int perf_file_section__fprintf_info(struct perf_file_section *section,
					   struct perf_header *ph,
					   int feat, int fd, void *data)
{
	struct header_print_data *hd = data;
	struct feat_fd ff;

	if (lseek(fd, section->offset, SEEK_SET) == (off_t)-1) {
		pr_debug("Failed to lseek to %" PRIu64 " offset for feature "
				"%d, continuing...\n", section->offset, feat);
		return 0;
	}
	if (feat >= HEADER_LAST_FEATURE) {
		pr_warning("unknown feature %d\n", feat);
		return 0;
	}
	if (!feat_ops[feat].print)
		return 0;

	ff = (struct  feat_fd) {
		.fd = fd,
		.ph = ph,
	};

	if (!feat_ops[feat].full_only || hd->full)
		feat_ops[feat].print(&ff, hd->fp);
	else
		fprintf(hd->fp, "# %s info available, use -I to display\n",
			feat_ops[feat].name);

	return 0;
}

int perf_header__fprintf_info(struct perf_session *session, FILE *fp, bool full)
{
	struct header_print_data hd;
	struct perf_header *header = &session->header;
	int fd = perf_data_file__fd(session->file);
	struct stat st;
	int ret, bit;

	hd.fp = fp;
	hd.full = full;

	ret = fstat(fd, &st);
	if (ret == -1)
		return -1;

	fprintf(fp, "# captured on: %s", ctime(&st.st_ctime));

	perf_header__process_sections(header, fd, &hd,
				      perf_file_section__fprintf_info);

	if (session->file->is_pipe)
		return 0;

	fprintf(fp, "# missing features: ");
	for_each_clear_bit(bit, header->adds_features, HEADER_LAST_FEATURE) {
		if (bit)
			fprintf(fp, "%s ", feat_ops[bit].name);
	}

	fprintf(fp, "\n");
	return 0;
}

static int do_write_feat(struct feat_fd *ff, int type,
			 struct perf_file_section **p,
			 struct perf_evlist *evlist)
{
	int err;
	int ret = 0;

	if (perf_header__has_feat(ff->ph, type)) {
		if (!feat_ops[type].write)
			return -1;

		(*p)->offset = lseek(ff->fd, 0, SEEK_CUR);

		err = feat_ops[type].write(ff, evlist);
		if (err < 0) {
			pr_debug("failed to write feature %s\n", feat_ops[type].name);

			/* undo anything written */
			lseek(ff->fd, (*p)->offset, SEEK_SET);

			return -1;
		}
		(*p)->size = lseek(ff->fd, 0, SEEK_CUR) - (*p)->offset;
		(*p)++;
	}
	return ret;
}

static int perf_header__adds_write(struct perf_header *header,
				   struct perf_evlist *evlist, int fd)
{
	int nr_sections;
	struct feat_fd ff;
	struct perf_file_section *feat_sec, *p;
	int sec_size;
	u64 sec_start;
	int feat;
	int err;

	ff = (struct feat_fd){
		.fd  = fd,
		.ph = header,
	};

	nr_sections = bitmap_weight(header->adds_features, HEADER_FEAT_BITS);
	if (!nr_sections)
		return 0;

	feat_sec = p = calloc(nr_sections, sizeof(*feat_sec));
	if (feat_sec == NULL)
		return -ENOMEM;

	sec_size = sizeof(*feat_sec) * nr_sections;

	sec_start = header->feat_offset;
	lseek(fd, sec_start + sec_size, SEEK_SET);

	for_each_set_bit(feat, header->adds_features, HEADER_FEAT_BITS) {
		if (do_write_feat(&ff, feat, &p, evlist))
			perf_header__clear_feat(header, feat);
	}

	lseek(fd, sec_start, SEEK_SET);
	/*
	 * may write more than needed due to dropped feature, but
	 * this is okay, reader will skip the mising entries
	 */
	err = do_write(&ff, feat_sec, sec_size);
	if (err < 0)
		pr_debug("failed to write feature section\n");
	free(feat_sec);
	return err;
}

int perf_header__write_pipe(int fd)
{
	struct perf_pipe_file_header f_header;
	struct feat_fd ff;
	int err;

	ff = (struct feat_fd){ .fd = fd };

	f_header = (struct perf_pipe_file_header){
		.magic	   = PERF_MAGIC,
		.size	   = sizeof(f_header),
	};

	err = do_write(&ff, &f_header, sizeof(f_header));
	if (err < 0) {
		pr_debug("failed to write perf pipe header\n");
		return err;
	}

	return 0;
}

int perf_session__write_header(struct perf_session *session,
			       struct perf_evlist *evlist,
			       int fd, bool at_exit)
{
	struct perf_file_header f_header;
	struct perf_file_attr   f_attr;
	struct perf_header *header = &session->header;
	struct perf_evsel *evsel;
	struct feat_fd ff;
	u64 attr_offset;
	int err;

	ff = (struct feat_fd){ .fd = fd};
	lseek(fd, sizeof(f_header), SEEK_SET);

	evlist__for_each_entry(session->evlist, evsel) {
		evsel->id_offset = lseek(fd, 0, SEEK_CUR);
		err = do_write(&ff, evsel->id, evsel->ids * sizeof(u64));
		if (err < 0) {
			pr_debug("failed to write perf header\n");
			return err;
		}
	}

	attr_offset = lseek(ff.fd, 0, SEEK_CUR);

	evlist__for_each_entry(evlist, evsel) {
		f_attr = (struct perf_file_attr){
			.attr = evsel->attr,
			.ids  = {
				.offset = evsel->id_offset,
				.size   = evsel->ids * sizeof(u64),
			}
		};
		err = do_write(&ff, &f_attr, sizeof(f_attr));
		if (err < 0) {
			pr_debug("failed to write perf header attribute\n");
			return err;
		}
	}

	if (!header->data_offset)
		header->data_offset = lseek(fd, 0, SEEK_CUR);
	header->feat_offset = header->data_offset + header->data_size;

	if (at_exit) {
		err = perf_header__adds_write(header, evlist, fd);
		if (err < 0)
			return err;
	}

	f_header = (struct perf_file_header){
		.magic	   = PERF_MAGIC,
		.size	   = sizeof(f_header),
		.attr_size = sizeof(f_attr),
		.attrs = {
			.offset = attr_offset,
			.size   = evlist->nr_entries * sizeof(f_attr),
		},
		.data = {
			.offset = header->data_offset,
			.size	= header->data_size,
		},
		/* event_types is ignored, store zeros */
	};

	memcpy(&f_header.adds_features, &header->adds_features, sizeof(header->adds_features));

	lseek(fd, 0, SEEK_SET);
	err = do_write(&ff, &f_header, sizeof(f_header));
	if (err < 0) {
		pr_debug("failed to write perf header\n");
		return err;
	}
	lseek(fd, header->data_offset + header->data_size, SEEK_SET);

	return 0;
}

static int perf_header__getbuffer64(struct perf_header *header,
				    int fd, void *buf, size_t size)
{
	if (readn(fd, buf, size) <= 0)
		return -1;

	if (header->needs_swap)
		mem_bswap_64(buf, size);

	return 0;
}

int perf_header__process_sections(struct perf_header *header, int fd,
				  void *data,
				  int (*process)(struct perf_file_section *section,
						 struct perf_header *ph,
						 int feat, int fd, void *data))
{
	struct perf_file_section *feat_sec, *sec;
	int nr_sections;
	int sec_size;
	int feat;
	int err;

	nr_sections = bitmap_weight(header->adds_features, HEADER_FEAT_BITS);
	if (!nr_sections)
		return 0;

	feat_sec = sec = calloc(nr_sections, sizeof(*feat_sec));
	if (!feat_sec)
		return -1;

	sec_size = sizeof(*feat_sec) * nr_sections;

	lseek(fd, header->feat_offset, SEEK_SET);

	err = perf_header__getbuffer64(header, fd, feat_sec, sec_size);
	if (err < 0)
		goto out_free;

	for_each_set_bit(feat, header->adds_features, HEADER_LAST_FEATURE) {
		err = process(sec++, header, feat, fd, data);
		if (err < 0)
			goto out_free;
	}
	err = 0;
out_free:
	free(feat_sec);
	return err;
}

static const int attr_file_abi_sizes[] = {
	[0] = PERF_ATTR_SIZE_VER0,
	[1] = PERF_ATTR_SIZE_VER1,
	[2] = PERF_ATTR_SIZE_VER2,
	[3] = PERF_ATTR_SIZE_VER3,
	[4] = PERF_ATTR_SIZE_VER4,
	0,
};

/*
 * In the legacy file format, the magic number is not used to encode endianness.
 * hdr_sz was used to encode endianness. But given that hdr_sz can vary based
 * on ABI revisions, we need to try all combinations for all endianness to
 * detect the endianness.
 */
static int try_all_file_abis(uint64_t hdr_sz, struct perf_header *ph)
{
	uint64_t ref_size, attr_size;
	int i;

	for (i = 0 ; attr_file_abi_sizes[i]; i++) {
		ref_size = attr_file_abi_sizes[i]
			 + sizeof(struct perf_file_section);
		if (hdr_sz != ref_size) {
			attr_size = bswap_64(hdr_sz);
			if (attr_size != ref_size)
				continue;

			ph->needs_swap = true;
		}
		pr_debug("ABI%d perf.data file detected, need_swap=%d\n",
			 i,
			 ph->needs_swap);
		return 0;
	}
	/* could not determine endianness */
	return -1;
}

#define PERF_PIPE_HDR_VER0	16

static const size_t attr_pipe_abi_sizes[] = {
	[0] = PERF_PIPE_HDR_VER0,
	0,
};

/*
 * In the legacy pipe format, there is an implicit assumption that endiannesss
 * between host recording the samples, and host parsing the samples is the
 * same. This is not always the case given that the pipe output may always be
 * redirected into a file and analyzed on a different machine with possibly a
 * different endianness and perf_event ABI revsions in the perf tool itself.
 */
static int try_all_pipe_abis(uint64_t hdr_sz, struct perf_header *ph)
{
	u64 attr_size;
	int i;

	for (i = 0 ; attr_pipe_abi_sizes[i]; i++) {
		if (hdr_sz != attr_pipe_abi_sizes[i]) {
			attr_size = bswap_64(hdr_sz);
			if (attr_size != hdr_sz)
				continue;

			ph->needs_swap = true;
		}
		pr_debug("Pipe ABI%d perf.data file detected\n", i);
		return 0;
	}
	return -1;
}

bool is_perf_magic(u64 magic)
{
	if (!memcmp(&magic, __perf_magic1, sizeof(magic))
		|| magic == __perf_magic2
		|| magic == __perf_magic2_sw)
		return true;

	return false;
}

static int check_magic_endian(u64 magic, uint64_t hdr_sz,
			      bool is_pipe, struct perf_header *ph)
{
	int ret;

	/* check for legacy format */
	ret = memcmp(&magic, __perf_magic1, sizeof(magic));
	if (ret == 0) {
		ph->version = PERF_HEADER_VERSION_1;
		pr_debug("legacy perf.data format\n");
		if (is_pipe)
			return try_all_pipe_abis(hdr_sz, ph);

		return try_all_file_abis(hdr_sz, ph);
	}
	/*
	 * the new magic number serves two purposes:
	 * - unique number to identify actual perf.data files
	 * - encode endianness of file
	 */
	ph->version = PERF_HEADER_VERSION_2;

	/* check magic number with one endianness */
	if (magic == __perf_magic2)
		return 0;

	/* check magic number with opposite endianness */
	if (magic != __perf_magic2_sw)
		return -1;

	ph->needs_swap = true;

	return 0;
}

int perf_file_header__read(struct perf_file_header *header,
			   struct perf_header *ph, int fd)
{
	ssize_t ret;

	lseek(fd, 0, SEEK_SET);

	ret = readn(fd, header, sizeof(*header));
	if (ret <= 0)
		return -1;

	if (check_magic_endian(header->magic,
			       header->attr_size, false, ph) < 0) {
		pr_debug("magic/endian check failed\n");
		return -1;
	}

	if (ph->needs_swap) {
		mem_bswap_64(header, offsetof(struct perf_file_header,
			     adds_features));
	}

	if (header->size != sizeof(*header)) {
		/* Support the previous format */
		if (header->size == offsetof(typeof(*header), adds_features))
			bitmap_zero(header->adds_features, HEADER_FEAT_BITS);
		else
			return -1;
	} else if (ph->needs_swap) {
		/*
		 * feature bitmap is declared as an array of unsigned longs --
		 * not good since its size can differ between the host that
		 * generated the data file and the host analyzing the file.
		 *
		 * We need to handle endianness, but we don't know the size of
		 * the unsigned long where the file was generated. Take a best
		 * guess at determining it: try 64-bit swap first (ie., file
		 * created on a 64-bit host), and check if the hostname feature
		 * bit is set (this feature bit is forced on as of fbe96f2).
		 * If the bit is not, undo the 64-bit swap and try a 32-bit
		 * swap. If the hostname bit is still not set (e.g., older data
		 * file), punt and fallback to the original behavior --
		 * clearing all feature bits and setting buildid.
		 */
		mem_bswap_64(&header->adds_features,
			    BITS_TO_U64(HEADER_FEAT_BITS));

		if (!test_bit(HEADER_HOSTNAME, header->adds_features)) {
			/* unswap as u64 */
			mem_bswap_64(&header->adds_features,
				    BITS_TO_U64(HEADER_FEAT_BITS));

			/* unswap as u32 */
			mem_bswap_32(&header->adds_features,
				    BITS_TO_U32(HEADER_FEAT_BITS));
		}

		if (!test_bit(HEADER_HOSTNAME, header->adds_features)) {
			bitmap_zero(header->adds_features, HEADER_FEAT_BITS);
			set_bit(HEADER_BUILD_ID, header->adds_features);
		}
	}

	memcpy(&ph->adds_features, &header->adds_features,
	       sizeof(ph->adds_features));

	ph->data_offset  = header->data.offset;
	ph->data_size	 = header->data.size;
	ph->feat_offset  = header->data.offset + header->data.size;
	return 0;
}

static int perf_file_section__process(struct perf_file_section *section,
				      struct perf_header *ph,
				      int feat, int fd, void *data)
{
	struct feat_fd fdd = {
		.fd	= fd,
		.ph	= ph,
		.size	= section->size,
		.offset	= section->offset,
	};

	if (lseek(fd, section->offset, SEEK_SET) == (off_t)-1) {
		pr_debug("Failed to lseek to %" PRIu64 " offset for feature "
			  "%d, continuing...\n", section->offset, feat);
		return 0;
	}

	if (feat >= HEADER_LAST_FEATURE) {
		pr_debug("unknown feature %d, continuing...\n", feat);
		return 0;
	}

	if (!feat_ops[feat].process)
		return 0;

	return feat_ops[feat].process(&fdd, data);
}

static int perf_file_header__read_pipe(struct perf_pipe_file_header *header,
				       struct perf_header *ph, int fd,
				       bool repipe)
{
	struct feat_fd ff = {
		.fd = STDOUT_FILENO,
		.ph = ph,
	};
	ssize_t ret;

	ret = readn(fd, header, sizeof(*header));
	if (ret <= 0)
		return -1;

	if (check_magic_endian(header->magic, header->size, true, ph) < 0) {
		pr_debug("endian/magic failed\n");
		return -1;
	}

	if (ph->needs_swap)
		header->size = bswap_64(header->size);

	if (repipe && do_write(&ff, header, sizeof(*header)) < 0)
		return -1;

	return 0;
}

static int perf_header__read_pipe(struct perf_session *session)
{
	struct perf_header *header = &session->header;
	struct perf_pipe_file_header f_header;

	if (perf_file_header__read_pipe(&f_header, header,
					perf_data_file__fd(session->file),
					session->repipe) < 0) {
		pr_debug("incompatible file format\n");
		return -EINVAL;
	}

	return 0;
}

static int read_attr(int fd, struct perf_header *ph,
		     struct perf_file_attr *f_attr)
{
	struct perf_event_attr *attr = &f_attr->attr;
	size_t sz, left;
	size_t our_sz = sizeof(f_attr->attr);
	ssize_t ret;

	memset(f_attr, 0, sizeof(*f_attr));

	/* read minimal guaranteed structure */
	ret = readn(fd, attr, PERF_ATTR_SIZE_VER0);
	if (ret <= 0) {
		pr_debug("cannot read %d bytes of header attr\n",
			 PERF_ATTR_SIZE_VER0);
		return -1;
	}

	/* on file perf_event_attr size */
	sz = attr->size;

	if (ph->needs_swap)
		sz = bswap_32(sz);

	if (sz == 0) {
		/* assume ABI0 */
		sz =  PERF_ATTR_SIZE_VER0;
	} else if (sz > our_sz) {
		pr_debug("file uses a more recent and unsupported ABI"
			 " (%zu bytes extra)\n", sz - our_sz);
		return -1;
	}
	/* what we have not yet read and that we know about */
	left = sz - PERF_ATTR_SIZE_VER0;
	if (left) {
		void *ptr = attr;
		ptr += PERF_ATTR_SIZE_VER0;

		ret = readn(fd, ptr, left);
	}
	/* read perf_file_section, ids are read in caller */
	ret = readn(fd, &f_attr->ids, sizeof(f_attr->ids));

	return ret <= 0 ? -1 : 0;
}

static int perf_evsel__prepare_tracepoint_event(struct perf_evsel *evsel,
						struct pevent *pevent)
{
	struct event_format *event;
	char bf[128];

	/* already prepared */
	if (evsel->tp_format)
		return 0;

	if (pevent == NULL) {
		pr_debug("broken or missing trace data\n");
		return -1;
	}

	event = pevent_find_event(pevent, evsel->attr.config);
	if (event == NULL) {
		pr_debug("cannot find event format for %d\n", (int)evsel->attr.config);
		return -1;
	}

	if (!evsel->name) {
		snprintf(bf, sizeof(bf), "%s:%s", event->system, event->name);
		evsel->name = strdup(bf);
		if (evsel->name == NULL)
			return -1;
	}

	evsel->tp_format = event;
	return 0;
}

static int perf_evlist__prepare_tracepoint_events(struct perf_evlist *evlist,
						  struct pevent *pevent)
{
	struct perf_evsel *pos;

	evlist__for_each_entry(evlist, pos) {
		if (pos->attr.type == PERF_TYPE_TRACEPOINT &&
		    perf_evsel__prepare_tracepoint_event(pos, pevent))
			return -1;
	}

	return 0;
}

int perf_session__read_header(struct perf_session *session)
{
	struct perf_data_file *file = session->file;
	struct perf_header *header = &session->header;
	struct perf_file_header	f_header;
	struct perf_file_attr	f_attr;
	u64			f_id;
	int nr_attrs, nr_ids, i, j;
	int fd = perf_data_file__fd(file);

	session->evlist = perf_evlist__new();
	if (session->evlist == NULL)
		return -ENOMEM;

	session->evlist->env = &header->env;
	session->machines.host.env = &header->env;
	if (perf_data_file__is_pipe(file))
		return perf_header__read_pipe(session);

	if (perf_file_header__read(&f_header, header, fd) < 0)
		return -EINVAL;

	/*
	 * Sanity check that perf.data was written cleanly; data size is
	 * initialized to 0 and updated only if the on_exit function is run.
	 * If data size is still 0 then the file contains only partial
	 * information.  Just warn user and process it as much as it can.
	 */
	if (f_header.data.size == 0) {
		pr_warning("WARNING: The %s file's data size field is 0 which is unexpected.\n"
			   "Was the 'perf record' command properly terminated?\n",
			   file->path);
	}

	nr_attrs = f_header.attrs.size / f_header.attr_size;
	lseek(fd, f_header.attrs.offset, SEEK_SET);

	for (i = 0; i < nr_attrs; i++) {
		struct perf_evsel *evsel;
		off_t tmp;

		if (read_attr(fd, header, &f_attr) < 0)
			goto out_errno;

		if (header->needs_swap) {
			f_attr.ids.size   = bswap_64(f_attr.ids.size);
			f_attr.ids.offset = bswap_64(f_attr.ids.offset);
			perf_event__attr_swap(&f_attr.attr);
		}

		tmp = lseek(fd, 0, SEEK_CUR);
		evsel = perf_evsel__new(&f_attr.attr);

		if (evsel == NULL)
			goto out_delete_evlist;

		evsel->needs_swap = header->needs_swap;
		/*
		 * Do it before so that if perf_evsel__alloc_id fails, this
		 * entry gets purged too at perf_evlist__delete().
		 */
		perf_evlist__add(session->evlist, evsel);

		nr_ids = f_attr.ids.size / sizeof(u64);
		/*
		 * We don't have the cpu and thread maps on the header, so
		 * for allocating the perf_sample_id table we fake 1 cpu and
		 * hattr->ids threads.
		 */
		if (perf_evsel__alloc_id(evsel, 1, nr_ids))
			goto out_delete_evlist;

		lseek(fd, f_attr.ids.offset, SEEK_SET);

		for (j = 0; j < nr_ids; j++) {
			if (perf_header__getbuffer64(header, fd, &f_id, sizeof(f_id)))
				goto out_errno;

			perf_evlist__id_add(session->evlist, evsel, 0, j, f_id);
		}

		lseek(fd, tmp, SEEK_SET);
	}

	symbol_conf.nr_events = nr_attrs;

	perf_header__process_sections(header, fd, &session->tevent,
				      perf_file_section__process);

	if (perf_evlist__prepare_tracepoint_events(session->evlist,
						   session->tevent.pevent))
		goto out_delete_evlist;

	return 0;
out_errno:
	return -errno;

out_delete_evlist:
	perf_evlist__delete(session->evlist);
	session->evlist = NULL;
	return -ENOMEM;
}

int perf_event__synthesize_attr(struct perf_tool *tool,
				struct perf_event_attr *attr, u32 ids, u64 *id,
				perf_event__handler_t process)
{
	union perf_event *ev;
	size_t size;
	int err;

	size = sizeof(struct perf_event_attr);
	size = PERF_ALIGN(size, sizeof(u64));
	size += sizeof(struct perf_event_header);
	size += ids * sizeof(u64);

	ev = malloc(size);

	if (ev == NULL)
		return -ENOMEM;

	ev->attr.attr = *attr;
	memcpy(ev->attr.id, id, ids * sizeof(u64));

	ev->attr.header.type = PERF_RECORD_HEADER_ATTR;
	ev->attr.header.size = (u16)size;

	if (ev->attr.header.size == size)
		err = process(tool, ev, NULL, NULL);
	else
		err = -E2BIG;

	free(ev);

	return err;
}

static struct event_update_event *
event_update_event__new(size_t size, u64 type, u64 id)
{
	struct event_update_event *ev;

	size += sizeof(*ev);
	size  = PERF_ALIGN(size, sizeof(u64));

	ev = zalloc(size);
	if (ev) {
		ev->header.type = PERF_RECORD_EVENT_UPDATE;
		ev->header.size = (u16)size;
		ev->type = type;
		ev->id = id;
	}
	return ev;
}

int
perf_event__synthesize_event_update_unit(struct perf_tool *tool,
					 struct perf_evsel *evsel,
					 perf_event__handler_t process)
{
	struct event_update_event *ev;
	size_t size = strlen(evsel->unit);
	int err;

	ev = event_update_event__new(size + 1, PERF_EVENT_UPDATE__UNIT, evsel->id[0]);
	if (ev == NULL)
		return -ENOMEM;

	strncpy(ev->data, evsel->unit, size);
	err = process(tool, (union perf_event *)ev, NULL, NULL);
	free(ev);
	return err;
}

int
perf_event__synthesize_event_update_scale(struct perf_tool *tool,
					  struct perf_evsel *evsel,
					  perf_event__handler_t process)
{
	struct event_update_event *ev;
	struct event_update_event_scale *ev_data;
	int err;

	ev = event_update_event__new(sizeof(*ev_data), PERF_EVENT_UPDATE__SCALE, evsel->id[0]);
	if (ev == NULL)
		return -ENOMEM;

	ev_data = (struct event_update_event_scale *) ev->data;
	ev_data->scale = evsel->scale;
	err = process(tool, (union perf_event*) ev, NULL, NULL);
	free(ev);
	return err;
}

int
perf_event__synthesize_event_update_name(struct perf_tool *tool,
					 struct perf_evsel *evsel,
					 perf_event__handler_t process)
{
	struct event_update_event *ev;
	size_t len = strlen(evsel->name);
	int err;

	ev = event_update_event__new(len + 1, PERF_EVENT_UPDATE__NAME, evsel->id[0]);
	if (ev == NULL)
		return -ENOMEM;

	strncpy(ev->data, evsel->name, len);
	err = process(tool, (union perf_event*) ev, NULL, NULL);
	free(ev);
	return err;
}

int
perf_event__synthesize_event_update_cpus(struct perf_tool *tool,
					struct perf_evsel *evsel,
					perf_event__handler_t process)
{
	size_t size = sizeof(struct event_update_event);
	struct event_update_event *ev;
	int max, err;
	u16 type;

	if (!evsel->own_cpus)
		return 0;

	ev = cpu_map_data__alloc(evsel->own_cpus, &size, &type, &max);
	if (!ev)
		return -ENOMEM;

	ev->header.type = PERF_RECORD_EVENT_UPDATE;
	ev->header.size = (u16)size;
	ev->type = PERF_EVENT_UPDATE__CPUS;
	ev->id   = evsel->id[0];

	cpu_map_data__synthesize((struct cpu_map_data *) ev->data,
				 evsel->own_cpus,
				 type, max);

	err = process(tool, (union perf_event*) ev, NULL, NULL);
	free(ev);
	return err;
}

size_t perf_event__fprintf_event_update(union perf_event *event, FILE *fp)
{
	struct event_update_event *ev = &event->event_update;
	struct event_update_event_scale *ev_scale;
	struct event_update_event_cpus *ev_cpus;
	struct cpu_map *map;
	size_t ret;

	ret = fprintf(fp, "\n... id:    %" PRIu64 "\n", ev->id);

	switch (ev->type) {
	case PERF_EVENT_UPDATE__SCALE:
		ev_scale = (struct event_update_event_scale *) ev->data;
		ret += fprintf(fp, "... scale: %f\n", ev_scale->scale);
		break;
	case PERF_EVENT_UPDATE__UNIT:
		ret += fprintf(fp, "... unit:  %s\n", ev->data);
		break;
	case PERF_EVENT_UPDATE__NAME:
		ret += fprintf(fp, "... name:  %s\n", ev->data);
		break;
	case PERF_EVENT_UPDATE__CPUS:
		ev_cpus = (struct event_update_event_cpus *) ev->data;
		ret += fprintf(fp, "... ");

		map = cpu_map__new_data(&ev_cpus->cpus);
		if (map)
			ret += cpu_map__fprintf(map, fp);
		else
			ret += fprintf(fp, "failed to get cpus\n");
		break;
	default:
		ret += fprintf(fp, "... unknown type\n");
		break;
	}

	return ret;
}

int perf_event__synthesize_attrs(struct perf_tool *tool,
				   struct perf_session *session,
				   perf_event__handler_t process)
{
	struct perf_evsel *evsel;
	int err = 0;

	evlist__for_each_entry(session->evlist, evsel) {
		err = perf_event__synthesize_attr(tool, &evsel->attr, evsel->ids,
						  evsel->id, process);
		if (err) {
			pr_debug("failed to create perf header attribute\n");
			return err;
		}
	}

	return err;
}

int perf_event__process_attr(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_evlist **pevlist)
{
	u32 i, ids, n_ids;
	struct perf_evsel *evsel;
	struct perf_evlist *evlist = *pevlist;

	if (evlist == NULL) {
		*pevlist = evlist = perf_evlist__new();
		if (evlist == NULL)
			return -ENOMEM;
	}

	evsel = perf_evsel__new(&event->attr.attr);
	if (evsel == NULL)
		return -ENOMEM;

	perf_evlist__add(evlist, evsel);

	ids = event->header.size;
	ids -= (void *)&event->attr.id - (void *)event;
	n_ids = ids / sizeof(u64);
	/*
	 * We don't have the cpu and thread maps on the header, so
	 * for allocating the perf_sample_id table we fake 1 cpu and
	 * hattr->ids threads.
	 */
	if (perf_evsel__alloc_id(evsel, 1, n_ids))
		return -ENOMEM;

	for (i = 0; i < n_ids; i++) {
		perf_evlist__id_add(evlist, evsel, 0, i, event->attr.id[i]);
	}

	symbol_conf.nr_events = evlist->nr_entries;

	return 0;
}

int perf_event__process_event_update(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_evlist **pevlist)
{
	struct event_update_event *ev = &event->event_update;
	struct event_update_event_scale *ev_scale;
	struct event_update_event_cpus *ev_cpus;
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct cpu_map *map;

	if (!pevlist || *pevlist == NULL)
		return -EINVAL;

	evlist = *pevlist;

	evsel = perf_evlist__id2evsel(evlist, ev->id);
	if (evsel == NULL)
		return -EINVAL;

	switch (ev->type) {
	case PERF_EVENT_UPDATE__UNIT:
		evsel->unit = strdup(ev->data);
		break;
	case PERF_EVENT_UPDATE__NAME:
		evsel->name = strdup(ev->data);
		break;
	case PERF_EVENT_UPDATE__SCALE:
		ev_scale = (struct event_update_event_scale *) ev->data;
		evsel->scale = ev_scale->scale;
		break;
	case PERF_EVENT_UPDATE__CPUS:
		ev_cpus = (struct event_update_event_cpus *) ev->data;

		map = cpu_map__new_data(&ev_cpus->cpus);
		if (map)
			evsel->own_cpus = map;
		else
			pr_err("failed to get event_update cpus\n");
	default:
		break;
	}

	return 0;
}

int perf_event__synthesize_tracing_data(struct perf_tool *tool, int fd,
					struct perf_evlist *evlist,
					perf_event__handler_t process)
{
	union perf_event ev;
	struct tracing_data *tdata;
	ssize_t size = 0, aligned_size = 0, padding;
	struct feat_fd ff;
	int err __maybe_unused = 0;

	/*
	 * We are going to store the size of the data followed
	 * by the data contents. Since the fd descriptor is a pipe,
	 * we cannot seek back to store the size of the data once
	 * we know it. Instead we:
	 *
	 * - write the tracing data to the temp file
	 * - get/write the data size to pipe
	 * - write the tracing data from the temp file
	 *   to the pipe
	 */
	tdata = tracing_data_get(&evlist->entries, fd, true);
	if (!tdata)
		return -1;

	memset(&ev, 0, sizeof(ev));

	ev.tracing_data.header.type = PERF_RECORD_HEADER_TRACING_DATA;
	size = tdata->size;
	aligned_size = PERF_ALIGN(size, sizeof(u64));
	padding = aligned_size - size;
	ev.tracing_data.header.size = sizeof(ev.tracing_data);
	ev.tracing_data.size = aligned_size;

	process(tool, &ev, NULL, NULL);

	/*
	 * The put function will copy all the tracing data
	 * stored in temp file to the pipe.
	 */
	tracing_data_put(tdata);

	ff = (struct feat_fd){ .fd = fd };
	if (write_padded(&ff, NULL, 0, padding))
		return -1;

	return aligned_size;
}

int perf_event__process_tracing_data(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_session *session)
{
	ssize_t size_read, padding, size = event->tracing_data.size;
	int fd = perf_data_file__fd(session->file);
	off_t offset = lseek(fd, 0, SEEK_CUR);
	char buf[BUFSIZ];

	/* setup for reading amidst mmap */
	lseek(fd, offset + sizeof(struct tracing_data_event),
	      SEEK_SET);

	size_read = trace_report(fd, &session->tevent,
				 session->repipe);
	padding = PERF_ALIGN(size_read, sizeof(u64)) - size_read;

	if (readn(fd, buf, padding) < 0) {
		pr_err("%s: reading input file", __func__);
		return -1;
	}
	if (session->repipe) {
		int retw = write(STDOUT_FILENO, buf, padding);
		if (retw <= 0 || retw != padding) {
			pr_err("%s: repiping tracing data padding", __func__);
			return -1;
		}
	}

	if (size_read + padding != size) {
		pr_err("%s: tracing data size mismatch", __func__);
		return -1;
	}

	perf_evlist__prepare_tracepoint_events(session->evlist,
					       session->tevent.pevent);

	return size_read + padding;
}

int perf_event__synthesize_build_id(struct perf_tool *tool,
				    struct dso *pos, u16 misc,
				    perf_event__handler_t process,
				    struct machine *machine)
{
	union perf_event ev;
	size_t len;
	int err = 0;

	if (!pos->hit)
		return err;

	memset(&ev, 0, sizeof(ev));

	len = pos->long_name_len + 1;
	len = PERF_ALIGN(len, NAME_ALIGN);
	memcpy(&ev.build_id.build_id, pos->build_id, sizeof(pos->build_id));
	ev.build_id.header.type = PERF_RECORD_HEADER_BUILD_ID;
	ev.build_id.header.misc = misc;
	ev.build_id.pid = machine->pid;
	ev.build_id.header.size = sizeof(ev.build_id) + len;
	memcpy(&ev.build_id.filename, pos->long_name, pos->long_name_len);

	err = process(tool, &ev, NULL, machine);

	return err;
}

int perf_event__process_build_id(struct perf_tool *tool __maybe_unused,
				 union perf_event *event,
				 struct perf_session *session)
{
	__event_process_build_id(&event->build_id,
				 event->build_id.filename,
				 session);
	return 0;
}
