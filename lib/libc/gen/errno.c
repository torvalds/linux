/*	$OpenBSD: errno.c,v 1.6 2016/05/07 19:05:22 guenther Exp $ */
/* PUBLIC DOMAIN: No Rights Reserved.   Marco S Hyman <marc@snafu.org> */

#include <tib.h>
#include <errno.h>
#include <unistd.h>
#include "thread_private.h"


#ifdef TCB_HAVE_MD_GET
/*
 * If there's an MD TCB_GET() macro, then getting the TCB address is
 * cheap enough that we can do it even in single-threaded programs,
 * so the tc_errnoptr and tc_tcb callbacks will be unused, and __errno()
 * can just use TIB_GET().
 */
int *
__errno(void)
{
	return (&TIB_GET()->tib_errno);
}
DEF_STRONG(__errno);

#else /* ! TCB_HAVE_MD_GET */
/*
 * Otherwise, getting the TCB address requires the __get_tcb()
 * syscall.  Rather than pay that cost for single-threaded programs,
 * the syscall stubs will invoke the tc_errnoptr callback to set errno
 * and other code will invoke the tc_tcb callback to get the TCB
 * for cancelation checks, etc.  The default callbacks will just
 * work from the cached location of the initial thread's TCB;
 * libpthread can override them to the necessary more expensive
 * versions that use __get_tcb().
 */

/* cached pointer to the TCB of the only thread in single-threaded programs */
void	*_libc_single_tcb = NULL;

static inline void *
single_threaded_tcb(void)
{
	if (__predict_false(_libc_single_tcb == NULL))
		_libc_single_tcb = TCB_GET();
	return (_libc_single_tcb);
}

static int *
single_threaded_errnoptr(void)
{
	return &TCB_TO_TIB(single_threaded_tcb())->tib_errno;
}

/*
 * __errno(): just use the callback to get the applicable current method
 */
int *
__errno(void)
{
	return (_thread_cb.tc_errnoptr());
}
DEF_STRONG(__errno);

#endif /* !TCB_HAVE_MD_GET */


struct thread_callbacks _thread_cb =
{
#ifndef	TCB_HAVE_MD_GET
	.tc_errnoptr	= &single_threaded_errnoptr,
	.tc_tcb		= &single_threaded_tcb,
#endif
};
