// SPDX-License-Identifier: GPL-2.0
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <byteswap.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/stringify.h>

#include "util.h"
#include "event.h"
#include "debug.h"
#include "evlist.h"
#include "symbol.h"
#include <elf.h>

#include "tsc.h"
#include "session.h"
#include "jit.h"
#include "jitdump.h"
#include "genelf.h"
#include "../builtin.h"

#include "sane_ctype.h"

struct jit_buf_desc {
	struct perf_data *output;
	struct perf_session *session;
	struct machine *machine;
	union jr_entry   *entry;
	void             *buf;
	uint64_t	 sample_type;
	size_t           bufsize;
	FILE             *in;
	bool		 needs_bswap; /* handles cross-endianess */
	bool		 use_arch_timestamp;
	void		 *debug_data;
	void		 *unwinding_data;
	uint64_t	 unwinding_size;
	uint64_t	 unwinding_mapped_size;
	uint64_t         eh_frame_hdr_size;
	size_t		 nr_debug_entries;
	uint32_t         code_load_count;
	u64		 bytes_written;
	struct rb_root   code_root;
	char		 dir[PATH_MAX];
};

struct debug_line_info {
	unsigned long vma;
	unsigned int lineno;
	/* The filename format is unspecified, absolute path, relative etc. */
	char const filename[0];
};

struct jit_tool {
	struct perf_tool tool;
	struct perf_data	output;
	struct perf_data	input;
	u64 bytes_written;
};

#define hmax(a, b) ((a) > (b) ? (a) : (b))
#define get_jit_tool(t) (container_of(tool, struct jit_tool, tool))

static int
jit_emit_elf(char *filename,
	     const char *sym,
	     uint64_t code_addr,
	     const void *code,
	     int csize,
	     void *debug,
	     int nr_debug_entries,
	     void *unwinding,
	     uint32_t unwinding_header_size,
	     uint32_t unwinding_size)
{
	int ret, fd;

	if (verbose > 0)
		fprintf(stderr, "write ELF image %s\n", filename);

	fd = open(filename, O_CREAT|O_TRUNC|O_WRONLY, 0644);
	if (fd == -1) {
		pr_warning("cannot create jit ELF %s: %s\n", filename, strerror(errno));
		return -1;
	}

	ret = jit_write_elf(fd, code_addr, sym, (const void *)code, csize, debug, nr_debug_entries,
			    unwinding, unwinding_header_size, unwinding_size);

        close(fd);

        if (ret)
                unlink(filename);

	return ret;
}

static void
jit_close(struct jit_buf_desc *jd)
{
	if (!(jd && jd->in))
		return;
	funlockfile(jd->in);
	fclose(jd->in);
	jd->in = NULL;
}

static int
jit_validate_events(struct perf_session *session)
{
	struct perf_evsel *evsel;

	/*
	 * check that all events use CLOCK_MONOTONIC
	 */
	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->attr.use_clockid == 0 || evsel->attr.clockid != CLOCK_MONOTONIC)
			return -1;
	}
	return 0;
}

static int
jit_open(struct jit_buf_desc *jd, const char *name)
{
	struct jitheader header;
	struct jr_prefix *prefix;
	ssize_t bs, bsz = 0;
	void *n, *buf = NULL;
	int ret, retval = -1;

	jd->in = fopen(name, "r");
	if (!jd->in)
		return -1;

	bsz = hmax(sizeof(header), sizeof(*prefix));

	buf = malloc(bsz);
	if (!buf)
		goto error;

	/*
	 * protect from writer modifying the file while we are reading it
	 */
	flockfile(jd->in);

	ret = fread(buf, sizeof(header), 1, jd->in);
	if (ret != 1)
		goto error;

	memcpy(&header, buf, sizeof(header));

	if (header.magic != JITHEADER_MAGIC) {
		if (header.magic != JITHEADER_MAGIC_SW)
			goto error;
		jd->needs_bswap = true;
	}

	if (jd->needs_bswap) {
		header.version    = bswap_32(header.version);
		header.total_size = bswap_32(header.total_size);
		header.pid	  = bswap_32(header.pid);
		header.elf_mach   = bswap_32(header.elf_mach);
		header.timestamp  = bswap_64(header.timestamp);
		header.flags      = bswap_64(header.flags);
	}

	jd->use_arch_timestamp = header.flags & JITDUMP_FLAGS_ARCH_TIMESTAMP;

	if (verbose > 2)
		pr_debug("version=%u\nhdr.size=%u\nts=0x%llx\npid=%d\nelf_mach=%d\nuse_arch_timestamp=%d\n",
			header.version,
			header.total_size,
			(unsigned long long)header.timestamp,
			header.pid,
			header.elf_mach,
			jd->use_arch_timestamp);

	if (header.version > JITHEADER_VERSION) {
		pr_err("wrong jitdump version %u, expected " __stringify(JITHEADER_VERSION),
			header.version);
		goto error;
	}

	if (header.flags & JITDUMP_FLAGS_RESERVED) {
		pr_err("jitdump file contains invalid or unsupported flags 0x%llx\n",
		       (unsigned long long)header.flags & JITDUMP_FLAGS_RESERVED);
		goto error;
	}

	if (jd->use_arch_timestamp && !jd->session->time_conv.time_mult) {
		pr_err("jitdump file uses arch timestamps but there is no timestamp conversion\n");
		goto error;
	}

	/*
	 * validate event is using the correct clockid
	 */
	if (!jd->use_arch_timestamp && jit_validate_events(jd->session)) {
		pr_err("error, jitted code must be sampled with perf record -k 1\n");
		goto error;
	}

	bs = header.total_size - sizeof(header);

	if (bs > bsz) {
		n = realloc(buf, bs);
		if (!n)
			goto error;
		bsz = bs;
		buf = n;
		/* read extra we do not know about */
		ret = fread(buf, bs - bsz, 1, jd->in);
		if (ret != 1)
			goto error;
	}
	/*
	 * keep dirname for generating files and mmap records
	 */
	strcpy(jd->dir, name);
	dirname(jd->dir);

	return 0;
error:
	funlockfile(jd->in);
	fclose(jd->in);
	return retval;
}

static union jr_entry *
jit_get_next_entry(struct jit_buf_desc *jd)
{
	struct jr_prefix *prefix;
	union jr_entry *jr;
	void *addr;
	size_t bs, size;
	int id, ret;

	if (!(jd && jd->in))
		return NULL;

	if (jd->buf == NULL) {
		size_t sz = getpagesize();
		if (sz < sizeof(*prefix))
			sz = sizeof(*prefix);

		jd->buf = malloc(sz);
		if (jd->buf == NULL)
			return NULL;

		jd->bufsize = sz;
	}

	prefix = jd->buf;

	/*
	 * file is still locked at this point
	 */
	ret = fread(prefix, sizeof(*prefix), 1, jd->in);
	if (ret  != 1)
		return NULL;

	if (jd->needs_bswap) {
		prefix->id   	   = bswap_32(prefix->id);
		prefix->total_size = bswap_32(prefix->total_size);
		prefix->timestamp  = bswap_64(prefix->timestamp);
	}
	id   = prefix->id;
	size = prefix->total_size;

	bs = (size_t)size;
	if (bs < sizeof(*prefix))
		return NULL;

	if (id >= JIT_CODE_MAX) {
		pr_warning("next_entry: unknown record type %d, skipping\n", id);
	}
	if (bs > jd->bufsize) {
		void *n;
		n = realloc(jd->buf, bs);
		if (!n)
			return NULL;
		jd->buf = n;
		jd->bufsize = bs;
	}

	addr = ((void *)jd->buf) + sizeof(*prefix);

	ret = fread(addr, bs - sizeof(*prefix), 1, jd->in);
	if (ret != 1)
		return NULL;

	jr = (union jr_entry *)jd->buf;

	switch(id) {
	case JIT_CODE_DEBUG_INFO:
		if (jd->needs_bswap) {
			uint64_t n;
			jr->info.code_addr = bswap_64(jr->info.code_addr);
			jr->info.nr_entry  = bswap_64(jr->info.nr_entry);
			for (n = 0 ; n < jr->info.nr_entry; n++) {
				jr->info.entries[n].addr    = bswap_64(jr->info.entries[n].addr);
				jr->info.entries[n].lineno  = bswap_32(jr->info.entries[n].lineno);
				jr->info.entries[n].discrim = bswap_32(jr->info.entries[n].discrim);
			}
		}
		break;
	case JIT_CODE_UNWINDING_INFO:
		if (jd->needs_bswap) {
			jr->unwinding.unwinding_size = bswap_64(jr->unwinding.unwinding_size);
			jr->unwinding.eh_frame_hdr_size = bswap_64(jr->unwinding.eh_frame_hdr_size);
			jr->unwinding.mapped_size = bswap_64(jr->unwinding.mapped_size);
		}
		break;
	case JIT_CODE_CLOSE:
		break;
	case JIT_CODE_LOAD:
		if (jd->needs_bswap) {
			jr->load.pid       = bswap_32(jr->load.pid);
			jr->load.tid       = bswap_32(jr->load.tid);
			jr->load.vma       = bswap_64(jr->load.vma);
			jr->load.code_addr = bswap_64(jr->load.code_addr);
			jr->load.code_size = bswap_64(jr->load.code_size);
			jr->load.code_index= bswap_64(jr->load.code_index);
		}
		jd->code_load_count++;
		break;
	case JIT_CODE_MOVE:
		if (jd->needs_bswap) {
			jr->move.pid           = bswap_32(jr->move.pid);
			jr->move.tid           = bswap_32(jr->move.tid);
			jr->move.vma           = bswap_64(jr->move.vma);
			jr->move.old_code_addr = bswap_64(jr->move.old_code_addr);
			jr->move.new_code_addr = bswap_64(jr->move.new_code_addr);
			jr->move.code_size     = bswap_64(jr->move.code_size);
			jr->move.code_index    = bswap_64(jr->move.code_index);
		}
		break;
	case JIT_CODE_MAX:
	default:
		/* skip unknown record (we have read them) */
		break;
	}
	return jr;
}

static int
jit_inject_event(struct jit_buf_desc *jd, union perf_event *event)
{
	ssize_t size;

	size = perf_data__write(jd->output, event, event->header.size);
	if (size < 0)
		return -1;

	jd->bytes_written += size;
	return 0;
}

static uint64_t convert_timestamp(struct jit_buf_desc *jd, uint64_t timestamp)
{
	struct perf_tsc_conversion tc;

	if (!jd->use_arch_timestamp)
		return timestamp;

	tc.time_shift = jd->session->time_conv.time_shift;
	tc.time_mult  = jd->session->time_conv.time_mult;
	tc.time_zero  = jd->session->time_conv.time_zero;

	if (!tc.time_mult)
		return 0;

	return tsc_to_perf_time(timestamp, &tc);
}

static int jit_repipe_code_load(struct jit_buf_desc *jd, union jr_entry *jr)
{
	struct perf_sample sample;
	union perf_event *event;
	struct perf_tool *tool = jd->session->tool;
	uint64_t code, addr;
	uintptr_t uaddr;
	char *filename;
	struct stat st;
	size_t size;
	u16 idr_size;
	const char *sym;
	uint32_t count;
	int ret, csize, usize;
	pid_t pid, tid;
	struct {
		u32 pid, tid;
		u64 time;
	} *id;

	pid   = jr->load.pid;
	tid   = jr->load.tid;
	csize = jr->load.code_size;
	usize = jd->unwinding_mapped_size;
	addr  = jr->load.code_addr;
	sym   = (void *)((unsigned long)jr + sizeof(jr->load));
	code  = (unsigned long)jr + jr->load.p.total_size - csize;
	count = jr->load.code_index;
	idr_size = jd->machine->id_hdr_size;

	event = calloc(1, sizeof(*event) + idr_size);
	if (!event)
		return -1;

	filename = event->mmap2.filename;
	size = snprintf(filename, PATH_MAX, "%s/jitted-%d-%u.so",
			jd->dir,
			pid,
			count);

	size++; /* for \0 */

	size = PERF_ALIGN(size, sizeof(u64));
	uaddr = (uintptr_t)code;
	ret = jit_emit_elf(filename, sym, addr, (const void *)uaddr, csize, jd->debug_data, jd->nr_debug_entries,
			   jd->unwinding_data, jd->eh_frame_hdr_size, jd->unwinding_size);

	if (jd->debug_data && jd->nr_debug_entries) {
		free(jd->debug_data);
		jd->debug_data = NULL;
		jd->nr_debug_entries = 0;
	}

	if (jd->unwinding_data && jd->eh_frame_hdr_size) {
		free(jd->unwinding_data);
		jd->unwinding_data = NULL;
		jd->eh_frame_hdr_size = 0;
		jd->unwinding_mapped_size = 0;
		jd->unwinding_size = 0;
	}

	if (ret) {
		free(event);
		return -1;
	}
	if (stat(filename, &st))
		memset(&st, 0, sizeof(st));

	event->mmap2.header.type = PERF_RECORD_MMAP2;
	event->mmap2.header.misc = PERF_RECORD_MISC_USER;
	event->mmap2.header.size = (sizeof(event->mmap2) -
			(sizeof(event->mmap2.filename) - size) + idr_size);

	event->mmap2.pgoff = GEN_ELF_TEXT_OFFSET;
	event->mmap2.start = addr;
	event->mmap2.len   = usize ? ALIGN_8(csize) + usize : csize;
	event->mmap2.pid   = pid;
	event->mmap2.tid   = tid;
	event->mmap2.ino   = st.st_ino;
	event->mmap2.maj   = major(st.st_dev);
	event->mmap2.min   = minor(st.st_dev);
	event->mmap2.prot  = st.st_mode;
	event->mmap2.flags = MAP_SHARED;
	event->mmap2.ino_generation = 1;

	id = (void *)((unsigned long)event + event->mmap.header.size - idr_size);
	if (jd->sample_type & PERF_SAMPLE_TID) {
		id->pid  = pid;
		id->tid  = tid;
	}
	if (jd->sample_type & PERF_SAMPLE_TIME)
		id->time = convert_timestamp(jd, jr->load.p.timestamp);

	/*
	 * create pseudo sample to induce dso hit increment
	 * use first address as sample address
	 */
	memset(&sample, 0, sizeof(sample));
	sample.cpumode = PERF_RECORD_MISC_USER;
	sample.pid  = pid;
	sample.tid  = tid;
	sample.time = id->time;
	sample.ip   = addr;

	ret = perf_event__process_mmap2(tool, event, &sample, jd->machine);
	if (ret)
		return ret;

	ret = jit_inject_event(jd, event);
	/*
	 * mark dso as use to generate buildid in the header
	 */
	if (!ret)
		build_id__mark_dso_hit(tool, event, &sample, NULL, jd->machine);

	return ret;
}

static int jit_repipe_code_move(struct jit_buf_desc *jd, union jr_entry *jr)
{
	struct perf_sample sample;
	union perf_event *event;
	struct perf_tool *tool = jd->session->tool;
	char *filename;
	size_t size;
	struct stat st;
	int usize;
	u16 idr_size;
	int ret;
	pid_t pid, tid;
	struct {
		u32 pid, tid;
		u64 time;
	} *id;

	pid = jr->move.pid;
	tid =  jr->move.tid;
	usize = jd->unwinding_mapped_size;
	idr_size = jd->machine->id_hdr_size;

	/*
	 * +16 to account for sample_id_all (hack)
	 */
	event = calloc(1, sizeof(*event) + 16);
	if (!event)
		return -1;

	filename = event->mmap2.filename;
	size = snprintf(filename, PATH_MAX, "%s/jitted-%d-%"PRIu64,
	         jd->dir,
	         pid,
		 jr->move.code_index);

	size++; /* for \0 */

	if (stat(filename, &st))
		memset(&st, 0, sizeof(st));

	size = PERF_ALIGN(size, sizeof(u64));

	event->mmap2.header.type = PERF_RECORD_MMAP2;
	event->mmap2.header.misc = PERF_RECORD_MISC_USER;
	event->mmap2.header.size = (sizeof(event->mmap2) -
			(sizeof(event->mmap2.filename) - size) + idr_size);
	event->mmap2.pgoff = GEN_ELF_TEXT_OFFSET;
	event->mmap2.start = jr->move.new_code_addr;
	event->mmap2.len   = usize ? ALIGN_8(jr->move.code_size) + usize
				   : jr->move.code_size;
	event->mmap2.pid   = pid;
	event->mmap2.tid   = tid;
	event->mmap2.ino   = st.st_ino;
	event->mmap2.maj   = major(st.st_dev);
	event->mmap2.min   = minor(st.st_dev);
	event->mmap2.prot  = st.st_mode;
	event->mmap2.flags = MAP_SHARED;
	event->mmap2.ino_generation = 1;

	id = (void *)((unsigned long)event + event->mmap.header.size - idr_size);
	if (jd->sample_type & PERF_SAMPLE_TID) {
		id->pid  = pid;
		id->tid  = tid;
	}
	if (jd->sample_type & PERF_SAMPLE_TIME)
		id->time = convert_timestamp(jd, jr->load.p.timestamp);

	/*
	 * create pseudo sample to induce dso hit increment
	 * use first address as sample address
	 */
	memset(&sample, 0, sizeof(sample));
	sample.cpumode = PERF_RECORD_MISC_USER;
	sample.pid  = pid;
	sample.tid  = tid;
	sample.time = id->time;
	sample.ip   = jr->move.new_code_addr;

	ret = perf_event__process_mmap2(tool, event, &sample, jd->machine);
	if (ret)
		return ret;

	ret = jit_inject_event(jd, event);
	if (!ret)
		build_id__mark_dso_hit(tool, event, &sample, NULL, jd->machine);

	return ret;
}

static int jit_repipe_debug_info(struct jit_buf_desc *jd, union jr_entry *jr)
{
	void *data;
	size_t sz;

	if (!(jd && jr))
		return -1;

	sz  = jr->prefix.total_size - sizeof(jr->info);
	data = malloc(sz);
	if (!data)
		return -1;

	memcpy(data, &jr->info.entries, sz);

	jd->debug_data       = data;

	/*
	 * we must use nr_entry instead of size here because
	 * we cannot distinguish actual entry from padding otherwise
	 */
	jd->nr_debug_entries = jr->info.nr_entry;

	return 0;
}

static int
jit_repipe_unwinding_info(struct jit_buf_desc *jd, union jr_entry *jr)
{
	void *unwinding_data;
	uint32_t unwinding_data_size;

	if (!(jd && jr))
		return -1;

	unwinding_data_size  = jr->prefix.total_size - sizeof(jr->unwinding);
	unwinding_data = malloc(unwinding_data_size);
	if (!unwinding_data)
		return -1;

	memcpy(unwinding_data, &jr->unwinding.unwinding_data,
	       unwinding_data_size);

	jd->eh_frame_hdr_size = jr->unwinding.eh_frame_hdr_size;
	jd->unwinding_size = jr->unwinding.unwinding_size;
	jd->unwinding_mapped_size = jr->unwinding.mapped_size;
	jd->unwinding_data = unwinding_data;

	return 0;
}

static int
jit_process_dump(struct jit_buf_desc *jd)
{
	union jr_entry *jr;
	int ret = 0;

	while ((jr = jit_get_next_entry(jd))) {
		switch(jr->prefix.id) {
		case JIT_CODE_LOAD:
			ret = jit_repipe_code_load(jd, jr);
			break;
		case JIT_CODE_MOVE:
			ret = jit_repipe_code_move(jd, jr);
			break;
		case JIT_CODE_DEBUG_INFO:
			ret = jit_repipe_debug_info(jd, jr);
			break;
		case JIT_CODE_UNWINDING_INFO:
			ret = jit_repipe_unwinding_info(jd, jr);
			break;
		default:
			ret = 0;
			continue;
		}
	}
	return ret;
}

static int
jit_inject(struct jit_buf_desc *jd, char *path)
{
	int ret;

	if (verbose > 0)
		fprintf(stderr, "injecting: %s\n", path);

	ret = jit_open(jd, path);
	if (ret)
		return -1;

	ret = jit_process_dump(jd);

	jit_close(jd);

	if (verbose > 0)
		fprintf(stderr, "injected: %s (%d)\n", path, ret);

	return 0;
}

/*
 * File must be with pattern .../jit-XXXX.dump
 * where XXXX is the PID of the process which did the mmap()
 * as captured in the RECORD_MMAP record
 */
static int
jit_detect(char *mmap_name, pid_t pid)
 {
	char *p;
	char *end = NULL;
	pid_t pid2;

	if (verbose > 2)
		fprintf(stderr, "jit marker trying : %s\n", mmap_name);
	/*
	 * get file name
	 */
	p = strrchr(mmap_name, '/');
	if (!p)
		return -1;

	/*
	 * match prefix
	 */
	if (strncmp(p, "/jit-", 5))
		return -1;

	/*
	 * skip prefix
	 */
	p += 5;

	/*
	 * must be followed by a pid
	 */
	if (!isdigit(*p))
		return -1;

	pid2 = (int)strtol(p, &end, 10);
	if (!end)
		return -1;

	/*
	 * pid does not match mmap pid
	 * pid==0 in system-wide mode (synthesized)
	 */
	if (pid && pid2 != pid)
		return -1;
	/*
	 * validate suffix
	 */
	if (strcmp(end, ".dump"))
		return -1;

	if (verbose > 0)
		fprintf(stderr, "jit marker found: %s\n", mmap_name);

	return 0;
}

int
jit_process(struct perf_session *session,
	    struct perf_data *output,
	    struct machine *machine,
	    char *filename,
	    pid_t pid,
	    u64 *nbytes)
{
	struct perf_evsel *first;
	struct jit_buf_desc jd;
	int ret;

	/*
	 * first, detect marker mmap (i.e., the jitdump mmap)
	 */
	if (jit_detect(filename, pid))
		return 0;

	memset(&jd, 0, sizeof(jd));

	jd.session = session;
	jd.output  = output;
	jd.machine = machine;

	/*
	 * track sample_type to compute id_all layout
	 * perf sets the same sample type to all events as of now
	 */
	first = perf_evlist__first(session->evlist);
	jd.sample_type = first->attr.sample_type;

	*nbytes = 0;

	ret = jit_inject(&jd, filename);
	if (!ret) {
		*nbytes = jd.bytes_written;
		ret = 1;
	}

	return ret;
}
