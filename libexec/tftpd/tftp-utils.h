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

/*
 */
#define	TIMEOUT		5
#define	MAX_TIMEOUTS	5

/* Generic values */
#define MAXSEGSIZE	65464		/* Maximum size of the data segment */
#define	MAXPKTSIZE	(MAXSEGSIZE + 4) /* Maximum size of the packet */

/* For the blksize option */
#define BLKSIZE_MIN	8		/* Minimum size of the data segment */
#define BLKSIZE_MAX	MAXSEGSIZE	/* Maximum size of the data segment */

/* For the timeout option */
#define TIMEOUT_MIN	0		/* Minimum timeout value */
#define TIMEOUT_MAX	255		/* Maximum timeout value */
#define MIN_TIMEOUTS	3

extern int	timeoutpacket;
extern int	timeoutnetwork;
extern int	maxtimeouts;
int	settimeouts(int timeoutpacket, int timeoutnetwork, int maxtimeouts);

extern uint16_t	segsize;
extern uint16_t	pktsize;

extern int	acting_as_client;

/*
 */
void	unmappedaddr(struct sockaddr_in6 *sin6);
ssize_t	get_field(int peer, char *buffer, ssize_t size);

/*
 * Packet types
 */
struct packettypes {
	int	value;
	const char *const name;
};
extern struct packettypes packettypes[];
const char *packettype(int);

/*
 * RP_
 */
struct rp_errors {
	int	error;
	const char *const desc;
};
extern struct rp_errors rp_errors[];
char	*rp_strerror(int error);

/*
 * Debug features
 */
#define	DEBUG_NONE	0x0000
#define DEBUG_PACKETS	0x0001
#define DEBUG_SIMPLE	0x0002
#define DEBUG_OPTIONS	0x0004
#define DEBUG_ACCESS	0x0008
struct debugs {
	int	value;
	const char *const name;
	const char *const desc;
};
extern int	debug;
extern struct debugs debugs[];
extern int	packetdroppercentage;
int	debug_find(char *s);
int	debug_finds(char *s);
const char *debug_show(int d);

/*
 * Log routines
 */
#define DEBUG(s) tftp_log(LOG_DEBUG, "%s", s)
extern int tftp_logtostdout;
void	tftp_openlog(const char *ident, int logopt, int facility);
void	tftp_closelog(void);
void	tftp_log(int priority, const char *message, ...) __printflike(2, 3);

/*
 * Performance figures
 */
struct tftp_stats {
	size_t		amount;
	int		rollovers;
	uint32_t	blocks;
	int		retries;
	struct timeval	tstart;
	struct timeval	tstop;
};

void	stats_init(struct tftp_stats *ts);
void	printstats(const char *direction, int verbose, struct tftp_stats *ts);
