#include <linux/signal.h>

#define SIGUNKNOWN 0
#define MAXMAPPED_SIG 35
/* provide a mapping of arch signal to internal signal # for mediation
 * those that are always an alias SIGCLD for SIGCLHD and SIGPOLL for SIGIO
 * map to the same entry those that may/or may not get a separate entry
 */
static const int sig_map[MAXMAPPED_SIG] = {
	[0] = MAXMAPPED_SIG,	/* existence test */
	[SIGHUP] = 1,
	[SIGINT] = 2,
	[SIGQUIT] = 3,
	[SIGILL] = 4,
	[SIGTRAP] = 5,		/* -, 5, - */
	[SIGABRT] = 6,		/*  SIGIOT: -, 6, - */
	[SIGBUS] = 7,		/* 10, 7, 10 */
	[SIGFPE] = 8,
	[SIGKILL] = 9,
	[SIGUSR1] = 10,		/* 30, 10, 16 */
	[SIGSEGV] = 11,
	[SIGUSR2] = 12,		/* 31, 12, 17 */
	[SIGPIPE] = 13,
	[SIGALRM] = 14,
	[SIGTERM] = 15,
#ifdef SIGSTKFLT
	[SIGSTKFLT] = 16,	/* -, 16, - */
#endif
	[SIGCHLD] = 17,		/* 20, 17, 18.  SIGCHLD -, -, 18 */
	[SIGCONT] = 18,		/* 19, 18, 25 */
	[SIGSTOP] = 19,		/* 17, 19, 23 */
	[SIGTSTP] = 20,		/* 18, 20, 24 */
	[SIGTTIN] = 21,		/* 21, 21, 26 */
	[SIGTTOU] = 22,		/* 22, 22, 27 */
	[SIGURG] = 23,		/* 16, 23, 21 */
	[SIGXCPU] = 24,		/* 24, 24, 30 */
	[SIGXFSZ] = 25,		/* 25, 25, 31 */
	[SIGVTALRM] = 26,	/* 26, 26, 28 */
	[SIGPROF] = 27,		/* 27, 27, 29 */
	[SIGWINCH] = 28,	/* 28, 28, 20 */
	[SIGIO] = 29,		/* SIGPOLL: 23, 29, 22 */
	[SIGPWR] = 30,		/* 29, 30, 19.  SIGINFO 29, -, - */
#ifdef SIGSYS
	[SIGSYS] = 31,		/* 12, 31, 12. often SIG LOST/UNUSED */
#endif
#ifdef SIGEMT
	[SIGEMT] = 32,		/* 7, - , 7 */
#endif
#if defined(SIGLOST) && SIGPWR != SIGLOST		/* sparc */
	[SIGLOST] = 33,		/* unused on Linux */
#endif
#if defined(SIGUNUSED) && \
    defined(SIGLOST) && defined(SIGSYS) && SIGLOST != SIGSYS
	[SIGUNUSED] = 34,	/* -, 31, - */
#endif
};

/* this table is ordered post sig_map[sig] mapping */
static const char *const sig_names[MAXMAPPED_SIG + 1] = {
	"unknown",
	"hup",
	"int",
	"quit",
	"ill",
	"trap",
	"abrt",
	"bus",
	"fpe",
	"kill",
	"usr1",
	"segv",
	"usr2",
	"pipe",
	"alrm",
	"term",
	"stkflt",
	"chld",
	"cont",
	"stop",
	"stp",
	"ttin",
	"ttou",
	"urg",
	"xcpu",
	"xfsz",
	"vtalrm",
	"prof",
	"winch",
	"io",
	"pwr",
	"sys",
	"emt",
	"lost",
	"unused",

	"exists",	/* always last existence test mapped to MAXMAPPED_SIG */
};

