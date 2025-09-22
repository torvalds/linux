/*	$OpenBSD: exit.c,v 1.3 2003/09/02 23:52:17 david Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain.
 */
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{	
	_exit(0);
	abort();
}
