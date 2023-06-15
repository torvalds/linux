// SPDX-License-Identifier: GPL-2.0
/*
 * User Events Perf Events Test Program
 *
 * Copyright (c) 2021 Beau Belgrave <beaub@linux.microsoft.com>
 */

#include <errno.h>
#include <linux/user_events.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <asm/unistd.h>

#include "../kselftest_harness.h"

const char *data_file = "/sys/kernel/tracing/user_events_data";
const char *id_file = "/sys/kernel/tracing/events/user_events/__test_event/id";
const char *fmt_file = "/sys/kernel/tracing/events/user_events/__test_event/format";

struct event {
	__u32 index;
	__u32 field1;
	__u32 field2;
};

static long perf_event_open(struct perf_event_attr *pe, pid_t pid,
			    int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, pe, pid, cpu, group_fd, flags);
}

static int get_id(void)
{
	FILE *fp = fopen(id_file, "r");
	int ret, id = 0;

	if (!fp)
		return -1;

	ret = fscanf(fp, "%d", &id);
	fclose(fp);

	if (ret != 1)
		return -1;

	return id;
}

static int get_offset(void)
{
	FILE *fp = fopen(fmt_file, "r");
	int ret, c, last = 0, offset = 0;

	if (!fp)
		return -1;

	/* Read until empty line */
	while (true) {
		c = getc(fp);

		if (c == EOF)
			break;

		if (last == '\n' && c == '\n')
			break;

		last = c;
	}

	ret = fscanf(fp, "\tfield:u32 field1;\toffset:%d;", &offset);
	fclose(fp);

	if (ret != 1)
		return -1;

	return offset;
}

FIXTURE(user) {
	int data_fd;
	int check;
};

FIXTURE_SETUP(user) {
	self->data_fd = open(data_file, O_RDWR);
	ASSERT_NE(-1, self->data_fd);
}

FIXTURE_TEARDOWN(user) {
	close(self->data_fd);
}

TEST_F(user, perf_write) {
	struct perf_event_attr pe = {0};
	struct user_reg reg = {0};
	struct event event;
	struct perf_event_mmap_page *perf_page;
	int page_size = sysconf(_SC_PAGESIZE);
	int id, fd, offset;
	__u32 *val;

	reg.size = sizeof(reg);
	reg.name_args = (__u64)"__test_event u32 field1; u32 field2";
	reg.enable_bit = 31;
	reg.enable_addr = (__u64)&self->check;
	reg.enable_size = sizeof(self->check);

	/* Register should work */
	ASSERT_EQ(0, ioctl(self->data_fd, DIAG_IOCSREG, &reg));
	ASSERT_EQ(0, reg.write_index);
	ASSERT_EQ(0, self->check);

	/* Id should be there */
	id = get_id();
	ASSERT_NE(-1, id);
	offset = get_offset();
	ASSERT_NE(-1, offset);

	pe.type = PERF_TYPE_TRACEPOINT;
	pe.size = sizeof(pe);
	pe.config = id;
	pe.sample_type = PERF_SAMPLE_RAW;
	pe.sample_period = 1;
	pe.wakeup_events = 1;

	/* Tracepoint attach should work */
	fd = perf_event_open(&pe, 0, -1, -1, 0);
	ASSERT_NE(-1, fd);

	perf_page = mmap(NULL, page_size * 2, PROT_READ, MAP_SHARED, fd, 0);
	ASSERT_NE(MAP_FAILED, perf_page);

	/* Status should be updated */
	ASSERT_EQ(1 << reg.enable_bit, self->check);

	event.index = reg.write_index;
	event.field1 = 0xc001;
	event.field2 = 0xc01a;

	/* Ensure write shows up at correct offset */
	ASSERT_NE(-1, write(self->data_fd, &event, sizeof(event)));
	val = (void *)(((char *)perf_page) + perf_page->data_offset);
	ASSERT_EQ(PERF_RECORD_SAMPLE, *val);
	/* Skip over header and size, move to offset */
	val += 3;
	val = (void *)((char *)val) + offset;
	/* Ensure correct */
	ASSERT_EQ(event.field1, *val++);
	ASSERT_EQ(event.field2, *val++);

	munmap(perf_page, page_size * 2);
	close(fd);

	/* Status should be updated */
	ASSERT_EQ(0, self->check);
}

int main(int argc, char **argv)
{
	return test_harness_run(argc, argv);
}
