#include <signal.h>
#include "subcmd-util.h"
#include "sigchain.h"

#define SIGCHAIN_MAX_SIGNALS 32

struct sigchain_signal {
	sigchain_fun *old;
	int n;
	int alloc;
};
static struct sigchain_signal signals[SIGCHAIN_MAX_SIGNALS];

static void check_signum(int sig)
{
	if (sig < 1 || sig >= SIGCHAIN_MAX_SIGNALS)
		die("BUG: signal out of range: %d", sig);
}

static int sigchain_push(int sig, sigchain_fun f)
{
	struct sigchain_signal *s = signals + sig;
	check_signum(sig);

	ALLOC_GROW(s->old, s->n + 1, s->alloc);
	s->old[s->n] = signal(sig, f);
	if (s->old[s->n] == SIG_ERR)
		return -1;
	s->n++;
	return 0;
}

int sigchain_pop(int sig)
{
	struct sigchain_signal *s = signals + sig;
	check_signum(sig);
	if (s->n < 1)
		return 0;

	if (signal(sig, s->old[s->n - 1]) == SIG_ERR)
		return -1;
	s->n--;
	return 0;
}

void sigchain_push_common(sigchain_fun f)
{
	sigchain_push(SIGINT, f);
	sigchain_push(SIGHUP, f);
	sigchain_push(SIGTERM, f);
	sigchain_push(SIGQUIT, f);
	sigchain_push(SIGPIPE, f);
}
