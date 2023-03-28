// SPDX-License-Identifier: GPL-2.0
/*
 * File for any parts of the Coresight decoding that don't require
 * OpenCSD.
 */

#include <errno.h>
#include <inttypes.h>

#include "cs-etm.h"

static const char * const cs_etm_global_header_fmts[] = {
	[CS_HEADER_VERSION]	= "	Header version		       %llx\n",
	[CS_PMU_TYPE_CPUS]	= "	PMU type/num cpus	       %llx\n",
	[CS_ETM_SNAPSHOT]	= "	Snapshot		       %llx\n",
};

static const char * const cs_etm_priv_fmts[] = {
	[CS_ETM_MAGIC]		= "	Magic number		       %llx\n",
	[CS_ETM_CPU]		= "	CPU			       %lld\n",
	[CS_ETM_NR_TRC_PARAMS]	= "	NR_TRC_PARAMS		       %llx\n",
	[CS_ETM_ETMCR]		= "	ETMCR			       %llx\n",
	[CS_ETM_ETMTRACEIDR]	= "	ETMTRACEIDR		       %llx\n",
	[CS_ETM_ETMCCER]	= "	ETMCCER			       %llx\n",
	[CS_ETM_ETMIDR]		= "	ETMIDR			       %llx\n",
};

static const char * const cs_etmv4_priv_fmts[] = {
	[CS_ETM_MAGIC]		= "	Magic number		       %llx\n",
	[CS_ETM_CPU]		= "	CPU			       %lld\n",
	[CS_ETM_NR_TRC_PARAMS]	= "	NR_TRC_PARAMS		       %llx\n",
	[CS_ETMV4_TRCCONFIGR]	= "	TRCCONFIGR		       %llx\n",
	[CS_ETMV4_TRCTRACEIDR]	= "	TRCTRACEIDR		       %llx\n",
	[CS_ETMV4_TRCIDR0]	= "	TRCIDR0			       %llx\n",
	[CS_ETMV4_TRCIDR1]	= "	TRCIDR1			       %llx\n",
	[CS_ETMV4_TRCIDR2]	= "	TRCIDR2			       %llx\n",
	[CS_ETMV4_TRCIDR8]	= "	TRCIDR8			       %llx\n",
	[CS_ETMV4_TRCAUTHSTATUS] = "	TRCAUTHSTATUS		       %llx\n",
	[CS_ETMV4_TS_SOURCE]	= "	TS_SOURCE		       %lld\n",
};

static const char * const cs_ete_priv_fmts[] = {
	[CS_ETM_MAGIC]		= "	Magic number		       %llx\n",
	[CS_ETM_CPU]		= "	CPU			       %lld\n",
	[CS_ETM_NR_TRC_PARAMS]	= "	NR_TRC_PARAMS		       %llx\n",
	[CS_ETE_TRCCONFIGR]	= "	TRCCONFIGR		       %llx\n",
	[CS_ETE_TRCTRACEIDR]	= "	TRCTRACEIDR		       %llx\n",
	[CS_ETE_TRCIDR0]	= "	TRCIDR0			       %llx\n",
	[CS_ETE_TRCIDR1]	= "	TRCIDR1			       %llx\n",
	[CS_ETE_TRCIDR2]	= "	TRCIDR2			       %llx\n",
	[CS_ETE_TRCIDR8]	= "	TRCIDR8			       %llx\n",
	[CS_ETE_TRCAUTHSTATUS]	= "	TRCAUTHSTATUS		       %llx\n",
	[CS_ETE_TRCDEVARCH]	= "	TRCDEVARCH                     %llx\n",
	[CS_ETE_TS_SOURCE]	= "	TS_SOURCE                      %lld\n",
};

static const char * const param_unk_fmt =
	"	Unknown parameter [%d]	       %"PRIx64"\n";
static const char * const magic_unk_fmt =
	"	Magic number Unknown	       %"PRIx64"\n";

static int cs_etm__print_cpu_metadata_v0(u64 *val, int *offset)
{
	int i = *offset, j, nr_params = 0, fmt_offset;
	u64 magic;

	/* check magic value */
	magic = val[i + CS_ETM_MAGIC];
	if ((magic != __perf_cs_etmv3_magic) &&
	    (magic != __perf_cs_etmv4_magic)) {
		/* failure - note bad magic value */
		fprintf(stdout, magic_unk_fmt, magic);
		return -EINVAL;
	}

	/* print common header block */
	fprintf(stdout, cs_etm_priv_fmts[CS_ETM_MAGIC], val[i++]);
	fprintf(stdout, cs_etm_priv_fmts[CS_ETM_CPU], val[i++]);

	if (magic == __perf_cs_etmv3_magic) {
		nr_params = CS_ETM_NR_TRC_PARAMS_V0;
		fmt_offset = CS_ETM_ETMCR;
		/* after common block, offset format index past NR_PARAMS */
		for (j = fmt_offset; j < nr_params + fmt_offset; j++, i++)
			fprintf(stdout, cs_etm_priv_fmts[j], val[i]);
	} else if (magic == __perf_cs_etmv4_magic) {
		nr_params = CS_ETMV4_NR_TRC_PARAMS_V0;
		fmt_offset = CS_ETMV4_TRCCONFIGR;
		/* after common block, offset format index past NR_PARAMS */
		for (j = fmt_offset; j < nr_params + fmt_offset; j++, i++)
			fprintf(stdout, cs_etmv4_priv_fmts[j], val[i]);
	}
	*offset = i;
	return 0;
}

static int cs_etm__print_cpu_metadata_v1(u64 *val, int *offset)
{
	int i = *offset, j, total_params = 0;
	u64 magic;

	magic = val[i + CS_ETM_MAGIC];
	/* total params to print is NR_PARAMS + common block size for v1 */
	total_params = val[i + CS_ETM_NR_TRC_PARAMS] + CS_ETM_COMMON_BLK_MAX_V1;

	if (magic == __perf_cs_etmv3_magic) {
		for (j = 0; j < total_params; j++, i++) {
			/* if newer record - could be excess params */
			if (j >= CS_ETM_PRIV_MAX)
				fprintf(stdout, param_unk_fmt, j, val[i]);
			else
				fprintf(stdout, cs_etm_priv_fmts[j], val[i]);
		}
	} else if (magic == __perf_cs_etmv4_magic) {
		for (j = 0; j < total_params; j++, i++) {
			/* if newer record - could be excess params */
			if (j >= CS_ETMV4_PRIV_MAX)
				fprintf(stdout, param_unk_fmt, j, val[i]);
			else
				fprintf(stdout, cs_etmv4_priv_fmts[j], val[i]);
		}
	} else if (magic == __perf_cs_ete_magic) {
		for (j = 0; j < total_params; j++, i++) {
			/* if newer record - could be excess params */
			if (j >= CS_ETE_PRIV_MAX)
				fprintf(stdout, param_unk_fmt, j, val[i]);
			else
				fprintf(stdout, cs_ete_priv_fmts[j], val[i]);
		}
	} else {
		/* failure - note bad magic value and error out */
		fprintf(stdout, magic_unk_fmt, magic);
		return -EINVAL;
	}
	*offset = i;
	return 0;
}

static void cs_etm__print_auxtrace_info(u64 *val, int num)
{
	int i, cpu = 0, version, err;

	version = val[0];

	for (i = 0; i < CS_HEADER_VERSION_MAX; i++)
		fprintf(stdout, cs_etm_global_header_fmts[i], val[i]);

	for (i = CS_HEADER_VERSION_MAX; cpu < num; cpu++) {
		if (version == 0)
			err = cs_etm__print_cpu_metadata_v0(val, &i);
		else if (version == 1)
			err = cs_etm__print_cpu_metadata_v1(val, &i);
		if (err)
			return;
	}
}

/*
 * Do some basic checks and print the auxtrace info header before calling
 * into cs_etm__process_auxtrace_info_full() which requires OpenCSD to be
 * linked in. This allows some basic debugging if OpenCSD is missing.
 */
int cs_etm__process_auxtrace_info(union perf_event *event,
				  struct perf_session *session)
{
	struct perf_record_auxtrace_info *auxtrace_info = &event->auxtrace_info;
	int event_header_size = sizeof(struct perf_event_header);
	int num_cpu;
	u64 *ptr = NULL;
	u64 hdr_version;

	if (auxtrace_info->header.size < (event_header_size + INFO_HEADER_SIZE))
		return -EINVAL;

	/* First the global part */
	ptr = (u64 *) auxtrace_info->priv;

	/* Look for version of the header */
	hdr_version = ptr[0];
	if (hdr_version > CS_HEADER_CURRENT_VERSION) {
		pr_err("\nCS ETM Trace: Unknown Header Version = %#" PRIx64, hdr_version);
		pr_err(", version supported <= %x\n", CS_HEADER_CURRENT_VERSION);
		return -EINVAL;
	}

	if (dump_trace) {
		num_cpu = ptr[CS_PMU_TYPE_CPUS] & 0xffffffff;
		cs_etm__print_auxtrace_info(ptr, num_cpu);
	}

	return cs_etm__process_auxtrace_info_full(event, session);
}
