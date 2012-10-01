#include "../perf.h"
#include "util.h"
#include <sys/mman.h>
#ifndef NO_BACKTRACE
#include <execinfo.h>
#endif
#include <stdio.h>
#include <stdlib.h>

/*
 * XXX We need to find a better place for these things...
 */
bool perf_host  = true;
bool perf_guest = false;

void event_attr_init(struct perf_event_attr *attr)
{
	if (!perf_host)
		attr->exclude_host  = 1;
	if (!perf_guest)
		attr->exclude_guest = 1;
	/* to capture ABI version */
	attr->size = sizeof(*attr);
}

int mkdir_p(char *path, mode_t mode)
{
	struct stat st;
	int err;
	char *d = path;

	if (*d != '/')
		return -1;

	if (stat(path, &st) == 0)
		return 0;

	while (*++d == '/');

	while ((d = strchr(d, '/'))) {
		*d = '\0';
		err = stat(path, &st) && mkdir(path, mode);
		*d++ = '/';
		if (err)
			return -1;
		while (*d == '/')
			++d;
	}
	return (stat(path, &st) && mkdir(path, mode)) ? -1 : 0;
}

static int slow_copyfile(const char *from, const char *to)
{
	int err = 0;
	char *line = NULL;
	size_t n;
	FILE *from_fp = fopen(from, "r"), *to_fp;

	if (from_fp == NULL)
		goto out;

	to_fp = fopen(to, "w");
	if (to_fp == NULL)
		goto out_fclose_from;

	while (getline(&line, &n, from_fp) > 0)
		if (fputs(line, to_fp) == EOF)
			goto out_fclose_to;
	err = 0;
out_fclose_to:
	fclose(to_fp);
	free(line);
out_fclose_from:
	fclose(from_fp);
out:
	return err;
}

int copyfile(const char *from, const char *to)
{
	int fromfd, tofd;
	struct stat st;
	void *addr;
	int err = -1;

	if (stat(from, &st))
		goto out;

	if (st.st_size == 0) /* /proc? do it slowly... */
		return slow_copyfile(from, to);

	fromfd = open(from, O_RDONLY);
	if (fromfd < 0)
		goto out;

	tofd = creat(to, 0755);
	if (tofd < 0)
		goto out_close_from;

	addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fromfd, 0);
	if (addr == MAP_FAILED)
		goto out_close_to;

	if (write(tofd, addr, st.st_size) == st.st_size)
		err = 0;

	munmap(addr, st.st_size);
out_close_to:
	close(tofd);
	if (err)
		unlink(to);
out_close_from:
	close(fromfd);
out:
	return err;
}

unsigned long convert_unit(unsigned long value, char *unit)
{
	*unit = ' ';

	if (value > 1000) {
		value /= 1000;
		*unit = 'K';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'M';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'G';
	}

	return value;
}

int readn(int fd, void *buf, size_t n)
{
	void *buf_start = buf;

	while (n) {
		int ret = read(fd, buf, n);

		if (ret <= 0)
			return ret;

		n -= ret;
		buf += ret;
	}

	return buf - buf_start;
}

size_t hex_width(u64 v)
{
	size_t n = 1;

	while ((v >>= 4))
		++n;

	return n;
}

/* Obtain a backtrace and print it to stdout. */
#ifndef NO_BACKTRACE
void dump_stack(void)
{
	void *array[16];
	size_t size = backtrace(array, ARRAY_SIZE(array));
	char **strings = backtrace_symbols(array, size);
	size_t i;

	printf("Obtained %zd stack frames.\n", size);

	for (i = 0; i < size; i++)
		printf("%s\n", strings[i]);

	free(strings);
}
#else
void dump_stack(void) {}
#endif
