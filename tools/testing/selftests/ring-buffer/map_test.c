// SPDX-License-Identifier: GPL-2.0
/*
 * Ring-buffer memory mapping tests
 *
 * Copyright (c) 2024 Vincent Donnefort <vdonnefort@google.com>
 */
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/trace_mmap.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include "../user_events/user_events_selftests.h" /* share tracefs setup */
#include "../kselftest_harness.h"

#define TRACEFS_ROOT "/sys/kernel/tracing"

static int __tracefs_write(const char *path, const char *value)
{
	int fd, ret;

	fd = open(path, O_WRONLY | O_TRUNC);
	if (fd < 0)
		return fd;

	ret = write(fd, value, strlen(value));

	close(fd);

	return ret == -1 ? -errno : 0;
}

static int __tracefs_write_int(const char *path, int value)
{
	char *str;
	int ret;

	if (asprintf(&str, "%d", value) < 0)
		return -1;

	ret = __tracefs_write(path, str);

	free(str);

	return ret;
}

#define tracefs_write_int(path, value) \
	ASSERT_EQ(__tracefs_write_int((path), (value)), 0)

#define tracefs_write(path, value) \
	ASSERT_EQ(__tracefs_write((path), (value)), 0)

static int tracefs_reset(void)
{
	if (__tracefs_write_int(TRACEFS_ROOT"/tracing_on", 0))
		return -1;
	if (__tracefs_write(TRACEFS_ROOT"/trace", ""))
		return -1;
	if (__tracefs_write(TRACEFS_ROOT"/set_event", ""))
		return -1;
	if (__tracefs_write(TRACEFS_ROOT"/current_tracer", "nop"))
		return -1;

	return 0;
}

struct tracefs_cpu_map_desc {
	struct trace_buffer_meta	*meta;
	int				cpu_fd;
};

int tracefs_cpu_map(struct tracefs_cpu_map_desc *desc, int cpu)
{
	int page_size = getpagesize();
	char *cpu_path;
	void *map;

	if (asprintf(&cpu_path,
		     TRACEFS_ROOT"/per_cpu/cpu%d/trace_pipe_raw",
		     cpu) < 0)
		return -ENOMEM;

	desc->cpu_fd = open(cpu_path, O_RDONLY | O_NONBLOCK);
	free(cpu_path);
	if (desc->cpu_fd < 0)
		return -ENODEV;

again:
	map = mmap(NULL, page_size, PROT_READ, MAP_SHARED, desc->cpu_fd, 0);
	if (map == MAP_FAILED)
		return -errno;

	desc->meta = (struct trace_buffer_meta *)map;

	/* the meta-page is bigger than the original mapping */
	if (page_size < desc->meta->meta_struct_len) {
		int meta_page_size = desc->meta->meta_page_size;

		munmap(desc->meta, page_size);
		page_size = meta_page_size;
		goto again;
	}

	return 0;
}

void tracefs_cpu_unmap(struct tracefs_cpu_map_desc *desc)
{
	munmap(desc->meta, desc->meta->meta_page_size);
	close(desc->cpu_fd);
}

FIXTURE(map) {
	struct tracefs_cpu_map_desc	map_desc;
	bool				umount;
};

FIXTURE_VARIANT(map) {
	int	subbuf_size;
};

FIXTURE_VARIANT_ADD(map, subbuf_size_4k) {
	.subbuf_size = 4,
};

FIXTURE_VARIANT_ADD(map, subbuf_size_8k) {
	.subbuf_size = 8,
};

FIXTURE_SETUP(map)
{
	int cpu = sched_getcpu();
	cpu_set_t cpu_mask;
	bool fail, umount;
	char *message;

	if (getuid() != 0)
		SKIP(return, "Skipping: %s", "Please run the test as root");

	if (!tracefs_enabled(&message, &fail, &umount)) {
		if (fail) {
			TH_LOG("Tracefs setup failed: %s", message);
			ASSERT_FALSE(fail);
		}
		SKIP(return, "Skipping: %s", message);
	}

	self->umount = umount;

	ASSERT_GE(cpu, 0);

	ASSERT_EQ(tracefs_reset(), 0);

	tracefs_write_int(TRACEFS_ROOT"/buffer_subbuf_size_kb", variant->subbuf_size);

	ASSERT_EQ(tracefs_cpu_map(&self->map_desc, cpu), 0);

	/*
	 * Ensure generated events will be found on this very same ring-buffer.
	 */
	CPU_ZERO(&cpu_mask);
	CPU_SET(cpu, &cpu_mask);
	ASSERT_EQ(sched_setaffinity(0, sizeof(cpu_mask), &cpu_mask), 0);
}

FIXTURE_TEARDOWN(map)
{
	tracefs_reset();

	if (self->umount)
		tracefs_unmount();

	tracefs_cpu_unmap(&self->map_desc);
}

TEST_F(map, meta_page_check)
{
	struct tracefs_cpu_map_desc *desc = &self->map_desc;
	int cnt = 0;

	ASSERT_EQ(desc->meta->entries, 0);
	ASSERT_EQ(desc->meta->overrun, 0);
	ASSERT_EQ(desc->meta->read, 0);

	ASSERT_EQ(desc->meta->reader.id, 0);
	ASSERT_EQ(desc->meta->reader.read, 0);

	ASSERT_EQ(ioctl(desc->cpu_fd, TRACE_MMAP_IOCTL_GET_READER), 0);
	ASSERT_EQ(desc->meta->reader.id, 0);

	tracefs_write_int(TRACEFS_ROOT"/tracing_on", 1);
	for (int i = 0; i < 16; i++)
		tracefs_write_int(TRACEFS_ROOT"/trace_marker", i);
again:
	ASSERT_EQ(ioctl(desc->cpu_fd, TRACE_MMAP_IOCTL_GET_READER), 0);

	ASSERT_EQ(desc->meta->entries, 16);
	ASSERT_EQ(desc->meta->overrun, 0);
	ASSERT_EQ(desc->meta->read, 16);

	ASSERT_EQ(desc->meta->reader.id, 1);

	if (!(cnt++))
		goto again;
}

TEST_F(map, data_mmap)
{
	struct tracefs_cpu_map_desc *desc = &self->map_desc;
	unsigned long meta_len, data_len;
	void *data;

	meta_len = desc->meta->meta_page_size;
	data_len = desc->meta->subbuf_size * desc->meta->nr_subbufs;

	/* Map all the available subbufs */
	data = mmap(NULL, data_len, PROT_READ, MAP_SHARED,
		    desc->cpu_fd, meta_len);
	ASSERT_NE(data, MAP_FAILED);
	munmap(data, data_len);

	/* Map all the available subbufs - 1 */
	data_len -= desc->meta->subbuf_size;
	data = mmap(NULL, data_len, PROT_READ, MAP_SHARED,
		    desc->cpu_fd, meta_len);
	ASSERT_NE(data, MAP_FAILED);
	munmap(data, data_len);

	/* Offset within ring-buffer bounds, mapping size overflow */
	meta_len += desc->meta->subbuf_size * 2;
	data = mmap(NULL, data_len, PROT_READ, MAP_SHARED,
		    desc->cpu_fd, meta_len);
	ASSERT_EQ(data, MAP_FAILED);

	/* Offset outside ring-buffer bounds */
	data_len = desc->meta->subbuf_size * desc->meta->nr_subbufs;
	data = mmap(NULL, data_len, PROT_READ, MAP_SHARED,
		    desc->cpu_fd, data_len + (desc->meta->subbuf_size * 2));
	ASSERT_EQ(data, MAP_FAILED);

	/* Verify meta-page padding */
	if (desc->meta->meta_page_size > getpagesize()) {
		data_len = desc->meta->meta_page_size;
		data = mmap(NULL, data_len,
			    PROT_READ, MAP_SHARED, desc->cpu_fd, 0);
		ASSERT_NE(data, MAP_FAILED);

		for (int i = desc->meta->meta_struct_len;
		     i < desc->meta->meta_page_size; i += sizeof(int))
			ASSERT_EQ(*(int *)(data + i), 0);

		munmap(data, data_len);
	}
}

FIXTURE(snapshot) {
	bool	umount;
};

FIXTURE_SETUP(snapshot)
{
	bool fail, umount;
	struct stat sb;
	char *message;

	if (getuid() != 0)
		SKIP(return, "Skipping: %s", "Please run the test as root");

	if (stat(TRACEFS_ROOT"/snapshot", &sb))
		SKIP(return, "Skipping: %s", "snapshot not available");

	if (!tracefs_enabled(&message, &fail, &umount)) {
		if (fail) {
			TH_LOG("Tracefs setup failed: %s", message);
			ASSERT_FALSE(fail);
		}
		SKIP(return, "Skipping: %s", message);
	}

	self->umount = umount;
}

FIXTURE_TEARDOWN(snapshot)
{
	__tracefs_write(TRACEFS_ROOT"/events/sched/sched_switch/trigger",
			"!snapshot");
	tracefs_reset();

	if (self->umount)
		tracefs_unmount();
}

TEST_F(snapshot, excludes_map)
{
	struct tracefs_cpu_map_desc map_desc;
	int cpu = sched_getcpu();

	ASSERT_GE(cpu, 0);
	tracefs_write(TRACEFS_ROOT"/events/sched/sched_switch/trigger",
		      "snapshot");
	ASSERT_EQ(tracefs_cpu_map(&map_desc, cpu), -EBUSY);
}

TEST_F(snapshot, excluded_by_map)
{
	struct tracefs_cpu_map_desc map_desc;
	int cpu = sched_getcpu();

	ASSERT_EQ(tracefs_cpu_map(&map_desc, cpu), 0);

	ASSERT_EQ(__tracefs_write(TRACEFS_ROOT"/events/sched/sched_switch/trigger",
				  "snapshot"), -EBUSY);
	ASSERT_EQ(__tracefs_write(TRACEFS_ROOT"/snapshot",
				  "1"), -EBUSY);
}

TEST_HARNESS_MAIN
