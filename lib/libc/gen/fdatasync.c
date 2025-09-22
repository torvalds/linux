/*	$OpenBSD: fdatasync.c,v 1.1 2013/04/15 16:38:21 matthew Exp $ */
/*
 * Written by Matthew Dempsky, 2013.
 * Public domain.
 */

#include <unistd.h>

int
fdatasync(int fd)
{
	return (fsync(fd));
}
