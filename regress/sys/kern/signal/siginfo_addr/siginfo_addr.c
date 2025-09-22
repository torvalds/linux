#include <sys/types.h>
#include <sys/signal.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define FAULTADDR	0x123123

static void
handler(int sig, siginfo_t *sip, void *scp)
{
	char buf[1024];

	if (sip == NULL)
		_exit(1);
	if (sip->si_addr == 0)		/* wrong address */
		_exit(1);

	// snprintf(buf, sizeof buf, "addr %p\n", sip->si_addr);
	// write(STDOUT_FILENO, buf, strlen(buf));
	_exit(0);
}


int
main(int argc, char *argv[])
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sigfillset(&sa.sa_mask);
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;

	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	*(char *)FAULTADDR = 0;
}
