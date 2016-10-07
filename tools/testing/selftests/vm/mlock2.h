#include <syscall.h>
#include <errno.h>

#ifndef MLOCK_ONFAULT
#define MLOCK_ONFAULT 1
#endif

#ifndef MCL_ONFAULT
#define MCL_ONFAULT (MCL_FUTURE << 1)
#endif

static int mlock2_(void *start, size_t len, int flags)
{
#ifdef __NR_mlock2
	return syscall(__NR_mlock2, start, len, flags);
#else
	errno = ENOSYS;
	return -1;
#endif
}
