/*	$OpenBSD: minherit.c,v 1.3 2003/08/02 01:24:36 david Exp $	*/
/*
 * Written by Artur Grabowski <art@openbsd.org> Public Domain.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>

#define MAGIC "inherited"

int
main(int argc, char *argv[])
{
	void *map1, *map2;
	int page_size;
	int status;

	page_size = getpagesize();

	if ((map1 = mmap(NULL, page_size, PROT_READ|PROT_WRITE, MAP_ANON,
	    -1, 0)) == MAP_FAILED)
		err(1, "mmap");

	if ((map2 = mmap(NULL, page_size, PROT_READ|PROT_WRITE, MAP_ANON,
	    -1, 0)) == MAP_FAILED)
		err(1, "mmap");

	memset(map1, 0, sizeof(MAGIC));
	memcpy(map2, MAGIC, sizeof(MAGIC));

	if (minherit(map1, page_size, MAP_INHERIT_SHARE) != 0)
		err(1, "minherit");

	if (minherit(map2, page_size, MAP_INHERIT_NONE) != 0)
		err(1, "minherit");

	switch(fork()) {
	case -1:
		err(1, "fork");
	case 0:
		memcpy(map1, MAGIC, sizeof(MAGIC));
		/* map2 is not mapped and should give us error on munmap */
		if (munmap(map2, page_size) == 0)
			_exit(1);
		_exit(0);
	}

	if (wait(&status) < 0)
		err(1, "wait");

	if (!WIFEXITED(status))
		err(1, "child error");

	if (memcmp(map1, MAGIC, sizeof(MAGIC)) != 0)
		return 1;

	return WEXITSTATUS(status) != 0;
}
