/*	$OpenBSD: mmaptest.c,v 1.7 2019/05/09 23:13:31 guenther Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org>, 2001 Public Domain
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>

#include <sys/types.h>
#include <sys/mman.h>

/*
 * Map the same physical page in two places in memory.
 * Should cause a cache alias on virtual-aliased cache architectures.
 */

#define MAGIC "The voices in my head are trying to ignore me."

int
main(int argc, char *argv[])
{
	char fname[25] = "/tmp/mmaptestXXXXXXXXXX";
	int page_size;
	int fd;
	char *v1, *v2;

	if ((fd = mkstemp(fname)) < 0)
		err(1, "mkstemp");

	if (remove(fname) < 0)
		err(1, "remove");

	if ((page_size = sysconf(_SC_PAGESIZE)) < 0)
		err(1, "sysconf");

	if (ftruncate(fd, 2 * page_size) < 0)
		err(1, "ftruncate");

	/* map two pages, then map the first page over the second */

	v1 = mmap(NULL, 2 * page_size, PROT_READ|PROT_WRITE,
	    MAP_SHARED, fd, 0);
	if (v1 == MAP_FAILED)
		err(1, "mmap 1");

	/* No need to unmap, mmap is supposed to do that for us if MAP_FIXED */

	v2 = mmap(v1 + page_size, page_size, PROT_READ|PROT_WRITE,
	    MAP_SHARED|MAP_FIXED, fd, 0);
	if (v2 == MAP_FAILED)
		err(1, "mmap 2");

	memcpy(v1, MAGIC, sizeof(MAGIC));

	if (memcmp(v2, MAGIC, sizeof(MAGIC)) != 0)
		errx(1, "comparison 1 failed");

	if (memcmp(v1, v2, sizeof(MAGIC)) != 0)
		errx(1, "comparison 2 failed");

	if (munmap(v1, 2 * page_size) < 0)
		errx(1, "munmap");

	close(fd);

	return 0;
}

