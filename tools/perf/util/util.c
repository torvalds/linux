#include "util.h"
#include <sys/mman.h>

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
