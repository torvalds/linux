/*	$OpenBSD: timer_settime.c,v 1.7 2015/09/12 13:13:34 guenther Exp $ */

#include <time.h>
#include <errno.h>

int	timer_settime(timer_t, int, const struct itimerspec *,
	    struct itimerspec *);
PROTO_DEPRECATED(timer_settime);

int
timer_settime(timer_t timerid, int flags, const struct itimerspec *value,
    struct itimerspec *ovalue)
{
	errno = ENOSYS;
	return -1;
}
