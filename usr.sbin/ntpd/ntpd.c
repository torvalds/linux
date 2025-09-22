/*	$OpenBSD: ntpd.c,v 1.143 2025/08/20 10:40:21 henning Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2012 Mike Miller <mmiller@mgm51.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <tls.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

#include "ntpd.h"

void		sighdlr(int);
__dead void	usage(void);
int		auto_preconditions(const struct ntpd_conf *);
int		main(int, char *[]);
void		check_child(void);
int		dispatch_imsg(struct ntpd_conf *, int, char **);
void		reset_adjtime(void);
int		ntpd_adjtime(double);
void		ntpd_adjfreq(double, int);
void		ntpd_settime(double);
void		readfreq(void);
int		writefreq(double);
void		ctl_main(int, char*[]);
const char     *ctl_lookup_option(char *, const char **);
void		show_status_msg(struct imsg *);
void		show_peer_msg(struct imsg *, int);
void		show_sensor_msg(struct imsg *, int);

volatile sig_atomic_t	 quit = 0;
volatile sig_atomic_t	 reconfig = 0;
volatile sig_atomic_t	 sigchld = 0;
struct imsgbuf		*ibuf;
int			 timeout = INFTIM;

extern u_int		 constraint_cnt;

const char		*showopt;

static const char *ctl_showopt_list[] = {
	"peers", "Sensors", "status", "all", NULL
};

void
sighdlr(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		quit = 1;
		break;
	case SIGCHLD:
		sigchld = 1;
		break;
	case SIGHUP:
		reconfig = 1;
		break;
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	if (strcmp(__progname, "ntpctl") == 0)
		fprintf(stderr,
		    "usage: ntpctl -s all | peers | Sensors | status\n");
	else
		fprintf(stderr, "usage: %s [-dnv] [-f file]\n",
		    __progname);
	exit(1);
}

int
auto_preconditions(const struct ntpd_conf *cnf)
{
	int mib[2] = { CTL_KERN, KERN_SECURELVL };
	int constraints, securelevel;
	size_t sz = sizeof(int);

	if (sysctl(mib, 2, &securelevel, &sz, NULL, 0) == -1)
		err(1, "sysctl");
	constraints = !TAILQ_EMPTY(&cnf->constraints);
	return !cnf->settime && (constraints || cnf->trusted_peers ||
	    conf->trusted_sensors) && securelevel == 0;
}

#define POLL_MAX		8
#define PFD_PIPE		0
#define PFD_MAX			1

int
main(int argc, char *argv[])
{
	struct ntpd_conf	 lconf;
	struct pollfd		*pfd = NULL;
	pid_t			 pid;
	const char		*conffile;
	int			 ch, nfds, i, j;
	int			 pipe_chld[2];
	extern char		*__progname;
	u_int			 pfd_elms = 0, new_cnt;
	struct constraint	*cstr;
	struct passwd		*pw;
	void			*newp;
	int			argc0 = argc, logdest;
	char			**argv0 = argv;
	char			*pname = NULL;
	time_t			 settime_deadline;
	int			 sopt = 0;

	if (strcmp(__progname, "ntpctl") == 0) {
		ctl_main(argc, argv);
		/* NOTREACHED */
	}

	conffile = CONFFILE;

	memset(&lconf, 0, sizeof(lconf));

	while ((ch = getopt(argc, argv, "df:nP:sSv")) != -1) {
		switch (ch) {
		case 'd':
			lconf.debug = 1;
			break;
		case 'f':
			conffile = optarg;
			break;
		case 'n':
			lconf.debug = 1;
			lconf.noaction = 1;
			break;
		case 'P':
			pname = optarg;
			break;
		case 's':
		case 'S':
			sopt = ch;
			break;
		case 'v':
			lconf.verbose++;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	/* log to stderr until daemonized */
	logdest = LOG_TO_STDERR;
	if (!lconf.debug)
		logdest |= LOG_TO_SYSLOG;

	log_init(logdest, lconf.verbose, LOG_DAEMON);

	if (sopt) {
		log_warnx("-%c option no longer works and will be removed soon.",
		    sopt);
		log_warnx("Please reconfigure to use constraints or trusted servers.");
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (parse_config(conffile, &lconf))
		exit(1);

	if (lconf.noaction) {
		fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	if (geteuid())
		errx(1, "need root privileges");

	if ((pw = getpwnam(NTPD_USER)) == NULL)
		errx(1, "unknown user %s", NTPD_USER);

	lconf.automatic = auto_preconditions(&lconf);
	if (lconf.automatic)
		lconf.settime = 1;

	if (pname != NULL) {
		/* Remove our proc arguments, so child doesn't need to. */
		if (sanitize_argv(&argc0, &argv0) == -1)
			fatalx("sanitize_argv");

		if (strcmp(NTP_PROC_NAME, pname) == 0)
			ntp_main(&lconf, pw, argc0, argv0);
		else if (strcmp(NTPDNS_PROC_NAME, pname) == 0)
			ntp_dns(&lconf, pw);
		else if (strcmp(CONSTRAINT_PROC_NAME, pname) == 0)
			priv_constraint_child(pw->pw_dir, pw->pw_uid,
			    pw->pw_gid);
		else
			fatalx("%s: invalid process name '%s'", __func__,
			    pname);

		fatalx("%s: process '%s' failed", __func__, pname);
	} else {
		if ((control_check(CTLSOCKET)) == -1)
			fatalx("ntpd already running");
	}

	if (setpriority(PRIO_PROCESS, 0, -20) == -1)
		warn("can't set priority");
	reset_adjtime();

	logdest = lconf.debug ? LOG_TO_STDERR : LOG_TO_SYSLOG;
	if (!lconf.settime) {
		log_init(logdest, lconf.verbose, LOG_DAEMON);
		if (!lconf.debug)
			if (daemon(1, 0))
				fatal("daemon");
	} else {
		settime_deadline = getmonotime();
		timeout = 100;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, PF_UNSPEC,
	    pipe_chld) == -1)
		fatal("socketpair");

	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	signal(SIGCHLD, sighdlr);

	/* fork child process */
	start_child(NTP_PROC_NAME, pipe_chld[1], argc0, argv0);

	log_procinit("[priv]");
	readfreq();

	signal(SIGTERM, sighdlr);
	signal(SIGINT, sighdlr);
	signal(SIGHUP, sighdlr);

	constraint_purge();

	if ((ibuf = malloc(sizeof(struct imsgbuf))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(ibuf, pipe_chld[0]) == -1)
		fatal(NULL);

	constraint_cnt = 0;

	/*
	 * Constraint processes are forked with certificates in memory,
	 * then privdrop into chroot before speaking to the outside world.
	 */
	if (unveil("/usr/sbin/ntpd", "x") == -1)
		err(1, "unveil /usr/sbin/ntpd");
	if (pledge("stdio settime proc exec", NULL) == -1)
		err(1, "pledge");

	while (quit == 0) {
		new_cnt = PFD_MAX + constraint_cnt;
		if (new_cnt > pfd_elms) {
			if ((newp = reallocarray(pfd, new_cnt,
			    sizeof(*pfd))) == NULL) {
				/* panic for now */
				log_warn("could not resize pfd from %u -> "
				    "%u entries", pfd_elms, new_cnt);
				fatalx("exiting");
			}
			pfd = newp;
			pfd_elms = new_cnt;
		}

		memset(pfd, 0, sizeof(*pfd) * pfd_elms);
		pfd[PFD_PIPE].fd = ibuf->fd;
		pfd[PFD_PIPE].events = POLLIN;
		if (imsgbuf_queuelen(ibuf) > 0)
			pfd[PFD_PIPE].events |= POLLOUT;

		i = PFD_MAX;
		TAILQ_FOREACH(cstr, &conf->constraints, entry) {
			pfd[i].fd = cstr->fd;
			pfd[i].events = POLLIN;
			i++;
		}

		if ((nfds = poll(pfd, i, timeout)) == -1)
			if (errno != EINTR) {
				log_warn("poll error");
				quit = 1;
			}

		if (nfds == 0 && lconf.settime &&
		    getmonotime() > settime_deadline + SETTIME_TIMEOUT) {
			lconf.settime = 0;
			timeout = INFTIM;
			log_init(logdest, lconf.verbose, LOG_DAEMON);
			log_warnx("no reply received in time, skipping initial "
			    "time setting");
			if (!lconf.debug)
				if (daemon(1, 0))
					fatal("daemon");
		}

		if (nfds > 0 && (pfd[PFD_PIPE].revents & POLLOUT))
			if (imsgbuf_write(ibuf) == -1) {
				log_warn("pipe write error (to child)");
				quit = 1;
			}

		if (nfds > 0 && pfd[PFD_PIPE].revents & POLLIN) {
			nfds--;
			if (dispatch_imsg(&lconf, argc0, argv0) == -1)
				quit = 1;
		}

		for (j = PFD_MAX; nfds > 0 && j < i; j++) {
			nfds -= priv_constraint_dispatch(&pfd[j]);
		}

		if (sigchld) {
			check_child();
			sigchld = 0;
		}
	}

	signal(SIGCHLD, SIG_DFL);

	/* Close socket and start shutdown. */
	close(ibuf->fd);

	do {
		if ((pid = wait(NULL)) == -1 &&
		    errno != EINTR && errno != ECHILD)
			fatal("wait");
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	imsgbuf_clear(ibuf);
	free(ibuf);
	log_info("Terminating");
	return (0);
}

void
check_child(void)
{
	int	 status;
	pid_t	 pid;

	do {
		pid = waitpid(WAIT_ANY, &status, WNOHANG);
		if (pid <= 0)
			continue;

		priv_constraint_check_child(pid, status);
	} while (pid > 0 || (pid == -1 && errno == EINTR));
}

int
dispatch_imsg(struct ntpd_conf *lconf, int argc, char **argv)
{
	struct imsg		 imsg;
	int			 n;
	double			 d;

	if (imsgbuf_read(ibuf) != 1)
		return (-1);

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			return (-1);

		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_ADJTIME:
			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(d))
				fatalx("invalid IMSG_ADJTIME received");
			memcpy(&d, imsg.data, sizeof(d));
			n = ntpd_adjtime(d);
			if (n == -1)
				fatalx("IMSG_ADJTIME with invalid value");
			imsg_compose(ibuf, IMSG_ADJTIME, 0, 0, -1,
			     &n, sizeof(n));
			break;
		case IMSG_ADJFREQ:
			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(d))
				fatalx("invalid IMSG_ADJFREQ received");
			memcpy(&d, imsg.data, sizeof(d));
			ntpd_adjfreq(d, 1);
			break;
		case IMSG_SETTIME:
			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(d))
				fatalx("invalid IMSG_SETTIME received");
			if (!lconf->settime)
				break;
			log_init(lconf->debug ? LOG_TO_STDERR : LOG_TO_SYSLOG,
			    lconf->verbose, LOG_DAEMON);
			memcpy(&d, imsg.data, sizeof(d));
			ntpd_settime(d);
			/* daemonize now */
			if (!lconf->debug)
				if (daemon(1, 0))
					fatal("daemon");
			lconf->settime = 0;
			timeout = INFTIM;
			break;
		case IMSG_CONSTRAINT_QUERY:
			priv_constraint_msg(imsg.hdr.peerid,
			    imsg.data, imsg.hdr.len - IMSG_HEADER_SIZE,
			    argc, argv);
			break;
		case IMSG_CONSTRAINT_KILL:
			priv_constraint_kill(imsg.hdr.peerid);
			break;
		default:
			break;
		}
		imsg_free(&imsg);
	}
	return (0);
}

void
reset_adjtime(void)
{
	struct timeval	tv;

	timerclear(&tv);
	if (adjtime(&tv, NULL) == -1)
		log_warn("reset adjtime failed");
}

int
ntpd_adjtime(double d)
{
	struct timeval	tv, olddelta;
	int		synced = 0;
	static int	firstadj = 1;

	d += getoffset();
	if (d >= (double)LOG_NEGLIGIBLE_ADJTIME / 1000 ||
	    d <= -1 * (double)LOG_NEGLIGIBLE_ADJTIME / 1000)
		log_info("adjusting local clock by %fs", d);
	else
		log_debug("adjusting local clock by %fs", d);
	if (d_to_tv(d, &tv) == -1)
		return (-1);
	if (adjtime(&tv, &olddelta) == -1)
		log_warn("adjtime failed");
	else if (!firstadj && olddelta.tv_sec == 0 && olddelta.tv_usec == 0)
		synced = 1;
	firstadj = 0;
	return (synced);
}

void
ntpd_adjfreq(double relfreq, int wrlog)
{
	int64_t curfreq;
	double ppmfreq;
	int r;

	if (adjfreq(NULL, &curfreq) == -1) {
		log_warn("adjfreq failed");
		return;
	}

	/*
	 * adjfreq's unit is ns/s shifted left 32; convert relfreq to
	 * that unit before adding. We log values in part per million.
	 */
	curfreq += relfreq * 1e9 * (1LL << 32);
	r = writefreq(curfreq / 1e9 / (1LL << 32));
	ppmfreq = relfreq * 1e6;
	if (wrlog) {
		if (ppmfreq >= LOG_NEGLIGIBLE_ADJFREQ ||
		    ppmfreq <= -LOG_NEGLIGIBLE_ADJFREQ)
			log_info("adjusting clock frequency by %f to %fppm%s",
			    ppmfreq, curfreq / 1e3 / (1LL << 32),
			    r ? "" : " (no drift file)");
		else
			log_debug("adjusting clock frequency by %f to %fppm%s",
			    ppmfreq, curfreq / 1e3 / (1LL << 32),
			    r ? "" : " (no drift file)");
	}

	if (adjfreq(&curfreq, NULL) == -1)
		log_warn("adjfreq failed");
}

void
ntpd_settime(double d)
{
	struct timeval	tv, curtime;
	char		buf[80];
	time_t		tval;

	if (d == 0)
		return;

	if (gettimeofday(&curtime, NULL) == -1) {
		log_warn("gettimeofday");
		return;
	}
	if (d_to_tv(d, &tv) == -1) {
		log_warn("ntpd_settime: invalid value");
		return;
	}
	curtime.tv_usec += tv.tv_usec + 1000000;
	curtime.tv_sec += tv.tv_sec - 1 + (curtime.tv_usec / 1000000);
	curtime.tv_usec %= 1000000;

	if (settimeofday(&curtime, NULL) == -1) {
		log_warn("settimeofday");
		return;
	}
	tval = curtime.tv_sec;
	strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Z %Y",
	    localtime(&tval));
	log_info("set local clock to %s (offset %fs)", buf, d);
}

static FILE *freqfp;

void
readfreq(void)
{
	int64_t current;
	int fd;
	double d;

	fd = open(DRIFTFILE, O_RDWR);
	if (fd == -1) {
		log_warnx("creating new %s", DRIFTFILE);
		current = 0;
		if (adjfreq(&current, NULL) == -1)
			log_warn("adjfreq reset failed");
		freqfp = fopen(DRIFTFILE, "w");
		return;
	}

	freqfp = fdopen(fd, "r+");

	/* if we're adjusting frequency already, don't override */
	if (adjfreq(NULL, &current) == -1)
		log_warn("adjfreq failed");
	else if (current == 0 && freqfp) {
		if (fscanf(freqfp, "%lf", &d) == 1) {
			d /= 1e6;	/* scale from ppm */
			ntpd_adjfreq(d, 0);
		} else
			log_warnx("%s is empty", DRIFTFILE);
	}
}

int
writefreq(double d)
{
	int r;
	static int warnonce = 1;

	if (freqfp == NULL)
		return 0;
	rewind(freqfp);
	r = fprintf(freqfp, "%.3f\n", d * 1e6);	/* scale to ppm */
	if (r < 0 || fflush(freqfp) != 0) {
		if (warnonce) {
			log_warnx("can't write %s", DRIFTFILE);
			warnonce = 0;
		}
		clearerr(freqfp);
		return 0;
	}
	ftruncate(fileno(freqfp), ftello(freqfp));
	fsync(fileno(freqfp));
	return 1;
}

void
ctl_main(int argc, char *argv[])
{
	struct sockaddr_un	 sa;
	struct imsg		 imsg;
	struct imsgbuf		*ibuf_ctl;
	int			 fd, n, done, ch, action;
	char			*sockname;

	sockname = CTLSOCKET;

	if (argc < 2) {
		usage();
		/* NOTREACHED */
	}

	while ((ch = getopt(argc, argv, "s:")) != -1) {
		switch (ch) {
		case 's':
			showopt = ctl_lookup_option(optarg, ctl_showopt_list);
			if (showopt == NULL) {
				warnx("Unknown show modifier '%s'", optarg);
				usage();
			}
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	action = -1;
	if (showopt != NULL) {
		switch (*showopt) {
		case 'p':
			action = CTL_SHOW_PEERS;
			break;
		case 's':
			action = CTL_SHOW_STATUS;
			break;
		case 'S':
			action = CTL_SHOW_SENSORS;
			break;
		case 'a':
			action = CTL_SHOW_ALL;
			break;
		}
	}
	if (action == -1)
		usage();
		/* NOTREACHED */

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "ntpctl: socket");

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	if (strlcpy(sa.sun_path, sockname, sizeof(sa.sun_path)) >=
	    sizeof(sa.sun_path))
		errx(1, "ctl socket name too long");
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
		err(1, "connect: %s", sockname);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if ((ibuf_ctl = malloc(sizeof(struct imsgbuf))) == NULL)
		err(1, NULL);
	if (imsgbuf_init(ibuf_ctl, fd) == -1)
		err(1, NULL);

	switch (action) {
	case CTL_SHOW_STATUS:
		imsg_compose(ibuf_ctl, IMSG_CTL_SHOW_STATUS,
		    0, 0, -1, NULL, 0);
		break;
	case CTL_SHOW_PEERS:
		imsg_compose(ibuf_ctl, IMSG_CTL_SHOW_PEERS,
		    0, 0, -1, NULL, 0);
		break;
	case CTL_SHOW_SENSORS:
		imsg_compose(ibuf_ctl, IMSG_CTL_SHOW_SENSORS,
		    0, 0, -1, NULL, 0);
		break;
	case CTL_SHOW_ALL:
		imsg_compose(ibuf_ctl, IMSG_CTL_SHOW_ALL,
		    0, 0, -1, NULL, 0);
		break;
	default:
		errx(1, "invalid action");
		break; /* NOTREACHED */
	}

	if (imsgbuf_flush(ibuf_ctl) == -1)
		err(1, "write error");

	done = 0;
	while (!done) {
		if ((n = imsgbuf_read(ibuf_ctl)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");

		while (!done) {
			if ((n = imsg_get(ibuf_ctl, &imsg)) == -1)
				err(1, "ibuf_ctl: imsg_get error");
			if (n == 0)
				break;

			switch (action) {
			case CTL_SHOW_STATUS:
				show_status_msg(&imsg);
				done = 1;
				break;
			case CTL_SHOW_PEERS:
				show_peer_msg(&imsg, 0);
				if (imsg.hdr.type ==
				    IMSG_CTL_SHOW_PEERS_END)
					done = 1;
				break;
			case CTL_SHOW_SENSORS:
				show_sensor_msg(&imsg, 0);
				if (imsg.hdr.type ==
				    IMSG_CTL_SHOW_SENSORS_END)
					done = 1;
				break;
			case CTL_SHOW_ALL:
				switch (imsg.hdr.type) {
				case IMSG_CTL_SHOW_STATUS:
					show_status_msg(&imsg);
					break;
				case IMSG_CTL_SHOW_PEERS:
					show_peer_msg(&imsg, 1);
					break;
				case IMSG_CTL_SHOW_SENSORS:
					show_sensor_msg(&imsg, 1);
					break;
				case IMSG_CTL_SHOW_PEERS_END:
				case IMSG_CTL_SHOW_SENSORS_END:
					/* do nothing */
					break;
				case IMSG_CTL_SHOW_ALL_END:
					done=1;
					break;
				default:
					/* no action taken */
					break;
				}
			default:
				/* no action taken */
				break;
			}
			imsg_free(&imsg);
		}
	}
	close(fd);
	free(ibuf_ctl);
	exit(0);
}

const char *
ctl_lookup_option(char *cmd, const char **list)
{
	const char *item = NULL;
	if (cmd != NULL && *cmd)
		for (; *list; list++)
			if (!strncmp(cmd, *list, strlen(cmd))) {
				if (item == NULL)
					item = *list;
				else
					errx(1, "%s is ambiguous", cmd);
			}
	return (item);
}

void
show_status_msg(struct imsg *imsg)
{
	struct ctl_show_status	*cstatus;
	double			 clock_offset;
	struct timeval		 tv;

	if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(struct ctl_show_status))
		fatalx("invalid IMSG_CTL_SHOW_STATUS received");

	cstatus = (struct ctl_show_status *)imsg->data;

	if (cstatus->peercnt > 0)
		printf("%d/%d peers valid, ",
		    cstatus->valid_peers, cstatus->peercnt);

	if (cstatus->sensorcnt > 0)
		printf("%d/%d sensors valid, ",
		    cstatus->valid_sensors, cstatus->sensorcnt);

	if (cstatus->constraint_median) {
		tv.tv_sec = cstatus->constraint_median +
		    (getmonotime() - cstatus->constraint_last);
		tv.tv_usec = 0;
		d_to_tv(gettime_from_timeval(&tv) - gettime(), &tv);
		printf("constraint offset %llds", (long long)tv.tv_sec);
		if (cstatus->constraint_errors)
			printf(" (%d errors)",
			    cstatus->constraint_errors);
		printf(", ");
	} else if (cstatus->constraints)
		printf("constraints configured but none available, ");

	if (cstatus->peercnt + cstatus->sensorcnt == 0)
		printf("no peers and no sensors configured\n");

	if (cstatus->synced == 1)
		printf("clock synced, stratum %u\n", cstatus->stratum);
	else {
		printf("clock unsynced");
		clock_offset = cstatus->clock_offset < 0 ?
		    -1.0 * cstatus->clock_offset : cstatus->clock_offset;
		if (clock_offset > 5e-7)
			printf(", clock offset is %.3fms\n",
			    cstatus->clock_offset);
		else
			printf("\n");
	}
}

void
show_peer_msg(struct imsg *imsg, int calledfromshowall)
{
	struct ctl_show_peer	*cpeer;
	int			 cnt;
	char			 stratum[3];
	static int		 firsttime = 1;

	if (imsg->hdr.type == IMSG_CTL_SHOW_PEERS_END) {
		if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(cnt))
			fatalx("invalid IMSG_CTL_SHOW_PEERS_END received");
		memcpy(&cnt, imsg->data, sizeof(cnt));
		if (cnt == 0)
			printf("no peers configured\n");
		return;
	}

	if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(struct ctl_show_peer))
		fatalx("invalid IMSG_CTL_SHOW_PEERS received");

	cpeer = (struct ctl_show_peer *)imsg->data;

	if (strlen(cpeer->peer_desc) > MAX_DISPLAY_WIDTH - 1)
		fatalx("peer_desc is too long");

	if (firsttime) {
		firsttime = 0;
		if (calledfromshowall)
			printf("\n");
		printf("peer\n   wt tl st  next  poll          "
		    "offset       delay      jitter\n");
	}

	if (cpeer->stratum > 0)
		snprintf(stratum, sizeof(stratum), "%2u", cpeer->stratum);
	else
		strlcpy(stratum, " -", sizeof (stratum));

	printf("%s\n %1s %2u %2u %2s %4llds %4llds",
	    cpeer->peer_desc, cpeer->syncedto == 1 ? "*" : " ",
	    cpeer->weight, cpeer->trustlevel, stratum,
	    (long long)cpeer->next, (long long)cpeer->poll);

	if (cpeer->trustlevel >= TRUSTLEVEL_BADPEER)
		printf("  %12.3fms %9.3fms  %8.3fms\n", cpeer->offset,
		    cpeer->delay, cpeer->jitter);
	else
		printf("             ---- peer not valid ----\n");

}

void
show_sensor_msg(struct imsg *imsg, int calledfromshowall)
{
	struct ctl_show_sensor	*csensor;
	int			 cnt;
	static int		 firsttime = 1;

	if (imsg->hdr.type == IMSG_CTL_SHOW_SENSORS_END) {
		if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(cnt))
			fatalx("invalid IMSG_CTL_SHOW_SENSORS_END received");
		memcpy(&cnt, imsg->data, sizeof(cnt));
		if (cnt == 0)
			printf("no sensors configured\n");
		return;
	}

	if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(struct ctl_show_sensor))
		fatalx("invalid IMSG_CTL_SHOW_SENSORS received");

	csensor = (struct ctl_show_sensor *)imsg->data;

	if (strlen(csensor->sensor_desc) > MAX_DISPLAY_WIDTH - 1)
		fatalx("sensor_desc is too long");

	if (firsttime) {
		firsttime = 0;
		if (calledfromshowall)
			printf("\n");
		printf("sensor\n   wt gd st  next  poll          "
		    "offset  correction\n");
	}

	printf("%s\n %1s %2u %2u %2u %4llds %4llds",
	    csensor->sensor_desc, csensor->syncedto == 1 ? "*" : " ",
	    csensor->weight, csensor->good, csensor->stratum,
	    (long long)csensor->next, (long long)csensor->poll);

	if (csensor->good == 1)
		printf("   %11.3fms %9.3fms\n",
		    csensor->offset, csensor->correction);
	else
		printf("         - sensor not valid -\n");

}
