/*	$OpenBSD: dtors.c,v 1.5 2003/09/02 23:52:16 david Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org>, 2002 Public Domain.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>

void zap(void) __attribute__((destructor));

void *destarea;

#define MAGIC "destructed"

void
zap(void)
{
	memcpy(destarea, MAGIC, sizeof(MAGIC));
}

/*
 * XXX - horrible abuse of exit(3), minherit(2) and fork(2).
 */
int
main(int argc, char **argv)
{
	int status, ch;
	int fallthru = 0;

	while ((ch = getopt(argc, argv, "f")) != -1) {
		switch(ch) {
		case 'f':
			fallthru = 1;
			break;
		default:
			fprintf(stderr, "Usage: dtors [-f]\n");
			exit(1);
		}
	}

	destarea = mmap(NULL, getpagesize(), PROT_READ|PROT_WRITE, MAP_ANON,
	    -1, 0);
	if (destarea == MAP_FAILED)
		err(1, "mmap");

	if (minherit(destarea, getpagesize(), MAP_INHERIT_SHARE) != 0)
		err(1, "minherit");

	memset(destarea, 0, sizeof(MAGIC));

	switch(fork()) {
	case -1:
		err(1, "fork");
	case 0:
		/*
		 * Yes, it's exit(), not _exit(). We _want_ to run the
		 * destructors in the child.
		 */
		if (fallthru)
			return (0);
		else
			exit(0);
	}

	if (wait(&status) < 0)
		err(1, "wait");		/* XXX uses exit() */

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		err(1, "child error");	/* XXX uses exit() */

	_exit(memcmp(destarea, MAGIC, sizeof(MAGIC)) != 0);
}	
