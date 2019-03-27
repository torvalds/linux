/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Edwin Groothuis. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/tftp.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "tftp-utils.h"
#include "tftp-io.h"

/*
 * Default values, can be changed later via the TFTP Options
 */
int		timeoutpacket = TIMEOUT;
int		timeoutnetwork = MAX_TIMEOUTS * TIMEOUT;
int		maxtimeouts = MAX_TIMEOUTS;
uint16_t	segsize = SEGSIZE;
uint16_t	pktsize = SEGSIZE + 4;

int	acting_as_client;


/*
 * Set timeout values for packet reception. The idea is that you
 * get 'maxtimeouts' of 5 seconds between 'timeoutpacket' (i.e. the
 * first timeout) to 'timeoutnetwork' (i.e. the last timeout)
 */
int
settimeouts(int _timeoutpacket, int _timeoutnetwork, int _maxtimeouts __unused)
{
	int i;
	
	/* We cannot do impossible things */
	if (_timeoutpacket >= _timeoutnetwork)
		return (0);
	
	maxtimeouts = 0;
	i = _timeoutpacket;
	while (i < _timeoutnetwork || maxtimeouts < MIN_TIMEOUTS) {
		maxtimeouts++;
		i += 5;
	}

	timeoutpacket = _timeoutpacket;
	timeoutnetwork = i;
	return (1);
}

/* translate IPv4 mapped IPv6 address to IPv4 address */
void
unmappedaddr(struct sockaddr_in6 *sin6)
{
	struct sockaddr_in *sin4;
	u_int32_t addr;
	int port;

	if (sin6->sin6_family != AF_INET6 ||
	    !IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
		return;
	sin4 = (struct sockaddr_in *)sin6;
	memcpy(&addr, &sin6->sin6_addr.s6_addr[12], sizeof(addr));
	port = sin6->sin6_port;
	memset(sin4, 0, sizeof(struct sockaddr_in));
	sin4->sin_addr.s_addr = addr;
	sin4->sin_port = port;
	sin4->sin_family = AF_INET;
	sin4->sin_len = sizeof(struct sockaddr_in);
}

/* Get a field from a \0 separated string */
ssize_t
get_field(int peer, char *buffer, ssize_t size)
{
	char *cp = buffer;

	while (cp < buffer + size) {
		if (*cp == '\0') break;
		cp++;
	}
	if (*cp != '\0') {
		tftp_log(LOG_ERR, "Bad option - no trailing \\0 found");
		send_error(peer, EBADOP);
		exit(1);
	}
	return (cp - buffer + 1);
}

/*
 * Logging functions
 */
static int _tftp_logtostdout = 1;

void
tftp_openlog(const char *ident, int logopt, int facility)
{

	_tftp_logtostdout = (ident == NULL);
	if (_tftp_logtostdout == 0)
		openlog(ident, logopt, facility);
}

void
tftp_closelog(void)
{

	if (_tftp_logtostdout == 0)
		closelog();
}

void
tftp_log(int priority, const char *message, ...)
{
	va_list ap;
	char *s;

	va_start(ap, message);
	if (_tftp_logtostdout == 0) {
		vasprintf(&s, message, ap);
		syslog(priority, "%s", s);
	} else {
		vprintf(message, ap);
		printf("\n");
	}
	va_end(ap);
}

/*
 * Packet types
 */
struct packettypes packettypes[] = {
	{ RRQ,		"RRQ"	},
	{ WRQ,		"WRQ"	},
	{ DATA,		"DATA"	},
	{ ACK,		"ACK"	},
	{ ERROR,	"ERROR"	},
	{ OACK,		"OACK"	},
	{ 0,		NULL	},
};

const char *
packettype(int type)
{
	static char failed[100];
	int i = 0;

	while (packettypes[i].name != NULL) {
		if (packettypes[i].value == type)
			break;
		i++;
	}
	if (packettypes[i].name != NULL)
		return packettypes[i].name;
	sprintf(failed, "unknown (type: %d)", type);
	return (failed);
}

/*
 * Debugs
 */
int	debug = DEBUG_NONE;
struct debugs debugs[] = {
	{ DEBUG_PACKETS,	"packet",	"Packet debugging"	},
	{ DEBUG_SIMPLE,		"simple",	"Simple debugging"	},
	{ DEBUG_OPTIONS,	"options",	"Options debugging"	},
	{ DEBUG_ACCESS,		"access",	"TCPd access debugging"	},
	{ DEBUG_NONE,		NULL,		"No debugging"		},
};
int	packetdroppercentage = 0;

int
debug_find(char *s)
{
	int i = 0;

	while (debugs[i].name != NULL) {
		if (strcasecmp(debugs[i].name, s) == 0)
			break;
		i++;
	}
	return (debugs[i].value);
}

int
debug_finds(char *s)
{
	int i = 0;
	char *ps = s;

	while (s != NULL) {
		ps = strchr(s, ' ');
		if (ps != NULL)
			*ps = '\0';
		i += debug_find(s);
		if (ps != NULL)
			*ps = ' ';
		s = ps;
	}
	return (i);
}

const char *
debug_show(int d)
{
	static char s[100];
	size_t space = sizeof(s);
	int i = 0;

	s[0] = '\0';
	while (debugs[i].name != NULL) {
		if (d&debugs[i].value) {
			if (s[0] != '\0')
				strlcat(s, " ", space);
			strlcat(s, debugs[i].name, space);
		}
		i++;
	}
	if (s[0] != '\0')
		return (s);
	return ("none");
}

/*
 * RP_
 */
struct rp_errors rp_errors[] = {
	{ RP_TIMEOUT,		"Network timeout" },
	{ RP_TOOSMALL,		"Not enough data bytes" },
	{ RP_WRONGSOURCE,	"Invalid IP address of UDP port" },
	{ RP_ERROR,		"Error packet" },
	{ RP_RECVFROM,		"recvfrom() complained" },
	{ RP_TOOBIG,		"Too many data bytes" },
	{ RP_NONE,		NULL }
};

char *
rp_strerror(int error)
{
	static char s[100];
	size_t space = sizeof(s);
	int i = 0;

	while (rp_errors[i].desc != NULL) {
		if (rp_errors[i].error == error) {
			strlcpy(s, rp_errors[i].desc, space);
			space -= strlen(rp_errors[i].desc);
		}
		i++;
	}
	if (s[0] == '\0')
		sprintf(s, "unknown (error=%d)", error);
	return (s);
}

/*
 * Performance figures
 */

void
stats_init(struct tftp_stats *ts)
{

	ts->amount = 0;
	ts->rollovers = 0;
	ts->retries = 0;
	ts->blocks = 0;
	ts->amount = 0;
	gettimeofday(&(ts->tstart), NULL);
}

void
printstats(const char *direction, int verbose, struct tftp_stats *ts)
{
	double delta;	/* compute delta in 1/10's second units */

	delta = ((ts->tstop.tv_sec*10.)+(ts->tstop.tv_usec/100000)) -
		((ts->tstart.tv_sec*10.)+(ts->tstart.tv_usec/100000));
	delta = delta/10.;      /* back to seconds */

	printf("%s %zu bytes during %.1f seconds in %u blocks",
	    direction, ts->amount, delta, ts->blocks);

	if (ts->rollovers != 0)
		printf(" with %d rollover%s",
		    ts->rollovers, ts->rollovers != 1 ? "s" : "");

	if (verbose)
		printf(" [%.0f bits/sec]", (ts->amount*8.)/delta);
	putchar('\n');
}
