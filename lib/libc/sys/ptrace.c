/*	$OpenBSD: ptrace.c,v 1.5 2016/03/30 07:55:36 guenther Exp $	*/
/* David Leonard <d@openbsd.org>, 1999. Public domain. */

#include <sys/types.h>
#include <sys/ptrace.h>
#include <errno.h>

int
WRAP(ptrace)(int request, pid_t pid, caddr_t addr, int data)
{

	/* ptrace(2) is documented to clear errno on success: */
	errno = 0;
	return (ptrace(request, pid, addr, data));
}
DEF_WRAP(ptrace);
