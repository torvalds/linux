// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2018
 * Auxtrace support for s390 CPU-Measurement Sampling Facility
 *
 * Author(s):  Thomas Richter <tmricht@linux.ibm.com>
 */

#include <endian.h>
#include <errno.h>
#include <byteswap.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>

#include "cpumap.h"
#include "color.h"
#include "evsel.h"
#include "evlist.h"
#include "machine.h"
#include "session.h"
#include "util.h"
#include "thread.h"
#include "debug.h"
#include "auxtrace.h"
#include "s390-cpumsf.h"
#include "s390-cpumsf-kernel.h"

struct s390_cpumsf {
	struct auxtrace		auxtrace;
	struct auxtrace_queues	queues;
	struct auxtrace_heap	heap;
	struct perf_session	*session;
	struct machine		*machine;
	u32			auxtrace_type;
	u32			pmu_type;
	u16			machine_type;
};

/* Display s390 CPU measurement facility basic-sampling data entry */
static bool s390_cpumsf_basic_show(const char *color, size_t pos,
				   struct hws_basic_entry *basic)
{
	if (basic->def != 1) {
		pr_err("Invalid AUX trace basic entry [%#08zx]\n", pos);
		return false;
	}
	color_fprintf(stdout, color, "    [%#08zx] Basic   Def:%04x Inst:%#04x"
		      " %c%c%c%c AS:%d ASN:%#04x IA:%#018llx\n"
		      "\t\tCL:%d HPP:%#018llx GPP:%#018llx\n",
		      pos, basic->def, basic->U,
		      basic->T ? 'T' : ' ',
		      basic->W ? 'W' : ' ',
		      basic->P ? 'P' : ' ',
		      basic->I ? 'I' : ' ',
		      basic->AS, basic->prim_asn, basic->ia, basic->CL,
		      basic->hpp, basic->gpp);
	return true;
}

/* Display s390 CPU measurement facility diagnostic-sampling data entry */
static bool s390_cpumsf_diag_show(const char *color, size_t pos,
				  struct hws_diag_entry *diag)
{
	if (diag->def < S390_CPUMSF_DIAG_DEF_FIRST) {
		pr_err("Invalid AUX trace diagnostic entry [%#08zx]\n", pos);
		return false;
	}
	color_fprintf(stdout, color, "    [%#08zx] Diag    Def:%04x %c\n",
		      pos, diag->def, diag->I ? 'I' : ' ');
	return true;
}

/* Return TOD timestamp contained in an trailer entry */
static unsigned long long trailer_timestamp(struct hws_trailer_entry *te)
{
	/* te->t set: TOD in STCKE format, bytes 8-15
	 * to->t not set: TOD in STCK format, bytes 0-7
	 */
	unsigned long long ts;

	memcpy(&ts, &te->timestamp[te->t], sizeof(ts));
	return ts;
}

/* Display s390 CPU measurement facility trailer entry */
static bool s390_cpumsf_trailer_show(const char *color, size_t pos,
				     struct hws_trailer_entry *te)
{
	if (te->bsdes != sizeof(struct hws_basic_entry)) {
		pr_err("Invalid AUX trace trailer entry [%#08zx]\n", pos);
		return false;
	}
	color_fprintf(stdout, color, "    [%#08zx] Trailer %c%c%c bsdes:%d"
		      " dsdes:%d Overflow:%lld Time:%#llx\n"
		      "\t\tC:%d TOD:%#lx 1:%#llx 2:%#llx\n",
		      pos,
		      te->f ? 'F' : ' ',
		      te->a ? 'A' : ' ',
		      te->t ? 'T' : ' ',
		      te->bsdes, te->dsdes, te->overflow,
		      trailer_timestamp(te), te->clock_base, te->progusage2,
		      te->progusage[0], te->progusage[1]);
	return true;
}

/* Test a sample data block. It must be 4KB or a multiple thereof in size and
 * 4KB page aligned. Each sample data page has a trailer entry at the
 * end which contains the sample entry data sizes.
 *
 * Return true if the sample data block passes the checks and set the
 * basic set entry size and diagnostic set entry size.
 *
 * Return false on failure.
 *
 * Note: Old hardware does not set the basic or diagnostic entry sizes
 * in the trailer entry. Use the type number instead.
 */
static bool s390_cpumsf_validate(int machine_type,
				 unsigned char *buf, size_t len,
				 unsigned short *bsdes,
				 unsigned short *dsdes)
{
	struct hws_basic_entry *basic = (struct hws_basic_entry *)buf;
	struct hws_trailer_entry *te;

	*dsdes = *bsdes = 0;
	if (len & (S390_CPUMSF_PAGESZ - 1))	/* Illegal size */
		return false;
	if (basic->def != 1)		/* No basic set entry, must be first */
		return false;
	/* Check for trailer entry at end of SDB */
	te = (struct hws_trailer_entry *)(buf + S390_CPUMSF_PAGESZ
					      - sizeof(*te));
	*bsdes = te->bsdes;
	*dsdes = te->dsdes;
	if (!te->bsdes && !te->dsdes) {
		/* Very old hardware, use CPUID */
		switch (machine_type) {
		case 2097:
		case 2098:
			*dsdes = 64;
			*bsdes = 32;
			break;
		case 2817:
		case 2818:
			*dsdes = 74;
			*bsdes = 32;
			break;
		case 2827:
		case 2828:
			*dsdes = 85;
			*bsdes = 32;
			break;
		default:
			/* Illegal trailer entry */
			return false;
		}
	}
	return true;
}

/* Return true if there is room for another entry */
static bool s390_cpumsf_reached_trailer(size_t entry_sz, size_t pos)
{
	size_t payload = S390_CPUMSF_PAGESZ - sizeof(struct hws_trailer_entry);

	if (payload - (pos & (S390_CPUMSF_PAGESZ - 1)) < entry_sz)
		return false;
	return true;
}

/* Dump an auxiliary buffer. These buffers are multiple of
 * 4KB SDB pages.
 */
static void s390_cpumsf_dump(struct s390_cpumsf *sf,
			     unsigned char *buf, size_t len)
{
	const char *color = PERF_COLOR_BLUE;
	struct hws_basic_entry *basic;
	struct hws_diag_entry *diag;
	size_t pos = 0;
	unsigned short bsdes, dsdes;

	color_fprintf(stdout, color,
		      ". ... s390 AUX data: size %zu bytes\n",
		      len);

	if (!s390_cpumsf_validate(sf->machine_type, buf, len, &bsdes,
				  &dsdes)) {
		pr_err("Invalid AUX trace data block size:%zu"
		       " (type:%d bsdes:%hd dsdes:%hd)\n",
		       len, sf->machine_type, bsdes, dsdes);
		return;
	}

	/* s390 kernel always returns 4KB blocks fully occupied,
	 * no partially filled SDBs.
	 */
	while (pos < len) {
		/* Handle Basic entry */
		basic = (struct hws_basic_entry *)(buf + pos);
		if (s390_cpumsf_basic_show(color, pos, basic))
			pos += bsdes;
		else
			return;

		/* Handle Diagnostic entry */
		diag = (struct hws_diag_entry *)(buf + pos);
		if (s390_cpumsf_diag_show(color, pos, diag))
			pos += dsdes;
		else
			return;

		/* Check for trailer entry */
		if (!s390_cpumsf_reached_trailer(bsdes + dsdes, pos)) {
			/* Show trailer entry */
			struct hws_trailer_entry te;

			pos = (pos + S390_CPUMSF_PAGESZ)
			       & ~(S390_CPUMSF_PAGESZ - 1);
			pos -= sizeof(te);
			memcpy(&te, buf + pos, sizeof(te));
			/* Set descriptor sizes in case of old hardware
			 * where these values are not set.
			 */
			te.bsdes = bsdes;
			te.dsdes = dsdes;
			if (s390_cpumsf_trailer_show(color, pos, &te))
				pos += sizeof(te);
			else
				return;
		}
	}
}

static void s390_cpumsf_dump_event(struct s390_cpumsf *sf, unsigned char *buf,
				   size_t len)
{
	printf(".\n");
	s390_cpumsf_dump(sf, buf, len);
}

static int
s390_cpumsf_process_event(struct perf_session *session __maybe_unused,
			  union perf_event *event __maybe_unused,
			  struct perf_sample *sample __maybe_unused,
			  struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static int
s390_cpumsf_process_auxtrace_event(struct perf_session *session,
				   union perf_event *event __maybe_unused,
				   struct perf_tool *tool __maybe_unused)
{
	struct s390_cpumsf *sf = container_of(session->auxtrace,
					      struct s390_cpumsf,
					      auxtrace);

	int fd = perf_data__fd(session->data);
	struct auxtrace_buffer *buffer;
	off_t data_offset;
	int err;

	if (perf_data__is_pipe(session->data)) {
		data_offset = 0;
	} else {
		data_offset = lseek(fd, 0, SEEK_CUR);
		if (data_offset == -1)
			return -errno;
	}

	err = auxtrace_queues__add_event(&sf->queues, session, event,
					 data_offset, &buffer);
	if (err)
		return err;

	/* Dump here after copying piped trace out of the pipe */
	if (dump_trace) {
		if (auxtrace_buffer__get_data(buffer, fd)) {
			s390_cpumsf_dump_event(sf, buffer->data,
					       buffer->size);
			auxtrace_buffer__put_data(buffer);
		}
	}
	return 0;
}

static int s390_cpumsf_flush(struct perf_session *session __maybe_unused,
			     struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static void s390_cpumsf_free_events(struct perf_session *session)
{
	struct s390_cpumsf *sf = container_of(session->auxtrace,
					      struct s390_cpumsf,
					       auxtrace);
	struct auxtrace_queues *queues = &sf->queues;
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++)
		zfree(&queues->queue_array[i].priv);
	auxtrace_queues__free(queues);
}

static void s390_cpumsf_free(struct perf_session *session)
{
	struct s390_cpumsf *sf = container_of(session->auxtrace,
					      struct s390_cpumsf,
					      auxtrace);

	auxtrace_heap__free(&sf->heap);
	s390_cpumsf_free_events(session);
	session->auxtrace = NULL;
	free(sf);
}

static int s390_cpumsf_get_type(const char *cpuid)
{
	int ret, family = 0;

	ret = sscanf(cpuid, "%*[^,],%u", &family);
	return (ret == 1) ? family : 0;
}

int s390_cpumsf_process_auxtrace_info(union perf_event *event,
				      struct perf_session *session)
{
	struct auxtrace_info_event *auxtrace_info = &event->auxtrace_info;
	struct s390_cpumsf *sf;
	int err;

	if (auxtrace_info->header.size < sizeof(struct auxtrace_info_event))
		return -EINVAL;

	sf = zalloc(sizeof(struct s390_cpumsf));
	if (sf == NULL)
		return -ENOMEM;

	err = auxtrace_queues__init(&sf->queues);
	if (err)
		goto err_free;

	sf->session = session;
	sf->machine = &session->machines.host; /* No kvm support */
	sf->auxtrace_type = auxtrace_info->type;
	sf->pmu_type = PERF_TYPE_RAW;
	sf->machine_type = s390_cpumsf_get_type(session->evlist->env->cpuid);

	sf->auxtrace.process_event = s390_cpumsf_process_event;
	sf->auxtrace.process_auxtrace_event = s390_cpumsf_process_auxtrace_event;
	sf->auxtrace.flush_events = s390_cpumsf_flush;
	sf->auxtrace.free_events = s390_cpumsf_free_events;
	sf->auxtrace.free = s390_cpumsf_free;
	session->auxtrace = &sf->auxtrace;

	return 0;

err_free:
	free(sf);
	return err;
}
