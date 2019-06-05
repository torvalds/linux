// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <api/fs/fs.h>
#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/coresight-pmu.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/types.h>

#include "cs-etm.h"
#include "../../perf.h"
#include "../../util/auxtrace.h"
#include "../../util/cpumap.h"
#include "../../util/evlist.h"
#include "../../util/evsel.h"
#include "../../util/pmu.h"
#include "../../util/thread_map.h"
#include "../../util/cs-etm.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

struct cs_etm_recording {
	struct auxtrace_record	itr;
	struct perf_pmu		*cs_etm_pmu;
	struct perf_evlist	*evlist;
	int			wrapped_cnt;
	bool			*wrapped;
	bool			snapshot_mode;
	size_t			snapshot_size;
};

static bool cs_etm_is_etmv4(struct auxtrace_record *itr, int cpu);

static int cs_etm_parse_snapshot_options(struct auxtrace_record *itr,
					 struct record_opts *opts,
					 const char *str)
{
	struct cs_etm_recording *ptr =
				container_of(itr, struct cs_etm_recording, itr);
	unsigned long long snapshot_size = 0;
	char *endptr;

	if (str) {
		snapshot_size = strtoull(str, &endptr, 0);
		if (*endptr || snapshot_size > SIZE_MAX)
			return -1;
	}

	opts->auxtrace_snapshot_mode = true;
	opts->auxtrace_snapshot_size = snapshot_size;
	ptr->snapshot_size = snapshot_size;

	return 0;
}

static int cs_etm_set_sink_attr(struct perf_pmu *pmu,
				struct perf_evsel *evsel)
{
	char msg[BUFSIZ], path[PATH_MAX], *sink;
	struct perf_evsel_config_term *term;
	int ret = -EINVAL;
	u32 hash;

	if (evsel->attr.config2 & GENMASK(31, 0))
		return 0;

	list_for_each_entry(term, &evsel->config_terms, list) {
		if (term->type != PERF_EVSEL__CONFIG_TERM_DRV_CFG)
			continue;

		sink = term->val.drv_cfg;
		snprintf(path, PATH_MAX, "sinks/%s", sink);

		ret = perf_pmu__scan_file(pmu, path, "%x", &hash);
		if (ret != 1) {
			pr_err("failed to set sink \"%s\" on event %s with %d (%s)\n",
			       sink, perf_evsel__name(evsel), errno,
			       str_error_r(errno, msg, sizeof(msg)));
			return ret;
		}

		evsel->attr.config2 |= hash;
		return 0;
	}

	/*
	 * No sink was provided on the command line - for _now_ treat
	 * this as an error.
	 */
	return ret;
}

static int cs_etm_recording_options(struct auxtrace_record *itr,
				    struct perf_evlist *evlist,
				    struct record_opts *opts)
{
	int ret;
	struct cs_etm_recording *ptr =
				container_of(itr, struct cs_etm_recording, itr);
	struct perf_pmu *cs_etm_pmu = ptr->cs_etm_pmu;
	struct perf_evsel *evsel, *cs_etm_evsel = NULL;
	const struct cpu_map *cpus = evlist->cpus;
	bool privileged = (geteuid() == 0 || perf_event_paranoid() < 0);

	ptr->evlist = evlist;
	ptr->snapshot_mode = opts->auxtrace_snapshot_mode;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->attr.type == cs_etm_pmu->type) {
			if (cs_etm_evsel) {
				pr_err("There may be only one %s event\n",
				       CORESIGHT_ETM_PMU_NAME);
				return -EINVAL;
			}
			evsel->attr.freq = 0;
			evsel->attr.sample_period = 1;
			cs_etm_evsel = evsel;
			opts->full_auxtrace = true;
		}
	}

	/* no need to continue if at least one event of interest was found */
	if (!cs_etm_evsel)
		return 0;

	ret = cs_etm_set_sink_attr(cs_etm_pmu, cs_etm_evsel);
	if (ret)
		return ret;

	if (opts->use_clockid) {
		pr_err("Cannot use clockid (-k option) with %s\n",
		       CORESIGHT_ETM_PMU_NAME);
		return -EINVAL;
	}

	/* we are in snapshot mode */
	if (opts->auxtrace_snapshot_mode) {
		/*
		 * No size were given to '-S' or '-m,', so go with
		 * the default
		 */
		if (!opts->auxtrace_snapshot_size &&
		    !opts->auxtrace_mmap_pages) {
			if (privileged) {
				opts->auxtrace_mmap_pages = MiB(4) / page_size;
			} else {
				opts->auxtrace_mmap_pages =
							KiB(128) / page_size;
				if (opts->mmap_pages == UINT_MAX)
					opts->mmap_pages = KiB(256) / page_size;
			}
		} else if (!opts->auxtrace_mmap_pages && !privileged &&
						opts->mmap_pages == UINT_MAX) {
			opts->mmap_pages = KiB(256) / page_size;
		}

		/*
		 * '-m,xyz' was specified but no snapshot size, so make the
		 * snapshot size as big as the auxtrace mmap area.
		 */
		if (!opts->auxtrace_snapshot_size) {
			opts->auxtrace_snapshot_size =
				opts->auxtrace_mmap_pages * (size_t)page_size;
		}

		/*
		 * -Sxyz was specified but no auxtrace mmap area, so make the
		 * auxtrace mmap area big enough to fit the requested snapshot
		 * size.
		 */
		if (!opts->auxtrace_mmap_pages) {
			size_t sz = opts->auxtrace_snapshot_size;

			sz = round_up(sz, page_size) / page_size;
			opts->auxtrace_mmap_pages = roundup_pow_of_two(sz);
		}

		/* Snapshost size can't be bigger than the auxtrace area */
		if (opts->auxtrace_snapshot_size >
				opts->auxtrace_mmap_pages * (size_t)page_size) {
			pr_err("Snapshot size %zu must not be greater than AUX area tracing mmap size %zu\n",
			       opts->auxtrace_snapshot_size,
			       opts->auxtrace_mmap_pages * (size_t)page_size);
			return -EINVAL;
		}

		/* Something went wrong somewhere - this shouldn't happen */
		if (!opts->auxtrace_snapshot_size ||
		    !opts->auxtrace_mmap_pages) {
			pr_err("Failed to calculate default snapshot size and/or AUX area tracing mmap pages\n");
			return -EINVAL;
		}
	}

	/* We are in full trace mode but '-m,xyz' wasn't specified */
	if (opts->full_auxtrace && !opts->auxtrace_mmap_pages) {
		if (privileged) {
			opts->auxtrace_mmap_pages = MiB(4) / page_size;
		} else {
			opts->auxtrace_mmap_pages = KiB(128) / page_size;
			if (opts->mmap_pages == UINT_MAX)
				opts->mmap_pages = KiB(256) / page_size;
		}

	}

	/* Validate auxtrace_mmap_pages provided by user */
	if (opts->auxtrace_mmap_pages) {
		unsigned int max_page = (KiB(128) / page_size);
		size_t sz = opts->auxtrace_mmap_pages * (size_t)page_size;

		if (!privileged &&
		    opts->auxtrace_mmap_pages > max_page) {
			opts->auxtrace_mmap_pages = max_page;
			pr_err("auxtrace too big, truncating to %d\n",
			       max_page);
		}

		if (!is_power_of_2(sz)) {
			pr_err("Invalid mmap size for %s: must be a power of 2\n",
			       CORESIGHT_ETM_PMU_NAME);
			return -EINVAL;
		}
	}

	if (opts->auxtrace_snapshot_mode)
		pr_debug2("%s snapshot size: %zu\n", CORESIGHT_ETM_PMU_NAME,
			  opts->auxtrace_snapshot_size);

	/*
	 * To obtain the auxtrace buffer file descriptor, the auxtrace
	 * event must come first.
	 */
	perf_evlist__to_front(evlist, cs_etm_evsel);

	/*
	 * In the case of per-cpu mmaps, we need the CPU on the
	 * AUX event.
	 */
	if (!cpu_map__empty(cpus))
		perf_evsel__set_sample_bit(cs_etm_evsel, CPU);

	/* Add dummy event to keep tracking */
	if (opts->full_auxtrace) {
		struct perf_evsel *tracking_evsel;
		int err;

		err = parse_events(evlist, "dummy:u", NULL);
		if (err)
			return err;

		tracking_evsel = perf_evlist__last(evlist);
		perf_evlist__set_tracking_event(evlist, tracking_evsel);

		tracking_evsel->attr.freq = 0;
		tracking_evsel->attr.sample_period = 1;

		/* In per-cpu case, always need the time of mmap events etc */
		if (!cpu_map__empty(cpus))
			perf_evsel__set_sample_bit(tracking_evsel, TIME);
	}

	return 0;
}

static u64 cs_etm_get_config(struct auxtrace_record *itr)
{
	u64 config = 0;
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);
	struct perf_pmu *cs_etm_pmu = ptr->cs_etm_pmu;
	struct perf_evlist *evlist = ptr->evlist;
	struct perf_evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->attr.type == cs_etm_pmu->type) {
			/*
			 * Variable perf_event_attr::config is assigned to
			 * ETMv3/PTM.  The bit fields have been made to match
			 * the ETMv3.5 ETRMCR register specification.  See the
			 * PMU_FORMAT_ATTR() declarations in
			 * drivers/hwtracing/coresight/coresight-perf.c for
			 * details.
			 */
			config = evsel->attr.config;
			break;
		}
	}

	return config;
}

#ifndef BIT
#define BIT(N) (1UL << (N))
#endif

static u64 cs_etmv4_get_config(struct auxtrace_record *itr)
{
	u64 config = 0;
	u64 config_opts = 0;

	/*
	 * The perf event variable config bits represent both
	 * the command line options and register programming
	 * bits in ETMv3/PTM. For ETMv4 we must remap options
	 * to real bits
	 */
	config_opts = cs_etm_get_config(itr);
	if (config_opts & BIT(ETM_OPT_CYCACC))
		config |= BIT(ETM4_CFG_BIT_CYCACC);
	if (config_opts & BIT(ETM_OPT_TS))
		config |= BIT(ETM4_CFG_BIT_TS);
	if (config_opts & BIT(ETM_OPT_RETSTK))
		config |= BIT(ETM4_CFG_BIT_RETSTK);

	return config;
}

static size_t
cs_etm_info_priv_size(struct auxtrace_record *itr __maybe_unused,
		      struct perf_evlist *evlist __maybe_unused)
{
	int i;
	int etmv3 = 0, etmv4 = 0;
	struct cpu_map *event_cpus = evlist->cpus;
	struct cpu_map *online_cpus = cpu_map__new(NULL);

	/* cpu map is not empty, we have specific CPUs to work with */
	if (!cpu_map__empty(event_cpus)) {
		for (i = 0; i < cpu__max_cpu(); i++) {
			if (!cpu_map__has(event_cpus, i) ||
			    !cpu_map__has(online_cpus, i))
				continue;

			if (cs_etm_is_etmv4(itr, i))
				etmv4++;
			else
				etmv3++;
		}
	} else {
		/* get configuration for all CPUs in the system */
		for (i = 0; i < cpu__max_cpu(); i++) {
			if (!cpu_map__has(online_cpus, i))
				continue;

			if (cs_etm_is_etmv4(itr, i))
				etmv4++;
			else
				etmv3++;
		}
	}

	cpu_map__put(online_cpus);

	return (CS_ETM_HEADER_SIZE +
	       (etmv4 * CS_ETMV4_PRIV_SIZE) +
	       (etmv3 * CS_ETMV3_PRIV_SIZE));
}

static const char *metadata_etmv3_ro[CS_ETM_PRIV_MAX] = {
	[CS_ETM_ETMCCER]	= "mgmt/etmccer",
	[CS_ETM_ETMIDR]		= "mgmt/etmidr",
};

static const char *metadata_etmv4_ro[CS_ETMV4_PRIV_MAX] = {
	[CS_ETMV4_TRCIDR0]		= "trcidr/trcidr0",
	[CS_ETMV4_TRCIDR1]		= "trcidr/trcidr1",
	[CS_ETMV4_TRCIDR2]		= "trcidr/trcidr2",
	[CS_ETMV4_TRCIDR8]		= "trcidr/trcidr8",
	[CS_ETMV4_TRCAUTHSTATUS]	= "mgmt/trcauthstatus",
};

static bool cs_etm_is_etmv4(struct auxtrace_record *itr, int cpu)
{
	bool ret = false;
	char path[PATH_MAX];
	int scan;
	unsigned int val;
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);
	struct perf_pmu *cs_etm_pmu = ptr->cs_etm_pmu;

	/* Take any of the RO files for ETMv4 and see if it present */
	snprintf(path, PATH_MAX, "cpu%d/%s",
		 cpu, metadata_etmv4_ro[CS_ETMV4_TRCIDR0]);
	scan = perf_pmu__scan_file(cs_etm_pmu, path, "%x", &val);

	/* The file was read successfully, we have a winner */
	if (scan == 1)
		ret = true;

	return ret;
}

static int cs_etm_get_ro(struct perf_pmu *pmu, int cpu, const char *path)
{
	char pmu_path[PATH_MAX];
	int scan;
	unsigned int val = 0;

	/* Get RO metadata from sysfs */
	snprintf(pmu_path, PATH_MAX, "cpu%d/%s", cpu, path);

	scan = perf_pmu__scan_file(pmu, pmu_path, "%x", &val);
	if (scan != 1)
		pr_err("%s: error reading: %s\n", __func__, pmu_path);

	return val;
}

static void cs_etm_get_metadata(int cpu, u32 *offset,
				struct auxtrace_record *itr,
				struct auxtrace_info_event *info)
{
	u32 increment;
	u64 magic;
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);
	struct perf_pmu *cs_etm_pmu = ptr->cs_etm_pmu;

	/* first see what kind of tracer this cpu is affined to */
	if (cs_etm_is_etmv4(itr, cpu)) {
		magic = __perf_cs_etmv4_magic;
		/* Get trace configuration register */
		info->priv[*offset + CS_ETMV4_TRCCONFIGR] =
						cs_etmv4_get_config(itr);
		/* Get traceID from the framework */
		info->priv[*offset + CS_ETMV4_TRCTRACEIDR] =
						coresight_get_trace_id(cpu);
		/* Get read-only information from sysFS */
		info->priv[*offset + CS_ETMV4_TRCIDR0] =
			cs_etm_get_ro(cs_etm_pmu, cpu,
				      metadata_etmv4_ro[CS_ETMV4_TRCIDR0]);
		info->priv[*offset + CS_ETMV4_TRCIDR1] =
			cs_etm_get_ro(cs_etm_pmu, cpu,
				      metadata_etmv4_ro[CS_ETMV4_TRCIDR1]);
		info->priv[*offset + CS_ETMV4_TRCIDR2] =
			cs_etm_get_ro(cs_etm_pmu, cpu,
				      metadata_etmv4_ro[CS_ETMV4_TRCIDR2]);
		info->priv[*offset + CS_ETMV4_TRCIDR8] =
			cs_etm_get_ro(cs_etm_pmu, cpu,
				      metadata_etmv4_ro[CS_ETMV4_TRCIDR8]);
		info->priv[*offset + CS_ETMV4_TRCAUTHSTATUS] =
			cs_etm_get_ro(cs_etm_pmu, cpu,
				      metadata_etmv4_ro
				      [CS_ETMV4_TRCAUTHSTATUS]);

		/* How much space was used */
		increment = CS_ETMV4_PRIV_MAX;
	} else {
		magic = __perf_cs_etmv3_magic;
		/* Get configuration register */
		info->priv[*offset + CS_ETM_ETMCR] = cs_etm_get_config(itr);
		/* Get traceID from the framework */
		info->priv[*offset + CS_ETM_ETMTRACEIDR] =
						coresight_get_trace_id(cpu);
		/* Get read-only information from sysFS */
		info->priv[*offset + CS_ETM_ETMCCER] =
			cs_etm_get_ro(cs_etm_pmu, cpu,
				      metadata_etmv3_ro[CS_ETM_ETMCCER]);
		info->priv[*offset + CS_ETM_ETMIDR] =
			cs_etm_get_ro(cs_etm_pmu, cpu,
				      metadata_etmv3_ro[CS_ETM_ETMIDR]);

		/* How much space was used */
		increment = CS_ETM_PRIV_MAX;
	}

	/* Build generic header portion */
	info->priv[*offset + CS_ETM_MAGIC] = magic;
	info->priv[*offset + CS_ETM_CPU] = cpu;
	/* Where the next CPU entry should start from */
	*offset += increment;
}

static int cs_etm_info_fill(struct auxtrace_record *itr,
			    struct perf_session *session,
			    struct auxtrace_info_event *info,
			    size_t priv_size)
{
	int i;
	u32 offset;
	u64 nr_cpu, type;
	struct cpu_map *cpu_map;
	struct cpu_map *event_cpus = session->evlist->cpus;
	struct cpu_map *online_cpus = cpu_map__new(NULL);
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);
	struct perf_pmu *cs_etm_pmu = ptr->cs_etm_pmu;

	if (priv_size != cs_etm_info_priv_size(itr, session->evlist))
		return -EINVAL;

	if (!session->evlist->nr_mmaps)
		return -EINVAL;

	/* If the cpu_map is empty all online CPUs are involved */
	if (cpu_map__empty(event_cpus)) {
		cpu_map = online_cpus;
	} else {
		/* Make sure all specified CPUs are online */
		for (i = 0; i < cpu_map__nr(event_cpus); i++) {
			if (cpu_map__has(event_cpus, i) &&
			    !cpu_map__has(online_cpus, i))
				return -EINVAL;
		}

		cpu_map = event_cpus;
	}

	nr_cpu = cpu_map__nr(cpu_map);
	/* Get PMU type as dynamically assigned by the core */
	type = cs_etm_pmu->type;

	/* First fill out the session header */
	info->type = PERF_AUXTRACE_CS_ETM;
	info->priv[CS_HEADER_VERSION_0] = 0;
	info->priv[CS_PMU_TYPE_CPUS] = type << 32;
	info->priv[CS_PMU_TYPE_CPUS] |= nr_cpu;
	info->priv[CS_ETM_SNAPSHOT] = ptr->snapshot_mode;

	offset = CS_ETM_SNAPSHOT + 1;

	for (i = 0; i < cpu__max_cpu() && offset < priv_size; i++)
		if (cpu_map__has(cpu_map, i))
			cs_etm_get_metadata(i, &offset, itr, info);

	cpu_map__put(online_cpus);

	return 0;
}

static int cs_etm_alloc_wrapped_array(struct cs_etm_recording *ptr, int idx)
{
	bool *wrapped;
	int cnt = ptr->wrapped_cnt;

	/* Make @ptr->wrapped as big as @idx */
	while (cnt <= idx)
		cnt++;

	/*
	 * Free'ed in cs_etm_recording_free().  Using realloc() to avoid
	 * cross compilation problems where the host's system supports
	 * reallocarray() but not the target.
	 */
	wrapped = realloc(ptr->wrapped, cnt * sizeof(bool));
	if (!wrapped)
		return -ENOMEM;

	wrapped[cnt - 1] = false;
	ptr->wrapped_cnt = cnt;
	ptr->wrapped = wrapped;

	return 0;
}

static bool cs_etm_buffer_has_wrapped(unsigned char *buffer,
				      size_t buffer_size, u64 head)
{
	u64 i, watermark;
	u64 *buf = (u64 *)buffer;
	size_t buf_size = buffer_size;

	/*
	 * We want to look the very last 512 byte (chosen arbitrarily) in
	 * the ring buffer.
	 */
	watermark = buf_size - 512;

	/*
	 * @head is continuously increasing - if its value is equal or greater
	 * than the size of the ring buffer, it has wrapped around.
	 */
	if (head >= buffer_size)
		return true;

	/*
	 * The value of @head is somewhere within the size of the ring buffer.
	 * This can be that there hasn't been enough data to fill the ring
	 * buffer yet or the trace time was so long that @head has numerically
	 * wrapped around.  To find we need to check if we have data at the very
	 * end of the ring buffer.  We can reliably do this because mmap'ed
	 * pages are zeroed out and there is a fresh mapping with every new
	 * session.
	 */

	/* @head is less than 512 byte from the end of the ring buffer */
	if (head > watermark)
		watermark = head;

	/*
	 * Speed things up by using 64 bit transactions (see "u64 *buf" above)
	 */
	watermark >>= 3;
	buf_size >>= 3;

	/*
	 * If we find trace data at the end of the ring buffer, @head has
	 * been there and has numerically wrapped around at least once.
	 */
	for (i = watermark; i < buf_size; i++)
		if (buf[i])
			return true;

	return false;
}

static int cs_etm_find_snapshot(struct auxtrace_record *itr,
				int idx, struct auxtrace_mmap *mm,
				unsigned char *data,
				u64 *head, u64 *old)
{
	int err;
	bool wrapped;
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);

	/*
	 * Allocate memory to keep track of wrapping if this is the first
	 * time we deal with this *mm.
	 */
	if (idx >= ptr->wrapped_cnt) {
		err = cs_etm_alloc_wrapped_array(ptr, idx);
		if (err)
			return err;
	}

	/*
	 * Check to see if *head has wrapped around.  If it hasn't only the
	 * amount of data between *head and *old is snapshot'ed to avoid
	 * bloating the perf.data file with zeros.  But as soon as *head has
	 * wrapped around the entire size of the AUX ring buffer it taken.
	 */
	wrapped = ptr->wrapped[idx];
	if (!wrapped && cs_etm_buffer_has_wrapped(data, mm->len, *head)) {
		wrapped = true;
		ptr->wrapped[idx] = true;
	}

	pr_debug3("%s: mmap index %d old head %zu new head %zu size %zu\n",
		  __func__, idx, (size_t)*old, (size_t)*head, mm->len);

	/* No wrap has occurred, we can just use *head and *old. */
	if (!wrapped)
		return 0;

	/*
	 * *head has wrapped around - adjust *head and *old to pickup the
	 * entire content of the AUX buffer.
	 */
	if (*head >= mm->len) {
		*old = *head - mm->len;
	} else {
		*head += mm->len;
		*old = *head - mm->len;
	}

	return 0;
}

static int cs_etm_snapshot_start(struct auxtrace_record *itr)
{
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);
	struct perf_evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->attr.type == ptr->cs_etm_pmu->type)
			return perf_evsel__disable(evsel);
	}
	return -EINVAL;
}

static int cs_etm_snapshot_finish(struct auxtrace_record *itr)
{
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);
	struct perf_evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->attr.type == ptr->cs_etm_pmu->type)
			return perf_evsel__enable(evsel);
	}
	return -EINVAL;
}

static u64 cs_etm_reference(struct auxtrace_record *itr __maybe_unused)
{
	return (((u64) rand() <<  0) & 0x00000000FFFFFFFFull) |
		(((u64) rand() << 32) & 0xFFFFFFFF00000000ull);
}

static void cs_etm_recording_free(struct auxtrace_record *itr)
{
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);

	zfree(&ptr->wrapped);
	free(ptr);
}

static int cs_etm_read_finish(struct auxtrace_record *itr, int idx)
{
	struct cs_etm_recording *ptr =
			container_of(itr, struct cs_etm_recording, itr);
	struct perf_evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->attr.type == ptr->cs_etm_pmu->type)
			return perf_evlist__enable_event_idx(ptr->evlist,
							     evsel, idx);
	}

	return -EINVAL;
}

struct auxtrace_record *cs_etm_record_init(int *err)
{
	struct perf_pmu *cs_etm_pmu;
	struct cs_etm_recording *ptr;

	cs_etm_pmu = perf_pmu__find(CORESIGHT_ETM_PMU_NAME);

	if (!cs_etm_pmu) {
		*err = -EINVAL;
		goto out;
	}

	ptr = zalloc(sizeof(struct cs_etm_recording));
	if (!ptr) {
		*err = -ENOMEM;
		goto out;
	}

	ptr->cs_etm_pmu			= cs_etm_pmu;
	ptr->itr.parse_snapshot_options	= cs_etm_parse_snapshot_options;
	ptr->itr.recording_options	= cs_etm_recording_options;
	ptr->itr.info_priv_size		= cs_etm_info_priv_size;
	ptr->itr.info_fill		= cs_etm_info_fill;
	ptr->itr.find_snapshot		= cs_etm_find_snapshot;
	ptr->itr.snapshot_start		= cs_etm_snapshot_start;
	ptr->itr.snapshot_finish	= cs_etm_snapshot_finish;
	ptr->itr.reference		= cs_etm_reference;
	ptr->itr.free			= cs_etm_recording_free;
	ptr->itr.read_finish		= cs_etm_read_finish;

	*err = 0;
	return &ptr->itr;
out:
	return NULL;
}
