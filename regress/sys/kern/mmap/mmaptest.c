/*	$OpenBSD: mmaptest.c,v 1.5 2004/10/10 03:08:30 mickey Exp $	*/

/*
 * Copyright (c) 2001 Niklas Hallqvist.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TEMPL "test-fileXXXXXXXXXX"
#define MAGIC 0x1234

int
main(int argc, char **argv)
{
	int fd;
	void *v;
	int i;
	ssize_t n;
	static char nm[] = TEMPL;
	off_t sz;

	fd = mkstemp(nm);
	if (fd == -1)
		err(1, "mkstemp");
	sz = sysconf(_SC_PAGESIZE);
	if (sz == -1)
		err(1, "sysconf");
	if (ftruncate(fd, sz) == -1)
		err(1, "ftruncate");
	v = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (v == MAP_FAILED)
		err(1, "mmap");
	*(int *)v = MAGIC;
	if (msync(v, sz, MS_SYNC) == -1)
		err(1, "msync");
	if (munmap(v, sz) == -1)
		err(1, "munmap");
	if (close(fd) == -1)
		err(1, "close");
	fd = open(nm, O_RDONLY);
	if (fd == -1)
		err(1, "open");
	if (unlink(nm) == -1)
		err(1, "unlink");
	n = read(fd, &i, sizeof i);
	if (n == -1)
		err(1, "read");
	if (n != sizeof i)
		errx(1, "short read");
	if (close(fd) == -1)
		err(1, "close");
	exit(i != MAGIC);
}
