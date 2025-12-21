/*
 * signal.c: Register a sigaltstack for objtool, to be able to
 *	     run a signal handler on a separate stack even if
 *	     the main process stack has overflown. Print out
 *	     stack overflow errors when this happens.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <string.h>

#include <objtool/objtool.h>
#include <objtool/warn.h>

static unsigned long stack_limit;

static bool is_stack_overflow(void *fault_addr)
{
	unsigned long fault = (unsigned long)fault_addr;

	/* Check if fault is in the guard page just below the limit. */
	return fault < stack_limit && fault >= stack_limit - 4096;
}

static void signal_handler(int sig_num, siginfo_t *info, void *context)
{
	struct sigaction sa_dfl = {0};
	const char *sig_name;
	char msg[256];
	int msg_len;

	switch (sig_num) {
	case SIGSEGV:	sig_name = "SIGSEGV";		break;
	case SIGBUS:	sig_name = "SIGBUS";		break;
	case SIGILL:	sig_name = "SIGILL";		break;
	case SIGABRT:	sig_name = "SIGABRT";		break;
	default:	sig_name = "Unknown signal";	break;
	}

	if (is_stack_overflow(info->si_addr)) {
		msg_len = snprintf(msg, sizeof(msg),
				   "%s: error: %s: objtool stack overflow!\n",
				   objname, sig_name);
	} else {
		msg_len = snprintf(msg, sizeof(msg),
				   "%s: error: %s: objtool crash!\n",
				   objname, sig_name);
	}

	msg_len = write(STDERR_FILENO, msg, msg_len);

	/* Re-raise the signal to trigger the core dump */
	sa_dfl.sa_handler = SIG_DFL;
	sigaction(sig_num, &sa_dfl, NULL);
	raise(sig_num);
}

static int read_stack_limit(void)
{
	unsigned long stack_start, stack_end;
	struct rlimit rlim;
	char line[256];
	int ret = 0;
	FILE *fp;

	if (getrlimit(RLIMIT_STACK, &rlim)) {
		ERROR_GLIBC("getrlimit");
		return -1;
	}

	fp = fopen("/proc/self/maps", "r");
	if (!fp) {
		ERROR_GLIBC("fopen");
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		if (strstr(line, "[stack]")) {
			if (sscanf(line, "%lx-%lx", &stack_start, &stack_end) != 2) {
				ERROR_GLIBC("sscanf");
				ret = -1;
				goto done;
			}
			stack_limit = stack_end - rlim.rlim_cur;
			goto done;
		}
	}

	ret = -1;
	ERROR("/proc/self/maps: can't find [stack]");

done:
	fclose(fp);

	return ret;
}

int init_signal_handler(void)
{
	int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT};
	struct sigaction sa;
	stack_t ss;

	if (read_stack_limit())
		return -1;

	ss.ss_sp = malloc(SIGSTKSZ);
	if (!ss.ss_sp) {
		ERROR_GLIBC("malloc");
		return -1;
	}
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;

	if (sigaltstack(&ss, NULL) == -1) {
		ERROR_GLIBC("sigaltstack");
		return -1;
	}

	sa.sa_sigaction = signal_handler;
	sigemptyset(&sa.sa_mask);

	sa.sa_flags = SA_ONSTACK | SA_SIGINFO;

	for (int i = 0; i < ARRAY_SIZE(signals); i++) {
		if (sigaction(signals[i], &sa, NULL) == -1) {
			ERROR_GLIBC("sigaction");
			return -1;
		}
	}

	return 0;
}
