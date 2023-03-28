// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013-2015, Michael Ellerman, IBM Corp.
 */

#define _GNU_SOURCE	/* For CPU_ZERO etc. */

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <link.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/limits.h>

#include "utils.h"

static char auxv[4096];

int read_file(const char *path, char *buf, size_t count, size_t *len)
{
	ssize_t rc;
	int fd;
	int err;
	char eof;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	rc = read(fd, buf, count);
	if (rc < 0) {
		err = -errno;
		goto out;
	}

	if (len)
		*len = rc;

	/* Overflow if there are still more bytes after filling the buffer */
	if (rc == count) {
		rc = read(fd, &eof, 1);
		if (rc != 0) {
			err = -EOVERFLOW;
			goto out;
		}
	}

	err = 0;

out:
	close(fd);
	errno = -err;
	return err;
}

int read_file_alloc(const char *path, char **buf, size_t *len)
{
	size_t read_offset = 0;
	size_t buffer_len = 0;
	char *buffer = NULL;
	int err;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	/*
	 * We don't use stat & preallocate st_size because some non-files
	 * report 0 file size. Instead just dynamically grow the buffer
	 * as needed.
	 */
	while (1) {
		ssize_t rc;

		if (read_offset >= buffer_len / 2) {
			char *next_buffer;

			buffer_len = buffer_len ? buffer_len * 2 : 4096;
			next_buffer = realloc(buffer, buffer_len);
			if (!next_buffer) {
				err = -errno;
				goto out;
			}
			buffer = next_buffer;
		}

		rc = read(fd, buffer + read_offset, buffer_len - read_offset);
		if (rc < 0) {
			err = -errno;
			goto out;
		}

		if (rc == 0)
			break;

		read_offset += rc;
	}

	*buf = buffer;
	if (len)
		*len = read_offset;

	err = 0;

out:
	close(fd);
	if (err)
		free(buffer);
	errno = -err;
	return err;
}

int write_file(const char *path, const char *buf, size_t count)
{
	int fd;
	int err;
	ssize_t rc;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -errno;

	rc = write(fd, buf, count);
	if (rc < 0) {
		err = -errno;
		goto out;
	}

	if (rc != count) {
		err = -EOVERFLOW;
		goto out;
	}

	err = 0;

out:
	close(fd);
	errno = -err;
	return err;
}

int read_auxv(char *buf, ssize_t buf_size)
{
	int err;

	err = read_file("/proc/self/auxv", buf, buf_size, NULL);
	if (err) {
		perror("Error reading /proc/self/auxv");
		return err;
	}

	return 0;
}

int read_debugfs_file(const char *subpath, char *buf, size_t count)
{
	char path[PATH_MAX] = "/sys/kernel/debug/";

	strncat(path, subpath, sizeof(path) - strlen(path) - 1);

	return read_file(path, buf, count, NULL);
}

int write_debugfs_file(const char *subpath, const char *buf, size_t count)
{
	char path[PATH_MAX] = "/sys/kernel/debug/";

	strncat(path, subpath, sizeof(path) - strlen(path) - 1);

	return write_file(path, buf, count);
}

static int validate_int_parse(const char *buffer, size_t count, char *end)
{
	int err = 0;

	/* Require at least one digit */
	if (end == buffer) {
		err = -EINVAL;
		goto out;
	}

	/* Require all remaining characters be whitespace-ish */
	for (; end < buffer + count; end++) {
		if (*end == '\0')
			break;

		if (*end != ' ' && *end != '\n') {
			err = -EINVAL;
			goto out;
		}
	}

out:
	errno = -err;
	return err;
}

static int parse_bounded_int(const char *buffer, size_t count, intmax_t *result,
			     int base, intmax_t min, intmax_t max)
{
	int err;
	char *end;

	errno = 0;
	*result = strtoimax(buffer, &end, base);

	if (errno)
		return -errno;

	err = validate_int_parse(buffer, count, end);
	if (err)
		goto out;

	if (*result < min || *result > max)
		err = -EOVERFLOW;

out:
	errno = -err;
	return err;
}

static int parse_bounded_uint(const char *buffer, size_t count, uintmax_t *result,
			      int base, uintmax_t max)
{
	int err = 0;
	char *end;

	errno = 0;
	*result = strtoumax(buffer, &end, base);

	if (errno)
		return -errno;

	err = validate_int_parse(buffer, count, end);
	if (err)
		goto out;

	if (*result > max)
		err = -EOVERFLOW;

out:
	errno = -err;
	return err;
}

int parse_intmax(const char *buffer, size_t count, intmax_t *result, int base)
{
	return parse_bounded_int(buffer, count, result, base, INTMAX_MIN, INTMAX_MAX);
}

int parse_uintmax(const char *buffer, size_t count, uintmax_t *result, int base)
{
	return parse_bounded_uint(buffer, count, result, base, UINTMAX_MAX);
}

int parse_int(const char *buffer, size_t count, int *result, int base)
{
	intmax_t parsed;
	int err = parse_bounded_int(buffer, count, &parsed, base, INT_MIN, INT_MAX);

	*result = parsed;
	return err;
}

int parse_uint(const char *buffer, size_t count, unsigned int *result, int base)
{
	uintmax_t parsed;
	int err = parse_bounded_uint(buffer, count, &parsed, base, UINT_MAX);

	*result = parsed;
	return err;
}

int parse_long(const char *buffer, size_t count, long *result, int base)
{
	intmax_t parsed;
	int err = parse_bounded_int(buffer, count, &parsed, base, LONG_MIN, LONG_MAX);

	*result = parsed;
	return err;
}

int parse_ulong(const char *buffer, size_t count, unsigned long *result, int base)
{
	uintmax_t parsed;
	int err = parse_bounded_uint(buffer, count, &parsed, base, ULONG_MAX);

	*result = parsed;
	return err;
}

int read_long(const char *path, long *result, int base)
{
	int err;
	char buffer[32] = {0};

	err = read_file(path, buffer, sizeof(buffer) - 1, NULL);
	if (err)
		return err;

	return parse_long(buffer, sizeof(buffer), result, base);
}

int read_ulong(const char *path, unsigned long *result, int base)
{
	int err;
	char buffer[32] = {0};

	err = read_file(path, buffer, sizeof(buffer) - 1, NULL);
	if (err)
		return err;

	return parse_ulong(buffer, sizeof(buffer), result, base);
}

int write_long(const char *path, long result, int base)
{
	int err;
	int len;
	char buffer[32];

	/* Decimal only for now: no format specifier for signed hex values */
	if (base != 10) {
		err = -EINVAL;
		goto out;
	}

	len = snprintf(buffer, sizeof(buffer), "%ld", result);
	if (len < 0 || len >= sizeof(buffer)) {
		err = -EOVERFLOW;
		goto out;
	}

	err = write_file(path, buffer, len);

out:
	errno = -err;
	return err;
}

int write_ulong(const char *path, unsigned long result, int base)
{
	int err;
	int len;
	char buffer[32];
	char *fmt;

	switch (base) {
	case 10:
		fmt = "%lu";
		break;
	case 16:
		fmt = "%lx";
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	len = snprintf(buffer, sizeof(buffer), fmt, result);
	if (len < 0 || len >= sizeof(buffer)) {
		err = -errno;
		goto out;
	}

	err = write_file(path, buffer, len);

out:
	errno = -err;
	return err;
}

void *find_auxv_entry(int type, char *auxv)
{
	ElfW(auxv_t) *p;

	p = (ElfW(auxv_t) *)auxv;

	while (p->a_type != AT_NULL) {
		if (p->a_type == type)
			return p;

		p++;
	}

	return NULL;
}

void *get_auxv_entry(int type)
{
	ElfW(auxv_t) *p;

	if (read_auxv(auxv, sizeof(auxv)))
		return NULL;

	p = find_auxv_entry(type, auxv);
	if (p)
		return (void *)p->a_un.a_val;

	return NULL;
}

int pick_online_cpu(void)
{
	int ncpus, cpu = -1;
	cpu_set_t *mask;
	size_t size;

	ncpus = get_nprocs_conf();
	size = CPU_ALLOC_SIZE(ncpus);
	mask = CPU_ALLOC(ncpus);
	if (!mask) {
		perror("malloc");
		return -1;
	}

	CPU_ZERO_S(size, mask);

	if (sched_getaffinity(0, size, mask)) {
		perror("sched_getaffinity");
		goto done;
	}

	/* We prefer a primary thread, but skip 0 */
	for (cpu = 8; cpu < ncpus; cpu += 8)
		if (CPU_ISSET_S(cpu, size, mask))
			goto done;

	/* Search for anything, but in reverse */
	for (cpu = ncpus - 1; cpu >= 0; cpu--)
		if (CPU_ISSET_S(cpu, size, mask))
			goto done;

	printf("No cpus in affinity mask?!\n");

done:
	CPU_FREE(mask);
	return cpu;
}

bool is_ppc64le(void)
{
	struct utsname uts;
	int rc;

	errno = 0;
	rc = uname(&uts);
	if (rc) {
		perror("uname");
		return false;
	}

	return strcmp(uts.machine, "ppc64le") == 0;
}

int read_sysfs_file(char *fpath, char *result, size_t result_size)
{
	char path[PATH_MAX] = "/sys/";

	strncat(path, fpath, PATH_MAX - strlen(path) - 1);

	return read_file(path, result, result_size, NULL);
}

int read_debugfs_int(const char *debugfs_file, int *result)
{
	int err;
	char value[16] = {0};

	err = read_debugfs_file(debugfs_file, value, sizeof(value) - 1);
	if (err)
		return err;

	return parse_int(value, sizeof(value), result, 10);
}

int write_debugfs_int(const char *debugfs_file, int result)
{
	char value[16];

	snprintf(value, 16, "%d", result);

	return write_debugfs_file(debugfs_file, value, strlen(value));
}

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
		int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu,
		      group_fd, flags);
}

static void perf_event_attr_init(struct perf_event_attr *event_attr,
					unsigned int type,
					unsigned long config)
{
	memset(event_attr, 0, sizeof(*event_attr));

	event_attr->type = type;
	event_attr->size = sizeof(struct perf_event_attr);
	event_attr->config = config;
	event_attr->read_format = PERF_FORMAT_GROUP;
	event_attr->disabled = 1;
	event_attr->exclude_kernel = 1;
	event_attr->exclude_hv = 1;
	event_attr->exclude_guest = 1;
}

int perf_event_open_counter(unsigned int type,
			    unsigned long config, int group_fd)
{
	int fd;
	struct perf_event_attr event_attr;

	perf_event_attr_init(&event_attr, type, config);

	fd = perf_event_open(&event_attr, 0, -1, group_fd, 0);

	if (fd < 0)
		perror("perf_event_open() failed");

	return fd;
}

int perf_event_enable(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error while enabling perf events");
		return -1;
	}

	return 0;
}

int perf_event_disable(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error disabling perf events");
		return -1;
	}

	return 0;
}

int perf_event_reset(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error resetting perf events");
		return -1;
	}

	return 0;
}

int using_hash_mmu(bool *using_hash)
{
	char line[128];
	FILE *f;
	int rc;

	f = fopen("/proc/cpuinfo", "r");
	FAIL_IF(!f);

	rc = 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		if (!strcmp(line, "MMU		: Hash\n") ||
		    !strcmp(line, "platform	: Cell\n") ||
		    !strcmp(line, "platform	: PowerMac\n")) {
			*using_hash = true;
			goto out;
		}

		if (strcmp(line, "MMU		: Radix\n") == 0) {
			*using_hash = false;
			goto out;
		}
	}

	rc = -1;
out:
	fclose(f);
	return rc;
}
