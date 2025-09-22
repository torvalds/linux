/*	$OpenBSD: sigaltstack_fork.c,v 1.2 2011/11/26 04:11:34 guenther Exp $	*/

/*
 * Public domain.  2011, Joshua Elsasser
 *
 * Test if child processes inherit an alternate signal stack.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void
check_stack(void *buf, const char *label)
{
	struct sigaltstack ss;

	bzero(&ss, sizeof(ss));
	if (sigaltstack(NULL, &ss) != 0)
		err(1, "failed to get sigaltstack in %s", label);
	if (ss.ss_sp != buf ||
	    ss.ss_size != SIGSTKSZ ||
	    ss.ss_flags != 0)
		errx(1, "bad sigaltstack value in %s: "
		    "buf=%p ss_sp=%p ss_size=%zu ss_flags=0x%x",
		    label, buf, ss.ss_sp, ss.ss_size, ss.ss_flags);
}

int
main(int argc, char *argv[])
{
	struct sigaltstack ss;
	int status;
	pid_t kid;
	void *buf;

	if ((buf = malloc(SIGSTKSZ)) == NULL)
		err(1, "malloc failed");

	bzero(&ss, sizeof(ss));
	ss.ss_sp = buf;
	ss.ss_size = SIGSTKSZ;
	if (sigaltstack(&ss, NULL) != 0)
		err(1, "failed to set sigaltstack");

	check_stack(buf, "parent");

	if ((kid = fork()) == -1)
		err(1, "fork failed");

	if (kid == 0) {
		check_stack(buf, "child");
		_exit(0);
	}

	if (waitpid(kid, &status, 0) != kid)
		err(1, "waitpid failed");
	if (!WIFEXITED(status))
		errx(1, "child did not exit normally");

	return (WEXITSTATUS(status));
}
