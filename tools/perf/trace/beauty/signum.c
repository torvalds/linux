#include <signal.h>

static size_t syscall_arg__scnprintf_signum(char *bf, size_t size, struct syscall_arg *arg)
{
	int sig = arg->val;

	switch (sig) {
#define	P_SIGNUM(n) case SIG##n: return scnprintf(bf, size, #n)
	P_SIGNUM(HUP);
	P_SIGNUM(INT);
	P_SIGNUM(QUIT);
	P_SIGNUM(ILL);
	P_SIGNUM(TRAP);
	P_SIGNUM(ABRT);
	P_SIGNUM(BUS);
	P_SIGNUM(FPE);
	P_SIGNUM(KILL);
	P_SIGNUM(USR1);
	P_SIGNUM(SEGV);
	P_SIGNUM(USR2);
	P_SIGNUM(PIPE);
	P_SIGNUM(ALRM);
	P_SIGNUM(TERM);
	P_SIGNUM(CHLD);
	P_SIGNUM(CONT);
	P_SIGNUM(STOP);
	P_SIGNUM(TSTP);
	P_SIGNUM(TTIN);
	P_SIGNUM(TTOU);
	P_SIGNUM(URG);
	P_SIGNUM(XCPU);
	P_SIGNUM(XFSZ);
	P_SIGNUM(VTALRM);
	P_SIGNUM(PROF);
	P_SIGNUM(WINCH);
	P_SIGNUM(IO);
	P_SIGNUM(PWR);
	P_SIGNUM(SYS);
#ifdef SIGEMT
	P_SIGNUM(EMT);
#endif
#ifdef SIGSTKFLT
	P_SIGNUM(STKFLT);
#endif
#ifdef SIGSWI
	P_SIGNUM(SWI);
#endif
	default: break;
	}

	return scnprintf(bf, size, "%#x", sig);
}

#define SCA_SIGNUM syscall_arg__scnprintf_signum
