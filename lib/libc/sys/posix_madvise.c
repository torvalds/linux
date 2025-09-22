/*	$OpenBSD: posix_madvise.c,v 1.4 2015/09/11 13:26:20 guenther Exp $ */
/*
 * Ted Unangst wrote this file and placed it into the public domain.
 */
#include <sys/mman.h>
#include <errno.h>

int
posix_madvise(void *addr, size_t len, int behav)
{
	return (madvise(addr, len, behav) ? errno : 0);
}
