/*	$OpenBSD: apmd.c,v 1.113 2025/05/24 02:56:41 kn Exp $	*/

/*
 *  Copyright (c) 1995, 1996 John T. Kohl
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <machine/apmvar.h>

#include "pathnames.h"
#include "apm-proto.h"

#define AUTO_SUSPEND 1
#define AUTO_HIBERNATE 2

int debug = 0;

extern char *__progname;

void usage(void);
int power_status(int fd, int force, struct apm_power_info *pinfo);
int bind_socket(const char *sn);
void handle_client(int sock_fd, int ctl_fd);
void warnlow(void);
int suspend(int ctl_fd);
int stand_by(int ctl_fd);
int hibernate(int ctl_fd);
void resumed(int ctl_fd);
void setperfpolicy(char *policy);
void sigexit(int signo);
void do_etc_file(const char *file);
void error(const char *fmt, const char *arg);
void set_driver_messages(int fd, int mode);

void
sigexit(int signo)
{
	_exit(1);
}

void
logmsg(int prio, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	if (debug) {
		vfprintf(stderr, msg, ap);
		fprintf(stderr, "\n");
	} else {
		vsyslog(prio, msg, ap);
	}
	va_end(ap);
}

void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-AadHLs] [-f devname] [-S sockname] [-t seconds]"
	    " [-w percent]\n"
	    "\t[-Z percent] [-z percent]\n", __progname);
	exit(1);
}

void
error(const char *fmt, const char *arg)
{
	char buf[128];

	if (debug)
		err(1, fmt, arg);
	else {
		strlcpy(buf, fmt, sizeof(buf));
		strlcat(buf, ": %m", sizeof(buf));
		syslog(LOG_ERR, buf, arg);
		exit(1);
	}
}


/*
 * tell the driver if it should display messages or not.
 */
void
set_driver_messages(int fd, int mode)
{
	if (ioctl(fd, APM_IOC_PRN_CTL, &mode) == -1)
		logmsg(LOG_DEBUG, "can't disable driver messages, error: %s",
		    strerror(errno));
}

int
power_status(int fd, int force, struct apm_power_info *pinfo)
{
	struct apm_power_info bstate;
	static struct apm_power_info last;
	int acon = 0, priority = LOG_NOTICE;

	if (fd == -1) {
		if (pinfo) {
			bstate.battery_state = 255;
			bstate.ac_state = 255;
			bstate.battery_life = 0;
			bstate.minutes_left = -1;
			*pinfo = bstate;
		}

		return 0;
	}

	if (ioctl(fd, APM_IOC_GETPOWER, &bstate) == 0) {
	/* various conditions under which we report status:  something changed
	 * enough since last report, or asked to force a print */
		if (bstate.ac_state == APM_AC_ON)
			acon = 1;
		if (bstate.battery_state == APM_BATT_CRITICAL &&
		    bstate.battery_state != last.battery_state)
			priority = LOG_EMERG;
		if (force ||
		    bstate.ac_state != last.ac_state ||
		    bstate.battery_state != last.battery_state ||
		    ((bstate.battery_state != APM_BATT_CHARGING) &&
		     (bstate.minutes_left && bstate.minutes_left < 15)) ||
		    abs(bstate.battery_life - last.battery_life) >= 10) {
			if ((int)bstate.minutes_left > 0)
				logmsg(priority, "battery status: %s. "
				    "external power status: %s. "
				    "estimated battery life %d%% "
				    "(%u minutes %s time estimate)",
				    battstate(bstate.battery_state),
				    ac_state(bstate.ac_state),
				    bstate.battery_life,
				    bstate.minutes_left,
				    (bstate.battery_state == APM_BATT_CHARGING)
					? "recharge" : "life");
			else
				logmsg(priority, "battery status: %s. "
				    "external power status: %s. "
				    "estimated battery life %d%%",
				    battstate(bstate.battery_state),
				    ac_state(bstate.ac_state),
				    bstate.battery_life);
			last = bstate;
		}
		if (pinfo)
			*pinfo = bstate;
	} else
		logmsg(LOG_ERR, "cannot fetch power status: %s", strerror(errno));

	return acon;
}

int
bind_socket(const char *sockname)
{
	struct sockaddr_un s_un;
	mode_t old_umask;
	int sock;

	sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (sock == -1)
		error("cannot create local socket", NULL);

	s_un.sun_family = AF_UNIX;
	strlcpy(s_un.sun_path, sockname, sizeof(s_un.sun_path));

	/* remove it if present, we're moving in */
	(void) remove(sockname);

	old_umask = umask(077);
	if (bind(sock, (struct sockaddr *)&s_un, sizeof(s_un)) == -1)
		error("cannot bind on APM socket", NULL);
	umask(old_umask);
	if (chmod(sockname, 0660) == -1 || chown(sockname, 0, 0) == -1)
		error("cannot set socket mode/owner/group to 660/0/0", NULL);

	listen(sock, 1);

	return sock;
}

void
handle_client(int sock_fd, int ctl_fd)
{
	/* accept a handle from the client, process it, then clean up */
	int cli_fd;
	struct sockaddr_un from;
	socklen_t fromlen;
	struct apm_command cmd;
	struct apm_reply reply;
	int perfpol_mib[] = { CTL_HW, HW_PERFPOLICY };
	char perfpol[32];
	size_t perfpol_sz = sizeof(perfpol);
	int cpuspeed_mib[] = { CTL_HW, HW_CPUSPEED };
	int cpuspeed = 0;
	size_t cpuspeed_sz = sizeof(cpuspeed);

	fromlen = sizeof(from);
	cli_fd = accept(sock_fd, (struct sockaddr *)&from, &fromlen);
	if (cli_fd == -1) {
		logmsg(LOG_INFO, "client accept failure: %s", strerror(errno));
		return;
	}

	if (recv(cli_fd, &cmd, sizeof(cmd), 0) != sizeof(cmd)) {
		(void) close(cli_fd);
		logmsg(LOG_INFO, "client size botch");
		return;
	}

	if (cmd.vno != APMD_VNO) {
		close(cli_fd);			/* terminate client */
		/* no error message, just drop it. */
		return;
	}

	bzero(&reply, sizeof(reply));
	power_status(ctl_fd, 0, &reply.batterystate);
	switch (cmd.action) {
	case SUSPEND:
		reply.newstate = SUSPENDING;
		reply.error = suspend(ctl_fd);
		break;
	case STANDBY:
		reply.newstate = STANDING_BY;
		reply.error = stand_by(ctl_fd);
		break;
	case HIBERNATE:
		reply.newstate = HIBERNATING;
		reply.error = hibernate(ctl_fd);
		break;
	case SETPERF_LOW:
		reply.newstate = NORMAL;
		logmsg(LOG_NOTICE, "setting hw.perfpolicy to low");
		setperfpolicy("low");
		break;
	case SETPERF_HIGH:
		reply.newstate = NORMAL;
		logmsg(LOG_NOTICE, "setting hw.perfpolicy to high");
		setperfpolicy("high");
		break;
	case SETPERF_AUTO:
		reply.newstate = NORMAL;
		logmsg(LOG_NOTICE, "setting hw.perfpolicy to auto");
		setperfpolicy("auto");
		break;
	default:
		reply.newstate = NORMAL;
		break;
	}

	reply.perfmode = PERF_NONE;
	if (sysctl(perfpol_mib, 2, perfpol, &perfpol_sz, NULL, 0) == -1)
		logmsg(LOG_INFO, "cannot read hw.perfpolicy");
	else {
		if (strcmp(perfpol, "manual") == 0 ||
		    strcmp(perfpol, "high") == 0) {
			reply.perfmode = PERF_MANUAL;
		} else if (strcmp(perfpol, "auto") == 0)
			reply.perfmode = PERF_AUTO;
	}

	if (sysctl(cpuspeed_mib, 2, &cpuspeed, &cpuspeed_sz, NULL, 0) == -1) {
		logmsg(LOG_INFO, "cannot read hw.cpuspeed");
		cpuspeed = 0;
	}
	reply.cpuspeed = cpuspeed;
	reply.vno = APMD_VNO;
	if (send(cli_fd, &reply, sizeof(reply), 0) != sizeof(reply))
		logmsg(LOG_INFO, "reply to client botched");
	close(cli_fd);
}

/*
 * Refresh the random file read by the bootblocks, and remove the +t bit
 * which the bootblock use to track "reuse of the file".
 */
void
fixrandom(void)
{
	char buf[512];
	int fd;

	fd = open("/etc/random.seed", O_WRONLY);
	if (fd != -1) {
		arc4random_buf(buf, sizeof buf);
		write(fd, buf, sizeof buf);
		fchmod(fd, 0600);
		close(fd);
	}
}

void
warnlow(void)
{
	do_etc_file(_PATH_APM_ETC_WARNLOW);
}

int
suspend(int ctl_fd)
{
	int error = 0;

	logmsg(LOG_NOTICE, "system suspending");
	power_status(ctl_fd, 1, NULL);
	fixrandom();
	do_etc_file(_PATH_APM_ETC_SUSPEND);
	sync();
	sleep(1);

	if (ioctl(ctl_fd, APM_IOC_SUSPEND, 0) == -1) {
		error = errno;
		logmsg(LOG_WARNING, "%s: %s", __func__, strerror(errno));
	}

	return error;
}

int
stand_by(int ctl_fd)
{
	int error = 0;

	logmsg(LOG_NOTICE, "system entering standby");
	power_status(ctl_fd, 1, NULL);
	fixrandom();
	do_etc_file(_PATH_APM_ETC_STANDBY);
	sync();
	sleep(1);

	if (ioctl(ctl_fd, APM_IOC_STANDBY, 0) == -1) {
		error = errno;
		logmsg(LOG_WARNING, "%s: %s", __func__, strerror(errno));
	}

	return error;
}

int
hibernate(int ctl_fd)
{
	int error = 0;

	logmsg(LOG_NOTICE, "system hibernating");
	power_status(ctl_fd, 1, NULL);
	fixrandom();
	do_etc_file(_PATH_APM_ETC_HIBERNATE);
	sync();
	sleep(1);

	if (ioctl(ctl_fd, APM_IOC_HIBERNATE, 0) == -1) {
		error = errno;
		logmsg(LOG_WARNING, "%s: %s", __func__, strerror(errno));
	}

	return error;
}

void
resumed(int ctl_fd)
{
	do_etc_file(_PATH_APM_ETC_RESUME);
	logmsg(LOG_NOTICE, "system resumed from sleep");
	power_status(ctl_fd, 1, NULL);
}

#define TIMO (10*60)			/* 10 minutes */
#define AUTOACTION_GRACE_PERIOD	(60)	/* 1 minute after resume */
#define WARNING_GRACE_PERIOD	(2*60)	/* 2 minutes after last warning */

int
main(int argc, char *argv[])
{
	const char *fname = _PATH_APM_CTLDEV;
	int ctl_fd, sock_fd, ch, warnlows, suspends, standbys, hibernates, resumes;
	int autoaction = 0, autoaction_inflight = 0;
	int autolimit = 0, warnlimit = 0;
	int statonly = 0;
	int powerstatus = 0, powerbak = 0, powerchange = 0;
	int noacsleep = 0;
	struct timespec ts = {TIMO, 0}, sts = {0, 0};
	struct timespec last_resume = { 0, 0 };
	struct apm_power_info pinfo;
	const char *sockname = _PATH_APM_SOCKET;
	const char *errstr;
	int kq, nchanges;
	struct kevent ev[2];
	int doperf = PERF_NONE;

	while ((ch = getopt(argc, argv, "aACdHLsf:t:S:w:z:Z:")) != -1)
		switch(ch) {
		case 'a':
			noacsleep = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'f':
			fname = optarg;
			break;
		case 'S':
			sockname = optarg;
			break;
		case 't':
			ts.tv_sec = strtonum(optarg, 1, LLONG_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "number of seconds is %s: %s", errstr,
				    optarg);
			break;
		case 's':	/* status only */
			statonly = 1;
			break;
		case 'A':
		case 'C':
			if (doperf != PERF_NONE)
				usage();
			doperf = PERF_AUTO;
			setperfpolicy("auto");
			break;
		case 'L':
			if (doperf != PERF_NONE)
				usage();
			doperf = PERF_MANUAL;
			setperfpolicy("low");
			break;
		case 'H':
			if (doperf != PERF_NONE)
				usage();
			doperf = PERF_MANUAL;
			setperfpolicy("high");
			break;
		case 'w':
			warnlimit = strtonum(optarg, 1, 100, &errstr);
			if (errstr != NULL)
				errx(1, "battery percentage is %s: %s", errstr,
				    optarg);
			break;
		case 'Z':
			autoaction = AUTO_HIBERNATE;
			autolimit = strtonum(optarg, 1, 100, &errstr);
			if (errstr != NULL)
				errx(1, "battery percentage is %s: %s", errstr,
				    optarg);
			break;
		case 'z':
			autoaction = AUTO_SUSPEND;
			autolimit = strtonum(optarg, 1, 100, &errstr);
			if (errstr != NULL)
				errx(1, "battery percentage is %s: %s", errstr,
				    optarg);
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (doperf == PERF_NONE)
		doperf = PERF_MANUAL;

	if (debug == 0) {
		if (daemon(0, 0) == -1)
			error("failed to daemonize", NULL);
		openlog(__progname, LOG_CONS, LOG_DAEMON);
		setlogmask(LOG_UPTO(LOG_NOTICE));
	}

	(void) signal(SIGTERM, sigexit);
	(void) signal(SIGHUP, sigexit);
	(void) signal(SIGINT, sigexit);

	if ((ctl_fd = open(fname, O_RDWR | O_CLOEXEC)) == -1) {
		if (errno != ENXIO && errno != ENOENT)
			error("cannot open device file `%s'", fname);
	}

	sock_fd = bind_socket(sockname);

	power_status(ctl_fd, 1, &pinfo);

	if (statonly)
		exit(0);

	if (unveil(_PATH_APM_ETC_DIR, "rx") == -1)
		err(1, "unveil %s", _PATH_APM_ETC_DIR);
	if (unveil("/etc/random.seed", "w") == -1)
		err(1, "unveil /etc/random.seed");
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	set_driver_messages(ctl_fd, APM_PRINT_OFF);

	kq = kqueue();
	if (kq <= 0)
		error("kqueue", NULL);

	EV_SET(&ev[0], sock_fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
	    0, 0, NULL);
	if (ctl_fd == -1)
		nchanges = 1;
	else {
		EV_SET(&ev[1], ctl_fd, EVFILT_READ, EV_ADD | EV_ENABLE |
		    EV_CLEAR, 0, 0, NULL);
		nchanges = 2;
	}
	if (kevent(kq, ev, nchanges, NULL, 0, &sts) == -1)
		error("kevent", NULL);

	for (;;) {
		int rv, event, index;

		sts = ts;

		if ((rv = kevent(kq, NULL, 0, ev, 1, &sts)) == -1)
			break;

		if (rv == 1 && ev->ident == sock_fd) {
			handle_client(sock_fd, ctl_fd);
			continue;
		}

		warnlows = suspends = standbys = hibernates = resumes = 0;

		if (rv == 0 && ctl_fd == -1) {
			/* timeout and no way to query status */
			continue;
		} else if (rv == 0) {
			/* wakeup for timeout: take status */
			event = APM_POWER_CHANGE;
			index = -1;
		} else {
			assert(rv == 1 && ev->ident == ctl_fd);
			event = APM_EVENT_TYPE(ev->data);
			index = APM_EVENT_INDEX(ev->data);
		}

		logmsg(LOG_DEBUG, "apmevent %04x index %d", event, index);

		switch (event) {
		case APM_SUSPEND_REQ:
		case APM_USER_SUSPEND_REQ:
		case APM_CRIT_SUSPEND_REQ:
		case APM_BATTERY_LOW:
			suspends++;
			break;
		case APM_USER_STANDBY_REQ:
		case APM_STANDBY_REQ:
			standbys++;
			break;
		case APM_USER_HIBERNATE_REQ:
			hibernates++;
			break;
		case APM_NORMAL_RESUME:
		case APM_CRIT_RESUME:
		case APM_SYS_STANDBY_RESUME:
			powerbak = power_status(ctl_fd, 0, &pinfo);
			if (powerstatus != powerbak) {
				powerstatus = powerbak;
				powerchange = 1;
			}
			clock_gettime(CLOCK_MONOTONIC, &last_resume);
			autoaction_inflight = 0;
			resumes++;
			break;
		case APM_POWER_CHANGE:
			powerbak = power_status(ctl_fd, 0, &pinfo);
			if (powerstatus != powerbak) {
				powerstatus = powerbak;
				powerchange = 1;
			}

			if (!powerstatus && autoaction &&
			    autolimit > (int)pinfo.battery_life) {
				struct timespec graceperiod, now;

				graceperiod = last_resume;
				graceperiod.tv_sec += AUTOACTION_GRACE_PERIOD;
				clock_gettime(CLOCK_MONOTONIC, &now);

				logmsg(LOG_NOTICE,
				    "estimated battery life %d%%"
				    " below configured limit %d%%%s%s",
				    pinfo.battery_life, autolimit,
				    !autoaction_inflight ? "" : ", in flight",
				    timespeccmp(&now, &graceperiod, >) ?
				        "" : ", grace period"
				);

				if (!autoaction_inflight &&
				    timespeccmp(&now, &graceperiod, >)) {
					if (autoaction == AUTO_SUSPEND)
						suspends++;
					else
						hibernates++;
					/* Block autoaction until next resume */
					autoaction_inflight = 1;
				}
			} else if (!powerstatus && warnlimit > 0 &&
			    warnlimit > (int)pinfo.battery_life) {
				static struct timespec last_warn = {0, 0};
				struct timespec graceperiod_w, now_w;

				graceperiod_w = last_warn;
				graceperiod_w.tv_sec += WARNING_GRACE_PERIOD;
				clock_gettime(CLOCK_MONOTONIC, &now_w);

				if (timespeccmp(&now_w, &graceperiod_w, >)) {
					logmsg(LOG_NOTICE,
					    "estimated battery life %d%%"
					    " below configured limit %d%%",
					    pinfo.battery_life, warnlimit);

					last_warn = now_w;
					warnlows++;
				}
			}
			break;
		default:
			;
		}

		if ((standbys || suspends) && noacsleep &&
		    power_status(ctl_fd, 0, &pinfo))
			logmsg(LOG_DEBUG, "no! sleep! till brooklyn!");
		else if (warnlows)
			warnlow();
		else if (suspends)
			suspend(ctl_fd);
		else if (standbys)
			stand_by(ctl_fd);
		else if (hibernates)
			hibernate(ctl_fd);
		else if (resumes)
			resumed(ctl_fd);

		if (powerchange) {
			if (powerstatus)
				do_etc_file(_PATH_APM_ETC_POWERUP);
			else
				do_etc_file(_PATH_APM_ETC_POWERDOWN);
			powerchange = 0;
		}
	}
	error("kevent loop", NULL);

	return 1;
}

void
setperfpolicy(char *policy)
{
	int hw_perfpol_mib[] = { CTL_HW, HW_PERFPOLICY };
	int hw_perf_mib[] = { CTL_HW, HW_SETPERF };
	int new_perf = -1;

	if (strcmp(policy, "low") == 0) {
		policy = "manual";
		new_perf = 0;
	} else if (strcmp(policy, "high") == 0) {
		policy = "manual";
		new_perf = 100;
	}

	if (sysctl(hw_perfpol_mib, 2, NULL, NULL,
	    policy, strlen(policy) + 1) == -1)
		logmsg(LOG_INFO, "cannot set hw.perfpolicy");

	if (new_perf == -1)
		return;

	if (sysctl(hw_perf_mib, 2, NULL, NULL,
	    &new_perf, sizeof(new_perf)) == -1)
		logmsg(LOG_INFO, "cannot set hw.setperf");
}

void
do_etc_file(const char *file)
{
	pid_t pid;
	int status;
	const char *prog;

	/* If file doesn't exist, do nothing. */
	if (access(file, X_OK|R_OK)) {
		logmsg(LOG_DEBUG, "do_etc_file(): cannot access file %s", file);
		return;
	}

	prog = strrchr(file, '/');
	if (prog)
		prog++;
	else
		prog = file;

	pid = fork();
	switch (pid) {
	case -1:
		logmsg(LOG_ERR, "failed to fork(): %s", strerror(errno));
		return;
	case 0:
		/* We are the child. */
		execl(file, prog, (char *)NULL);
		logmsg(LOG_ERR, "failed to exec %s: %s", file, strerror(errno));
		_exit(1);
		/* NOTREACHED */
	default:
		/* We are the parent. */
		wait4(pid, &status, 0, 0);
		if (WIFEXITED(status))
			logmsg(LOG_DEBUG, "%s exited with status %d", file,
			    WEXITSTATUS(status));
		else
			logmsg(LOG_ERR, "%s exited abnormally.", file);
	}
}
