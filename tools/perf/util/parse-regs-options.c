// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "util/debug.h"
#include <dwarf-regs.h>
#include <sys/param.h>
#include <subcmd/parse-options.h>
#include "util/perf_regs.h"
#include "util/parse-regs-options.h"
#include "record.h"

static void
__list_gp_regs(FILE *fp, uint64_t mask, int abi)
{
	const char *last_name = NULL;

	for (int reg = 0; reg < 64; reg++) {
		const char *name;

		if (((1ULL << reg) & mask) == 0)
			continue;

		name = perf_reg_name(reg, EM_HOST, EF_HOST, abi);
		if (name && (!last_name || strcmp(last_name, name)))
			fprintf(fp, "%s%s", reg > 0 ? " " : "", name);
		last_name = name;
	}
}

static void
__list_simd_regs(FILE *fp, uint64_t mask, bool intr, bool pred)
{
	uint64_t bitmap = 0;
	uint16_t qwords = 0;
	const char *name;
	int i = 0;

	for (int reg_c = 0; reg_c < 64; reg_c++) {
		if (((1ULL << reg_c) & mask) == 0)
			continue;

		name = perf_simd_reg_class_name(EM_HOST, reg_c, pred);
		bitmap = intr ?
			 perf_intr_simd_reg_class_bitmap_qwords(EM_HOST, reg_c, &qwords, pred) :
			 perf_user_simd_reg_class_bitmap_qwords(EM_HOST, reg_c, &qwords, pred);
		if (name && bitmap)
			fprintf(fp, "%s%s0-%d", i++ > 0 ? " " : "",
				name, fls64(bitmap) - 1);
	}
}

static void
list_perf_regs(FILE *fp, uint64_t mask, uint64_t simd_mask,
	       uint64_t pred_mask, int abi, bool intr)
{
	bool printed = false;

	fprintf(fp, "available registers: ");

	if (mask) {
		__list_gp_regs(fp, mask, abi);
		printed = true;
	}

	if (simd_mask) {
		if (printed)
			fprintf(fp, " ");
		__list_simd_regs(fp, simd_mask, intr, /*pred=*/false);
		printed = true;
	}

	if (pred_mask) {
		if (printed)
			fprintf(fp, " ");
		__list_simd_regs(fp, pred_mask, intr, /*pred=*/true);
		printed = true;
	}

	fputc('\n', fp);
}

static uint64_t
name_to_gp_reg_mask(const char *to_match, uint64_t mask, int abi)
{
	uint64_t reg_mask = 0;

	if (!mask)
		return reg_mask;

	for (int reg = 0; reg < 64; reg++) {
		const char *name;

		if (((1ULL << reg) & mask) == 0)
			continue;

		name = perf_reg_name(reg, EM_HOST, EF_HOST, abi);
		if (!name)
			continue;

		if (!strcasecmp(to_match, name))
			reg_mask |= 1ULL << reg;
	}
	return reg_mask;
}

static bool
name_to_simd_reg_mask(struct record_opts *opts, const char *to_match,
		      uint64_t mask, bool intr, bool pred)
{
	bool matched = false;
	uint64_t bitmap;
	uint16_t qwords;
	int reg_c;

	if (!mask)
		return false;

	for (reg_c = 0; reg_c < 64; reg_c++) {
		const char *name;

		if (((1ULL << reg_c) & mask) == 0)
			continue;

		name = perf_simd_reg_class_name(EM_HOST, reg_c, pred);
		if (!name)
			continue;

		if (!strcasecmp(to_match, name)) {
			matched = true;
			break;
		}
	}

	if (!matched)
		return false;

	if (intr) {
		bitmap = perf_intr_simd_reg_class_bitmap_qwords(EM_HOST,
							reg_c, &qwords, pred);
	} else {
		bitmap = perf_user_simd_reg_class_bitmap_qwords(EM_HOST,
							reg_c, &qwords, pred);
	}

	/*
	 * Assume higher width SIMD registers are always the superset of lower
	 * width SIMD registers. So only pick the largest qwords and bitmap.
	 */
	if (pred) {
		opts->sample_pred_reg_qwords =
			MAX(qwords, opts->sample_pred_reg_qwords);
		if (intr &&
		    hweight64(bitmap) > hweight32(opts->sample_intr_pred_regs))
			opts->sample_intr_pred_regs = bitmap;
		if (!intr &&
		    hweight64(bitmap) > hweight32(opts->sample_user_pred_regs))
			opts->sample_user_pred_regs = bitmap;

		/*
		 * sample_intr_pred_regs and sample_user_pred_regs share
		 * sample_pred_reg_qwords. If both are set, keep their bitmaps identical.
		 */
		if (opts->sample_intr_pred_regs && opts->sample_user_pred_regs) {
			uint64_t max_pred_regs = MAX(opts->sample_intr_pred_regs,
						     opts->sample_user_pred_regs);

			opts->sample_intr_pred_regs = max_pred_regs;
			opts->sample_user_pred_regs = max_pred_regs;
		}
	} else {
		opts->sample_vec_reg_qwords =
			MAX(qwords, opts->sample_vec_reg_qwords);
		if (intr &&
		    hweight64(bitmap) > hweight64(opts->sample_intr_vec_regs))
			opts->sample_intr_vec_regs = bitmap;
		if (!intr &&
		    hweight64(bitmap) > hweight64(opts->sample_user_vec_regs))
			opts->sample_user_vec_regs = bitmap;

		/*
		 * sample_intr_vec_regs and sample_user_vec_regs share
		 * sample_vec_reg_qwords. If both are set, keep their bitmaps identical.
		 */
		if (opts->sample_intr_vec_regs && opts->sample_user_vec_regs) {
			uint64_t max_vec_regs = MAX(opts->sample_intr_vec_regs,
						    opts->sample_user_vec_regs);

			opts->sample_intr_vec_regs = max_vec_regs;
			opts->sample_user_vec_regs = max_vec_regs;
		}
	}

	if (opts->sample_pred_reg_qwords || opts->sample_vec_reg_qwords)
		opts->sample_simd_regs_enabled = 1;

	return true;
}

static int
__parse_regs(const struct option *opt, const char *str, int unset, bool intr)
{
	uint64_t *mode = (uint64_t *)opt->value;
	struct record_opts *opts;
	char *s, *os = NULL, *p;
	uint64_t simd_mask;
	uint64_t pred_mask;
	uint64_t mask;
	const char *warn;
	bool matched;
	int ret = -1;
	int abi = 0;

	if (unset)
		return 0;

	mask = intr ? perf_intr_reg_mask(EM_HOST, &abi) :
		      perf_user_reg_mask(EM_HOST, &abi);
	opts = intr ? container_of(opt->value, struct record_opts, sample_intr_regs) :
		      container_of(opt->value, struct record_opts, sample_user_regs);

	/* str may be NULL in case no arg is passed to -I */
	if (!str) {
		*mode = mask;
		if (abi & PERF_SAMPLE_REGS_ABI_SIMD)
			opts->sample_simd_regs_enabled = 1;
		return 0;
	}

	/* because str is read-only */
	s = os = strdup(str);
	if (!s)
		return -1;

	if (intr) {
		simd_mask = perf_intr_simd_reg_class_mask(EM_HOST, /*pred=*/false);
		pred_mask = perf_intr_simd_reg_class_mask(EM_HOST, /*pred=*/true);
	} else {
		simd_mask = perf_user_simd_reg_class_mask(EM_HOST, /*pred=*/false);
		pred_mask = perf_user_simd_reg_class_mask(EM_HOST, /*pred=*/true);
	}

	warn = "Unknown register \"%s\", check man page or run \"perf record %s?\"\n";
	for (;;) {
		uint64_t reg_mask;

		p = strchr(s, ',');
		if (p)
			*p = '\0';

		if (!strcmp(s, "?")) {
			list_perf_regs(stderr, mask, simd_mask, pred_mask, abi, intr);
			goto error;
		}

		reg_mask = name_to_gp_reg_mask(s, mask, abi);
		if (reg_mask) {
			if (abi & PERF_SAMPLE_REGS_ABI_SIMD)
				opts->sample_simd_regs_enabled = 1;
		} else {
			matched = name_to_simd_reg_mask(opts, s, simd_mask,
							intr, /*pred=*/false) ||
				  name_to_simd_reg_mask(opts, s, pred_mask,
							intr, /*pred=*/true);
			if (!matched) {
				ui__warning(warn, s, intr ? "-I" : "--user-regs=");
				goto error;
			}
		}
		*mode |= reg_mask;

		if (!p)
			break;

		s = p + 1;
	}
	ret = 0;

error:
	free(os);
	return ret;
}

int
parse_user_regs(const struct option *opt, const char *str, int unset)
{
	return __parse_regs(opt, str, unset, false);
}

int
parse_intr_regs(const struct option *opt, const char *str, int unset)
{
	return __parse_regs(opt, str, unset, true);
}
