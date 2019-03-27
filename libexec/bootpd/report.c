/* $FreeBSD$ */

/*
 * report() - calls syslog
 */

#include <stdarg.h>

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

#include "report.h"

#ifndef LOG_NDELAY
#define LOG_NDELAY	0
#endif
#ifndef LOG_DAEMON
#define LOG_DAEMON	0
#endif
#ifndef	LOG_BOOTP
#define LOG_BOOTP	LOG_DAEMON
#endif

extern int debug;
extern char *progname;

/*
 * This is initialized so you get stderr until you call
 *	report_init()
 */
static int stderr_only = 1;

void
report_init(nolog)
	int nolog;
{
	stderr_only = nolog;
#ifdef SYSLOG
	if (!stderr_only) {
		openlog(progname, LOG_PID | LOG_NDELAY, LOG_BOOTP);
	}
#endif
}

/*
 * This routine reports errors and such via stderr and syslog() if
 * appopriate.  It just helps avoid a lot of "#ifdef SYSLOG" constructs
 * from being scattered throughout the code.
 *
 * The syntax is identical to syslog(3), but %m is not considered special
 * for output to stderr (i.e. you'll see "%m" in the output. . .).  Also,
 * control strings should normally end with \n since newlines aren't
 * automatically generated for stderr output (whereas syslog strips out all
 * newlines and adds its own at the end).
 */

static char *levelnames[] = {
#ifdef LOG_SALERT
	"level(0): ",
	"alert(1): ",
	"alert(2): ",
	"emerg(3): ",
	"error(4): ",
	"crit(5):  ",
	"warn(6):  ",
	"note(7):  ",
	"info(8):  ",
	"debug(9): ",
	"level(?): "
#else
	"emerg(0): ",
	"alert(1): ",
	"crit(2):  ",
	"error(3): ",
	"warn(4):  ",
	"note(5):  ",
	"info(6):  ",
	"debug(7): ",
	"level(?): "
#endif
};
static int numlevels = sizeof(levelnames) / sizeof(levelnames[0]);


/*
 * Print a log message using syslog(3) and/or stderr.
 * The message passed in should not include a newline.
 */
void
report(int priority, const char *fmt,...)
{
	va_list ap;
	static char buf[128];

	if ((priority < 0) || (priority >= numlevels)) {
		priority = numlevels - 1;
	}
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/*
	 * Print the message
	 */
	if (stderr_only || (debug > 2)) {
		fprintf(stderr, "%s: %s %s\n",
				progname, levelnames[priority], buf);
	}
#ifdef SYSLOG
	if (!stderr_only)
		syslog((priority | LOG_BOOTP), "%s", buf);
#endif
}



/*
 * Return pointer to static string which gives full filesystem error message.
 */
const char *
get_errmsg()
{
	return strerror(errno);
}

/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-argdecl-indent: 4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: -4
 * c-label-offset: -4
 * c-brace-offset: 0
 * End:
 */
