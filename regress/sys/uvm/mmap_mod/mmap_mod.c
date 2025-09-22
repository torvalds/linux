/*	$OpenBSD: mmap_mod.c,v 1.2 2010/06/20 17:56:07 phessler Exp $	*/

/*
 * Public domain. 2007, Artur Grabowski <art@openbsd.org>
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Test a corner case where a pmap can lose mod/ref bits after unmapping
 * and remapping a page.
 */
int
main()
{
	char name[20] = "/tmp/fluff.XXXXXX";
	char *buf, *pat;
	size_t ps;
	int fd;

	ps = getpagesize();

	if ((fd = mkstemp(name)) == -1)
		err(1, "mkstemp");

	if (unlink(name) == -1)
		err(1, "unlink");

	if (ftruncate(fd, ps))
		err(1, "ftruncate");

	if ((pat = malloc(ps)) == NULL)
		err(1, "malloc");

	memset(pat, 'a', ps);

	if (pwrite(fd, pat, ps, 0) != ps)
		err(1, "write");

	buf = mmap(NULL, ps, PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED)
		err(1, "mmap");

	if (*buf != 'a')
		errx(1, "mapped area - no file data ('%c' != 'a')", *buf);

	memset(buf, 'x', ps);

	if (munmap(buf, ps) == -1)
		err(1, "munmap");

	buf = mmap(NULL, ps, PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED)
		err(1, "mmap 2");

	if (*buf != 'x')
		errx(1, "mapped area lost modifications ('%c' != 'x')", *buf);

	if (msync(buf, ps, MS_SYNC) == -1)
		err(1, "msync");

	if (pread(fd, pat, ps, 0) != ps)
		err(1, "pread");

	if (*pat != 'x')
		errx(1, "synced area lost modifications ('%c' != 'x')", *pat);

	return (0);
}
