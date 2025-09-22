/*	$OpenBSD: timer_delete.c,v 1.6 2015/09/12 13:13:34 guenther Exp $ */

#include <time.h>
#include <errno.h>

int	timer_delete(timer_t);
PROTO_DEPRECATED(timer_delete);

int
timer_delete(timer_t timerid)
{
	errno = ENOSYS;
	return -1;
}
