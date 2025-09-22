/*	$OpenBSD: main.c,v 1.59 2024/08/23 00:43:34 millert Exp $	*/

/*
 * main.c - Point-to-Point Protocol main module
 *
 * Copyright (c) 1984-2000 Carnegie Mellon University. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any legal
 *    details, please contact
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <netdb.h>
#include <utmp.h>
#include <pwd.h>

#include "pppd.h"
#include "magic.h"
#include "fsm.h"
#include "lcp.h"
#include "ipcp.h"
#include "upap.h"
#include "chap.h"
#include "ccp.h"
#include "pathnames.h"
#include "patchlevel.h"

#ifdef CBCP_SUPPORT
#include "cbcp.h"
#endif

#if defined(SUNOS4)
extern char *strerror();
#endif

#ifdef AT_CHANGE
#include "atcp.h"
#endif

/* interface vars */
char ifname[IFNAMSIZ];		/* Interface name */
int ifunit;			/* Interface unit number */

char hostname[HOST_NAME_MAX+1];	/* Our hostname */
static char default_devnam[PATH_MAX];	/* name of default device */
static pid_t pid;		/* Our pid */
static uid_t uid;		/* Our real user-id */
static int conn_running;	/* we have a [dis]connector running */
static volatile sig_atomic_t crashed = 0;

int ttyfd = -1;			/* Serial port file descriptor */
mode_t tty_mode = -1;		/* Original access permissions to tty */
int baud_rate;			/* Actual bits/second for serial device */
int hungup;			/* terminal has been hung up */
int privileged;			/* we're running as real uid root */
int need_holdoff;		/* need holdoff period before restarting */
int detached;			/* have detached from terminal */

int phase;			/* where the link is at */
volatile sig_atomic_t kill_link;
volatile sig_atomic_t open_ccp_flag;
volatile sig_atomic_t got_sigchld;

char **script_env;		/* Env. variable values for scripts */
int s_env_nalloc;		/* # words avail at script_env */

u_char outpacket_buf[PPP_MRU+PPP_HDRLEN]; /* buffer for outgoing packet */
u_char inpacket_buf[PPP_MRU+PPP_HDRLEN]; /* buffer for incoming packet */

static int locked;		/* lock() has succeeded */

char *no_ppp_msg = "Sorry - this system lacks PPP kernel support\n";

/* Prototypes for procedures local to this file. */

static void cleanup(void);
static void close_tty(void);
static void get_input(void);
static void calltimeout(void);
static struct timeval *timeleft(struct timeval *);
static void kill_my_pg(int);
static void hup(int);
static void term(int);
static void chld(int);
static void toggle_debug(int);
static void open_ccp(int);
static void bad_signal(int);
static void holdoff_end(void *);
static int device_script(char *, int, int);
static void reap_kids(void);
static void pr_log(void *, char *, ...);

extern	char	*ttyname(int);
extern	char	*getlogin(void);
int main(int, char *[]);

#ifdef ultrix
#undef	O_NONBLOCK
#define	O_NONBLOCK	O_NDELAY
#endif

#ifdef ULTRIX
#define setlogmask(x)
#endif

/*
 * PPP Data Link Layer "protocol" table.
 * One entry per supported protocol.
 * The last entry must be NULL.
 */
struct protent *protocols[] = {
    &lcp_protent,
    &pap_protent,
    &chap_protent,
#ifdef CBCP_SUPPORT
    &cbcp_protent,
#endif
    &ipcp_protent,
    &ccp_protent,
#ifdef AT_CHANGE
    &atcp_protent,
#endif
    NULL
};

int
main(int argc, char *argv[])
{
    int i, fdflags;
    struct sigaction sa;
    char *p;
    struct passwd *pw;
    struct timeval timo;
    sigset_t mask;
    struct protent *protp;
    struct stat statbuf;
    char numbuf[16];

    phase = PHASE_INITIALIZE;
    p = ttyname(0);
    if (p)
	strlcpy(devnam, p, PATH_MAX);
    strlcpy(default_devnam, devnam, sizeof default_devnam);

    script_env = NULL;

    /* Initialize syslog facilities */
#ifdef ULTRIX
    openlog("pppd", LOG_PID);
#else
    openlog("pppd", LOG_PID | LOG_NDELAY, LOG_PPP);
    setlogmask(LOG_UPTO(LOG_INFO));
#endif

    if (gethostname(hostname, sizeof hostname) < 0 ) {
	option_error("Couldn't get hostname: %m");
	die(1);
    }

    uid = getuid();
    privileged = uid == 0;
    snprintf(numbuf, sizeof numbuf, "%u", uid);
    script_setenv("UID", numbuf);

    /*
     * Initialize to the standard option set, then parse, in order,
     * the system options file, the user's options file,
     * the tty's options file, and the command line arguments.
     */
    for (i = 0; (protp = protocols[i]) != NULL; ++i)
	(*protp->init)(0);

    if (!options_from_file(_PATH_SYSOPTIONS, !privileged, 0, 1)
	|| !options_from_user())
	exit(1);
    scan_args(argc-1, argv+1);	/* look for tty name on command line */
    if (!options_for_tty()
	|| !parse_args(argc-1, argv+1))
	exit(1);

    /*
     * Check that we are running as root.
     */
    if (geteuid() != 0) {
	option_error("must be root to run %s, since it is not setuid-root",
		     argv[0]);
	die(1);
    }

    if (!ppp_available()) {
	option_error(no_ppp_msg);
	exit(1);
    }

    /*
     * Check that the options given are valid and consistent.
     */
    sys_check_options();
    auth_check_options();
    for (i = 0; (protp = protocols[i]) != NULL; ++i)
	if (protp->check_options != NULL)
	    (*protp->check_options)();
    if (demand && connector == 0) {
	option_error("connect script required for demand-dialling\n");
	exit(1);
    }

    script_setenv("DEVICE", devnam);
    snprintf(numbuf, sizeof numbuf, "%d", baud_rate);
    script_setenv("SPEED", numbuf);

    /*
     * If the user has specified the default device name explicitly,
     * pretend they hadn't.
     */
    if (!default_device && strcmp(devnam, default_devnam) == 0)
	default_device = 1;
    if (default_device)
	nodetach = 1;

    /*
     * Initialize system-dependent stuff and magic number package.
     */
    sys_init();
    magic_init();
    if (debug)
	setlogmask(LOG_UPTO(LOG_DEBUG));

    /*
     * Detach ourselves from the terminal, if required,
     * and identify who is running us.
     */
    if (nodetach == 0)
	detach();
    pid = getpid();
    p = getlogin();
    if (p == NULL) {
	pw = getpwuid(uid);
	if (pw != NULL && pw->pw_name != NULL)
	    p = pw->pw_name;
	else
	    p = "(unknown)";
    }
    syslog(LOG_NOTICE, "pppd %s.%d%s started by %s, uid %u",
	   VERSION, PATCHLEVEL, IMPLEMENTATION, p, uid);

    /*
     * Compute mask of all interesting signals and install signal handlers
     * for each.  Only one signal handler may be active at a time.  Therefore,
     * all other signals should be masked when any handler is executing.
     */
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);

#define SIGNAL(s, handler)	{ \
	sa.sa_handler = handler; \
	if (sigaction(s, &sa, NULL) < 0) { \
	    syslog(LOG_ERR, "Couldn't establish signal handler (%d): %m", s); \
	    die(1); \
	} \
    }

    sa.sa_mask = mask;
    sa.sa_flags = 0;
    SIGNAL(SIGHUP, hup);		/* Hangup */
    SIGNAL(SIGINT, term);		/* Interrupt */
    SIGNAL(SIGTERM, term);		/* Terminate */
    SIGNAL(SIGCHLD, chld);

    SIGNAL(SIGUSR1, toggle_debug);	/* Toggle debug flag */
    SIGNAL(SIGUSR2, open_ccp);		/* Reopen CCP */

    /*
     * Install a handler for other signals which would otherwise
     * cause pppd to exit without cleaning up.
     */
    SIGNAL(SIGABRT, bad_signal);
    SIGNAL(SIGALRM, bad_signal);
    SIGNAL(SIGFPE, bad_signal);
    SIGNAL(SIGILL, bad_signal);
    SIGNAL(SIGPIPE, bad_signal);
    SIGNAL(SIGQUIT, bad_signal);
#if SIGSEGV_CHECK
    SIGNAL(SIGSEGV, bad_signal);
#endif
#ifdef SIGBUS
    SIGNAL(SIGBUS, bad_signal);
#endif
#ifdef SIGEMT
    SIGNAL(SIGEMT, bad_signal);
#endif
#ifdef SIGPOLL
    SIGNAL(SIGPOLL, bad_signal);
#endif
#ifdef SIGPROF
    SIGNAL(SIGPROF, bad_signal);
#endif
#ifdef SIGSYS
    SIGNAL(SIGSYS, bad_signal);
#endif
#ifdef SIGTRAP
    SIGNAL(SIGTRAP, bad_signal);
#endif
#ifdef SIGVTALRM
    SIGNAL(SIGVTALRM, bad_signal);
#endif
#ifdef SIGXCPU
    SIGNAL(SIGXCPU, bad_signal);
#endif
#ifdef SIGXFSZ
    SIGNAL(SIGXFSZ, bad_signal);
#endif

    /*
     * Apparently we can get a SIGPIPE when we call syslog, if
     * syslogd has died and been restarted.  Ignoring it seems
     * be sufficient.
     */
    signal(SIGPIPE, SIG_IGN);

    /*
     * If we're doing dial-on-demand, set up the interface now.
     */
    if (demand) {
	/*
	 * Open the loopback channel and set it up to be the ppp interface.
	 */
	open_ppp_loopback();

	syslog(LOG_INFO, "Using interface ppp%d", ifunit);
	(void) snprintf(ifname, sizeof ifname, "ppp%d", ifunit);
	script_setenv("IFNAME", ifname);

	/*
	 * Configure the interface and mark it up, etc.
	 */
	demand_conf();
    }

    for (;;) {

	need_holdoff = 1;

	if (demand) {
	    /*
	     * Don't do anything until we see some activity.
	     */
	    phase = PHASE_DORMANT;
	    kill_link = 0;
	    demand_unblock();
	    for (;;) {
		wait_loop_output(timeleft(&timo));
		calltimeout();
		if (kill_link) {
		    if (!persist)
			die(0);
		    kill_link = 0;
		}
		if (get_loop_output())
		    break;
		reap_kids();
	    }

	    /*
	     * Now we want to bring up the link.
	     */
	    demand_drop();
	    syslog(LOG_INFO, "Starting link");
	}

	/*
	 * Lock the device if we've been asked to.
	 */
	if (lockflag && !default_device) {
	    if (lock(devnam) < 0)
		goto fail;
	    locked = 1;
	}

	/*
	 * Open the serial device and set it up to be the ppp interface.
	 * First we open it in non-blocking mode so we can set the
	 * various termios flags appropriately.  If we aren't dialling
	 * out and we want to use the modem lines, we reopen it later
	 * in order to wait for the carrier detect signal from the modem.
	 */
	while ((ttyfd = open(devnam, O_NONBLOCK | O_RDWR)) < 0) {
	    if (errno != EINTR)
		syslog(LOG_ERR, "Failed to open %s: %m", devnam);
	    if (!persist || errno != EINTR)
		goto fail;
	}
	if ((fdflags = fcntl(ttyfd, F_GETFL)) == -1
	    || fcntl(ttyfd, F_SETFL, fdflags & ~O_NONBLOCK) < 0)
	    syslog(LOG_WARNING,
		   "Couldn't reset non-blocking mode on device: %m");

	hungup = 0;
	kill_link = 0;

	/*
	 * Do the equivalent of `mesg n' to stop broadcast messages.
	 */
	if (fstat(ttyfd, &statbuf) < 0
	    || fchmod(ttyfd, statbuf.st_mode & ~(S_IWGRP | S_IWOTH)) < 0) {
	    syslog(LOG_WARNING,
		   "Couldn't restrict write permissions to %s: %m", devnam);
	} else
	    tty_mode = statbuf.st_mode;

	/* run connection script */
	if (connector && connector[0]) {
	    MAINDEBUG((LOG_INFO, "Connecting with <%s>", connector));

	    /*
	     * Set line speed, flow control, etc.
	     * On most systems we set CLOCAL for now so that we can talk
	     * to the modem before carrier comes up.  But this has the
	     * side effect that we might miss it if CD drops before we
	     * get to clear CLOCAL below.  On systems where we can talk
	     * successfully to the modem with CLOCAL clear and CD down,
	     * we can clear CLOCAL at this point.
	     */
	    set_up_tty(ttyfd, (modem_chat == 0));

	    /* drop dtr to hang up in case modem is off hook */
	    if (!default_device && modem) {
		setdtr(ttyfd, FALSE);
		sleep(1);
		setdtr(ttyfd, TRUE);
	    }

	    if (device_script(connector, ttyfd, ttyfd) < 0) {
		syslog(LOG_ERR, "Connect script failed");
		setdtr(ttyfd, FALSE);
		goto fail;
	    }

	    syslog(LOG_INFO, "Serial connection established.");
	    sleep(1);		/* give it time to set up its terminal */
	}

	set_up_tty(ttyfd, 0);

	/* reopen tty if necessary to wait for carrier */
	if (connector == NULL && modem) {
	    while ((i = open(devnam, O_RDWR)) < 0) {
		if (errno != EINTR)
		    syslog(LOG_ERR, "Failed to reopen %s: %m", devnam);
		if (!persist || errno != EINTR || hungup || kill_link)
		    goto fail;
	    }
	    close(i);
	}

	/* run welcome script, if any */
	if (welcomer && welcomer[0]) {
	    if (device_script(welcomer, ttyfd, ttyfd) < 0)
		syslog(LOG_WARNING, "Welcome script failed");
	}

	/* set up the serial device as a ppp interface */
	establish_ppp(ttyfd);

	if (!demand) {

	    syslog(LOG_INFO, "Using interface ppp%d", ifunit);
	    (void) snprintf(ifname, sizeof ifname, "ppp%d", ifunit);
	    script_setenv("IFNAME", ifname);
	}

	/*
	 * Start opening the connection and wait for
	 * incoming events (reply, timeout, etc.).
	 */
	syslog(LOG_NOTICE, "Connect: %s <--> %s", ifname, devnam);
	lcp_lowerup(0);
	lcp_open(0);		/* Start protocol */
	for (phase = PHASE_ESTABLISH; phase != PHASE_DEAD; ) {
	    wait_input(timeleft(&timo));
	    calltimeout();
	    get_input();
	    if (kill_link) {
		lcp_close(0, "User request");
		kill_link = 0;
	    }
	    if (open_ccp_flag) {
		if (phase == PHASE_NETWORK) {
		    ccp_fsm[0].flags = OPT_RESTART; /* clears OPT_SILENT */
		    (*ccp_protent.open)(0);
		}
		open_ccp_flag = 0;
	    }
	    reap_kids();	/* Don't leave dead kids lying around */
	}

	/*
	 * If we may want to bring the link up again, transfer
	 * the ppp unit back to the loopback.  Set the
	 * real serial device back to its normal mode of operation.
	 */
	clean_check();
	if (demand)
	    restore_loop();
	disestablish_ppp(ttyfd);

	/*
	 * Run disconnector script, if requested.
	 * XXX we may not be able to do this if the line has hung up!
	 */
	if (disconnector && !hungup) {
	    set_up_tty(ttyfd, 1);
	    if (device_script(disconnector, ttyfd, ttyfd) < 0) {
		syslog(LOG_WARNING, "disconnect script failed");
	    } else {
		syslog(LOG_INFO, "Serial link disconnected.");
	    }
	}

    fail:
	if (ttyfd >= 0)
	    close_tty();
	if (locked) {
	    unlock();
	    locked = 0;
	}

	if (!persist)
	    die(1);

	if (holdoff > 0 && need_holdoff) {
	    phase = PHASE_HOLDOFF;
	    TIMEOUT(holdoff_end, NULL, holdoff);
	    do {
		wait_time(timeleft(&timo));
		calltimeout();
		if (kill_link) {
		    if (!persist)
			die(0);
		    kill_link = 0;
		    phase = PHASE_DORMANT; /* allow signal to end holdoff */
		}
		reap_kids();
	    } while (phase == PHASE_HOLDOFF);
	}
    }

    die(0);
    return 0;
}

/*
 * detach - detach us from the controlling terminal.
 */
void
detach(void)
{
    if (detached)
	return;
    if (daemon(0, 0) < 0) {
	perror("Couldn't detach from controlling terminal");
	die(1);
    }
    detached = 1;
    pid = getpid();
}

/*
 * holdoff_end - called via a timeout when the holdoff period ends.
 */
static void
holdoff_end(void *arg)
{
    phase = PHASE_DORMANT;
}

/*
 * get_input - called when incoming data is available.
 */
static void
get_input(void)
{
    int len, i;
    u_char *p;
    u_short protocol;
    struct protent *protp;

    p = inpacket_buf;	/* point to beginning of packet buffer */

    len = read_packet(inpacket_buf);
    if (len < 0)
	return;

    if (len == 0) {
	syslog(LOG_NOTICE, "Modem hangup");
	hungup = 1;
	lcp_lowerdown(0);	/* serial link is no longer available */
	link_terminated(0);
	return;
    }

    if (debug /*&& (debugflags & DBG_INPACKET)*/)
	log_packet(p, len, "rcvd ", LOG_DEBUG);

    if (len < PPP_HDRLEN) {
	MAINDEBUG((LOG_INFO, "io(): Received short packet."));
	return;
    }

    p += 2;				/* Skip address and control */
    GETSHORT(protocol, p);
    len -= PPP_HDRLEN;

    /*
     * Toss all non-LCP packets unless LCP is OPEN.
     */
    if (protocol != PPP_LCP && lcp_fsm[0].state != OPENED) {
	MAINDEBUG((LOG_INFO,
		   "get_input: Received non-LCP packet when LCP not open."));
	return;
    }

    /*
     * Until we get past the authentication phase, toss all packets
     * except LCP, LQR and authentication packets.
     */
    if (phase <= PHASE_AUTHENTICATE
	&& !(protocol == PPP_LCP || protocol == PPP_LQR
	|| protocol == PPP_PAP || protocol == PPP_CHAP)) {
	MAINDEBUG((LOG_INFO, "get_input: discarding proto 0x%x in phase %d",
		   protocol, phase));
	return;
    }

    /*
     * Upcall the proper protocol input routine.
     */
    for (i = 0; (protp = protocols[i]) != NULL; ++i) {
	if (protp->protocol == protocol && protp->enabled_flag) {
	    (*protp->input)(0, p, len);
	    return;
	}
	if (protocol == (protp->protocol & ~0x8000) && protp->enabled_flag
	    && protp->datainput != NULL) {
	    (*protp->datainput)(0, p, len);
	    return;
	}
    }

    if (debug)
    	syslog(LOG_WARNING, "Unsupported protocol (0x%x) received", protocol);
    lcp_sprotrej(0, p - PPP_HDRLEN, len + PPP_HDRLEN);
}


/*
 * quit - Clean up state and exit (with an error indication).
 */
void
quit(void)
{
    die(1);
}

/*
 * die - like quit, except we can specify an exit status.
 */
void
die(int status)
{
    struct syslog_data sdata = SYSLOG_DATA_INIT;

    cleanup();
    syslog_r(LOG_INFO, &sdata, "Exit.");
    _exit(status);
}

/*
 * cleanup - restore anything which needs to be restored before we exit
 */
static void
cleanup(void)
{
    sys_cleanup();

    if (ttyfd >= 0)
	close_tty();

    if (locked)
	unlock();
}

/*
 * close_tty - restore the terminal device and close it.
 */
static void
close_tty(void)
{
    disestablish_ppp(ttyfd);

    /* drop dtr to hang up */
    if (modem) {
	setdtr(ttyfd, FALSE);
	/*
	 * This sleep is in case the serial port has CLOCAL set by default,
	 * and consequently will reassert DTR when we close the device.
	 */
	sleep(1);
    }

    restore_tty(ttyfd);

    if (tty_mode != (mode_t) -1)
	fchmod(ttyfd, tty_mode);

    close(ttyfd);
    ttyfd = -1;
}


struct	callout {
    struct timeval	c_time;		/* time at which to call routine */
    void		*c_arg;		/* argument to routine */
    void		(*c_func)(void *); /* routine */
    struct		callout *c_next;
};

static struct callout *callout = NULL;	/* Callout list */
static struct timeval timenow;		/* Current time */

/*
 * timeout - Schedule a timeout.
 *
 * Note that this timeout takes the number of seconds, NOT hz (as in
 * the kernel).
 */
void
timeout(void (*func)(void *), void *arg, int time)
{
    struct callout *newp, *p, **pp;

    MAINDEBUG((LOG_DEBUG, "Timeout %lx:%lx in %d seconds.",
	       (long) func, (long) arg, time));

    /*
     * Allocate timeout.
     */
    if ((newp = (struct callout *) malloc(sizeof(struct callout))) == NULL) {
	syslog(LOG_ERR, "Out of memory in timeout()!");
	die(1);
    }
    newp->c_arg = arg;
    newp->c_func = func;
    gettimeofday(&timenow, NULL);
    newp->c_time.tv_sec = timenow.tv_sec + time;
    newp->c_time.tv_usec = timenow.tv_usec;

    /*
     * Find correct place and link it in.
     */
    for (pp = &callout; (p = *pp); pp = &p->c_next)
	if (newp->c_time.tv_sec < p->c_time.tv_sec
	    || (newp->c_time.tv_sec == p->c_time.tv_sec
		&& newp->c_time.tv_usec < p->c_time.tv_sec))
	    break;
    newp->c_next = p;
    *pp = newp;
}


/*
 * untimeout - Unschedule a timeout.
 */
void
untimeout(void (*func)(void *), void *arg)
{
    struct callout **copp, *freep;

    MAINDEBUG((LOG_DEBUG, "Untimeout %lx:%lx.", (long) func, (long) arg));

    /*
     * Find first matching timeout and remove it from the list.
     */
    for (copp = &callout; (freep = *copp); copp = &freep->c_next)
	if (freep->c_func == func && freep->c_arg == arg) {
	    *copp = freep->c_next;
	    (void) free((char *) freep);
	    break;
	}
}


/*
 * calltimeout - Call any timeout routines which are now due.
 */
static void
calltimeout(void)
{
    struct callout *p;

    while (callout != NULL) {
	p = callout;

	if (gettimeofday(&timenow, NULL) < 0) {
	    syslog(LOG_ERR, "Failed to get time of day: %m");
	    die(1);
	}
	if (!(p->c_time.tv_sec < timenow.tv_sec
	      || (p->c_time.tv_sec == timenow.tv_sec
		  && p->c_time.tv_usec <= timenow.tv_usec)))
	    break;		/* no, it's not time yet */

	callout = p->c_next;
	(*p->c_func)(p->c_arg);

	free((char *) p);
    }
}


/*
 * timeleft - return the length of time until the next timeout is due.
 */
static struct timeval *
timeleft(struct timeval *tvp)
{
    if (callout == NULL)
	return NULL;

    gettimeofday(&timenow, NULL);
    tvp->tv_sec = callout->c_time.tv_sec - timenow.tv_sec;
    tvp->tv_usec = callout->c_time.tv_usec - timenow.tv_usec;
    if (tvp->tv_usec < 0) {
	tvp->tv_usec += 1000000;
	tvp->tv_sec -= 1;
    }
    if (tvp->tv_sec < 0)
	tvp->tv_sec = tvp->tv_usec = 0;

    return tvp;
}


/*
 * kill_my_pg - send a signal to our process group, and ignore it ourselves.
 */
static void
kill_my_pg(int sig)
{
    struct sigaction act, oldact;

    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;
    kill(0, sig);
    sigaction(sig, &act, &oldact);
    sigaction(sig, &oldact, NULL);
}


/*
 * hup - Catch SIGHUP signal.
 *
 * Indicates that the physical layer has been disconnected.
 * We don't rely on this indication; if the user has sent this
 * signal, we just take the link down.
 */
static void
hup(int sig)
{
    int save_errno = errno;
    struct syslog_data sdata = SYSLOG_DATA_INIT;

    if (crashed)
	_exit(127);
    syslog_r(LOG_INFO, &sdata, "Hangup (SIGHUP)");
    kill_link = 1;
    if (conn_running)
	/* Send the signal to the [dis]connector process(es) also */
	kill_my_pg(sig);
    errno = save_errno;
}


/*
 * term - Catch SIGTERM signal and SIGINT signal (^C/del).
 *
 * Indicates that we should initiate a graceful disconnect and exit.
 */
static void
term(int sig)
{
    int save_errno = errno;
    struct syslog_data sdata = SYSLOG_DATA_INIT;

    if (crashed)
	_exit(127);
    syslog_r(LOG_INFO, &sdata, "Terminating on signal %d.", sig);
    persist = 0;		/* don't try to restart */
    kill_link = 1;
    if (conn_running)
	/* Send the signal to the [dis]connector process(es) also */
	kill_my_pg(sig);
    errno = save_errno;
}


/*
 * chld - Catch SIGCHLD signal.
 * Calls reap_kids to get status for any dead kids.
 */
static void
chld(int sig)
{
    got_sigchld = 1;
}


/*
 * toggle_debug - Catch SIGUSR1 signal.
 *
 * Toggle debug flag.
 */
static void
toggle_debug(int sig)
{
    debug = !debug;
    if (debug) {
	setlogmask(LOG_UPTO(LOG_DEBUG));	/* XXX safe, but wrong */
    } else {
	setlogmask(LOG_UPTO(LOG_WARNING));	/* XXX safe, but wrong */
    }
}


/*
 * open_ccp - Catch SIGUSR2 signal.
 *
 * Try to (re)negotiate compression.
 */
static void
open_ccp(int sig)
{
    open_ccp_flag = 1;
}


/*
 * bad_signal - We've caught a fatal signal.  Clean up state and exit.
 */
static void
bad_signal(int sig)
{
    struct syslog_data sdata = SYSLOG_DATA_INIT;

    if (crashed)
	_exit(127);
    crashed = 1;
    syslog_r(LOG_ERR, &sdata, "Fatal signal %d", sig);
    if (conn_running)
	kill_my_pg(SIGTERM);
    die(1);					/* XXX unsafe! */
}


/*
 * device_script - run a program to connect or disconnect the
 * serial device.
 */
static int
device_script(char *program, int in, int out)
{
    pid_t pid;
    int status;
    int errfd;
    gid_t gid;
    uid_t uid;

    conn_running = 1;
    pid = fork();

    if (pid < 0) {
	conn_running = 0;
	syslog(LOG_ERR, "Failed to create child process: %m");
	die(1);
    }

    if (pid == 0) {
	sys_close();
	closelog();
	if (in == out) {
	    if (in != 0) {
		dup2(in, 0);
		close(in);
	    }
	    dup2(0, 1);
	} else {
	    if (out == 0)
		out = dup(out);
	    if (in != 0) {
		dup2(in, 0);
		close(in);
	    }
	    if (out != 1) {
		dup2(out, 1);
		close(out);
	    }
	}
	if (nodetach == 0) {
	    close(2);
	    errfd = open(_PATH_CONNERRS, O_WRONLY | O_APPEND | O_CREAT, 0600);
	    if (errfd >= 0 && errfd != 2) {
		dup2(errfd, 2);
		close(errfd);
	    }
	}

	/* revoke privs */
	gid = getgid();
	uid = getuid();
	if (setresgid(gid, gid, gid) == -1 || setresuid(uid, uid, uid) == -1) {
		syslog(LOG_ERR, "revoke privileges: %s", strerror(errno));
		_exit(1);
	}

	execl("/bin/sh", "sh", "-c", program, (char *)NULL);
	syslog(LOG_ERR, "could not exec /bin/sh: %m");
	_exit(99);
	/* NOTREACHED */
    }

    while (waitpid(pid, &status, 0) < 0) {
	if (errno == EINTR)
	    continue;
	syslog(LOG_ERR, "error waiting for (dis)connection process: %m");
	die(1);
    }
    conn_running = 0;

    return (status == 0 ? 0 : -1);
}


/*
 * run-program - execute a program with given arguments,
 * but don't wait for it.
 * If the program can't be executed, logs an error unless
 * must_exist is 0 and the program file doesn't exist.
 */
int
run_program(char *prog, char **args, int must_exist)
{
    pid_t pid;
    uid_t uid;
    gid_t gid;

    pid = fork();
    if (pid == -1) {
	syslog(LOG_ERR, "Failed to create child process for %s: %m", prog);
	return -1;
    }
    if (pid == 0) {
	int new_fd;

	/* Leave the current location */
	(void) setsid();    /* No controlling tty. */
	(void) umask (S_IRWXG|S_IRWXO);
	(void) chdir ("/"); /* no current directory. */

	/* revoke privs */
	uid = getuid();
	gid = getgid();
	if (setresgid(gid, gid, gid) == -1 || setresuid(uid, uid, uid) == -1) {
		syslog(LOG_ERR, "revoke privileges: %s", strerror(errno));
		_exit(1);
	}

	/* Ensure that nothing of our device environment is inherited. */
	sys_close();
	closelog();
	close (0);
	close (1);
	close (2);
	close (ttyfd);  /* tty interface to the ppp device */

	/* Don't pass handles to the PPP device, even by accident. */
	new_fd = open (_PATH_DEVNULL, O_RDWR);
	if (new_fd >= 0) {
	    if (new_fd != 0) {
		dup2  (new_fd, 0); /* stdin <- /dev/null */
		close (new_fd);
	    }
	    dup2 (0, 1); /* stdout -> /dev/null */
	    dup2 (0, 2); /* stderr -> /dev/null */
	}

	/* Force the priority back to zero if pppd is running higher. */
	if (setpriority (PRIO_PROCESS, 0, 0) < 0)
	    syslog (LOG_WARNING, "can't reset priority to 0: %m");

	/* SysV recommends a second fork at this point. */

	/* run the program; give it a null environment */
	execve(prog, args, script_env);
	if (must_exist || errno != ENOENT)
	    syslog(LOG_WARNING, "Can't execute %s: %m", prog);
	_exit(1);
    }
    MAINDEBUG((LOG_DEBUG, "Script %s started; pid = %ld", prog, (long)pid));
    return 0;
}


/*
 * reap_kids - get status from any dead child processes,
 * and log a message for abnormal terminations.
 */
static void
reap_kids(void)
{
    int status;
    pid_t pid;

    if (!got_sigchld)
	return;
    got_sigchld = 0;

    for (;;) {
	pid = waitpid(-1, &status, WNOHANG);
	switch (pid) {
	case -1:
	    if (errno == EINTR)
		continue;
	    if (errno != ECHILD)
		syslog(LOG_ERR, "Error waiting for child process: %m");
	    return;
	case 0:
	    /* No children left */
	    return;
	default:
	    if (WIFSIGNALED(status)) {
		syslog(LOG_WARNING,
		    "Child process %d terminated with signal %d",
		    (int)pid, WTERMSIG(status));
	    }
	    break;
	}
    }
}


/*
 * log_packet - format a packet and log it.
 */

char line[256];			/* line to be logged accumulated here */
char *linep;

void
log_packet(u_char *p, int len, char *prefix, int level)
{
    strlcpy(line, prefix, sizeof line);
    linep = line + strlen(line);
    format_packet(p, len, pr_log, NULL);
    if (linep != line)
	syslog(level, "%s", line);
}

/*
 * format_packet - make a readable representation of a packet,
 * calling `printer(arg, format, ...)' to output it.
 */
void
format_packet(u_char *p, int len, void (*printer)(void *, char *, ...), void *arg)
{
    int i, n;
    u_short proto;
    u_char x;
    struct protent *protp;

    if (len >= PPP_HDRLEN && p[0] == PPP_ALLSTATIONS && p[1] == PPP_UI) {
	p += 2;
	GETSHORT(proto, p);
	len -= PPP_HDRLEN;
	for (i = 0; (protp = protocols[i]) != NULL; ++i)
	    if (proto == protp->protocol)
		break;
	if (protp != NULL) {
	    printer(arg, "[%s", protp->name);
	    n = (*protp->printpkt)(p, len, printer, arg);
	    printer(arg, "]");
	    p += n;
	    len -= n;
	} else {
	    printer(arg, "[proto=0x%x]", proto);
	}
    }

    for (; len > 0; --len) {
	GETCHAR(x, p);
	printer(arg, " %.2x", x);
    }
}

static void
pr_log(void *arg, char *fmt, ...)
{
    int n;
    va_list pvar;
    char buf[256];

    va_start(pvar, fmt);

    n = vfmtmsg(buf, sizeof(buf), fmt, pvar);
    va_end(pvar);

    if (linep + n + 1 > line + sizeof(line)) {
	syslog(LOG_DEBUG, "%s", line);
	linep = line;
    }
    strlcpy(linep, buf, line + sizeof line - linep);
    linep += n;
}

/*
 * print_string - print a readable representation of a string using
 * printer.
 */
void
print_string(char *p, int len, void (*printer)(void *, char *, ...), void *arg)
{
    int c;

    printer(arg, "\"");
    for (; len > 0; --len) {
	c = *p++;
	if (' ' <= c && c <= '~') {
	    if (c == '\\' || c == '"')
		printer(arg, "\\");
	    printer(arg, "%c", c);
	} else {
	    switch (c) {
	    case '\n':
		printer(arg, "\\n");
		break;
	    case '\r':
		printer(arg, "\\r");
		break;
	    case '\t':
		printer(arg, "\\t");
		break;
	    default:
		printer(arg, "\\%.3o", c);
	    }
	}
    }
    printer(arg, "\"");
}

/*
 * novm - log an error message saying we ran out of memory, and die.
 */
void
novm(char *msg)
{
    syslog(LOG_ERR, "Virtual memory exhausted allocating %s", msg);
    die(1);
}

/*
 * fmtmsg - format a message into a buffer.  Like snprintf except we
 * also specify the length of the output buffer, and we handle
 * %m (error message) and %I (IP address) formats.
 * Doesn't do floating-point formats.
 * Returns the number of chars put into buf.
 */
int
fmtmsg(char *buf, int buflen, char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = vfmtmsg(buf, buflen, fmt, args);
    va_end(args);
    return n;
}

/*
 * vfmtmsg - like fmtmsg, takes a va_list instead of a list of args.
 */
#define OUTCHAR(c)	(buflen > 0? (--buflen, *buf++ = (c)): 0)

int
vfmtmsg(char *buf, int buflen, char *fmt, va_list args)
{
    int c, i, n;
    int width, prec, fillch;
    int base, len, neg, quoted;
    unsigned long val = 0;
    char *str, *f, *buf0;
    unsigned char *p;
    char num[32];
    time_t t;
    static char hexchars[] = "0123456789abcdef";

    buf0 = buf;
    --buflen;
    while (buflen > 0) {
	for (f = fmt; *f != '%' && *f != 0; ++f)
	    ;
	if (f > fmt) {
	    len = f - fmt;
	    if (len > buflen)
		len = buflen;
	    memcpy(buf, fmt, len);
	    buf += len;
	    buflen -= len;
	    fmt = f;
	}
	if (*fmt == 0)
	    break;
	c = *++fmt;
	width = prec = 0;
	fillch = ' ';
	if (c == '0') {
	    fillch = '0';
	    c = *++fmt;
	}
	if (c == '*') {
	    width = va_arg(args, int);
	    c = *++fmt;
	} else {
	    while (isdigit(c)) {
		width = width * 10 + c - '0';
		c = *++fmt;
	    }
	}
	if (c == '.') {
	    c = *++fmt;
	    if (c == '*') {
		prec = va_arg(args, int);
		c = *++fmt;
	    } else {
		while (isdigit(c)) {
		    prec = prec * 10 + c - '0';
		    c = *++fmt;
		}
	    }
	}
	str = 0;
	base = 0;
	neg = 0;
	++fmt;
	switch (c) {
	case 'd':
	    i = va_arg(args, int);
	    if (i < 0) {
		neg = 1;
		val = -i;
	    } else
		val = i;
	    base = 10;
	    break;
	case 'o':
	    val = va_arg(args, unsigned int);
	    base = 8;
	    break;
	case 'x':
	    val = va_arg(args, unsigned int);
	    base = 16;
	    break;
	case 'p':
	    val = (unsigned long) va_arg(args, void *);
	    base = 16;
	    neg = 2;
	    break;
	case 's':
	    str = va_arg(args, char *);
	    break;
	case 'c':
	    num[0] = va_arg(args, int);
	    num[1] = 0;
	    str = num;
	    break;
	case 'm':
	    str = strerror(errno);
	    break;
	case 'I':
	    str = ip_ntoa(va_arg(args, u_int32_t));
	    break;
	case 't':
	    time(&t);
	    str = ctime(&t);
	    str += 4;		/* chop off the day name */
	    str[15] = 0;	/* chop off year and newline */
	    break;
	case 'v':		/* "visible" string */
	case 'q':		/* quoted string */
	    quoted = c == 'q';
	    p = va_arg(args, unsigned char *);
	    if (fillch == '0' && prec > 0) {
		n = prec;
	    } else {
		n = strlen((char *)p);
		if (prec > 0 && prec < n)
		    n = prec;
	    }
	    while (n > 0 && buflen > 0) {
		c = *p++;
		--n;
		if (!quoted && c >= 0x80) {
		    OUTCHAR('M');
		    OUTCHAR('-');
		    c -= 0x80;
		}
		if (quoted && (c == '"' || c == '\\'))
		    OUTCHAR('\\');
		if (c < 0x20 || (0x7f <= c && c < 0xa0)) {
		    if (quoted) {
			OUTCHAR('\\');
			switch (c) {
			case '\t':	OUTCHAR('t');	break;
			case '\n':	OUTCHAR('n');	break;
			case '\b':	OUTCHAR('b');	break;
			case '\f':	OUTCHAR('f');	break;
			default:
			    OUTCHAR('x');
			    OUTCHAR(hexchars[c >> 4]);
			    OUTCHAR(hexchars[c & 0xf]);
			}
		    } else {
			if (c == '\t')
			    OUTCHAR(c);
			else {
			    OUTCHAR('^');
			    OUTCHAR(c ^ 0x40);
			}
		    }
		} else
		    OUTCHAR(c);
	    }
	    continue;
	default:
	    *buf++ = '%';
	    if (c != '%')
		--fmt;		/* so %z outputs %z etc. */
	    --buflen;
	    continue;
	}
	if (base != 0) {
	    str = num + sizeof(num);
	    *--str = 0;
	    while (str > num + neg) {
		*--str = hexchars[val % base];
		val = val / base;
		if (--prec <= 0 && val == 0)
		    break;
	    }
	    switch (neg) {
	    case 1:
		*--str = '-';
		break;
	    case 2:
		*--str = 'x';
		*--str = '0';
		break;
	    }
	    len = num + sizeof(num) - 1 - str;
	} else {
	    len = strlen(str);
	    if (prec > 0 && len > prec)
		len = prec;
	}
	if (width > 0) {
	    if (width > buflen)
		width = buflen;
	    if ((n = width - len) > 0) {
		buflen -= n;
		for (; n > 0; --n)
		    *buf++ = fillch;
	    }
	}
	if (len > buflen)
	    len = buflen;
	memcpy(buf, str, len);
	buf += len;
	buflen -= len;
    }
    *buf = 0;
    return buf - buf0;
}

/*
 * script_setenv - set an environment variable value to be used
 * for scripts that we run (e.g. ip-up, auth-up, etc.)
 */
void
script_setenv(char *var, char *value)
{
    int vl = strlen(var);
    int i;
    char *p, *newstring;

    if (asprintf(&newstring, "%s=%s", var, value) == -1)
	novm("script_setenv");

    /* check if this variable is already set */
    if (script_env != 0) {
	for (i = 0; (p = script_env[i]) != 0; ++i) {
	    if (strncmp(p, var, vl) == 0 && p[vl] == '=') {
		free(p);
		script_env[i] = newstring;
		return;
	    }
	}
    } else {
	i = 0;
	script_env = (char **) calloc(16, sizeof(char *));
	if (script_env == 0)
	    novm("script_setenv");
	s_env_nalloc = 16;
    }

    /* reallocate script_env with more space if needed */
    if (i + 1 >= s_env_nalloc) {
	int new_n = i + 17;
	char **newenv = reallocarray(script_env,
	    new_n, sizeof(char *));
	if (newenv == 0)
	    novm("script_setenv");
	script_env = newenv;
	s_env_nalloc = new_n;
    }

    script_env[i] = newstring;
    script_env[i+1] = 0;
}
