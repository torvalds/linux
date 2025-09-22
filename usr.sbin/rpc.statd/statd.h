/*	$OpenBSD: statd.h,v 1.1 2008/06/15 04:43:28 sturm Exp $	*/

/*
 * Copyright (c) 1995
 *	A.R. Gordon (andrew.gordon@net-tel.co.uk).  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the FreeBSD project
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANDREW GORDON AND CONTRIBUTORS ``AS IS'' AND
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
 *
 */

#include "sm_inter.h"

/* ------------------------------------------------------------------------- */
/*
 * Data structures for recording monitored hosts
 *
 * The information held by the status monitor comprises a list of hosts
 * that we have been asked to monitor, and, associated with each monitored
 * host, one or more clients to be called back if the monitored host crashes.
 *
 * The list of monitored hosts must be retained over a crash, so that upon
 * re-boot we can call the SM_NOTIFY procedure in all those hosts so as to
 * cause them to start recovery processing.  On the other hand, the client
 * call-backs are not required to be preserved: they are assumed (in the
 * protocol design) to be local processes which will have crashed when
 * we did, and so are discarded on restart.
 *
 * We handle this by keeping the list of monitored hosts in a file
 * (/var/statd.state) which is mmap()ed and whose format is described
 * by the typedef Header.  The lists of client callbacks are chained
 * off this structure, but are held in normal memory and so will be
 * lost after a re-boot.  Hence the actual values of MonList * pointers
 * in the copy on disc have no significance, but their NULL/non-NULL
 * status indicates whether this host is actually being monitored or if it
 * is an empty slot in the file.
 */

typedef struct MonList_s {
	struct MonList_s *next;		/* Next in list or NULL */
	char	notifyHost[SM_MAXSTRLEN + 1]; /* Host to notify */
	int     notifyProg;		/* RPC program number to call */
	int     notifyVers;		/* version number */
	int     notifyProc;		/* procedure number */
	u_char	notifyData[16];		/* Opaque data from caller */
}       MonList;

typedef struct {
	int     notifyReqd;	/* Time of our next attempt or 0
				   informed the monitored host */
	int	attempts;	/* Number of attempts we tried so far */
	MonList *monList;	/* List of clients to inform if we
				   hear that the monitored host has
				   crashed, NULL if no longer monitored	 */
}       HostInfo;


/* Overall file layout. */

typedef struct {
	int	magic;		/* Zero magic */
	int	ourState;	/* State number as defined in statd protocol */
}       Header;

/* ------------------------------------------------------------------------- */

/* Global variables */

extern int	debug;		/* = 1 to enable diagnostics to syslog */
extern struct sigaction sa;
extern Header status_info;

/* Function prototypes */

/* stat_proc.c */
struct sm_stat_res *sm_stat_1_svc(sm_name *, struct svc_req *);
struct sm_stat_res *sm_mon_1_svc(mon *, struct svc_req *);
struct sm_stat *sm_unmon_1_svc(mon_id *, struct svc_req *);
struct sm_stat *sm_unmon_all_1_svc(my_id *, struct svc_req *);
void *sm_simu_crash_1_svc(void *, struct svc_req *);
void *sm_notify_1_svc(stat_chge *, struct svc_req *);
int	do_unmon(char *, HostInfo *, void *);

/* statd.c */
void notify_handler(int);
void sync_file(void);
void unmon_hosts(void);
void change_host(char *, HostInfo *);
HostInfo *find_host(char *, HostInfo *);
void reset_database(void);

void sm_prog_1(struct svc_req *, SVCXPRT *);

#define NO_ALARM sa.sa_handler == SIG_DFL ? 0 : \
    (sa.sa_handler = SIG_IGN, sigaction(SIGALRM, &sa, NULL))
#define ALARM sa.sa_handler == SIG_DFL ? 0 : \
    (sa.sa_handler = notify_handler, sigaction(SIGALRM, &sa, NULL))
#define CLR_ALARM sa.sa_handler == SIG_DFL ? 0 : \
    (sa.sa_handler = SIG_DFL, sigaction(SIGALRM, &sa, NULL))
