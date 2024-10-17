// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2019
 * Author(s): Thomas Richter <tmricht@linux.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * Architecture specific trace_event function. Save event's bc000 raw data
 * to file. File name is aux.ctr.## where ## stands for the CPU number the
 * sample was taken from.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <sys/stat.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>

#include "debug.h"
#include "session.h"
#include "evlist.h"
#include "color.h"
#include "sample-raw.h"
#include "s390-cpumcf-kernel.h"
#include "util/pmu.h"
#include "util/sample.h"

static size_t ctrset_size(struct cf_ctrset_entry *set)
{
	return sizeof(*set) + set->ctr * sizeof(u64);
}

static bool ctrset_valid(struct cf_ctrset_entry *set)
{
	return set->def == S390_CPUMCF_DIAG_DEF;
}

/* CPU Measurement Counter Facility raw data is a byte stream. It is 8 byte
 * aligned and might have trailing padding bytes.
 * Display the raw data on screen.
 */
static bool s390_cpumcfdg_testctr(struct perf_sample *sample)
{
	size_t len = sample->raw_size, offset = 0;
	unsigned char *buf = sample->raw_data;
	struct cf_trailer_entry *te;
	struct cf_ctrset_entry *cep, ce;

	while (offset < len) {
		cep = (struct cf_ctrset_entry *)(buf + offset);
		ce.def = be16_to_cpu(cep->def);
		ce.set = be16_to_cpu(cep->set);
		ce.ctr = be16_to_cpu(cep->ctr);
		ce.res1 = be16_to_cpu(cep->res1);

		if (!ctrset_valid(&ce) || offset + ctrset_size(&ce) > len) {
			/* Raw data for counter sets are always multiple of 8
			 * bytes. Prepending a 4 bytes size field to the
			 * raw data block in the sample causes the perf tool
			 * to append 4 padding bytes to make the raw data part
			 * of the sample a multiple of eight bytes again.
			 *
			 * If the last entry (trailer) is 4 bytes off the raw
			 * area data end, all is good.
			 */
			if (len - offset - sizeof(*te) == 4)
				break;
			pr_err("Invalid counter set entry at %zd\n", offset);
			return false;
		}
		offset += ctrset_size(&ce);
	}
	return true;
}

/* Dump event bc000 on screen, already tested on correctness. */
static void s390_cpumcfdg_dumptrail(const char *color, size_t offset,
				    struct cf_trailer_entry *tep)
{
	struct cf_trailer_entry  te;

	te.flags = be64_to_cpu(tep->flags);
	te.cfvn = be16_to_cpu(tep->cfvn);
	te.csvn = be16_to_cpu(tep->csvn);
	te.cpu_speed = be32_to_cpu(tep->cpu_speed);
	te.timestamp = be64_to_cpu(tep->timestamp);
	te.progusage1 = be64_to_cpu(tep->progusage1);
	te.progusage2 = be64_to_cpu(tep->progusage2);
	te.progusage3 = be64_to_cpu(tep->progusage3);
	te.tod_base = be64_to_cpu(tep->tod_base);
	te.mach_type = be16_to_cpu(tep->mach_type);
	te.res1 = be16_to_cpu(tep->res1);
	te.res2 = be32_to_cpu(tep->res2);

	color_fprintf(stdout, color, "    [%#08zx] Trailer:%c%c%c%c%c"
		      " Cfvn:%d Csvn:%d Speed:%d TOD:%#lx\n",
		      offset, te.clock_base ? 'T' : ' ',
		      te.speed ? 'S' : ' ', te.mtda ? 'M' : ' ',
		      te.caca ? 'C' : ' ', te.lcda ? 'L' : ' ',
		      te.cfvn, te.csvn, te.cpu_speed, te.timestamp);
	color_fprintf(stdout, color, "\t\t1:%lx 2:%lx 3:%lx TOD-Base:%#lx"
		      " Type:%x\n\n",
		      te.progusage1, te.progusage2, te.progusage3,
		      te.tod_base, te.mach_type);
}

/* Return starting number of a counter set */
static int get_counterset_start(int setnr)
{
	switch (setnr) {
	case CPUMF_CTR_SET_BASIC:		/* Basic counter set */
		return 0;
	case CPUMF_CTR_SET_USER:		/* Problem state counter set */
		return 32;
	case CPUMF_CTR_SET_CRYPTO:		/* Crypto counter set */
		return 64;
	case CPUMF_CTR_SET_EXT:			/* Extended counter set */
		return 128;
	case CPUMF_CTR_SET_MT_DIAG:		/* Diagnostic counter set */
		return 448;
	case PERF_EVENT_PAI_NNPA_ALL:		/* PAI NNPA counter set */
	case PERF_EVENT_PAI_CRYPTO_ALL:		/* PAI CRYPTO counter set */
		return setnr;
	default:
		return -1;
	}
}

struct get_counter_name_data {
	int wanted;
	char *result;
};

static int get_counter_name_callback(void *vdata, struct pmu_event_info *info)
{
	struct get_counter_name_data *data = vdata;
	int rc, event_nr;
	const char *event_str;

	if (info->str == NULL)
		return 0;

	event_str = strstr(info->str, "event=");
	if (!event_str)
		return 0;

	rc = sscanf(event_str, "event=%x", &event_nr);
	if (rc == 1 && event_nr == data->wanted) {
		data->result = strdup(info->name);
		return 1; /* Terminate the search. */
	}
	return 0;
}

/* Scan the PMU and extract the logical name of a counter from the event. Input
 * is the counter set and counter number with in the set. Construct the event
 * number and use this as key. If they match return the name of this counter.
 * If no match is found a NULL pointer is returned.
 */
static char *get_counter_name(int set, int nr, struct perf_pmu *pmu)
{
	struct get_counter_name_data data = {
		.wanted = get_counterset_start(set) + nr,
		.result = NULL,
	};

	if (!pmu)
		return NULL;

	perf_pmu__for_each_event(pmu, /*skip_duplicate_pmus=*/ true,
				 &data, get_counter_name_callback);
	return data.result;
}

static void s390_cpumcfdg_dump(struct perf_pmu *pmu, struct perf_sample *sample)
{
	size_t i, len = sample->raw_size, offset = 0;
	unsigned char *buf = sample->raw_data;
	const char *color = PERF_COLOR_BLUE;
	struct cf_ctrset_entry *cep, ce;
	u64 *p;

	while (offset < len) {
		cep = (struct cf_ctrset_entry *)(buf + offset);

		ce.def = be16_to_cpu(cep->def);
		ce.set = be16_to_cpu(cep->set);
		ce.ctr = be16_to_cpu(cep->ctr);
		ce.res1 = be16_to_cpu(cep->res1);

		if (!ctrset_valid(&ce)) {	/* Print trailer */
			s390_cpumcfdg_dumptrail(color, offset,
						(struct cf_trailer_entry *)cep);
			return;
		}

		color_fprintf(stdout, color, "    [%#08zx] Counterset:%d"
			      " Counters:%d\n", offset, ce.set, ce.ctr);
		for (i = 0, p = (u64 *)(cep + 1); i < ce.ctr; ++i, ++p) {
			char *ev_name = get_counter_name(ce.set, i, pmu);

			color_fprintf(stdout, color,
				      "\tCounter:%03zd %s Value:%#018"PRIx64"\n", i,
				      ev_name ?: "<unknown>", be64_to_cpu(*p));
			free(ev_name);
		}
		offset += ctrset_size(&ce);
	}
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic ignored "-Wattributes"
/*
 * Check for consistency of PAI_CRYPTO/PAI_NNPA raw data.
 */
struct pai_data {		/* Event number and value */
	u16 event_nr;
	u64 event_val;
} __packed;

#pragma GCC diagnostic pop

/*
 * Test for valid raw data. At least one PAI event should be in the raw
 * data section.
 */
static bool s390_pai_all_test(struct perf_sample *sample)
{
	size_t len = sample->raw_size;

	if (len < 0xa)
		return false;
	return true;
}

static void s390_pai_all_dump(struct evsel *evsel, struct perf_sample *sample)
{
	size_t len = sample->raw_size, offset = 0;
	unsigned char *p = sample->raw_data;
	const char *color = PERF_COLOR_BLUE;
	struct pai_data pai_data;
	char *ev_name;

	while (offset < len) {
		memcpy(&pai_data.event_nr, p, sizeof(pai_data.event_nr));
		pai_data.event_nr = be16_to_cpu(pai_data.event_nr);
		p += sizeof(pai_data.event_nr);
		offset += sizeof(pai_data.event_nr);

		memcpy(&pai_data.event_val, p, sizeof(pai_data.event_val));
		pai_data.event_val = be64_to_cpu(pai_data.event_val);
		p += sizeof(pai_data.event_val);
		offset += sizeof(pai_data.event_val);

		ev_name = get_counter_name(evsel->core.attr.config,
					   pai_data.event_nr, evsel->pmu);
		color_fprintf(stdout, color, "\tCounter:%03d %s Value:%#018"PRIx64"\n",
			      pai_data.event_nr, ev_name ?: "<unknown>",
			      pai_data.event_val);
		free(ev_name);

		if (offset + 0xa > len)
			break;
	}
	color_fprintf(stdout, color, "\n");
}

/* S390 specific trace event function. Check for PERF_RECORD_SAMPLE events
 * and if the event was triggered by a
 * - counter set diagnostic event
 * - processor activity assist (PAI) crypto counter event
 * - processor activity assist (PAI) neural network processor assist (NNPA)
 *   counter event
 * display its raw data.
 * The function is only invoked when the dump flag -D is set.
 *
 * Function evlist__s390_sample_raw() is defined as call back after it has
 * been verified that the perf.data file was created on s390 platform.
 */
void evlist__s390_sample_raw(struct evlist *evlist, union perf_event *event,
			     struct perf_sample *sample)
{
	const char *pai_name;
	struct evsel *evsel;

	if (event->header.type != PERF_RECORD_SAMPLE)
		return;

	evsel = evlist__event2evsel(evlist, event);
	if (!evsel)
		return;

	/* Check for raw data in sample */
	if (!sample->raw_size || !sample->raw_data)
		return;

	/* Display raw data on screen */
	if (evsel->core.attr.config == PERF_EVENT_CPUM_CF_DIAG) {
		if (!evsel->pmu)
			evsel->pmu = perf_pmus__find("cpum_cf");
		if (!s390_cpumcfdg_testctr(sample))
			pr_err("Invalid counter set data encountered\n");
		else
			s390_cpumcfdg_dump(evsel->pmu, sample);
		return;
	}

	switch (evsel->core.attr.config) {
	case PERF_EVENT_PAI_NNPA_ALL:
		pai_name = "NNPA_ALL";
		break;
	case PERF_EVENT_PAI_CRYPTO_ALL:
		pai_name = "CRYPTO_ALL";
		break;
	default:
		return;
	}

	if (!s390_pai_all_test(sample)) {
		pr_err("Invalid %s raw data encountered\n", pai_name);
	} else {
		if (!evsel->pmu)
			evsel->pmu = perf_pmus__find_by_type(evsel->core.attr.type);
		s390_pai_all_dump(evsel, sample);
	}
}
