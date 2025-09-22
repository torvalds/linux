/* Public domain */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void foo(void);
void (*foobar)(void) = foo;

void
bar(void)
{
	foobar();
}

void
handler(int sig, siginfo_t *si, void *context)
{
	if (si->si_signo == SIGILL && si->si_code == ILL_BTCFI)
		exit(0);
}

#if defined(__amd64__)

static int
has_btcfi(void)
{
	uint32_t d;

	asm("cpuid" : "=d" (d) : "a" (7), "c" (0));
	return (d & (1U << 20)) ? 1 : 0;
}

#elif defined(__aarch64__)

#include <sys/types.h>
#include <sys/sysctl.h>

#include <machine/armreg.h>
#include <machine/cpu.h>

static int
has_btcfi(void)
{
	int mib[] = { CTL_MACHDEP, CPU_ID_AA64PFR1 };
	uint64_t id_aa64pfr1 = 0;
	size_t size = sizeof(id_aa64pfr1);

	sysctl(mib, 2, &id_aa64pfr1, &size, NULL, 0);
	return ID_AA64PFR1_BT(id_aa64pfr1) >= ID_AA64PFR1_BT_IMPL;
}

#else

static int
has_btcfi(void)
{
	return 0;
}

#endif

int
main(void)
{
	struct sigaction sa;

	if (!has_btcfi()) {
		printf("Unsupported CPU\n");
		printf("SKIPPED\n");
		exit(0);
	}

	sa.sa_sigaction = handler;
	sa.sa_mask = 0;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGILL, &sa, NULL);

	bar();
	exit(1);
}
