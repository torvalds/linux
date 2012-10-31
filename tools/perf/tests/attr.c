
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
#include <inttypes.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include "../perf.h"
#include "util.h"

#define ENV "PERF_TEST_ATTR"

bool test_attr__enabled;

static char *dir;

void test_attr__init(void)
{
	dir = getenv(ENV);
	test_attr__enabled = (dir != NULL);
}

#define BUFSIZE 1024

#define WRITE_ASS(str, fmt, data)					\
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
	WRITE_ASS(fd,       "d", fd);
	WRITE_ASS(group_fd, "d", group_fd);
	WRITE_ASS(cpu,      "d", cpu);
	WRITE_ASS(pid,      "d", pid);
	WRITE_ASS(flags,   "lu", flags);

	/* struct perf_event_attr */
	WRITE_ASS(type,   PRIu32,  attr->type);
	WRITE_ASS(size,   PRIu32,  attr->size);
	WRITE_ASS(config,  "llu",  attr->config);
	WRITE_ASS(sample_period, "llu", attr->sample_period);
	WRITE_ASS(sample_type,   "llu", attr->sample_type);
	WRITE_ASS(read_format,   "llu", attr->read_format);
	WRITE_ASS(disabled,       "d", attr->disabled);
	WRITE_ASS(inherit,        "d", attr->inherit);
	WRITE_ASS(pinned,         "d", attr->pinned);
	WRITE_ASS(exclusive,      "d", attr->exclusive);
	WRITE_ASS(exclude_user,   "d", attr->exclude_user);
	WRITE_ASS(exclude_kernel, "d", attr->exclude_kernel);
	WRITE_ASS(exclude_hv,     "d", attr->exclude_hv);
	WRITE_ASS(exclude_idle,   "d", attr->exclude_idle);
	WRITE_ASS(mmap,           "d", attr->mmap);
	WRITE_ASS(comm,           "d", attr->comm);
	WRITE_ASS(freq,           "d", attr->freq);
	WRITE_ASS(inherit_stat,   "d", attr->inherit_stat);
	WRITE_ASS(enable_on_exec, "d", attr->enable_on_exec);
	WRITE_ASS(task,           "d", attr->task);
	WRITE_ASS(watermask,      "d", attr->watermark);
	WRITE_ASS(precise_ip,     "d", attr->precise_ip);
	WRITE_ASS(mmap_data,      "d", attr->mmap_data);
	WRITE_ASS(sample_id_all,  "d", attr->sample_id_all);
	WRITE_ASS(exclude_host,   "d", attr->exclude_host);
	WRITE_ASS(exclude_guest,  "d", attr->exclude_guest);
	WRITE_ASS(exclude_callchain_kernel, "d",
		  attr->exclude_callchain_kernel);
	WRITE_ASS(exclude_callchain_user, "d",
		  attr->exclude_callchain_user);
	WRITE_ASS(wakeup_events, PRIu32, attr->wakeup_events);
	WRITE_ASS(bp_type, PRIu32, attr->bp_type);
	WRITE_ASS(config1, "llu", attr->config1);
	WRITE_ASS(config2, "llu", attr->config2);
	WRITE_ASS(branch_sample_type, "llu", attr->branch_sample_type);
	WRITE_ASS(sample_regs_user,   "llu", attr->sample_regs_user);
	WRITE_ASS(sample_stack_user,  PRIu32, attr->sample_stack_user);
	WRITE_ASS(optional, "d", 0);

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
