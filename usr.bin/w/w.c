/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1991, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)w.c	8.4 (Berkeley) 4/16/94";
#endif

/*
 * w - print system status (who and what)
 *
 * This program is similar to the systat command on Tenex/Tops 10/20
 *
 */
#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/tty.h>
#include <sys/types.h>

#include <machine/cpu.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <langinfo.h>
#include <libgen.h>
#include <libutil.h>
#include <limits.h>
#include <locale.h>
#include <netdb.h>
#include <nlist.h>
#include <paths.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timeconv.h>
#include <unistd.h>
#include <utmpx.h>
#include <vis.h>
#include <libxo/xo.h>

#include "extern.h"

static struct utmpx *utmp;
static struct winsize ws;
static kvm_t   *kd;
static time_t	now;		/* the current time of day */
static int	ttywidth;	/* width of tty */
static int	argwidth;	/* width of tty */
static int	header = 1;	/* true if -h flag: don't print heading */
static int	nflag;		/* true if -n flag: don't convert addrs */
static int	dflag;		/* true if -d flag: output debug info */
static int	sortidle;	/* sort by idle time */
int		use_ampm;	/* use AM/PM time */
static int	use_comma;      /* use comma as floats separator */
static char   **sel_users;	/* login array of particular users selected */

/*
 * One of these per active utmp entry.
 */
static struct entry {
	struct	entry *next;
	struct	utmpx utmp;
	dev_t	tdev;			/* dev_t of terminal */
	time_t	idle;			/* idle time of terminal in seconds */
	struct	kinfo_proc *kp;		/* `most interesting' proc */
	char	*args;			/* arg list of interesting process */
	struct	kinfo_proc *dkp;	/* debug option proc list */
} *ep, *ehead = NULL, **nextp = &ehead;

#define	debugproc(p) *(&((struct kinfo_proc *)p)->ki_udata)

#define	W_DISPUSERSIZE	10
#define	W_DISPLINESIZE	8
#define	W_DISPHOSTSIZE	40

static void		 pr_header(time_t *, int);
static struct stat	*ttystat(char *);
static void		 usage(int);

char *fmt_argv(char **, char *, char *, size_t);	/* ../../bin/ps/fmt.c */

int
main(int argc, char *argv[])
{
	struct kinfo_proc *kp;
	struct kinfo_proc *dkp;
	struct stat *stp;
	time_t touched;
	int ch, i, nentries, nusers, wcmd, longidle, longattime;
	const char *memf, *nlistf, *p, *save_p;
	char *x_suffix;
	char buf[MAXHOSTNAMELEN], errbuf[_POSIX2_LINE_MAX];
	char fn[MAXHOSTNAMELEN];
	char *dot;

	(void)setlocale(LC_ALL, "");
	use_ampm = (*nl_langinfo(T_FMT_AMPM) != '\0');
	use_comma = (*nl_langinfo(RADIXCHAR) != ',');

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		exit(1);

	/* Are we w(1) or uptime(1)? */
	if (strcmp(basename(argv[0]), "uptime") == 0) {
		wcmd = 0;
		p = "";
	} else {
		wcmd = 1;
		p = "dhiflM:N:nsuw";
	}

	memf = _PATH_DEVNULL;
	nlistf = NULL;
	while ((ch = getopt(argc, argv, p)) != -1)
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'h':
			header = 0;
			break;
		case 'i':
			sortidle = 1;
			break;
		case 'M':
			header = 0;
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'n':
			nflag += 1;
			break;
		case 'f': case 'l': case 's': case 'u': case 'w':
			warnx("[-flsuw] no longer supported");
			/* FALLTHROUGH */
		case '?':
		default:
			usage(wcmd);
		}
	argc -= optind;
	argv += optind;

	if (!(_res.options & RES_INIT))
		res_init();
	_res.retrans = 2;	/* resolver timeout to 2 seconds per try */
	_res.retry = 1;		/* only try once.. */

	if ((kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, errbuf)) == NULL)
		errx(1, "%s", errbuf);

	(void)time(&now);

	if (*argv)
		sel_users = argv;

	setutxent();
	for (nusers = 0; (utmp = getutxent()) != NULL;) {
		if (utmp->ut_type != USER_PROCESS)
			continue;
		if (!(stp = ttystat(utmp->ut_line)))
			continue;	/* corrupted record */
		++nusers;
		if (wcmd == 0)
			continue;
		if (sel_users) {
			int usermatch;
			char **user;

			usermatch = 0;
			for (user = sel_users; !usermatch && *user; user++)
				if (!strcmp(utmp->ut_user, *user))
					usermatch = 1;
			if (!usermatch)
				continue;
		}
		if ((ep = calloc(1, sizeof(struct entry))) == NULL)
			errx(1, "calloc");
		*nextp = ep;
		nextp = &ep->next;
		memmove(&ep->utmp, utmp, sizeof *utmp);
		ep->tdev = stp->st_rdev;
		/*
		 * If this is the console device, attempt to ascertain
		 * the true console device dev_t.
		 */
		if (ep->tdev == 0) {
			size_t size;

			size = sizeof(dev_t);
			(void)sysctlbyname("machdep.consdev", &ep->tdev, &size, NULL, 0);
		}
		touched = stp->st_atime;
		if (touched < ep->utmp.ut_tv.tv_sec) {
			/* tty untouched since before login */
			touched = ep->utmp.ut_tv.tv_sec;
		}
		if ((ep->idle = now - touched) < 0)
			ep->idle = 0;
	}
	endutxent();

	xo_open_container("uptime-information");

	if (header || wcmd == 0) {
		pr_header(&now, nusers);
		if (wcmd == 0) {
			xo_close_container("uptime-information");
			xo_finish();

			(void)kvm_close(kd);
			exit(0);
		}

#define HEADER_USER		"USER"
#define HEADER_TTY		"TTY"
#define HEADER_FROM		"FROM"
#define HEADER_LOGIN_IDLE	"LOGIN@  IDLE "
#define HEADER_WHAT		"WHAT\n"
#define WUSED  (W_DISPUSERSIZE + W_DISPLINESIZE + W_DISPHOSTSIZE + \
		sizeof(HEADER_LOGIN_IDLE) + 3)	/* header width incl. spaces */ 
		xo_emit("{T:/%-*.*s} {T:/%-*.*s} {T:/%-*.*s}  {T:/%s}", 
				W_DISPUSERSIZE, W_DISPUSERSIZE, HEADER_USER,
				W_DISPLINESIZE, W_DISPLINESIZE, HEADER_TTY,
				W_DISPHOSTSIZE, W_DISPHOSTSIZE, HEADER_FROM,
				HEADER_LOGIN_IDLE HEADER_WHAT);
	}

	if ((kp = kvm_getprocs(kd, KERN_PROC_ALL, 0, &nentries)) == NULL)
		err(1, "%s", kvm_geterr(kd));
	for (i = 0; i < nentries; i++, kp++) {
		if (kp->ki_stat == SIDL || kp->ki_stat == SZOMB ||
		    kp->ki_tdev == NODEV)
			continue;
		for (ep = ehead; ep != NULL; ep = ep->next) {
			if (ep->tdev == kp->ki_tdev) {
				/*
				 * proc is associated with this terminal
				 */
				if (ep->kp == NULL && kp->ki_pgid == kp->ki_tpgid) {
					/*
					 * Proc is 'most interesting'
					 */
					if (proc_compare(ep->kp, kp))
						ep->kp = kp;
				}
				/*
				 * Proc debug option info; add to debug
				 * list using kinfo_proc ki_spare[0]
				 * as next pointer; ptr to ptr avoids the
				 * ptr = long assumption.
				 */
				dkp = ep->dkp;
				ep->dkp = kp;
				debugproc(kp) = dkp;
			}
		}
	}
	if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 &&
	     ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == -1 &&
	     ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) || ws.ws_col == 0)
	       ttywidth = 79;
        else
	       ttywidth = ws.ws_col - 1;
	argwidth = ttywidth - WUSED;
	if (argwidth < 4)
		argwidth = 8;
	for (ep = ehead; ep != NULL; ep = ep->next) {
		if (ep->kp == NULL) {
			ep->args = strdup("-");
			continue;
		}
		ep->args = fmt_argv(kvm_getargv(kd, ep->kp, argwidth),
		    ep->kp->ki_comm, NULL, MAXCOMLEN);
		if (ep->args == NULL)
			err(1, NULL);
	}
	/* sort by idle time */
	if (sortidle && ehead != NULL) {
		struct entry *from, *save;

		from = ehead;
		ehead = NULL;
		while (from != NULL) {
			for (nextp = &ehead;
			    (*nextp) && from->idle >= (*nextp)->idle;
			    nextp = &(*nextp)->next)
				continue;
			save = from;
			from = from->next;
			save->next = *nextp;
			*nextp = save;
		}
	}

	xo_open_container("user-table");
	xo_open_list("user-entry");

	for (ep = ehead; ep != NULL; ep = ep->next) {
		struct addrinfo hints, *res;
		struct sockaddr_storage ss;
		struct sockaddr *sa = (struct sockaddr *)&ss;
		struct sockaddr_in *lsin = (struct sockaddr_in *)&ss;
		struct sockaddr_in6 *lsin6 = (struct sockaddr_in6 *)&ss;
		time_t t;
		int isaddr;

		xo_open_instance("user-entry");

		save_p = p = *ep->utmp.ut_host ? ep->utmp.ut_host : "-";
		if ((x_suffix = strrchr(p, ':')) != NULL) {
			if ((dot = strchr(x_suffix, '.')) != NULL &&
			    strchr(dot+1, '.') == NULL)
				*x_suffix++ = '\0';
			else
				x_suffix = NULL;
		}

		isaddr = 0;
		memset(&ss, '\0', sizeof(ss));
		if (inet_pton(AF_INET6, p, &lsin6->sin6_addr) == 1) {
			lsin6->sin6_len = sizeof(*lsin6);
			lsin6->sin6_family = AF_INET6;
			isaddr = 1;
		} else if (inet_pton(AF_INET, p, &lsin->sin_addr) == 1) {
			lsin->sin_len = sizeof(*lsin);
			lsin->sin_family = AF_INET;
			isaddr = 1;
		}
		if (nflag == 0) {
			/* Attempt to change an IP address into a name */
			if (isaddr && realhostname_sa(fn, sizeof(fn), sa,
			    sa->sa_len) == HOSTNAME_FOUND)
				p = fn;
		} else if (!isaddr && nflag > 1) {
			/*
			 * If a host has only one A/AAAA RR, change a
			 * name into an IP address
			 */
			memset(&hints, 0, sizeof(hints));
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			if (getaddrinfo(p, NULL, &hints, &res) == 0) {
				if (res->ai_next == NULL &&
				    getnameinfo(res->ai_addr, res->ai_addrlen,
					fn, sizeof(fn), NULL, 0,
					NI_NUMERICHOST) == 0)
					p = fn;
				freeaddrinfo(res);
			}
		}

		if (x_suffix) {
			(void)snprintf(buf, sizeof(buf), "%s:%s", p, x_suffix);
			p = buf;
		}
		if (dflag) {
		        xo_open_container("process-table");
		        xo_open_list("process-entry");

			for (dkp = ep->dkp; dkp != NULL; dkp = debugproc(dkp)) {
				const char *ptr;

				ptr = fmt_argv(kvm_getargv(kd, dkp, argwidth),
				    dkp->ki_comm, NULL, MAXCOMLEN);
				if (ptr == NULL)
					ptr = "-";
				xo_open_instance("process-entry");
				xo_emit("\t\t{:process-id/%-9d/%d} {:command/%s}\n",
				    dkp->ki_pid, ptr);
				xo_close_instance("process-entry");
			}
		        xo_close_list("process-entry");
		        xo_close_container("process-table");
		}
		xo_emit("{:user/%-*.*s/%@**@s} {:tty/%-*.*s/%@**@s} ",
			W_DISPUSERSIZE, W_DISPUSERSIZE, ep->utmp.ut_user,
			W_DISPLINESIZE, W_DISPLINESIZE,
			*ep->utmp.ut_line ?
			(strncmp(ep->utmp.ut_line, "tty", 3) &&
			 strncmp(ep->utmp.ut_line, "cua", 3) ?
			 ep->utmp.ut_line : ep->utmp.ut_line + 3) : "-");

		if (save_p && save_p != p)
		    xo_attr("address", "%s", save_p);
		xo_emit("{:from/%-*.*s/%@**@s} ",
		    W_DISPHOSTSIZE, W_DISPHOSTSIZE, *p ? p : "-");
		t = ep->utmp.ut_tv.tv_sec;
		longattime = pr_attime(&t, &now);
		longidle = pr_idle(ep->idle);
		xo_emit("{:command/%.*s/%@*@s}\n",
		    argwidth - longidle - longattime,
		    ep->args);

		xo_close_instance("user-entry");
	}

	xo_close_list("user-entry");
	xo_close_container("user-table");
	xo_close_container("uptime-information");
	xo_finish();

	(void)kvm_close(kd);
	exit(0);
}

static void
pr_header(time_t *nowp, int nusers)
{
	double avenrun[3];
	time_t uptime;
	struct timespec tp;
	int days, hrs, i, mins, secs;
	char buf[256];
	struct sbuf *upbuf;

	upbuf = sbuf_new_auto();
	/*
	 * Print time of day.
	 */
	if (strftime(buf, sizeof(buf),
	    use_ampm ? "%l:%M%p" : "%k:%M", localtime(nowp)) != 0)
		xo_emit("{:time-of-day/%s} ", buf);
	/*
	 * Print how long system has been up.
	 */
	if (clock_gettime(CLOCK_UPTIME, &tp) != -1) {
		uptime = tp.tv_sec;
		if (uptime > 60)
			uptime += 30;
		days = uptime / 86400;
		uptime %= 86400;
		hrs = uptime / 3600;
		uptime %= 3600;
		mins = uptime / 60;
		secs = uptime % 60;
		xo_emit(" up");
		xo_emit("{e:uptime/%lu}", (unsigned long) tp.tv_sec);
		xo_emit("{e:days/%d}{e:hours/%d}{e:minutes/%d}{e:seconds/%d}", days, hrs, mins, secs);

		if (days > 0)
			sbuf_printf(upbuf, " %d day%s,",
				days, days > 1 ? "s" : "");
		if (hrs > 0 && mins > 0)
			sbuf_printf(upbuf, " %2d:%02d,", hrs, mins);
		else if (hrs > 0)
			sbuf_printf(upbuf, " %d hr%s,",
				hrs, hrs > 1 ? "s" : "");
		else if (mins > 0)
			sbuf_printf(upbuf, " %d min%s,",
				mins, mins > 1 ? "s" : "");
		else 
			sbuf_printf(upbuf, " %d sec%s,",
				secs, secs > 1 ? "s" : "");
		if (sbuf_finish(upbuf) != 0)
			xo_err(1, "Could not generate output");
		xo_emit("{:uptime-human/%s}", sbuf_data(upbuf));
		sbuf_delete(upbuf);
	}

	/* Print number of users logged in to system */
	xo_emit(" {:users/%d} {Np:user,users}", nusers);

	/*
	 * Print 1, 5, and 15 minute load averages.
	 */
	if (getloadavg(avenrun, nitems(avenrun)) == -1)
		xo_emit(", no load average information available\n");
	else {
	        static const char *format[] = {
		    " {:load-average-1/%.2f}",
		    " {:load-average-5/%.2f}",
		    " {:load-average-15/%.2f}",
		};
		xo_emit(", load averages:");
		for (i = 0; i < (int)(nitems(avenrun)); i++) {
			if (use_comma && i > 0)
				xo_emit(",");
			xo_emit(format[i], avenrun[i]);
		}
		xo_emit("\n");
	}
}

static struct stat *
ttystat(char *line)
{
	static struct stat sb;
	char ttybuf[MAXPATHLEN];

	(void)snprintf(ttybuf, sizeof(ttybuf), "%s%s", _PATH_DEV, line);
	if (stat(ttybuf, &sb) == 0 && S_ISCHR(sb.st_mode)) {
		return (&sb);
	} else
		return (NULL);
}

static void
usage(int wcmd)
{
	if (wcmd)
		xo_error("usage: w [-dhin] [-M core] [-N system] [user ...]\n");
	else
		xo_error("usage: uptime\n");
	xo_finish();
	exit(1);
}
