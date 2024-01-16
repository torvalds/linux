/*
 * jvmti_agent.c: JVMTI agent interface
 *
 * Adapted from the Oprofile code in opagent.c:
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright 2007 OProfile authors
 * Jens Wilke
 * Daniel Hansel
 * Copyright IBM Corporation 2007
 */
#include <sys/types.h>
#include <sys/stat.h> /* for mkdir() */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <syscall.h> /* for gettid() */
#include <err.h>
#include <linux/kernel.h>

#include "jvmti_agent.h"
#include "../util/jitdump.h"

#define JIT_LANG "java"

static char jit_path[PATH_MAX];
static void *marker_addr;

#ifndef HAVE_GETTID
static inline pid_t gettid(void)
{
	return (pid_t)syscall(__NR_gettid);
}
#endif

static int get_e_machine(struct jitheader *hdr)
{
	ssize_t sret;
	char id[16];
	int fd, ret = -1;
	struct {
		uint16_t e_type;
		uint16_t e_machine;
	} info;

	fd = open("/proc/self/exe", O_RDONLY);
	if (fd == -1)
		return -1;

	sret = read(fd, id, sizeof(id));
	if (sret != sizeof(id))
		goto error;

	/* check ELF signature */
	if (id[0] != 0x7f || id[1] != 'E' || id[2] != 'L' || id[3] != 'F')
		goto error;

	sret = read(fd, &info, sizeof(info));
	if (sret != sizeof(info))
		goto error;

	hdr->elf_mach = info.e_machine;
	ret = 0;
error:
	close(fd);
	return ret;
}

static int use_arch_timestamp;

static inline uint64_t
get_arch_timestamp(void)
{
#if defined(__i386__) || defined(__x86_64__)
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((uint64_t)high) << 32;
#else
	return 0;
#endif
}

#define NSEC_PER_SEC	1000000000
static int perf_clk_id = CLOCK_MONOTONIC;

static inline uint64_t
timespec_to_ns(const struct timespec *ts)
{
        return ((uint64_t) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

static inline uint64_t
perf_get_timestamp(void)
{
	struct timespec ts;
	int ret;

	if (use_arch_timestamp)
		return get_arch_timestamp();

	ret = clock_gettime(perf_clk_id, &ts);
	if (ret)
		return 0;

	return timespec_to_ns(&ts);
}

static int
create_jit_cache_dir(void)
{
	char str[32];
	char *base, *p;
	struct tm tm;
	time_t t;
	int ret;

	time(&t);
	localtime_r(&t, &tm);

	base = getenv("JITDUMPDIR");
	if (!base)
		base = getenv("HOME");
	if (!base)
		base = ".";

	strftime(str, sizeof(str), JIT_LANG"-jit-%Y%m%d", &tm);

	ret = snprintf(jit_path, PATH_MAX, "%s/.debug/", base);
	if (ret >= PATH_MAX) {
		warnx("jvmti: cannot generate jit cache dir because %s/.debug/"
			" is too long, please check the cwd, JITDUMPDIR, and"
			" HOME variables", base);
		return -1;
	}
	ret = mkdir(jit_path, 0755);
	if (ret == -1) {
		if (errno != EEXIST) {
			warn("jvmti: cannot create jit cache dir %s", jit_path);
			return -1;
		}
	}

	ret = snprintf(jit_path, PATH_MAX, "%s/.debug/jit", base);
	if (ret >= PATH_MAX) {
		warnx("jvmti: cannot generate jit cache dir because"
			" %s/.debug/jit is too long, please check the cwd,"
			" JITDUMPDIR, and HOME variables", base);
		return -1;
	}
	ret = mkdir(jit_path, 0755);
	if (ret == -1) {
		if (errno != EEXIST) {
			warn("jvmti: cannot create jit cache dir %s", jit_path);
			return -1;
		}
	}

	ret = snprintf(jit_path, PATH_MAX, "%s/.debug/jit/%s.XXXXXXXX", base, str);
	if (ret >= PATH_MAX) {
		warnx("jvmti: cannot generate jit cache dir because"
			" %s/.debug/jit/%s.XXXXXXXX is too long, please check"
			" the cwd, JITDUMPDIR, and HOME variables",
			base, str);
		return -1;
	}
	p = mkdtemp(jit_path);
	if (p != jit_path) {
		warn("jvmti: cannot create jit cache dir %s", jit_path);
		return -1;
	}

	return 0;
}

static int
perf_open_marker_file(int fd)
{
	long pgsz;

	pgsz = sysconf(_SC_PAGESIZE);
	if (pgsz == -1)
		return -1;

	/*
	 * we mmap the jitdump to create an MMAP RECORD in perf.data file.
	 * The mmap is captured either live (perf record running when we mmap)
	 * or  in deferred mode, via /proc/PID/maps
	 * the MMAP record is used as a marker of a jitdump file for more meta
	 * data info about the jitted code. Perf report/annotate detect this
	 * special filename and process the jitdump file.
	 *
	 * mapping must be PROT_EXEC to ensure it is captured by perf record
	 * even when not using -d option
	 */
	marker_addr = mmap(NULL, pgsz, PROT_READ|PROT_EXEC, MAP_PRIVATE, fd, 0);
	return (marker_addr == MAP_FAILED) ? -1 : 0;
}

static void
perf_close_marker_file(void)
{
	long pgsz;

	if (!marker_addr)
		return;

	pgsz = sysconf(_SC_PAGESIZE);
	if (pgsz == -1)
		return;

	munmap(marker_addr, pgsz);
}

static void
init_arch_timestamp(void)
{
	char *str = getenv("JITDUMP_USE_ARCH_TIMESTAMP");

	if (!str || !*str || !strcmp(str, "0"))
		return;

	use_arch_timestamp = 1;
}

void *jvmti_open(void)
{
	char dump_path[PATH_MAX];
	struct jitheader header;
	int fd, ret;
	FILE *fp;

	init_arch_timestamp();

	/*
	 * check if clockid is supported
	 */
	if (!perf_get_timestamp()) {
		if (use_arch_timestamp)
			warnx("jvmti: arch timestamp not supported");
		else
			warnx("jvmti: kernel does not support %d clock id", perf_clk_id);
	}

	memset(&header, 0, sizeof(header));

	/*
	 * jitdump file dir
	 */
	if (create_jit_cache_dir() < 0)
		return NULL;

	/*
	 * jitdump file name
	 */
	ret = snprintf(dump_path, PATH_MAX, "%s/jit-%i.dump", jit_path, getpid());
	if (ret >= PATH_MAX) {
		warnx("jvmti: cannot generate jitdump file full path because"
			" %s/jit-%i.dump is too long, please check the cwd,"
			" JITDUMPDIR, and HOME variables", jit_path, getpid());
		return NULL;
	}

	fd = open(dump_path, O_CREAT|O_TRUNC|O_RDWR, 0666);
	if (fd == -1)
		return NULL;

	/*
	 * create perf.data maker for the jitdump file
	 */
	if (perf_open_marker_file(fd)) {
		warnx("jvmti: failed to create marker file");
		return NULL;
	}

	fp = fdopen(fd, "w+");
	if (!fp) {
		warn("jvmti: cannot create %s", dump_path);
		close(fd);
		goto error;
	}

	warnx("jvmti: jitdump in %s", dump_path);

	if (get_e_machine(&header)) {
		warn("get_e_machine failed\n");
		goto error;
	}

	header.magic      = JITHEADER_MAGIC;
	header.version    = JITHEADER_VERSION;
	header.total_size = sizeof(header);
	header.pid        = getpid();

	header.timestamp = perf_get_timestamp();

	if (use_arch_timestamp)
		header.flags |= JITDUMP_FLAGS_ARCH_TIMESTAMP;

	if (!fwrite(&header, sizeof(header), 1, fp)) {
		warn("jvmti: cannot write dumpfile header");
		goto error;
	}
	return fp;
error:
	fclose(fp);
	return NULL;
}

int
jvmti_close(void *agent)
{
	struct jr_code_close rec;
	FILE *fp = agent;

	if (!fp) {
		warnx("jvmti: invalid fd in close_agent");
		return -1;
	}

	rec.p.id = JIT_CODE_CLOSE;
	rec.p.total_size = sizeof(rec);

	rec.p.timestamp = perf_get_timestamp();

	if (!fwrite(&rec, sizeof(rec), 1, fp))
		return -1;

	fclose(fp);

	fp = NULL;

	perf_close_marker_file();

	return 0;
}

int
jvmti_write_code(void *agent, char const *sym,
	uint64_t vma, void const *code, unsigned int const size)
{
	static int code_generation = 1;
	struct jr_code_load rec;
	size_t sym_len;
	FILE *fp = agent;
	int ret = -1;

	/* don't care about 0 length function, no samples */
	if (size == 0)
		return 0;

	if (!fp) {
		warnx("jvmti: invalid fd in write_native_code");
		return -1;
	}

	sym_len = strlen(sym) + 1;

	rec.p.id           = JIT_CODE_LOAD;
	rec.p.total_size   = sizeof(rec) + sym_len;
	rec.p.timestamp    = perf_get_timestamp();

	rec.code_size  = size;
	rec.vma        = vma;
	rec.code_addr  = vma;
	rec.pid	       = getpid();
	rec.tid	       = gettid();

	if (code)
		rec.p.total_size += size;

	/*
	 * If JVM is multi-threaded, multiple concurrent calls to agent
	 * may be possible, so protect file writes
	 */
	flockfile(fp);

	/*
	 * get code index inside lock to avoid race condition
	 */
	rec.code_index = code_generation++;

	ret = fwrite_unlocked(&rec, sizeof(rec), 1, fp);
	fwrite_unlocked(sym, sym_len, 1, fp);

	if (code)
		fwrite_unlocked(code, size, 1, fp);

	funlockfile(fp);

	ret = 0;

	return ret;
}

int
jvmti_write_debug_info(void *agent, uint64_t code,
    int nr_lines, jvmti_line_info_t *li,
    const char * const * file_names)
{
	struct jr_code_debug_info rec;
	size_t sret, len, size, flen = 0;
	uint64_t addr;
	FILE *fp = agent;
	int i;

	/*
	 * no entry to write
	 */
	if (!nr_lines)
		return 0;

	if (!fp) {
		warnx("jvmti: invalid fd in write_debug_info");
		return -1;
	}

	for (i = 0; i < nr_lines; ++i) {
	    flen += strlen(file_names[i]) + 1;
	}

	rec.p.id        = JIT_CODE_DEBUG_INFO;
	size            = sizeof(rec);
	rec.p.timestamp = perf_get_timestamp();
	rec.code_addr   = (uint64_t)(uintptr_t)code;
	rec.nr_entry    = nr_lines;

	/*
	 * on disk source line info layout:
	 * uint64_t : addr
	 * int      : line number
	 * int      : column discriminator
	 * file[]   : source file name
	 */
	size += nr_lines * sizeof(struct debug_entry);
	size += flen;
	rec.p.total_size = size;

	/*
	 * If JVM is multi-threaded, multiple concurrent calls to agent
	 * may be possible, so protect file writes
	 */
	flockfile(fp);

	sret = fwrite_unlocked(&rec, sizeof(rec), 1, fp);
	if (sret != 1)
		goto error;

	for (i = 0; i < nr_lines; i++) {

		addr = (uint64_t)li[i].pc;
		len  = sizeof(addr);
		sret = fwrite_unlocked(&addr, len, 1, fp);
		if (sret != 1)
			goto error;

		len  = sizeof(li[0].line_number);
		sret = fwrite_unlocked(&li[i].line_number, len, 1, fp);
		if (sret != 1)
			goto error;

		len  = sizeof(li[0].discrim);
		sret = fwrite_unlocked(&li[i].discrim, len, 1, fp);
		if (sret != 1)
			goto error;

		sret = fwrite_unlocked(file_names[i], strlen(file_names[i]) + 1, 1, fp);
		if (sret != 1)
			goto error;
	}
	funlockfile(fp);
	return 0;
error:
	funlockfile(fp);
	return -1;
}
