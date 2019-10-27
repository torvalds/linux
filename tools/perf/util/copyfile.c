// SPDX-License-Identifier: GPL-2.0
#include "util/copyfile.h"
#include "util/namespaces.h"
#include <internal/lib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int slow_copyfile(const char *from, const char *to, struct nsinfo *nsi)
{
	int err = -1;
	char *line = NULL;
	size_t n;
	FILE *from_fp, *to_fp;
	struct nscookie nsc;

	nsinfo__mountns_enter(nsi, &nsc);
	from_fp = fopen(from, "r");
	nsinfo__mountns_exit(&nsc);
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

int copyfile_offset(int ifd, loff_t off_in, int ofd, loff_t off_out, u64 size)
{
	void *ptr;
	loff_t pgoff;

	pgoff = off_in & ~(page_size - 1);
	off_in -= pgoff;

	ptr = mmap(NULL, off_in + size, PROT_READ, MAP_PRIVATE, ifd, pgoff);
	if (ptr == MAP_FAILED)
		return -1;

	while (size) {
		ssize_t ret = pwrite(ofd, ptr + off_in, size, off_out);
		if (ret < 0 && errno == EINTR)
			continue;
		if (ret <= 0)
			break;

		size -= ret;
		off_in += ret;
		off_out += ret;
	}
	munmap(ptr, off_in + size);

	return size ? -1 : 0;
}

static int copyfile_mode_ns(const char *from, const char *to, mode_t mode,
			    struct nsinfo *nsi)
{
	int fromfd, tofd;
	struct stat st;
	int err;
	char *tmp = NULL, *ptr = NULL;
	struct nscookie nsc;

	nsinfo__mountns_enter(nsi, &nsc);
	err = stat(from, &st);
	nsinfo__mountns_exit(&nsc);
	if (err)
		goto out;
	err = -1;

	/* extra 'x' at the end is to reserve space for '.' */
	if (asprintf(&tmp, "%s.XXXXXXx", to) < 0) {
		tmp = NULL;
		goto out;
	}
	ptr = strrchr(tmp, '/');
	if (!ptr)
		goto out;
	ptr = memmove(ptr + 1, ptr, strlen(ptr) - 1);
	*ptr = '.';

	tofd = mkstemp(tmp);
	if (tofd < 0)
		goto out;

	if (st.st_size == 0) { /* /proc? do it slowly... */
		err = slow_copyfile(from, tmp, nsi);
		if (!err && fchmod(tofd, mode))
			err = -1;
		goto out_close_to;
	}

	if (fchmod(tofd, mode))
		goto out_close_to;

	nsinfo__mountns_enter(nsi, &nsc);
	fromfd = open(from, O_RDONLY);
	nsinfo__mountns_exit(&nsc);
	if (fromfd < 0)
		goto out_close_to;

	err = copyfile_offset(fromfd, 0, tofd, 0, st.st_size);

	close(fromfd);
out_close_to:
	close(tofd);
	if (!err)
		err = link(tmp, to);
	unlink(tmp);
out:
	free(tmp);
	return err;
}

int copyfile_ns(const char *from, const char *to, struct nsinfo *nsi)
{
	return copyfile_mode_ns(from, to, 0755, nsi);
}

int copyfile_mode(const char *from, const char *to, mode_t mode)
{
	return copyfile_mode_ns(from, to, mode, NULL);
}

int copyfile(const char *from, const char *to)
{
	return copyfile_mode(from, to, 0755);
}
