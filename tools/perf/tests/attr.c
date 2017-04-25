/*
 * The struct perf_event_attr test support.
 *
 * This test is embedded inside into perf directly and is governed
 * by the PERF_TEST_ATTR environment variable and hook inside
 * sys_perf_event_open function.
 *
 * The general idea is to store 'struct perf_event_attr' details for
 * each event created within single perf command. Each event details
 * are stored into separate text file. Once perf command is finished
 * these files can be checked for values we expect for command.
 *
 * Besides 'struct perf_event_attr' values we also store 'fd' and
 * 'group_fd' values to allow checking for groups created.
 *
 * This all is triggered by setting PERF_TEST_ATTR environment variable.
 * It must contain name of existing directory with access and write
 * permissions. All the event text files are stored there.
 */

#include <stdlib.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include "../perf.h"
#include "util.h"
#include <subcmd/exec-cmd.h>
#include "tests.h"

#define ENV "PERF_TEST_ATTR"

extern int verbose;

static char *dir;

void test_attr__init(void)
{
	dir = getenv(ENV);
	test_attr__enabled = (dir != NULL);
}

#define BUFSIZE 1024

#define __WRITE_ASS(str, fmt, data)					\
do {									\
	char buf[BUFSIZE];						\
	size_t size;							\
									\
	size = snprintf(buf, BUFSIZE, #str "=%"fmt "\n", data);		\
	if (1 != fwrite(buf, size, 1, file)) {				\
		perror("test attr - failed to write event file");	\
		fclose(file);						\
		return -1;						\
	}								\
									\
} while (0)

#define WRITE_ASS(field, fmt) __WRITE_ASS(field, fmt, attr->field)

static int store_event(struct perf_event_attr *attr, pid_t pid, int cpu,
		       int fd, int group_fd, unsigned long flags)
{
	FILE *file;
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "%s/event-%d-%llu-%d", dir,
		 attr->type, attr->config, fd);

	file = fopen(path, "w+");
	if (!file) {
		perror("test attr - failed to open event file");
		return -1;
	}

	if (fprintf(file, "[event-%d-%llu-%d]\n",
		    attr->type, attr->config, fd) < 0) {
		perror("test attr - failed to write event file");
		fclose(file);
		return -1;
	}

	/* syscall arguments */
	__WRITE_ASS(fd,       "d", fd);
	__WRITE_ASS(group_fd, "d", group_fd);
	__WRITE_ASS(cpu,      "d", cpu);
	__WRITE_ASS(pid,      "d", pid);
	__WRITE_ASS(flags,   "lu", flags);

	/* struct perf_event_attr */
	WRITE_ASS(type,   PRIu32);
	WRITE_ASS(size,   PRIu32);
	WRITE_ASS(config,  "llu");
	WRITE_ASS(sample_period, "llu");
	WRITE_ASS(sample_type,   "llu");
	WRITE_ASS(read_format,   "llu");
	WRITE_ASS(disabled,       "d");
	WRITE_ASS(inherit,        "d");
	WRITE_ASS(pinned,         "d");
	WRITE_ASS(exclusive,      "d");
	WRITE_ASS(exclude_user,   "d");
	WRITE_ASS(exclude_kernel, "d");
	WRITE_ASS(exclude_hv,     "d");
	WRITE_ASS(exclude_idle,   "d");
	WRITE_ASS(mmap,           "d");
	WRITE_ASS(comm,           "d");
	WRITE_ASS(freq,           "d");
	WRITE_ASS(inherit_stat,   "d");
	WRITE_ASS(enable_on_exec, "d");
	WRITE_ASS(task,           "d");
	WRITE_ASS(watermark,      "d");
	WRITE_ASS(precise_ip,     "d");
	WRITE_ASS(mmap_data,      "d");
	WRITE_ASS(sample_id_all,  "d");
	WRITE_ASS(exclude_host,   "d");
	WRITE_ASS(exclude_guest,  "d");
	WRITE_ASS(exclude_callchain_kernel, "d");
	WRITE_ASS(exclude_callchain_user, "d");
	WRITE_ASS(wakeup_events, PRIu32);
	WRITE_ASS(bp_type, PRIu32);
	WRITE_ASS(config1, "llu");
	WRITE_ASS(config2, "llu");
	WRITE_ASS(branch_sample_type, "llu");
	WRITE_ASS(sample_regs_user,   "llu");
	WRITE_ASS(sample_stack_user,  PRIu32);

	fclose(file);
	return 0;
}

void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu,
		     int fd, int group_fd, unsigned long flags)
{
	int errno_saved = errno;

	if (store_event(attr, pid, cpu, fd, group_fd, flags))
		die("test attr FAILED");

	errno = errno_saved;
}

static int run_dir(const char *d, const char *perf)
{
	char v[] = "-vvvvv";
	int vcnt = min(verbose, (int) sizeof(v) - 1);
	char cmd[3*PATH_MAX];

	if (verbose > 0)
		vcnt++;

	snprintf(cmd, 3*PATH_MAX, PYTHON " %s/attr.py -d %s/attr/ -p %s %.*s",
		 d, d, perf, vcnt, v);

	return system(cmd);
}

int test__attr(int subtest __maybe_unused)
{
	struct stat st;
	char path_perf[PATH_MAX];
	char path_dir[PATH_MAX];

	/* First try developement tree tests. */
	if (!lstat("./tests", &st))
		return run_dir("./tests", "./perf");

	/* Then installed path. */
	snprintf(path_dir,  PATH_MAX, "%s/tests", get_argv_exec_path());
	snprintf(path_perf, PATH_MAX, "%s/perf", BINDIR);

	if (!lstat(path_dir, &st) &&
	    !lstat(path_perf, &st))
		return run_dir(path_dir, path_perf);

	return TEST_SKIP;
}
