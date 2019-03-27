/* $FreeBSD$ */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int stop_received;
int exit_received;
int cont_received;

void
job_handler(int sig, siginfo_t *si, void *ctx)
{
	int status;
	int ret;

	if (si->si_code == CLD_STOPPED) {
		printf("%d: stop received\n", si->si_pid);
		stop_received = 1;
		kill(si->si_pid, SIGCONT);
	} else if (si->si_code == CLD_EXITED) {
		printf("%d: exit received\n", si->si_pid);
		ret = waitpid(si->si_pid, &status, 0);
		if (ret == -1)
			errx(1, "waitpid");
		if (!WIFEXITED(status))
			errx(1, "!WIFEXITED(status)");
		exit_received = 1;
	} else if (si->si_code == CLD_CONTINUED) {
		printf("%d: cont received\n", si->si_pid);
		cont_received = 1;
	}
}

void
job_control_test(void)
{
	struct sigaction sa;
	pid_t pid;
	int count = 10;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = job_handler;
	sigaction(SIGCHLD, &sa, NULL);
	stop_received = 0;
	cont_received = 0;
	exit_received = 0;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		printf("child %d\n", getpid());
		kill(getpid(), SIGSTOP);
		sleep(2);
		exit(1);
	}

	while (!(cont_received && stop_received && exit_received)) {
		sleep(1);
		if (--count == 0)
			break;
	}
	if (!(cont_received && stop_received && exit_received))
		errx(1, "job signals lost");

	printf("job control test OK.\n");
}

void
rtsig_handler(int sig, siginfo_t *si, void *ctx)
{
}

int
main()
{
	struct sigaction sa;
	sigset_t set;
	union sigval val;

	/* test job control with empty signal queue */
	job_control_test();

	/* now full fill signal queue in kernel */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = rtsig_handler;
	sigaction(SIGRTMIN, &sa, NULL);
	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	sigprocmask(SIG_BLOCK, &set, NULL);
	val.sival_int = 1;
	while (sigqueue(getpid(), SIGRTMIN, val))
		;

	/* signal queue is fully filled, test the job control again. */
	job_control_test();
	return (0);
}
