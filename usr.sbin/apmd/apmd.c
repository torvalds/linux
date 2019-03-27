/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * APM (Advanced Power Management) Event Dispatcher
 *
 * Copyright (c) 1999 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
 * Copyright (c) 1999 KOIE Hidetaka <koie@suri.co.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <assert.h>
#include <bitstring.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <machine/apm_bios.h>

#include "apmd.h"

int		debug_level = 0;
int		verbose = 0;
int		soft_power_state_change = 0;
const char	*apmd_configfile = APMD_CONFIGFILE;
const char	*apmd_pidfile = APMD_PIDFILE;
int             apmctl_fd = -1, apmnorm_fd = -1;

/*
 * table of event handlers
 */
#define EVENT_CONFIG_INITIALIZER(EV,R) { #EV, NULL, R },
struct event_config events[EVENT_MAX] = {
	EVENT_CONFIG_INITIALIZER(NOEVENT, 0)
	EVENT_CONFIG_INITIALIZER(STANDBYREQ, 1)
	EVENT_CONFIG_INITIALIZER(SUSPENDREQ, 1)
	EVENT_CONFIG_INITIALIZER(NORMRESUME, 0)
	EVENT_CONFIG_INITIALIZER(CRITRESUME, 0)
	EVENT_CONFIG_INITIALIZER(BATTERYLOW, 0)
	EVENT_CONFIG_INITIALIZER(POWERSTATECHANGE, 0)
	EVENT_CONFIG_INITIALIZER(UPDATETIME, 0)
	EVENT_CONFIG_INITIALIZER(CRITSUSPEND, 1)
	EVENT_CONFIG_INITIALIZER(USERSTANDBYREQ, 1)
	EVENT_CONFIG_INITIALIZER(USERSUSPENDREQ, 1)
	EVENT_CONFIG_INITIALIZER(STANDBYRESUME, 0)
	EVENT_CONFIG_INITIALIZER(CAPABILITIESCHANGE, 0)
};

/*
 * List of battery events
 */
struct battery_watch_event *battery_watch_list = NULL;

#define BATT_CHK_INTV 10 /* how many seconds between battery state checks? */

/*
 * default procedure
 */
struct event_cmd *
event_cmd_default_clone(void *this)
{
	struct event_cmd * oldone = this;
	struct event_cmd * newone = malloc(oldone->len);

	newone->next = NULL;
	newone->len = oldone->len;
	newone->name = oldone->name;
	newone->op = oldone->op;
	return newone;
}

/*
 * exec command
 */
int
event_cmd_exec_act(void *this)
{
	struct event_cmd_exec * p = this;
	int status = -1;
	pid_t pid;

	switch ((pid = fork())) {
	case -1:
		warn("cannot fork");
		break;
	case 0:
		/* child process */
		signal(SIGHUP, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		execl(_PATH_BSHELL, "sh", "-c", p->line, (char *)NULL);
		_exit(127);
	default:
		/* parent process */
		do {
			pid = waitpid(pid, &status, 0);
		} while (pid == -1 && errno == EINTR);
		break;
	}
	return status;
}
void
event_cmd_exec_dump(void *this, FILE *fp)
{
	fprintf(fp, " \"%s\"", ((struct event_cmd_exec *)this)->line);
}
struct event_cmd *
event_cmd_exec_clone(void *this)
{
	struct event_cmd_exec * newone = (struct event_cmd_exec *) event_cmd_default_clone(this);
	struct event_cmd_exec * oldone = this;

	newone->evcmd.next = NULL;
	newone->evcmd.len = oldone->evcmd.len;
	newone->evcmd.name = oldone->evcmd.name;
	newone->evcmd.op = oldone->evcmd.op;
	if ((newone->line = strdup(oldone->line)) == NULL)
		err(1, "out of memory");
	return (struct event_cmd *) newone;
}
void
event_cmd_exec_free(void *this)
{
	free(((struct event_cmd_exec *)this)->line);
}
struct event_cmd_op event_cmd_exec_ops = {
	event_cmd_exec_act,
	event_cmd_exec_dump,
	event_cmd_exec_clone,
	event_cmd_exec_free
};

/*
 * reject command
 */
int
event_cmd_reject_act(void *this __unused)
{
	int rc = 0;

	if (ioctl(apmctl_fd, APMIO_REJECTLASTREQ, NULL)) {
		syslog(LOG_NOTICE, "fail to reject\n");
		rc = -1;
	}
	return rc;
}
struct event_cmd_op event_cmd_reject_ops = {
	event_cmd_reject_act,
	NULL,
	event_cmd_default_clone,
	NULL
};

/*
 * manipulate event_config
 */
struct event_cmd *
clone_event_cmd_list(struct event_cmd *p)
{
	struct event_cmd dummy;
	struct event_cmd *q = &dummy;
	for ( ;p; p = p->next) {
		assert(p->op->clone);
		if ((q->next = p->op->clone(p)) == NULL)
			err(1, "out of memory");
		q = q->next;
	}
	q->next = NULL;
	return dummy.next;
}
void
free_event_cmd_list(struct event_cmd *p)
{
	struct event_cmd * q;
	for ( ; p ; p = q) {
		q = p->next;
		if (p->op->free)
			p->op->free(p);
		free(p);
	}
}
int
register_battery_handlers(
	int level, int direction,
	struct event_cmd *cmdlist)
{
	/*
	 * level is negative if it's in "minutes", non-negative if
	 * percentage.
	 *
	 * direction =1 means we care about this level when charging,
	 * direction =-1 means we care about it when discharging.
	 */
	if (level>100) /* percentage > 100 */
		return -1;
	if (abs(direction) != 1) /* nonsense direction value */
		return -1;

	if (cmdlist) {
		struct battery_watch_event *we;
		
		if ((we = malloc(sizeof(struct battery_watch_event))) == NULL)
			err(1, "out of memory");

		we->next = battery_watch_list; /* starts at NULL */
		battery_watch_list = we;
		we->level = abs(level);
		we->type = (level<0)?BATTERY_MINUTES:BATTERY_PERCENT;
		we->direction = (direction<0)?BATTERY_DISCHARGING:
			BATTERY_CHARGING;
		we->done = 0;
		we->cmdlist = clone_event_cmd_list(cmdlist);
	}
	return 0;
}
int
register_apm_event_handlers(
	bitstr_t bit_decl(evlist, EVENT_MAX),
	struct event_cmd *cmdlist)
{
	if (cmdlist) {
		bitstr_t bit_decl(tmp, EVENT_MAX);
		memcpy(&tmp, evlist, bitstr_size(EVENT_MAX));

		for (;;) {
			int n;
			struct event_cmd *p;
			struct event_cmd *q;
			bit_ffs(tmp, EVENT_MAX, &n);
			if (n < 0)
				break;
			p = events[n].cmdlist;
			if ((q = clone_event_cmd_list(cmdlist)) == NULL)
				err(1, "out of memory");
			if (p) {
				while (p->next != NULL)
					p = p->next;
				p->next = q;
			} else {
				events[n].cmdlist = q;
			}
			bit_clear(tmp, n);
		}
	}
	return 0;
}

/*
 * execute command
 */
int
exec_run_cmd(struct event_cmd *p)
{
	int status = 0;

	for (; p; p = p->next) {
		assert(p->op->act);
		if (verbose)
			syslog(LOG_INFO, "action: %s", p->name);
		status = p->op->act(p);
		if (status) {
			syslog(LOG_NOTICE, "command finished with %d\n", status);
			break;
		}
	}
	return status;
}

/*
 * execute command -- the event version
 */
int
exec_event_cmd(struct event_config *ev)
{
	int status = 0;

	status = exec_run_cmd(ev->cmdlist);
	if (status && ev->rejectable) {
		syslog(LOG_ERR, "canceled");
		event_cmd_reject_act(NULL);
	}
	return status;
}

/*
 * read config file
 */
extern FILE * yyin;
extern int yydebug;

void
read_config(void)
{
	int i;

	if ((yyin = fopen(apmd_configfile, "r")) == NULL) {
		err(1, "cannot open config file");
	}

#ifdef DEBUG
	yydebug = debug_level;
#endif

	if (yyparse() != 0)
		err(1, "cannot parse config file");

	fclose(yyin);

	/* enable events */
	for (i = 0; i < EVENT_MAX; i++) {
		if (events[i].cmdlist) {
			u_int event_type = i;
			if (write(apmctl_fd, &event_type, sizeof(u_int)) == -1) {
				err(1, "cannot enable event 0x%x", event_type);
			}
		}
	}
}

void
dump_config(void)
{
	int i;
	struct battery_watch_event *q;

	for (i = 0; i < EVENT_MAX; i++) {
		struct event_cmd * p;
		if ((p = events[i].cmdlist)) {
			fprintf(stderr, "apm_event %s {\n", events[i].name);
			for ( ; p ; p = p->next) {
				fprintf(stderr, "\t%s", p->name);
				if (p->op->dump)
					p->op->dump(p, stderr);
				fprintf(stderr, ";\n");
			}
			fprintf(stderr, "}\n");
		}
	}
	for (q = battery_watch_list ; q != NULL ; q = q -> next) {
		struct event_cmd * p;
		fprintf(stderr, "apm_battery %d%s %s {\n",
			q -> level,
			(q -> type == BATTERY_PERCENT)?"%":"m",
			(q -> direction == BATTERY_CHARGING)?"charging":
				"discharging");
		for ( p = q -> cmdlist; p ; p = p->next) {
			fprintf(stderr, "\t%s", p->name);
			if (p->op->dump)
				p->op->dump(p, stderr);
			fprintf(stderr, ";\n");
		}
		fprintf(stderr, "}\n");
	}
}

void
destroy_config(void)
{
	int i;
	struct battery_watch_event *q;

	/* disable events */
	for (i = 0; i < EVENT_MAX; i++) {
		if (events[i].cmdlist) {
			u_int event_type = i;
			if (write(apmctl_fd, &event_type, sizeof(u_int)) == -1) {
				err(1, "cannot disable event 0x%x", event_type);
			}
		}
	}

	for (i = 0; i < EVENT_MAX; i++) {
		struct event_cmd * p;
		if ((p = events[i].cmdlist))
			free_event_cmd_list(p);
		events[i].cmdlist = NULL;
	}

	for( ; battery_watch_list; battery_watch_list = battery_watch_list -> next) {
		free_event_cmd_list(battery_watch_list->cmdlist);
		q = battery_watch_list->next;
		free(battery_watch_list);
		battery_watch_list = q;
	}
}

void
restart(void)
{
	destroy_config();
	read_config();
	if (verbose)
		dump_config();
}

/*
 * write pid file
 */
static void
write_pid(void)
{
	FILE *fp = fopen(apmd_pidfile, "w");

	if (fp) {
		fprintf(fp, "%ld\n", (long)getpid());
		fclose(fp);
	}
}

/*
 * handle signals
 */
static int signal_fd[2];

void
enque_signal(int sig)
{
	if (write(signal_fd[1], &sig, sizeof sig) != sizeof sig)
		err(1, "cannot process signal.");
}

void
wait_child(void)
{
	int status;
	while (waitpid(-1, &status, WNOHANG) > 0)
		;
}

int
proc_signal(int fd)
{
	int rc = 0;
	int sig;

	while (read(fd, &sig, sizeof sig) == sizeof sig) {
		syslog(LOG_INFO, "caught signal: %d", sig);
		switch (sig) {
		case SIGHUP:
			syslog(LOG_NOTICE, "restart by SIG");
			restart();
			break;
		case SIGTERM:
			syslog(LOG_NOTICE, "going down on signal %d", sig);
			rc = -1;
			return rc;
		case SIGCHLD:
			wait_child();
			break;
		default:
			warn("unexpected signal(%d) received.", sig);
			break;
		}
	}
	return rc;
}
void
proc_apmevent(int fd)
{
	struct apm_event_info apmevent;

	while (ioctl(fd, APMIO_NEXTEVENT, &apmevent) == 0) {
		int status;
		syslog(LOG_NOTICE, "apmevent %04x index %d\n",
			apmevent.type, apmevent.index);
		syslog(LOG_INFO, "apm event: %s", events[apmevent.type].name);
		if (fork() == 0) {
			status = exec_event_cmd(&events[apmevent.type]);
			exit(status);
		}
	}
}

#define AC_POWER_STATE ((pw_info.ai_acline == 1) ? BATTERY_CHARGING :\
	BATTERY_DISCHARGING)

void
check_battery(void)
{

	static int first_time=1, last_state;
	int status;

	struct apm_info pw_info;
	struct battery_watch_event *p;

	/* If we don't care, don't bother */
	if (battery_watch_list == NULL)
		return;

	if (first_time) {
		if ( ioctl(apmnorm_fd, APMIO_GETINFO, &pw_info) < 0)
			err(1, "cannot check battery state.");
/*
 * This next statement isn't entirely true. The spec does not tie AC
 * line state to battery charging or not, but this is a bit lazier to do.
 */
		last_state = AC_POWER_STATE;
		first_time = 0;
		return; /* We can't process events, we have no baseline */
	}

	/*
	 * XXX - should we do this a bunch of times and perform some sort
	 * of smoothing or correction?
	 */
	if ( ioctl(apmnorm_fd, APMIO_GETINFO, &pw_info) < 0)
		err(1, "cannot check battery state.");

	/*
	 * If we're not in the state now that we were in last time,
	 * then it's a transition, which means we must clean out
	 * the event-caught state.
	 */
	if (last_state != AC_POWER_STATE) {
		if (soft_power_state_change && fork() == 0) {
			status = exec_event_cmd(&events[PMEV_POWERSTATECHANGE]);
			exit(status);
		}
		last_state = AC_POWER_STATE;
		for (p = battery_watch_list ; p!=NULL ; p = p -> next)
			p->done = 0;
	}
	for (p = battery_watch_list ; p != NULL ; p = p -> next)
		if (p -> direction == AC_POWER_STATE &&
			!(p -> done) &&
			((p -> type == BATTERY_PERCENT && 
				p -> level == (int)pw_info.ai_batt_life) ||
			(p -> type == BATTERY_MINUTES &&
				p -> level == (pw_info.ai_batt_time / 60)))) {
			p -> done++;
			if (verbose)
				syslog(LOG_NOTICE, "Caught battery event: %s, %d%s",
					(p -> direction == BATTERY_CHARGING)?"charging":"discharging",
					p -> level,
					(p -> type == BATTERY_PERCENT)?"%":" minutes");
			if (fork() == 0) {
				status = exec_run_cmd(p -> cmdlist);
				exit(status);
			}
		}
}
void
event_loop(void)
{
	int		fdmax = 0;
	struct sigaction nsa;
	fd_set          master_rfds;
	sigset_t	sigmask, osigmask;

	FD_ZERO(&master_rfds);
	FD_SET(apmctl_fd, &master_rfds);
	fdmax = apmctl_fd > fdmax ? apmctl_fd : fdmax;

	FD_SET(signal_fd[0], &master_rfds);
	fdmax = signal_fd[0] > fdmax ? signal_fd[0] : fdmax;

	memset(&nsa, 0, sizeof nsa);
	nsa.sa_handler = enque_signal;
	sigfillset(&nsa.sa_mask);
	nsa.sa_flags = SA_RESTART;
	sigaction(SIGHUP, &nsa, NULL);
	sigaction(SIGCHLD, &nsa, NULL);
	sigaction(SIGTERM, &nsa, NULL);

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGHUP);
	sigaddset(&sigmask, SIGCHLD);
	sigaddset(&sigmask, SIGTERM);
	sigprocmask(SIG_SETMASK, &sigmask, &osigmask);

	while (1) {
		fd_set rfds;
		int res;
		struct timeval to;

		to.tv_sec = BATT_CHK_INTV;
		to.tv_usec = 0;

		memcpy(&rfds, &master_rfds, sizeof rfds);
		sigprocmask(SIG_SETMASK, &osigmask, NULL);
		if ((res=select(fdmax + 1, &rfds, 0, 0, &to)) < 0) {
			if (errno != EINTR)
				err(1, "select");
		}
		sigprocmask(SIG_SETMASK, &sigmask, NULL);

		if (res == 0) { /* time to check the battery */
			check_battery();
			continue;
		}

		if (FD_ISSET(signal_fd[0], &rfds)) {
			if (proc_signal(signal_fd[0]) < 0)
				return;
		}

		if (FD_ISSET(apmctl_fd, &rfds))
			proc_apmevent(apmctl_fd);
	}
}

int
main(int ac, char* av[])
{
	int	ch;
	int	daemonize = 1;
	char	*prog;
	int	logopt = LOG_NDELAY | LOG_PID;

	while ((ch = getopt(ac, av, "df:sv")) != -1) {
		switch (ch) {
		case 'd':
			daemonize = 0;
			debug_level++;
			break;
		case 'f':
			apmd_configfile = optarg;
			break;
		case 's':
			soft_power_state_change = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			err(1, "unknown option `%c'", ch);
		}
	}

	if (daemonize)
		daemon(0, 0);

#ifdef NICE_INCR
	nice(NICE_INCR);
#endif

	if (!daemonize)
		logopt |= LOG_PERROR;

	prog = strrchr(av[0], '/');
	openlog(prog ? prog+1 : av[0], logopt, LOG_DAEMON);

	syslog(LOG_NOTICE, "start");

	if (pipe(signal_fd) < 0)
		err(1, "pipe");
	if (fcntl(signal_fd[0], F_SETFL, O_NONBLOCK) < 0)
		err(1, "fcntl");

	if ((apmnorm_fd = open(APM_NORM_DEVICEFILE, O_RDWR)) == -1) {
		err(1, "cannot open device file `%s'", APM_NORM_DEVICEFILE);
	}

	if (fcntl(apmnorm_fd, F_SETFD, 1) == -1) {
		err(1, "cannot set close-on-exec flag for device file '%s'", APM_NORM_DEVICEFILE);
	}

	if ((apmctl_fd = open(APM_CTL_DEVICEFILE, O_RDWR)) == -1) {
		err(1, "cannot open device file `%s'", APM_CTL_DEVICEFILE);
	}

	if (fcntl(apmctl_fd, F_SETFD, 1) == -1) {
		err(1, "cannot set close-on-exec flag for device file '%s'", APM_CTL_DEVICEFILE);
	}

	restart();
	write_pid();
	event_loop();
	exit(EXIT_SUCCESS);
}

