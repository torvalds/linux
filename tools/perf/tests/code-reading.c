// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "parse-events.h"
#include "evlist.h"
#include "evsel.h"
#include "thread_map.h"
#include "cpumap.h"
#include "machine.h"
#include "map.h"
#include "symbol.h"
#include "event.h"
#include "thread.h"

#include "tests.h"

#include "sane_ctype.h"

#define BUFSZ	1024
#define READLEN	128

struct state {
	u64 done[1024];
	size_t done_cnt;
};

static unsigned int hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return c - 'A' + 10;
}

static size_t read_objdump_chunk(const char **line, unsigned char **buf,
				 size_t *buf_len)
{
	size_t bytes_read = 0;
	unsigned char *chunk_start = *buf;

	/* Read bytes */
	while (*buf_len > 0) {
		char c1, c2;

		/* Get 2 hex digits */
		c1 = *(*line)++;
		if (!isxdigit(c1))
			break;
		c2 = *(*line)++;
		if (!isxdigit(c2))
			break;

		/* Store byte and advance buf */
		**buf = (hex(c1) << 4) | hex(c2);
		(*buf)++;
		(*buf_len)--;
		bytes_read++;

		/* End of chunk? */
		if (isspace(**line))
			break;
	}

	/*
	 * objdump will display raw insn as LE if code endian
	 * is LE and bytes_per_chunk > 1. In that case reverse
	 * the chunk we just read.
	 *
	 * see disassemble_bytes() at binutils/objdump.c for details
	 * how objdump chooses display endian)
	 */
	if (bytes_read > 1 && !bigendian()) {
		unsigned char *chunk_end = chunk_start + bytes_read - 1;
		unsigned char tmp;

		while (chunk_start < chunk_end) {
			tmp = *chunk_start;
			*chunk_start = *chunk_end;
			*chunk_end = tmp;
			chunk_start++;
			chunk_end--;
		}
	}

	return bytes_read;
}

static size_t read_objdump_line(const char *line, unsigned char *buf,
				size_t buf_len)
{
	const char *p;
	size_t ret, bytes_read = 0;

	/* Skip to a colon */
	p = strchr(line, ':');
	if (!p)
		return 0;
	p++;

	/* Skip initial spaces */
	while (*p) {
		if (!isspace(*p))
			break;
		p++;
	}

	do {
		ret = read_objdump_chunk(&p, &buf, &buf_len);
		bytes_read += ret;
		p++;
	} while (ret > 0);

	/* return number of successfully read bytes */
	return bytes_read;
}

static int read_objdump_output(FILE *f, void *buf, size_t *len, u64 start_addr)
{
	char *line = NULL;
	size_t line_len, off_last = 0;
	ssize_t ret;
	int err = 0;
	u64 addr, last_addr = start_addr;

	while (off_last < *len) {
		size_t off, read_bytes, written_bytes;
		unsigned char tmp[BUFSZ];

		ret = getline(&line, &line_len, f);
		if (feof(f))
			break;
		if (ret < 0) {
			pr_debug("getline failed\n");
			err = -1;
			break;
		}

		/* read objdump data into temporary buffer */
		read_bytes = read_objdump_line(line, tmp, sizeof(tmp));
		if (!read_bytes)
			continue;

		if (sscanf(line, "%"PRIx64, &addr) != 1)
			continue;
		if (addr < last_addr) {
			pr_debug("addr going backwards, read beyond section?\n");
			break;
		}
		last_addr = addr;

		/* copy it from temporary buffer to 'buf' according
		 * to address on current objdump line */
		off = addr - start_addr;
		if (off >= *len)
			break;
		written_bytes = MIN(read_bytes, *len - off);
		memcpy(buf + off, tmp, written_bytes);
		off_last = off + written_bytes;
	}

	/* len returns number of bytes that could not be read */
	*len -= off_last;

	free(line);

	return err;
}

static int read_via_objdump(const char *filename, u64 addr, void *buf,
			    size_t len)
{
	char cmd[PATH_MAX * 2];
	const char *fmt;
	FILE *f;
	int ret;

	fmt = "%s -z -d --start-address=0x%"PRIx64" --stop-address=0x%"PRIx64" %s";
	ret = snprintf(cmd, sizeof(cmd), fmt, "objdump", addr, addr + len,
		       filename);
	if (ret <= 0 || (size_t)ret >= sizeof(cmd))
		return -1;

	pr_debug("Objdump command is: %s\n", cmd);

	/* Ignore objdump errors */
	strcat(cmd, " 2>/dev/null");

	f = popen(cmd, "r");
	if (!f) {
		pr_debug("popen failed\n");
		return -1;
	}

	ret = read_objdump_output(f, buf, &len, addr);
	if (len) {
		pr_debug("objdump read too few bytes: %zd\n", len);
		if (!ret)
			ret = len;
	}

	pclose(f);

	return ret;
}

static void dump_buf(unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		pr_debug("0x%02x ", buf[i]);
		if (i % 16 == 15)
			pr_debug("\n");
	}
	pr_debug("\n");
}

static int read_object_code(u64 addr, size_t len, u8 cpumode,
			    struct thread *thread, struct state *state)
{
	struct addr_location al;
	unsigned char buf1[BUFSZ];
	unsigned char buf2[BUFSZ];
	size_t ret_len;
	u64 objdump_addr;
	const char *objdump_name;
	char decomp_name[KMOD_DECOMP_LEN];
	bool decomp = false;
	int ret;

	pr_debug("Reading object code for memory address: %#"PRIx64"\n", addr);

	if (!thread__find_map(thread, cpumode, addr, &al) || !al.map->dso) {
		if (cpumode == PERF_RECORD_MISC_HYPERVISOR) {
			pr_debug("Hypervisor address can not be resolved - skipping\n");
			return 0;
		}

		pr_debug("thread__find_map failed\n");
		return -1;
	}

	pr_debug("File is: %s\n", al.map->dso->long_name);

	if (al.map->dso->symtab_type == DSO_BINARY_TYPE__KALLSYMS &&
	    !dso__is_kcore(al.map->dso)) {
		pr_debug("Unexpected kernel address - skipping\n");
		return 0;
	}

	pr_debug("On file address is: %#"PRIx64"\n", al.addr);

	if (len > BUFSZ)
		len = BUFSZ;

	/* Do not go off the map */
	if (addr + len > al.map->end)
		len = al.map->end - addr;

	/* Read the object code using perf */
	ret_len = dso__data_read_offset(al.map->dso, thread->mg->machine,
					al.addr, buf1, len);
	if (ret_len != len) {
		pr_debug("dso__data_read_offset failed\n");
		return -1;
	}

	/*
	 * Converting addresses for use by objdump requires more information.
	 * map__load() does that.  See map__rip_2objdump() for details.
	 */
	if (map__load(al.map))
		return -1;

	/* objdump struggles with kcore - try each map only once */
	if (dso__is_kcore(al.map->dso)) {
		size_t d;

		for (d = 0; d < state->done_cnt; d++) {
			if (state->done[d] == al.map->start) {
				pr_debug("kcore map tested already");
				pr_debug(" - skipping\n");
				return 0;
			}
		}
		if (state->done_cnt >= ARRAY_SIZE(state->done)) {
			pr_debug("Too many kcore maps - skipping\n");
			return 0;
		}
		state->done[state->done_cnt++] = al.map->start;
	}

	objdump_name = al.map->dso->long_name;
	if (dso__needs_decompress(al.map->dso)) {
		if (dso__decompress_kmodule_path(al.map->dso, objdump_name,
						 decomp_name,
						 sizeof(decomp_name)) < 0) {
			pr_debug("decompression failed\n");
			return -1;
		}

		decomp = true;
		objdump_name = decomp_name;
	}

	/* Read the object code using objdump */
	objdump_addr = map__rip_2objdump(al.map, al.addr);
	ret = read_via_objdump(objdump_name, objdump_addr, buf2, len);

	if (decomp)
		unlink(objdump_name);

	if (ret > 0) {
		/*
		 * The kernel maps are inaccurate - assume objdump is right in
		 * that case.
		 */
		if (cpumode == PERF_RECORD_MISC_KERNEL ||
		    cpumode == PERF_RECORD_MISC_GUEST_KERNEL) {
			len -= ret;
			if (len) {
				pr_debug("Reducing len to %zu\n", len);
			} else if (dso__is_kcore(al.map->dso)) {
				/*
				 * objdump cannot handle very large segments
				 * that may be found in kcore.
				 */
				pr_debug("objdump failed for kcore");
				pr_debug(" - skipping\n");
				return 0;
			} else {
				return -1;
			}
		}
	}
	if (ret < 0) {
		pr_debug("read_via_objdump failed\n");
		return -1;
	}

	/* The results should be identical */
	if (memcmp(buf1, buf2, len)) {
		pr_debug("Bytes read differ from those read by objdump\n");
		pr_debug("buf1 (dso):\n");
		dump_buf(buf1, len);
		pr_debug("buf2 (objdump):\n");
		dump_buf(buf2, len);
		return -1;
	}
	pr_debug("Bytes read match those read by objdump\n");

	return 0;
}

static int process_sample_event(struct machine *machine,
				struct perf_evlist *evlist,
				union perf_event *event, struct state *state)
{
	struct perf_sample sample;
	struct thread *thread;
	int ret;

	if (perf_evlist__parse_sample(evlist, event, &sample)) {
		pr_debug("perf_evlist__parse_sample failed\n");
		return -1;
	}

	thread = machine__findnew_thread(machine, sample.pid, sample.tid);
	if (!thread) {
		pr_debug("machine__findnew_thread failed\n");
		return -1;
	}

	ret = read_object_code(sample.ip, READLEN, sample.cpumode, thread, state);
	thread__put(thread);
	return ret;
}

static int process_event(struct machine *machine, struct perf_evlist *evlist,
			 union perf_event *event, struct state *state)
{
	if (event->header.type == PERF_RECORD_SAMPLE)
		return process_sample_event(machine, evlist, event, state);

	if (event->header.type == PERF_RECORD_THROTTLE ||
	    event->header.type == PERF_RECORD_UNTHROTTLE)
		return 0;

	if (event->header.type < PERF_RECORD_MAX) {
		int ret;

		ret = machine__process_event(machine, event, NULL);
		if (ret < 0)
			pr_debug("machine__process_event failed, event type %u\n",
				 event->header.type);
		return ret;
	}

	return 0;
}

static int process_events(struct machine *machine, struct perf_evlist *evlist,
			  struct state *state)
{
	union perf_event *event;
	struct perf_mmap *md;
	int i, ret;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		md = &evlist->mmap[i];
		if (perf_mmap__read_init(md) < 0)
			continue;

		while ((event = perf_mmap__read_event(md)) != NULL) {
			ret = process_event(machine, evlist, event, state);
			perf_mmap__consume(md);
			if (ret < 0)
				return ret;
		}
		perf_mmap__read_done(md);
	}
	return 0;
}

static int comp(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static void do_sort_something(void)
{
	int buf[40960], i;

	for (i = 0; i < (int)ARRAY_SIZE(buf); i++)
		buf[i] = ARRAY_SIZE(buf) - i - 1;

	qsort(buf, ARRAY_SIZE(buf), sizeof(int), comp);

	for (i = 0; i < (int)ARRAY_SIZE(buf); i++) {
		if (buf[i] != i) {
			pr_debug("qsort failed\n");
			break;
		}
	}
}

static void sort_something(void)
{
	int i;

	for (i = 0; i < 10; i++)
		do_sort_something();
}

static void syscall_something(void)
{
	int pipefd[2];
	int i;

	for (i = 0; i < 1000; i++) {
		if (pipe(pipefd) < 0) {
			pr_debug("pipe failed\n");
			break;
		}
		close(pipefd[1]);
		close(pipefd[0]);
	}
}

static void fs_something(void)
{
	const char *test_file_name = "temp-perf-code-reading-test-file--";
	FILE *f;
	int i;

	for (i = 0; i < 1000; i++) {
		f = fopen(test_file_name, "w+");
		if (f) {
			fclose(f);
			unlink(test_file_name);
		}
	}
}

static const char *do_determine_event(bool excl_kernel)
{
	const char *event = excl_kernel ? "cycles:u" : "cycles";

#ifdef __s390x__
	char cpuid[128], model[16], model_c[16], cpum_cf_v[16];
	unsigned int family;
	int ret, cpum_cf_a;

	if (get_cpuid(cpuid, sizeof(cpuid)))
		goto out_clocks;
	ret = sscanf(cpuid, "%*[^,],%u,%[^,],%[^,],%[^,],%x", &family, model_c,
		     model, cpum_cf_v, &cpum_cf_a);
	if (ret != 5)		 /* Not available */
		goto out_clocks;
	if (excl_kernel && (cpum_cf_a & 4))
		return event;
	if (!excl_kernel && (cpum_cf_a & 2))
		return event;

	/* Fall through: missing authorization */
out_clocks:
	event = excl_kernel ? "cpu-clock:u" : "cpu-clock";

#endif
	return event;
}

static void do_something(void)
{
	fs_something();

	sort_something();

	syscall_something();
}

enum {
	TEST_CODE_READING_OK,
	TEST_CODE_READING_NO_VMLINUX,
	TEST_CODE_READING_NO_KCORE,
	TEST_CODE_READING_NO_ACCESS,
	TEST_CODE_READING_NO_KERNEL_OBJ,
};

static int do_test_code_reading(bool try_kcore)
{
	struct machine *machine;
	struct thread *thread;
	struct record_opts opts = {
		.mmap_pages	     = UINT_MAX,
		.user_freq	     = UINT_MAX,
		.user_interval	     = ULLONG_MAX,
		.freq		     = 500,
		.target		     = {
			.uses_mmap   = true,
		},
	};
	struct state state = {
		.done_cnt = 0,
	};
	struct thread_map *threads = NULL;
	struct cpu_map *cpus = NULL;
	struct perf_evlist *evlist = NULL;
	struct perf_evsel *evsel = NULL;
	int err = -1, ret;
	pid_t pid;
	struct map *map;
	bool have_vmlinux, have_kcore, excl_kernel = false;

	pid = getpid();

	machine = machine__new_host();
	machine->env = &perf_env;

	ret = machine__create_kernel_maps(machine);
	if (ret < 0) {
		pr_debug("machine__create_kernel_maps failed\n");
		goto out_err;
	}

	/* Force the use of kallsyms instead of vmlinux to try kcore */
	if (try_kcore)
		symbol_conf.kallsyms_name = "/proc/kallsyms";

	/* Load kernel map */
	map = machine__kernel_map(machine);
	ret = map__load(map);
	if (ret < 0) {
		pr_debug("map__load failed\n");
		goto out_err;
	}
	have_vmlinux = dso__is_vmlinux(map->dso);
	have_kcore = dso__is_kcore(map->dso);

	/* 2nd time through we just try kcore */
	if (try_kcore && !have_kcore)
		return TEST_CODE_READING_NO_KCORE;

	/* No point getting kernel events if there is no kernel object */
	if (!have_vmlinux && !have_kcore)
		excl_kernel = true;

	threads = thread_map__new_by_tid(pid);
	if (!threads) {
		pr_debug("thread_map__new_by_tid failed\n");
		goto out_err;
	}

	ret = perf_event__synthesize_thread_map(NULL, threads,
						perf_event__process, machine, false);
	if (ret < 0) {
		pr_debug("perf_event__synthesize_thread_map failed\n");
		goto out_err;
	}

	thread = machine__findnew_thread(machine, pid, pid);
	if (!thread) {
		pr_debug("machine__findnew_thread failed\n");
		goto out_put;
	}

	cpus = cpu_map__new(NULL);
	if (!cpus) {
		pr_debug("cpu_map__new failed\n");
		goto out_put;
	}

	while (1) {
		const char *str;

		evlist = perf_evlist__new();
		if (!evlist) {
			pr_debug("perf_evlist__new failed\n");
			goto out_put;
		}

		perf_evlist__set_maps(evlist, cpus, threads);

		str = do_determine_event(excl_kernel);
		pr_debug("Parsing event '%s'\n", str);
		ret = parse_events(evlist, str, NULL);
		if (ret < 0) {
			pr_debug("parse_events failed\n");
			goto out_put;
		}

		perf_evlist__config(evlist, &opts, NULL);

		evsel = perf_evlist__first(evlist);

		evsel->attr.comm = 1;
		evsel->attr.disabled = 1;
		evsel->attr.enable_on_exec = 0;

		ret = perf_evlist__open(evlist);
		if (ret < 0) {
			if (!excl_kernel) {
				excl_kernel = true;
				/*
				 * Both cpus and threads are now owned by evlist
				 * and will be freed by following perf_evlist__set_maps
				 * call. Getting refference to keep them alive.
				 */
				cpu_map__get(cpus);
				thread_map__get(threads);
				perf_evlist__set_maps(evlist, NULL, NULL);
				perf_evlist__delete(evlist);
				evlist = NULL;
				continue;
			}

			if (verbose > 0) {
				char errbuf[512];
				perf_evlist__strerror_open(evlist, errno, errbuf, sizeof(errbuf));
				pr_debug("perf_evlist__open() failed!\n%s\n", errbuf);
			}

			goto out_put;
		}
		break;
	}

	ret = perf_evlist__mmap(evlist, UINT_MAX);
	if (ret < 0) {
		pr_debug("perf_evlist__mmap failed\n");
		goto out_put;
	}

	perf_evlist__enable(evlist);

	do_something();

	perf_evlist__disable(evlist);

	ret = process_events(machine, evlist, &state);
	if (ret < 0)
		goto out_put;

	if (!have_vmlinux && !have_kcore && !try_kcore)
		err = TEST_CODE_READING_NO_KERNEL_OBJ;
	else if (!have_vmlinux && !try_kcore)
		err = TEST_CODE_READING_NO_VMLINUX;
	else if (excl_kernel)
		err = TEST_CODE_READING_NO_ACCESS;
	else
		err = TEST_CODE_READING_OK;
out_put:
	thread__put(thread);
out_err:

	if (evlist) {
		perf_evlist__delete(evlist);
	} else {
		cpu_map__put(cpus);
		thread_map__put(threads);
	}
	machine__delete_threads(machine);
	machine__delete(machine);

	return err;
}

int test__code_reading(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	int ret;

	ret = do_test_code_reading(false);
	if (!ret)
		ret = do_test_code_reading(true);

	switch (ret) {
	case TEST_CODE_READING_OK:
		return 0;
	case TEST_CODE_READING_NO_VMLINUX:
		pr_debug("no vmlinux\n");
		return 0;
	case TEST_CODE_READING_NO_KCORE:
		pr_debug("no kcore\n");
		return 0;
	case TEST_CODE_READING_NO_ACCESS:
		pr_debug("no access\n");
		return 0;
	case TEST_CODE_READING_NO_KERNEL_OBJ:
		pr_debug("no kernel obj\n");
		return 0;
	default:
		return -1;
	};
}
