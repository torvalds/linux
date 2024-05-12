// SPDX-License-Identifier: GPL-2.0
/*
 * Intel dynamic_speed_select -- Enumerate and control features
 * Copyright (c) 2019 Intel Corporation.
 */

#include "isst.h"

static void printcpulist(int str_len, char *str, int mask_size,
			 cpu_set_t *cpu_mask)
{
	int i, first, curr_index, index;

	if (!CPU_COUNT_S(mask_size, cpu_mask)) {
		snprintf(str, str_len, "none");
		return;
	}

	curr_index = 0;
	first = 1;
	for (i = 0; i < get_topo_max_cpus(); ++i) {
		if (!CPU_ISSET_S(i, mask_size, cpu_mask))
			continue;
		if (!first) {
			index = snprintf(&str[curr_index],
					 str_len - curr_index, ",");
			curr_index += index;
			if (curr_index >= str_len)
				break;
		}
		index = snprintf(&str[curr_index], str_len - curr_index, "%d",
				 i);
		curr_index += index;
		if (curr_index >= str_len)
			break;
		first = 0;
	}
}

static void printcpumask(int str_len, char *str, int mask_size,
			 cpu_set_t *cpu_mask)
{
	int i, max_cpus = get_topo_max_cpus();
	unsigned int *mask;
	int size, index, curr_index;

	size = max_cpus / (sizeof(unsigned int) * 8);
	if (max_cpus % (sizeof(unsigned int) * 8))
		size++;

	mask = calloc(size, sizeof(unsigned int));
	if (!mask)
		return;

	for (i = 0; i < max_cpus; ++i) {
		int mask_index, bit_index;

		if (!CPU_ISSET_S(i, mask_size, cpu_mask))
			continue;

		mask_index = i / (sizeof(unsigned int) * 8);
		bit_index = i % (sizeof(unsigned int) * 8);
		mask[mask_index] |= BIT(bit_index);
	}

	curr_index = 0;
	for (i = size - 1; i >= 0; --i) {
		index = snprintf(&str[curr_index], str_len - curr_index, "%08x",
				 mask[i]);
		curr_index += index;
		if (curr_index >= str_len)
			break;
		if (i) {
			strncat(&str[curr_index], ",", str_len - curr_index);
			curr_index++;
		}
		if (curr_index >= str_len)
			break;
	}

	free(mask);
}

static void format_and_print_txt(FILE *outf, int level, char *header,
				 char *value)
{
	char *spaces = "  ";
	static char delimiters[256];
	int i, j = 0;

	if (!level)
		return;

	if (level == 1) {
		strcpy(delimiters, " ");
	} else {
		for (i = 0; i < level - 1; ++i)
			j += snprintf(&delimiters[j], sizeof(delimiters) - j,
				      "%s", spaces);
	}

	if (header && value) {
		fprintf(outf, "%s", delimiters);
		fprintf(outf, "%s:%s\n", header, value);
	} else if (header) {
		fprintf(outf, "%s", delimiters);
		fprintf(outf, "%s\n", header);
	}
}

static int last_level;
static void format_and_print(FILE *outf, int level, char *header, char *value)
{
	char *spaces = "  ";
	static char delimiters[256];
	int i;

	if (!out_format_is_json()) {
		format_and_print_txt(outf, level, header, value);
		return;
	}

	if (level == 0) {
		if (header)
			fprintf(outf, "{");
		else
			fprintf(outf, "\n}\n");

	} else {
		int j = 0;

		for (i = 0; i < level; ++i)
			j += snprintf(&delimiters[j], sizeof(delimiters) - j,
				      "%s", spaces);

		if (last_level == level)
			fprintf(outf, ",\n");

		if (value) {
			if (last_level != level)
				fprintf(outf, "\n");

			fprintf(outf, "%s\"%s\": ", delimiters, header);
			fprintf(outf, "\"%s\"", value);
		} else {
			for (i = last_level - 1; i >= level; --i) {
				int k = 0;

				for (j = i; j > 0; --j)
					k += snprintf(&delimiters[k],
						      sizeof(delimiters) - k,
						      "%s", spaces);
				if (i == level && header)
					fprintf(outf, "\n%s},", delimiters);
				else
					fprintf(outf, "\n%s}", delimiters);
			}
			if (abs(last_level - level) < 3)
				fprintf(outf, "\n");
			if (header)
				fprintf(outf, "%s\"%s\": {", delimiters,
					header);
		}
	}

	last_level = level;
}

static int print_package_info(struct isst_id *id, FILE *outf)
{
	char header[256];
	int level = 1;

	if (out_format_is_json()) {
		if (api_version() > 1)
			snprintf(header, sizeof(header), "package-%d:die-%d:powerdomain-%d:cpu-%d",
				 id->pkg, id->die, id->punit, id->cpu);
		else
			snprintf(header, sizeof(header), "package-%d:die-%d:cpu-%d",
				 id->pkg, id->die, id->cpu);
		format_and_print(outf, level, header, NULL);
		return 1;
	}
	snprintf(header, sizeof(header), "package-%d", id->pkg);
	format_and_print(outf, level++, header, NULL);
	snprintf(header, sizeof(header), "die-%d", id->die);
	format_and_print(outf, level++, header, NULL);
	if (api_version() > 1) {
		snprintf(header, sizeof(header), "powerdomain-%d", id->punit);
		format_and_print(outf, level++, header, NULL);
	}
	snprintf(header, sizeof(header), "cpu-%d", id->cpu);
	format_and_print(outf, level, header, NULL);

	return level;
}

static void _isst_pbf_display_information(struct isst_id *id, FILE *outf, int level,
					  struct isst_pbf_info *pbf_info,
					  int disp_level)
{
	char header[256];
	char value[512];

	snprintf(header, sizeof(header), "speed-select-base-freq-properties");
	format_and_print(outf, disp_level, header, NULL);

	snprintf(header, sizeof(header), "high-priority-base-frequency(MHz)");
	snprintf(value, sizeof(value), "%d",
		 pbf_info->p1_high * isst_get_disp_freq_multiplier());
	format_and_print(outf, disp_level + 1, header, value);

	snprintf(header, sizeof(header), "high-priority-cpu-mask");
	printcpumask(sizeof(value), value, pbf_info->core_cpumask_size,
		     pbf_info->core_cpumask);
	format_and_print(outf, disp_level + 1, header, value);

	snprintf(header, sizeof(header), "high-priority-cpu-list");
	printcpulist(sizeof(value), value,
		     pbf_info->core_cpumask_size,
		     pbf_info->core_cpumask);
	format_and_print(outf, disp_level + 1, header, value);

	snprintf(header, sizeof(header), "low-priority-base-frequency(MHz)");
	snprintf(value, sizeof(value), "%d",
		 pbf_info->p1_low * isst_get_disp_freq_multiplier());
	format_and_print(outf, disp_level + 1, header, value);

	if (is_clx_n_platform())
		return;

	snprintf(header, sizeof(header), "tjunction-temperature(C)");
	snprintf(value, sizeof(value), "%d", pbf_info->t_prochot);
	format_and_print(outf, disp_level + 1, header, value);

	snprintf(header, sizeof(header), "thermal-design-power(W)");
	snprintf(value, sizeof(value), "%d", pbf_info->tdp);
	format_and_print(outf, disp_level + 1, header, value);
}

static void _isst_fact_display_information(struct isst_id *id, FILE *outf, int level,
					   int fact_bucket, int fact_avx,
					   struct isst_fact_info *fact_info,
					   int base_level)
{
	struct isst_fact_bucket_info *bucket_info = fact_info->bucket_info;
	int trl_max_levels = isst_get_trl_max_levels();
	char header[256];
	char value[256];
	int print = 0, j;

	for (j = 0; j < ISST_FACT_MAX_BUCKETS; ++j) {
		if (fact_bucket != 0xff && fact_bucket != j)
			continue;

		/* core count must be valid for CPU power domain */
		if (!bucket_info[j].hp_cores && id->cpu >= 0)
			break;

		print = 1;
	}
	if (!print) {
		fprintf(stderr, "Invalid bucket\n");
		return;
	}

	snprintf(header, sizeof(header), "speed-select-turbo-freq-properties");
	format_and_print(outf, base_level, header, NULL);
	for (j = 0; j < ISST_FACT_MAX_BUCKETS; ++j) {
		int i;

		if (fact_bucket != 0xff && fact_bucket != j)
			continue;

		if (!bucket_info[j].hp_cores)
			break;

		snprintf(header, sizeof(header), "bucket-%d", j);
		format_and_print(outf, base_level + 1, header, NULL);

		snprintf(header, sizeof(header), "high-priority-cores-count");
		snprintf(value, sizeof(value), "%d",
			 bucket_info[j].hp_cores);
		format_and_print(outf, base_level + 2, header, value);
		for (i = 0; i < trl_max_levels; i++) {
			if (!bucket_info[j].hp_ratios[i] || (fact_avx != 0xFF && !(fact_avx & (1 << i))))
				continue;
			if (i == 0 && api_version() == 1 && !is_emr_platform())
				snprintf(header, sizeof(header),
					"high-priority-max-frequency(MHz)");
			else
				snprintf(header, sizeof(header),
					"high-priority-max-%s-frequency(MHz)", isst_get_trl_level_name(i));
			snprintf(value, sizeof(value), "%d",
				bucket_info[j].hp_ratios[i] * isst_get_disp_freq_multiplier());
			format_and_print(outf, base_level + 2, header, value);
		}
	}
	snprintf(header, sizeof(header),
		 "speed-select-turbo-freq-clip-frequencies");
	format_and_print(outf, base_level + 1, header, NULL);

	for (j = 0; j < trl_max_levels; j++) {
		if (!fact_info->lp_ratios[j])
			continue;

		/* No AVX level name for SSE to be consistent with previous formatting */
		if (j == 0 && api_version() == 1 && !is_emr_platform())
			snprintf(header, sizeof(header), "low-priority-max-frequency(MHz)");
		else
			snprintf(header, sizeof(header), "low-priority-max-%s-frequency(MHz)",
				isst_get_trl_level_name(j));
		snprintf(value, sizeof(value), "%d",
			 fact_info->lp_ratios[j] * isst_get_disp_freq_multiplier());
		format_and_print(outf, base_level + 2, header, value);
	}
}

void isst_ctdp_display_core_info(struct isst_id *id, FILE *outf, char *prefix,
				 unsigned int val, char *str0, char *str1)
{
	char value[256];
	int level = print_package_info(id, outf);

	level++;

	if (str0 && !val)
		snprintf(value, sizeof(value), "%s", str0);
	else if (str1 && val)
		snprintf(value, sizeof(value), "%s", str1);
	else
		snprintf(value, sizeof(value), "%u", val);
	format_and_print(outf, level, prefix, value);

	format_and_print(outf, 1, NULL, NULL);
}

void isst_ctdp_display_information(struct isst_id *id, FILE *outf, int tdp_level,
				   struct isst_pkg_ctdp *pkg_dev)
{
	char header[256];
	char value[512];
	static int level;
	int trl_max_levels = isst_get_trl_max_levels();
	int i;

	if (pkg_dev->processed)
		level = print_package_info(id, outf);

	for (i = 0; i <= pkg_dev->levels; ++i) {
		struct isst_pkg_ctdp_level_info *ctdp_level;
		int j, k;

		ctdp_level = &pkg_dev->ctdp_level[i];
		if (!ctdp_level->processed)
			continue;

		snprintf(header, sizeof(header), "perf-profile-level-%d",
			 ctdp_level->level);
		format_and_print(outf, level + 1, header, NULL);

		if (id->cpu >= 0) {
			snprintf(header, sizeof(header), "cpu-count");
			j = get_cpu_count(id);
			snprintf(value, sizeof(value), "%d", j);
			format_and_print(outf, level + 2, header, value);

			j = CPU_COUNT_S(ctdp_level->core_cpumask_size,
					ctdp_level->core_cpumask);
			if (j) {
				snprintf(header, sizeof(header), "enable-cpu-count");
				snprintf(value, sizeof(value), "%d", j);
				format_and_print(outf, level + 2, header, value);
			}

			if (ctdp_level->core_cpumask_size) {
				snprintf(header, sizeof(header), "enable-cpu-mask");
				printcpumask(sizeof(value), value,
					     ctdp_level->core_cpumask_size,
					     ctdp_level->core_cpumask);
				format_and_print(outf, level + 2, header, value);

				snprintf(header, sizeof(header), "enable-cpu-list");
				printcpulist(sizeof(value), value,
					     ctdp_level->core_cpumask_size,
					     ctdp_level->core_cpumask);
				format_and_print(outf, level + 2, header, value);
			}
		}

		snprintf(header, sizeof(header), "thermal-design-power-ratio");
		snprintf(value, sizeof(value), "%d", ctdp_level->tdp_ratio);
		format_and_print(outf, level + 2, header, value);

		snprintf(header, sizeof(header), "base-frequency(MHz)");
		if (!ctdp_level->sse_p1)
			ctdp_level->sse_p1 = ctdp_level->tdp_ratio;
		snprintf(value, sizeof(value), "%d",
			  ctdp_level->sse_p1 * isst_get_disp_freq_multiplier());
		format_and_print(outf, level + 2, header, value);

		if (ctdp_level->avx2_p1) {
			snprintf(header, sizeof(header), "base-frequency-avx2(MHz)");
			snprintf(value, sizeof(value), "%d",
				 ctdp_level->avx2_p1 * isst_get_disp_freq_multiplier());
			format_and_print(outf, level + 2, header, value);
		}

		if (ctdp_level->avx512_p1) {
			snprintf(header, sizeof(header), "base-frequency-avx512(MHz)");
			snprintf(value, sizeof(value), "%d",
				 ctdp_level->avx512_p1 * isst_get_disp_freq_multiplier());
			format_and_print(outf, level + 2, header, value);
		}

		if (ctdp_level->uncore_pm) {
			snprintf(header, sizeof(header), "uncore-frequency-min(MHz)");
			snprintf(value, sizeof(value), "%d",
				 ctdp_level->uncore_pm * isst_get_disp_freq_multiplier());
			format_and_print(outf, level + 2, header, value);
		}

		if (ctdp_level->uncore_p0) {
			snprintf(header, sizeof(header), "uncore-frequency-max(MHz)");
			snprintf(value, sizeof(value), "%d",
				 ctdp_level->uncore_p0 * isst_get_disp_freq_multiplier());
			format_and_print(outf, level + 2, header, value);
		}

		if (ctdp_level->amx_p1) {
			snprintf(header, sizeof(header), "base-frequency-amx(MHz)");
			snprintf(value, sizeof(value), "%d",
			ctdp_level->amx_p1 * isst_get_disp_freq_multiplier());
			format_and_print(outf, level + 2, header, value);
		}

		if (ctdp_level->uncore_p1) {
			snprintf(header, sizeof(header), "uncore-frequency-base(MHz)");
			snprintf(value, sizeof(value), "%d",
				 ctdp_level->uncore_p1 * isst_get_disp_freq_multiplier());
			format_and_print(outf, level + 2, header, value);
		}

		if (ctdp_level->mem_freq) {
			snprintf(header, sizeof(header), "mem-frequency(MHz)");
			snprintf(value, sizeof(value), "%d",
				 ctdp_level->mem_freq);
			format_and_print(outf, level + 2, header, value);
		}

		if (api_version() > 1) {
			snprintf(header, sizeof(header), "cooling_type");
			snprintf(value, sizeof(value), "%d",
				ctdp_level->cooling_type);
			format_and_print(outf, level + 2, header, value);
		}

		snprintf(header, sizeof(header),
			 "speed-select-turbo-freq");
		if (ctdp_level->fact_support) {
			if (ctdp_level->fact_enabled)
				snprintf(value, sizeof(value), "enabled");
			else
				snprintf(value, sizeof(value), "disabled");
		} else
			snprintf(value, sizeof(value), "unsupported");
		format_and_print(outf, level + 2, header, value);

		snprintf(header, sizeof(header),
			 "speed-select-base-freq");
		if (ctdp_level->pbf_support) {
			if (ctdp_level->pbf_enabled)
				snprintf(value, sizeof(value), "enabled");
			else
				snprintf(value, sizeof(value), "disabled");
		} else
			snprintf(value, sizeof(value), "unsupported");
		format_and_print(outf, level + 2, header, value);

		snprintf(header, sizeof(header),
			 "speed-select-core-power");
		if (ctdp_level->sst_cp_support) {
			if (ctdp_level->sst_cp_enabled)
				snprintf(value, sizeof(value), "enabled");
			else
				snprintf(value, sizeof(value), "disabled");
		} else
			snprintf(value, sizeof(value), "unsupported");
		format_and_print(outf, level + 2, header, value);

		if (is_clx_n_platform()) {
			if (ctdp_level->pbf_support)
				_isst_pbf_display_information(id, outf,
							      tdp_level,
							  &ctdp_level->pbf_info,
							      level + 2);
			continue;
		}

		if (ctdp_level->pkg_tdp) {
			snprintf(header, sizeof(header), "thermal-design-power(W)");
			snprintf(value, sizeof(value), "%d", ctdp_level->pkg_tdp);
			format_and_print(outf, level + 2, header, value);
		}

		if (ctdp_level->t_proc_hot) {
			snprintf(header, sizeof(header), "tjunction-max(C)");
			snprintf(value, sizeof(value), "%d", ctdp_level->t_proc_hot);
			format_and_print(outf, level + 2, header, value);
		}

		for (k = 0; k < trl_max_levels; k++) {
			if (!ctdp_level->trl_ratios[k][0])
				continue;

			snprintf(header, sizeof(header), "turbo-ratio-limits-%s", isst_get_trl_level_name(k));
			format_and_print(outf, level + 2, header, NULL);

			for (j = 0; j < 8; ++j) {
				snprintf(header, sizeof(header), "bucket-%d", j);
				format_and_print(outf, level + 3, header, NULL);

				snprintf(header, sizeof(header), "core-count");

				snprintf(value, sizeof(value), "%llu", (ctdp_level->trl_cores >> (j * 8)) & 0xff);
				format_and_print(outf, level + 4, header, value);

				snprintf(header, sizeof(header), "max-turbo-frequency(MHz)");
				snprintf(value, sizeof(value), "%d", ctdp_level->trl_ratios[k][j] * isst_get_disp_freq_multiplier());
				format_and_print(outf, level + 4, header, value);
			}
		}

		if (ctdp_level->pbf_support)
			_isst_pbf_display_information(id, outf, i,
						      &ctdp_level->pbf_info,
						      level + 2);
		if (ctdp_level->fact_support)
			_isst_fact_display_information(id, outf, i, 0xff, 0xff,
						       &ctdp_level->fact_info,
						       level + 2);
	}

	format_and_print(outf, 1, NULL, NULL);
}

static int start;
void isst_ctdp_display_information_start(FILE *outf)
{
	last_level = 0;
	format_and_print(outf, 0, "start", NULL);
	start = 1;
}

void isst_ctdp_display_information_end(FILE *outf)
{
	format_and_print(outf, 0, NULL, NULL);
	start = 0;
}

void isst_pbf_display_information(struct isst_id *id, FILE *outf, int level,
				  struct isst_pbf_info *pbf_info)
{
	int _level;

	_level = print_package_info(id, outf);
	_isst_pbf_display_information(id, outf, level, pbf_info, _level + 1);
	format_and_print(outf, 1, NULL, NULL);
}

void isst_fact_display_information(struct isst_id *id, FILE *outf, int level,
				   int fact_bucket, int fact_avx,
				   struct isst_fact_info *fact_info)
{
	int _level;

	_level = print_package_info(id, outf);
	_isst_fact_display_information(id, outf, level, fact_bucket, fact_avx,
				       fact_info, _level + 1);
	format_and_print(outf, 1, NULL, NULL);
}

void isst_clos_display_information(struct isst_id *id, FILE *outf, int clos,
				   struct isst_clos_config *clos_config)
{
	char header[256];
	char value[256];
	int level;

	level = print_package_info(id, outf);

	snprintf(header, sizeof(header), "core-power");
	format_and_print(outf, level + 1, header, NULL);

	snprintf(header, sizeof(header), "clos");
	snprintf(value, sizeof(value), "%d", clos);
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "epp");
	snprintf(value, sizeof(value), "%d", clos_config->epp);
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "clos-proportional-priority");
	snprintf(value, sizeof(value), "%d", clos_config->clos_prop_prio);
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "clos-min");
	snprintf(value, sizeof(value), "%d MHz", clos_config->clos_min * isst_get_disp_freq_multiplier());
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "clos-max");
	if ((clos_config->clos_max * isst_get_disp_freq_multiplier()) == 25500)
		snprintf(value, sizeof(value), "Max Turbo frequency");
	else
		snprintf(value, sizeof(value), "%d MHz", clos_config->clos_max * isst_get_disp_freq_multiplier());
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "clos-desired");
	snprintf(value, sizeof(value), "%d MHz", clos_config->clos_desired * isst_get_disp_freq_multiplier());
	format_and_print(outf, level + 2, header, value);

	format_and_print(outf, level, NULL, NULL);
}

void isst_clos_display_clos_information(struct isst_id *id, FILE *outf,
					int clos_enable, int type,
					int state, int cap)
{
	char header[256];
	char value[256];
	int level;

	level = print_package_info(id, outf);

	snprintf(header, sizeof(header), "core-power");
	format_and_print(outf, level + 1, header, NULL);

	snprintf(header, sizeof(header), "support-status");
	if (cap)
		snprintf(value, sizeof(value), "supported");
	else
		snprintf(value, sizeof(value), "unsupported");
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "enable-status");
	if (state)
		snprintf(value, sizeof(value), "enabled");
	else
		snprintf(value, sizeof(value), "disabled");
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "clos-enable-status");
	if (clos_enable)
		snprintf(value, sizeof(value), "enabled");
	else
		snprintf(value, sizeof(value), "disabled");
	format_and_print(outf, level + 2, header, value);

	snprintf(header, sizeof(header), "priority-type");
	if (type)
		snprintf(value, sizeof(value), "ordered");
	else
		snprintf(value, sizeof(value), "proportional");
	format_and_print(outf, level + 2, header, value);

	format_and_print(outf, level, NULL, NULL);
}

void isst_clos_display_assoc_information(struct isst_id *id, FILE *outf, int clos)
{
	char header[256];
	char value[256];
	int level;

	level = print_package_info(id, outf);

	snprintf(header, sizeof(header), "get-assoc");
	format_and_print(outf, level + 1, header, NULL);

	snprintf(header, sizeof(header), "clos");
	snprintf(value, sizeof(value), "%d", clos);
	format_and_print(outf, level + 2, header, value);

	format_and_print(outf, level, NULL, NULL);
}

void isst_display_result(struct isst_id *id, FILE *outf, char *feature, char *cmd,
			 int result)
{
	char header[256];
	char value[256];
	int level = 3;

	level = print_package_info(id, outf);

	snprintf(header, sizeof(header), "%s", feature);
	format_and_print(outf, level + 1, header, NULL);
	snprintf(header, sizeof(header), "%s", cmd);
	if (!result)
		snprintf(value, sizeof(value), "success");
	else
		snprintf(value, sizeof(value), "failed(error %d)", result);
	format_and_print(outf, level + 2, header, value);

	format_and_print(outf, level, NULL, NULL);
}

void isst_display_error_info_message(int error, char *msg, int arg_valid, int arg)
{
	FILE *outf = get_output_file();
	static int error_index;
	char header[256];
	char value[256];

	if (!out_format_is_json()) {
		if (arg_valid)
			snprintf(value, sizeof(value), "%s %d", msg, arg);
		else
			snprintf(value, sizeof(value), "%s", msg);

		if (error)
			fprintf(outf, "Error: %s\n", value);
		else
			fprintf(outf, "Information: %s\n", value);
		return;
	}

	if (!start)
		format_and_print(outf, 0, "start", NULL);

	if (error)
		snprintf(header, sizeof(header), "Error%d", error_index++);
	else
		snprintf(header, sizeof(header), "Information:%d", error_index++);
	format_and_print(outf, 1, header, NULL);

	snprintf(header, sizeof(header), "message");
	if (arg_valid)
		snprintf(value, sizeof(value), "%s %d", msg, arg);
	else
		snprintf(value, sizeof(value), "%s", msg);

	format_and_print(outf, 2, header, value);
	format_and_print(outf, 1, NULL, NULL);
	if (!start)
		format_and_print(outf, 0, NULL, NULL);
}

void isst_trl_display_information(struct isst_id *id, FILE *outf, unsigned long long trl)
{
	char header[256];
	char value[256];
	int level;

	level = print_package_info(id, outf);

	snprintf(header, sizeof(header), "get-trl");
	format_and_print(outf, level + 1, header, NULL);

	snprintf(header, sizeof(header), "trl");
	snprintf(value, sizeof(value), "0x%llx", trl);
	format_and_print(outf, level + 2, header, value);

	format_and_print(outf, level, NULL, NULL);
}
