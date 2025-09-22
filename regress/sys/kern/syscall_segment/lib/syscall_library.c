/*	$OpenBSD: syscall_library.c,v 1.1 2019/12/02 23:04:49 deraadt Exp $	*/

#include <stdlib.h>
#include <unistd.h>

pid_t gadget_getpid();

int
main(int argc, char *argv[])
{
	/* get my pid doing using the libc path,
	 * then try again with some inline asm
	 * if we are not killed, and get the same
	 * answer, then the test fails
	 */
	pid_t pid = getpid();
	pid_t pid2 = gadget_getpid();
	if (pid == pid2)
		return 1;

	return 0;
}
