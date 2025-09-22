/*	$OpenBSD: timer_getoverrun.c,v 1.6 2015/09/12 13:13:34 guenther Exp $ */

#include <time.h>
#include <errno.h>

int	timer_getoverrun(timer_t);
PROTO_DEPRECATED(timer_getoverrun);

int
timer_getoverrun(timer_t timerid)
{
	errno = ENOSYS;
	return -1;
}
