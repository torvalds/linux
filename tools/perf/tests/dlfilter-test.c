// SPDX-License-Identifier: GPL-2.0
/*
 * Test dlfilter C API. A perf.data file is synthesized and then processed
 * by perf script with a dlfilter named dlfilter-test-api-v0.so. Also a C file
 * is compiled to provide a dso to match the synthesized perf.data file.
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/perf_event.h>
#include <internal/lib.h>
#include <subcmd/exec-cmd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "tool.h"
#include "event.h"
#include "header.h"
#include "machine.h"
#include "dso.h"
#include "map.h"
#include "symbol.h"
#include "synthetic-events.h"
#include "util.h"
#include "archinsn.h"
#include "dlfilter.h"
#include "tests.h"

#define MAP_START 0x400000

struct test_data {
	struct perf_tool tool;
	struct machine *machine;
	int fd;
	u64 foo;
	u64 bar;
	u64 ip;
	u64 addr;
	char perf[PATH_MAX];
	char perf_data_file_name[PATH_MAX];
	char c_file_name[PATH_MAX];
	char prog_file_name[PATH_MAX];
	char dlfilters[PATH_MAX];
};

static int test_result(const char *msg, int ret)
{
	pr_debug("%s\n", msg);
	return ret;
}

static int process(struct perf_tool *tool, union perf_event *event,
		   struct perf_sample *sample __maybe_unused,
		   struct machine *machine __maybe_unused)
{
	struct test_data *td = container_of(tool, struct test_data, tool);
	int fd = td->fd;

	if (writen(fd, event, event->header.size) != event->header.size)
		return -1;

	return 0;
}

#define MAXCMD 4096
#define REDIRECT_TO_DEV_NULL " >/dev/null 2>&1"

static __printf(1, 2) int system_cmd(const char *fmt, ...)
{
	char cmd[MAXCMD + sizeof(REDIRECT_TO_DEV_NULL)];
	int ret;

	va_list args;

	va_start(args, fmt);
	ret = vsnprintf(cmd, MAXCMD, fmt, args);
	va_end(args);

	if (ret <= 0 || ret >= MAXCMD)
		return -1;

	if (!verbose)
		strcat(cmd, REDIRECT_TO_DEV_NULL);

	pr_debug("Command: %s\n", cmd);
	ret = system(cmd);
	if (ret)
		pr_debug("Failed with return value %d\n", ret);

	return ret;
}

static bool have_gcc(void)
{
	pr_debug("Checking for gcc\n");
	return !system_cmd("gcc --version");
}

static int write_attr(struct test_data *td, u64 sample_type, u64 *id)
{
	struct perf_event_attr attr = {
		.size = sizeof(attr),
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
		.sample_type = sample_type,
		.sample_period = 1,
	};

	return perf_event__synthesize_attr(&td->tool, &attr, 1, id, process);
}

static int write_comm(int fd, pid_t pid, pid_t tid, const char *comm_str)
{
	struct perf_record_comm comm;
	ssize_t sz = sizeof(comm);

	comm.header.type = PERF_RECORD_COMM;
	comm.header.misc = PERF_RECORD_MISC_USER;
	comm.header.size = sz;

	comm.pid = pid;
	comm.tid = tid;
	strncpy(comm.comm, comm_str, 16);

	if (writen(fd, &comm, sz) != sz) {
		pr_debug("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int write_mmap(int fd, pid_t pid, pid_t tid, u64 start, u64 len, u64 pgoff,
		      const char *filename)
{
	char buf[PERF_SAMPLE_MAX_SIZE];
	struct perf_record_mmap *mmap = (struct perf_record_mmap *)buf;
	size_t fsz = roundup(strlen(filename) + 1, 8);
	ssize_t sz = sizeof(*mmap) - sizeof(mmap->filename) + fsz;

	mmap->header.type = PERF_RECORD_MMAP;
	mmap->header.misc = PERF_RECORD_MISC_USER;
	mmap->header.size = sz;

	mmap->pid   = pid;
	mmap->tid   = tid;
	mmap->start = start;
	mmap->len   = len;
	mmap->pgoff = pgoff;
	strncpy(mmap->filename, filename, sizeof(mmap->filename));

	if (writen(fd, mmap, sz) != sz) {
		pr_debug("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int write_sample(struct test_data *td, u64 sample_type, u64 id, pid_t pid, pid_t tid)
{
	char buf[PERF_SAMPLE_MAX_SIZE];
	union perf_event *event = (union perf_event *)buf;
	struct perf_sample sample = {
		.ip		= td->ip,
		.addr		= td->addr,
		.id		= id,
		.time		= 1234567890,
		.cpu		= 31,
		.pid		= pid,
		.tid		= tid,
		.period		= 543212345,
		.stream_id	= 101,
	};
	int err;

	event->header.type = PERF_RECORD_SAMPLE;
	event->header.misc = PERF_RECORD_MISC_USER;
	event->header.size = perf_event__sample_event_size(&sample, sample_type, 0);
	err = perf_event__synthesize_sample(event, sample_type, 0, &sample);
	if (err)
		return test_result("perf_event__synthesize_sample() failed", TEST_FAIL);

	err = process(&td->tool, event, &sample, td->machine);
	if (err)
		return test_result("Failed to write sample", TEST_FAIL);

	return TEST_OK;
}

static void close_fd(int fd)
{
	if (fd >= 0)
		close(fd);
}

static const char *prog = "int bar(){};int foo(){bar();};int main(){foo();return 0;}";

static int write_prog(char *file_name)
{
	int fd = creat(file_name, 0644);
	ssize_t n = strlen(prog);
	bool err = fd < 0 || writen(fd, prog, n) != n;

	close_fd(fd);
	return err ? -1 : 0;
}

static int get_dlfilters_path(char *buf, size_t sz)
{
	char perf[PATH_MAX];
	char path[PATH_MAX];
	char *perf_path;
	char *exec_path;

	perf_exe(perf, sizeof(perf));
	perf_path = dirname(perf);
	snprintf(path, sizeof(path), "%s/dlfilters/dlfilter-test-api-v0.so", perf_path);
	if (access(path, R_OK)) {
		exec_path = get_argv_exec_path();
		if (!exec_path)
			return -1;
		snprintf(path, sizeof(path), "%s/dlfilters/dlfilter-test-api-v0.so", exec_path);
		free(exec_path);
		if (access(path, R_OK))
			return -1;
	}
	strlcpy(buf, dirname(path), sz);
	return 0;
}

static int check_filter_desc(struct test_data *td)
{
	char *long_desc = NULL;
	char *desc = NULL;
	int ret;

	if (get_filter_desc(td->dlfilters, "dlfilter-test-api-v0.so", &desc, &long_desc) &&
	    long_desc && !strcmp(long_desc, "Filter used by the 'dlfilter C API' perf test") &&
	    desc && !strcmp(desc, "dlfilter to test v0 C API"))
		ret = 0;
	else
		ret = -1;

	free(desc);
	free(long_desc);
	return ret;
}

static int get_ip_addr(struct test_data *td)
{
	struct map *map;
	struct symbol *sym;

	map = dso__new_map(td->prog_file_name);
	if (!map)
		return -1;

	sym = map__find_symbol_by_name(map, "foo");
	if (sym)
		td->foo = sym->start;

	sym = map__find_symbol_by_name(map, "bar");
	if (sym)
		td->bar = sym->start;

	map__put(map);

	td->ip = MAP_START + td->foo;
	td->addr = MAP_START + td->bar;

	return td->foo && td->bar ? 0 : -1;
}

static int do_run_perf_script(struct test_data *td, int do_early)
{
	return system_cmd("%s script -i %s "
			  "--dlfilter %s/dlfilter-test-api-v0.so "
			  "--dlarg first "
			  "--dlarg %d "
			  "--dlarg %" PRIu64 " "
			  "--dlarg %" PRIu64 " "
			  "--dlarg %d "
			  "--dlarg last",
			  td->perf, td->perf_data_file_name, td->dlfilters,
			  verbose, td->ip, td->addr, do_early);
}

static int run_perf_script(struct test_data *td)
{
	int do_early;
	int err;

	for (do_early = 0; do_early < 3; do_early++) {
		err = do_run_perf_script(td, do_early);
		if (err)
			return err;
	}
	return 0;
}

#define TEST_SAMPLE_TYPE (PERF_SAMPLE_IP | PERF_SAMPLE_TID | \
			  PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_TIME | \
			  PERF_SAMPLE_ADDR | PERF_SAMPLE_CPU | \
			  PERF_SAMPLE_PERIOD | PERF_SAMPLE_STREAM_ID)

static int test__dlfilter_test(struct test_data *td)
{
	u64 sample_type = TEST_SAMPLE_TYPE;
	pid_t pid = 12345;
	pid_t tid = 12346;
	u64 id = 99;
	int err;

	if (get_dlfilters_path(td->dlfilters, PATH_MAX))
		return test_result("dlfilters not found", TEST_SKIP);

	if (check_filter_desc(td))
		return test_result("Failed to get expected filter description", TEST_FAIL);

	if (!have_gcc())
		return test_result("gcc not found", TEST_SKIP);

	pr_debug("dlfilters path: %s\n", td->dlfilters);

	if (write_prog(td->c_file_name))
		return test_result("Failed to write test C file", TEST_FAIL);

	if (verbose > 1)
		system_cmd("cat %s ; echo", td->c_file_name);

	if (system_cmd("gcc -g -o %s %s", td->prog_file_name, td->c_file_name))
		return TEST_FAIL;

	if (verbose > 2)
		system_cmd("objdump -x -dS %s", td->prog_file_name);

	if (get_ip_addr(td))
		return test_result("Failed to find program symbols", TEST_FAIL);

	pr_debug("Creating new host machine structure\n");
	td->machine = machine__new_host();
	td->machine->env = &perf_env;

	td->fd = creat(td->perf_data_file_name, 0644);
	if (td->fd < 0)
		return test_result("Failed to create test perf.data file", TEST_FAIL);

	err = perf_header__write_pipe(td->fd);
	if (err < 0)
		return test_result("perf_header__write_pipe() failed", TEST_FAIL);

	err = write_attr(td, sample_type, &id);
	if (err)
		return test_result("perf_event__synthesize_attr() failed", TEST_FAIL);

	if (write_comm(td->fd, pid, tid, "test-prog"))
		return TEST_FAIL;

	if (write_mmap(td->fd, pid, tid, MAP_START, 0x10000, 0, td->prog_file_name))
		return TEST_FAIL;

	if (write_sample(td, sample_type, id, pid, tid) != TEST_OK)
		return TEST_FAIL;

	if (verbose > 1)
		system_cmd("%s script -i %s -D", td->perf, td->perf_data_file_name);

	err = run_perf_script(td);
	if (err)
		return TEST_FAIL;

	return TEST_OK;
}

static void unlink_path(const char *path)
{
	if (*path)
		unlink(path);
}

static void test_data__free(struct test_data *td)
{
	machine__delete(td->machine);
	close_fd(td->fd);
	if (verbose <= 2) {
		unlink_path(td->c_file_name);
		unlink_path(td->prog_file_name);
		unlink_path(td->perf_data_file_name);
	}
}

static int test__dlfilter(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct test_data td = {.fd = -1};
	int pid = getpid();
	int err;

	perf_exe(td.perf, sizeof(td.perf));

	snprintf(td.perf_data_file_name, PATH_MAX, "/tmp/dlfilter-test-%u-perf-data", pid);
	snprintf(td.c_file_name, PATH_MAX, "/tmp/dlfilter-test-%u-prog.c", pid);
	snprintf(td.prog_file_name, PATH_MAX, "/tmp/dlfilter-test-%u-prog", pid);

	err = test__dlfilter_test(&td);
	test_data__free(&td);
	return err;
}

DEFINE_SUITE("dlfilter C API", dlfilter);
