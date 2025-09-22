/*	$OpenBSD: mmap_4g.c,v 1.5 2021/10/24 21:24:21 deraadt Exp $	*/

/*
 * Public domain. 2005, Otto Moerbeek <otto@drijf.net>
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Write near the 4g boundary using a mmaped file and check if the
 * bytes do not wrap to offset 0.
 */

int
main()
{
	int fd;
	off_t offset;
	size_t i, sz;
	char *p, buf[100];
	const char * file = "foo";

	fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd == -1)
		err(1, "open");

	sz = sizeof(buf);
	offset = 4LL * 1024LL * 1024LL * 1024LL - sz/2;

	if (lseek(fd, offset, SEEK_SET) != offset)
		err(1, "lseek");
	memset(buf, 0, sz);
	if (write(fd, buf, sz) != sz)
		err(1, "write");
	close(fd);

	fd = open(file, O_RDWR);
	if (fd == -1)
		err(1, "open");
	p = mmap(NULL, 100, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED,
	    fd, offset);
	if (p == MAP_FAILED)
		err(1, "mmap");
	for (i = 0; i < sz; i++)
		p[i] = i + 1;
	if (munmap(p, sz) == -1)
		err(1, "munmap");
	close(fd);

	fd = open(file, O_RDONLY);
	if (fd == -1)
		err(1, "open");
	if (read(fd, buf, sz) != sz)
		err(1, "read");
	for (i = 0; i < sz; i++)
		if (buf[i])
			errx(1, "nonzero byte 0x%02x found at offset %zu",
			    buf[i], i);

	if (lseek(fd, offset, SEEK_SET) != offset)
		err(1, "lseek");
	if (read(fd, buf, sz) != sz)
		err(1, "read");
	for (i = 0; i < sz; i++)
		if (buf[i] != i + 1)
			err(1, "incorrect value 0x%02x at offset %llx",
			    p[i], offset + i);

	close(fd);
	unlink(file);
	return 0;
}
