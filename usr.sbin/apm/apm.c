/*	$OpenBSD: apm.c,v 1.45 2025/04/05 12:53:45 jca Exp $	*/

/*
 *  Copyright (c) 1996 John T. Kohl
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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <machine/apmvar.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <string.h>
#include "pathnames.h"
#include "apm-proto.h"

#define FALSE 0
#define TRUE 1

extern char *__progname;

static int		do_zzz(int, enum apm_action);
static int		open_socket(const char *);
static int		send_command(int, struct apm_command *,
			    struct apm_reply *);
static __dead void	usage(void);
static __dead void	zzusage(void);

static __dead void
usage(void)
{
	fprintf(stderr,"usage: %s [-AabHLlmPSvZz] [-f sockname]\n",
	    __progname);
	exit(1);
}

static __dead void
zzusage(void)
{
	fprintf(stderr,"usage: %s [-SZz] [-f sockname]\n",
	    __progname);
	exit(1);
}

static int
send_command(int fd, struct apm_command *cmd, struct apm_reply *reply)
{
	/* send a command to the apm daemon */
	cmd->vno = APMD_VNO;

	if (send(fd, cmd, sizeof(*cmd), 0) == sizeof(*cmd)) {
		if (recv(fd, reply, sizeof(*reply), 0) != sizeof(*reply)) {
			warn("invalid reply from APM daemon");
			return (1);
		}
	} else {
		warn("invalid send to APM daemon");
		return (1);
	}
	return (0);
}

static int
do_zzz(int fd, enum apm_action action)
{
	struct apm_command command;
	struct apm_reply reply;
	char *msg;
	int ret;

	bzero(&reply, sizeof reply);

	switch (action) {
	case NONE:
	case SUSPEND:
		command.action = SUSPEND;
		msg = "Suspending system";
		break;
	case STANDBY:
		command.action = STANDBY;
		msg = "System standing by";
		break;
	case HIBERNATE:
		command.action = HIBERNATE;
		msg = "Hibernating system";
		break;
	default:
		zzusage();
	}

	printf("%s...\n", msg);
	ret = send_command(fd, &command, &reply);
	if (ret == 0 && reply.error)
		errx(1, "%s: %s", apm_state(reply.newstate), strerror(reply.error));
	exit(ret);
}

static int
open_socket(const char *sockname)
{
	int sock, errr;
	struct sockaddr_un s_un;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
		err(1, "cannot create local socket");

	s_un.sun_family = AF_UNIX;
	strlcpy(s_un.sun_path, sockname, sizeof(s_un.sun_path));
	if (connect(sock, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		errr = errno;
		close(sock);
		errno = errr;
		sock = -1;
	}
	return (sock);
}

int
main(int argc, char *argv[])
{
	const char *sockname = _PATH_APM_SOCKET;
	int doac = FALSE;
	int dopct = FALSE;
	int dobstate = FALSE;
	int domin = FALSE;
	int doperf = FALSE;
	int verbose = FALSE;
	int ch, fd, rval;
	enum apm_action action = NONE;
	struct apm_command command;
	struct apm_reply reply;
	int perfpol_mib[] = { CTL_HW, HW_PERFPOLICY };
	char perfpol[32];
	size_t perfpol_sz = sizeof(perfpol);
	int cpuspeed_mib[] = { CTL_HW, HW_CPUSPEED }, cpuspeed;
	size_t cpuspeed_sz = sizeof(cpuspeed);

	if (sysctl(cpuspeed_mib, 2, &cpuspeed, &cpuspeed_sz, NULL, 0) == -1)
		cpuspeed = 0;

	if (sysctl(perfpol_mib, 2, perfpol, &perfpol_sz, NULL, 0) == -1)
		perfpol[0] = '\0';

	while ((ch = getopt(argc, argv, "ACHLlmbvaPSzZf:")) != -1) {
		switch (ch) {
		case 'v':
			verbose = TRUE;
			break;
		case 'f':
			sockname = optarg;
			break;
		case 'z':
			if (action != NONE)
				usage();
			action = SUSPEND;
			break;
		case 'S':
			if (action != NONE)
				usage();
			action = STANDBY;
			break;
		case 'Z':
			if (action != NONE)
				usage();
			action = HIBERNATE;
			break;
		case 'A':
		case 'C':
			if (action != NONE)
				usage();
			action = SETPERF_AUTO;
			break;
		case 'H':
			if (action != NONE)
				usage();
			action = SETPERF_HIGH;
			break;
		case 'L':
			if (action != NONE)
				usage();
			action = SETPERF_LOW;
			break;
		case 'b':
			if (action != NONE && action != GETSTATUS)
				usage();
			dobstate = TRUE;
			action = GETSTATUS;
			break;
		case 'l':
			if (action != NONE && action != GETSTATUS)
				usage();
			dopct = TRUE;
			action = GETSTATUS;
			break;
		case 'm':
			if (action != NONE && action != GETSTATUS)
				usage();
			domin = TRUE;
			action = GETSTATUS;
			break;
		case 'a':
			if (action != NONE && action != GETSTATUS)
				usage();
			doac = TRUE;
			action = GETSTATUS;
			break;
		case 'P':
			if (action != NONE && action != GETSTATUS)
				usage();
			doperf = TRUE;
			action = GETSTATUS;
			break;
		default:
			if (!strcmp(__progname, "zzz") ||
			    !strcmp(__progname, "ZZZ"))
				zzusage();
			else
				usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc)
		usage();

	fd = open_socket(sockname);

	if (fd != -1) {
		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");
	}

	if (!strcmp(__progname, "zzz")) {
		if (fd < 0)
			err(1, "cannot connect to apmd");
		else
			return (do_zzz(fd, action));
	} else if (!strcmp(__progname, "ZZZ")) {
		if (fd < 0)
			err(1, "cannot connect to apmd");
		else
			return (do_zzz(fd, HIBERNATE));
	}


	bzero(&reply, sizeof reply);
	reply.batterystate.battery_state = APM_BATT_UNKNOWN;
	reply.batterystate.ac_state = APM_AC_UNKNOWN;
	reply.cpuspeed = cpuspeed;
	if (strcmp(perfpol, "manual") == 0 ||
	    strcmp(perfpol, "high") == 0)
		reply.perfmode = PERF_MANUAL;
	else if (strcmp(perfpol, "auto") == 0)
		reply.perfmode = PERF_AUTO;
	else
		reply.perfmode = PERF_NONE;

	switch (action) {
	case SETPERF_LOW:
	case SETPERF_HIGH:
	case SETPERF_AUTO:
		if (fd == -1)
			errx(1, "cannot connect to apmd, "
			    "not changing performance adjustment mode");
		goto balony;
	case NONE:
		action = GETSTATUS;
		verbose = doac = dopct = dobstate = domin = doperf = TRUE;
		/* FALLTHROUGH */
	case GETSTATUS:
		if (fd == -1) {
			/* open the device directly and get status */
			fd = open(_PATH_APM_NORMAL, O_RDONLY);
			if (ioctl(fd, APM_IOC_GETPOWER,
			    &reply.batterystate) == 0) {
				if (pledge("stdio", NULL) == -1)
					err(1, "pledge");

				goto printval;
			}
		}
		/* FALLTHROUGH */
balony:
	case SUSPEND:
	case STANDBY:
	case HIBERNATE:
		command.action = action;
		break;
	default:
		usage();
	}

	if (fd != -1 && (rval = send_command(fd, &command, &reply)) != 0)
		errx(rval, "cannot get reply from APM daemon");

	switch (action) {
	case GETSTATUS:
	printval:
		if (!verbose) {
			if (dobstate)
				printf("%d\n",
				    reply.batterystate.battery_state);
			if (dopct)
				printf("%d\n",
				    reply.batterystate.battery_life);
			if (domin) {
				if (reply.batterystate.minutes_left ==
				    (u_int)-1)
					printf("unknown\n");
				else
					printf("%d\n",
					    reply.batterystate.minutes_left);
			}
			if (doac)
				printf("%d\n",
				    reply.batterystate.ac_state);
			if (doperf)
				printf("%d\n", reply.perfmode);
			break;
		}

		if (dobstate) {
			printf("Battery state: %s",
			    battstate(reply.batterystate.battery_state));
			if (!dopct && !domin)
				printf("\n");
		}

		if (dopct && !dobstate)
			printf("Battery remaining: %d percent",
			    reply.batterystate.battery_life);
		else if (dopct)
			printf(", %d%% remaining",
			    reply.batterystate.battery_life);
		if (dopct && !domin)
			printf("\n");

		if (domin && !dobstate && !dopct) {
			if (reply.batterystate.battery_state ==
			    APM_BATT_CHARGING)
				printf("Remaining battery recharge "
				    "time estimate: %d minutes\n",
				    reply.batterystate.minutes_left);
			else if (reply.batterystate.minutes_left == 0 &&
			    reply.batterystate.battery_life > 10)
				printf("Battery life estimate: "
				    "not available\n");
			else
			{
				printf("Battery life estimate: ");
				if (reply.batterystate.minutes_left ==
				    (u_int)-1)
					printf("unknown\n");
				else
					printf("%d minutes\n",
					    reply.batterystate.minutes_left);
			}
		} else if (domin) {
			if (reply.batterystate.battery_state ==
			    APM_BATT_CHARGING)
			{
				if (reply.batterystate.minutes_left ==
				    (u_int)-1)
					printf(", unknown");
				else
					printf(", %d minutes",
					    reply.batterystate.minutes_left);
				printf(" recharge time estimate\n");
			}
			else if (reply.batterystate.minutes_left == 0 &&
			    reply.batterystate.battery_life > 10)
				printf(", unknown life estimate\n");
			else
			{
				if (reply.batterystate.minutes_left ==
				    (u_int)-1)
					printf(", unknown");
				else
					printf(", %d minutes",
					    reply.batterystate.minutes_left);
				printf(" life estimate\n");
			}
		}

		if (doac)
			printf("AC adapter state: %s\n",
			    ac_state(reply.batterystate.ac_state));

		if (doperf)
			printf("Performance adjustment mode: %s (%d MHz)\n",
			    perf_mode(reply.perfmode), reply.cpuspeed);
		break;
	default:
		break;
	}

	switch (reply.newstate) {
	case SUSPEND:
		printf("System will enter suspend mode momentarily.\n");
		break;
	case STANDBY:
		printf("System will enter standby mode momentarily.\n");
		break;
	case HIBERNATE:
		printf("System will enter hibernate mode momentarily.\n");
		break;
	default:
		break;
	}
	if (reply.error)
		errx(1, "%s: %s", apm_state(reply.newstate), strerror(reply.error));
	return (0);
}
