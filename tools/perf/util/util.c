#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "util.h"

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

int copyfile(const char *from, const char *to)
{
	int fromfd, tofd;
	struct stat st;
	void *addr;
	int err = -1;

	if (stat(from, &st))
		goto out;

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
