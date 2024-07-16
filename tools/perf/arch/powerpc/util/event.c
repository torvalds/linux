// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/string.h>
#include <linux/zalloc.h>

#include "../../../util/event.h"
#include "../../../util/synthetic-events.h"
#include "../../../util/machine.h"
#include "../../../util/tool.h"
#include "../../../util/map.h"
#include "../../../util/debug.h"

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
		data->p_stage_cyc = weight.var3_w;
	}
}

void arch_perf_synthesize_sample_weight(const struct perf_sample *data,
					__u64 *array, u64 type)
{
	*array = data->weight;

	if (type & PERF_SAMPLE_WEIGHT_STRUCT) {
		*array &= 0xffffffff;
		*array |= ((u64)data->ins_lat << 32);
	}
}

const char *arch_perf_header_entry(const char *se_header)
{
	if (!strcmp(se_header, "Local INSTR Latency"))
		return "Finish Cyc";
	else if (!strcmp(se_header, "INSTR Latency"))
		return "Global Finish_cyc";
	else if (!strcmp(se_header, "Local Pipeline Stage Cycle"))
		return "Dispatch Cyc";
	else if (!strcmp(se_header, "Pipeline Stage Cycle"))
		return "Global Dispatch_cyc";
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
