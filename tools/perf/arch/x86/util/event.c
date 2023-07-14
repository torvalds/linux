// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <stdlib.h>

#include "../../../util/event.h"
#include "../../../util/synthetic-events.h"
#include "../../../util/machine.h"
#include "../../../util/tool.h"
#include "../../../util/map.h"
#include "../../../util/debug.h"
#include "util/sample.h"

#if defined(__x86_64__)

int perf_event__synthesize_extra_kmaps(struct perf_tool *tool,
				       perf_event__handler_t process,
				       struct machine *machine)
{
	int rc = 0;
	struct map_rb_node *pos;
	struct maps *kmaps = machine__kernel_maps(machine);
	union perf_event *event = zalloc(sizeof(event->mmap) +
					 machine->id_hdr_size);

	if (!event) {
		pr_debug("Not enough memory synthesizing mmap event "
			 "for extra kernel maps\n");
		return -1;
	}

	maps__for_each_entry(kmaps, pos) {
		struct kmap *kmap;
		size_t size;
		struct map *map = pos->map;

		if (!__map__is_extra_kernel_map(map))
			continue;

		kmap = map__kmap(map);

		size = sizeof(event->mmap) - sizeof(event->mmap.filename) +
		       PERF_ALIGN(strlen(kmap->name) + 1, sizeof(u64)) +
		       machine->id_hdr_size;

		memset(event, 0, size);

		event->mmap.header.type = PERF_RECORD_MMAP;

		/*
		 * kernel uses 0 for user space maps, see kernel/perf_event.c
		 * __perf_event_mmap
		 */
		if (machine__is_host(machine))
			event->header.misc = PERF_RECORD_MISC_KERNEL;
		else
			event->header.misc = PERF_RECORD_MISC_GUEST_KERNEL;

		event->mmap.header.size = size;

		event->mmap.start = map__start(map);
		event->mmap.len   = map__size(map);
		event->mmap.pgoff = map__pgoff(map);
		event->mmap.pid   = machine->pid;

		strlcpy(event->mmap.filename, kmap->name, PATH_MAX);

		if (perf_tool__process_synth_event(tool, event, machine,
						   process) != 0) {
			rc = -1;
			break;
		}
	}

	free(event);
	return rc;
}

#endif

void arch_perf_parse_sample_weight(struct perf_sample *data,
				   const __u64 *array, u64 type)
{
	union perf_sample_weight weight;

	weight.full = *array;
	if (type & PERF_SAMPLE_WEIGHT)
		data->weight = weight.full;
	else {
		data->weight = weight.var1_dw;
		data->ins_lat = weight.var2_w;
		data->retire_lat = weight.var3_w;
	}
}

void arch_perf_synthesize_sample_weight(const struct perf_sample *data,
					__u64 *array, u64 type)
{
	*array = data->weight;

	if (type & PERF_SAMPLE_WEIGHT_STRUCT) {
		*array &= 0xffffffff;
		*array |= ((u64)data->ins_lat << 32);
		*array |= ((u64)data->retire_lat << 48);
	}
}

const char *arch_perf_header_entry(const char *se_header)
{
	if (!strcmp(se_header, "Local Pipeline Stage Cycle"))
		return "Local Retire Latency";
	else if (!strcmp(se_header, "Pipeline Stage Cycle"))
		return "Retire Latency";

	return se_header;
}

int arch_support_sort_key(const char *sort_key)
{
	if (!strcmp(sort_key, "p_stage_cyc"))
		return 1;
	if (!strcmp(sort_key, "local_p_stage_cyc"))
		return 1;
	return 0;
}
