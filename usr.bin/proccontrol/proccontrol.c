/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/procctl.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
	MODE_ASLR,
	MODE_INVALID,
	MODE_TRACE,
	MODE_TRAPCAP,
#ifdef PROC_KPTI_CTL
	MODE_KPTI,
#endif
};

static pid_t
str2pid(const char *str)
{
	pid_t res;
	char *tail;

	res = strtol(str, &tail, 0);
	if (*tail != '\0') {
		warnx("non-numeric pid");
		return (-1);
	}
	return (res);
}

#ifdef PROC_KPTI_CTL
#define	KPTI_USAGE "|kpti"
#else
#define	KPTI_USAGE
#endif

static void __dead2
usage(void)
{

	fprintf(stderr, "Usage: proccontrol -m (aslr|trace|trapcap"
	    KPTI_USAGE") [-q] "
	    "[-s (enable|disable)] [-p pid | command]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int arg, ch, error, mode;
	pid_t pid;
	bool enable, do_command, query;

	mode = MODE_INVALID;
	enable = true;
	pid = -1;
	query = false;
	while ((ch = getopt(argc, argv, "m:qs:p:")) != -1) {
		switch (ch) {
		case 'm':
			if (strcmp(optarg, "aslr") == 0)
				mode = MODE_ASLR;
			else if (strcmp(optarg, "trace") == 0)
				mode = MODE_TRACE;
			else if (strcmp(optarg, "trapcap") == 0)
				mode = MODE_TRAPCAP;
#ifdef PROC_KPTI_CTL
			else if (strcmp(optarg, "kpti") == 0)
				mode = MODE_KPTI;
#endif
			else
				usage();
			break;
		case 's':
			if (strcmp(optarg, "enable") == 0)
				enable = true;
			else if (strcmp(optarg, "disable") == 0)
				enable = false;
			else
				usage();
			break;
		case 'p':
			pid = str2pid(optarg);
			break;
		case 'q':
			query = true;
			break;
		case '?':
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	do_command = argc != 0;
	if (do_command) {
		if (pid != -1 || query)
			usage();
		pid = getpid();
	} else if (pid == -1) {
		pid = getpid();
	}

	if (query) {
		switch (mode) {
		case MODE_ASLR:
			error = procctl(P_PID, pid, PROC_ASLR_STATUS, &arg);
			break;
		case MODE_TRACE:
			error = procctl(P_PID, pid, PROC_TRACE_STATUS, &arg);
			break;
		case MODE_TRAPCAP:
			error = procctl(P_PID, pid, PROC_TRAPCAP_STATUS, &arg);
			break;
#ifdef PROC_KPTI_CTL
		case MODE_KPTI:
			error = procctl(P_PID, pid, PROC_KPTI_STATUS, &arg);
			break;
#endif
		default:
			usage();
			break;
		}
		if (error != 0)
			err(1, "procctl status");
		switch (mode) {
		case MODE_ASLR:
			switch (arg & ~PROC_ASLR_ACTIVE) {
			case PROC_ASLR_FORCE_ENABLE:
				printf("force enabled");
				break;
			case PROC_ASLR_FORCE_DISABLE:
				printf("force disabled");
				break;
			case PROC_ASLR_NOFORCE:
				printf("not forced");
				break;
			}
			if ((arg & PROC_ASLR_ACTIVE) != 0)
				printf(", active\n");
			else
				printf(", not active\n");
			break;
		case MODE_TRACE:
			if (arg == -1)
				printf("disabled\n");
			else if (arg == 0)
				printf("enabled, no debugger\n");
			else
				printf("enabled, traced by %d\n", arg);
			break;
		case MODE_TRAPCAP:
			switch (arg) {
			case PROC_TRAPCAP_CTL_ENABLE:
				printf("enabled\n");
				break;
			case PROC_TRAPCAP_CTL_DISABLE:
				printf("disabled\n");
				break;
			}
			break;
#ifdef PROC_KPTI_CTL
		case MODE_KPTI:
			switch (arg & ~PROC_KPTI_STATUS_ACTIVE) {
			case PROC_KPTI_CTL_ENABLE_ON_EXEC:
				printf("enabled");
				break;
			case PROC_KPTI_CTL_DISABLE_ON_EXEC:
				printf("disabled");
				break;
			}
			if ((arg & PROC_KPTI_STATUS_ACTIVE) != 0)
				printf(", active\n");
			else
				printf(", not active\n");
			break;
#endif
		}
	} else {
		switch (mode) {
		case MODE_ASLR:
			arg = enable ? PROC_ASLR_FORCE_ENABLE :
			    PROC_ASLR_FORCE_DISABLE;
			error = procctl(P_PID, pid, PROC_ASLR_CTL, &arg);
			break;
		case MODE_TRACE:
			arg = enable ? PROC_TRACE_CTL_ENABLE :
			    PROC_TRACE_CTL_DISABLE;
			error = procctl(P_PID, pid, PROC_TRACE_CTL, &arg);
			break;
		case MODE_TRAPCAP:
			arg = enable ? PROC_TRAPCAP_CTL_ENABLE :
			    PROC_TRAPCAP_CTL_DISABLE;
			error = procctl(P_PID, pid, PROC_TRAPCAP_CTL, &arg);
			break;
#ifdef PROC_KPTI_CTL
		case MODE_KPTI:
			arg = enable ? PROC_KPTI_CTL_ENABLE_ON_EXEC :
			    PROC_KPTI_CTL_DISABLE_ON_EXEC;
			error = procctl(P_PID, pid, PROC_KPTI_CTL, &arg);
			break;
#endif
		default:
			usage();
			break;
		}
		if (error != 0)
			err(1, "procctl ctl");
		if (do_command) {
			error = execvp(argv[0], argv);
			err(1, "exec");
		}
	}
	exit(0);
}
