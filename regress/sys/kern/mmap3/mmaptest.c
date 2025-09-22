/* $OpenBSD: mmaptest.c,v 1.7 2014/09/27 06:28:45 doug Exp $ */
/*
 * Copyright (c) 2002 Marc Espie.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE OPENBSD PROJECT AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OPENBSD
 * PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>

#define	AREA	(16 * 4096)
/* 
 * Check for mmap/ftruncate interaction.  Specifically, ftruncate on
 * a short file may lose modifications made through an mmapped area.  
 */
int 
main(int argc, char *argv[])
{
	int i;
	int fd;
	char area[AREA];
	char *a2;
	for (i = 0 ; i < AREA; i++)
		area[i] = 5;

	fd = open("test.out", O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (fd == -1)
		err(1, "open(test.out)");
	if (write(fd, area, AREA) != AREA)
		err(1, "write");
	if (close(fd))
		err(1, "close");
	fd = open("test.out", O_RDWR);
	if (fd == -1)
		err(1, "open(test.out) 2");
	a2 = mmap(NULL, AREA, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (a2 == MAP_FAILED)
		err(1, "mmap");
	a2[10] = 3;
	msync(a2, AREA, MS_SYNC|MS_INVALIDATE);
	if (mlock(a2, AREA))
		err(1, "mlock");
	if (ftruncate(fd, 128))
		err(1, "ftruncate");
	if (munlock(a2, AREA))
		err(1, "munlock");
	if (close(fd))
		err(1, "close");
	fd = open("test.out", O_RDONLY);
	if (fd == -1)
		err(1, "open(test.out) 3");
	if (read(fd, area, AREA) != 128)
		err(1, "read");
	if (area[10] != 3)
		errx(1, "area[10] != 3");
	if (close(fd))
		err(1, "close");
	return 0;
}

