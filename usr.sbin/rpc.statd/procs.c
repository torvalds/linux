/*	$OpenBSD: procs.c,v 1.3 2017/01/21 08:33:51 krw Exp $	*/

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

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>

#include <rpc/rpc.h>

#include "statd.h"

/* sm_stat_1 --------------------------------------------------------------- */
/*
 * Purpose:	RPC call to enquire if a host can be monitored
 * Returns:	TRUE for any hostname that can be looked up to give
 *		an address.
 */
struct sm_stat_res *
sm_stat_1_svc(sm_name *arg, struct svc_req *req)
{
	static sm_stat_res res;

	NO_ALARM;
	if (debug)
		syslog(LOG_DEBUG, "stat called for host %s", arg->mon_name);

	if (gethostbyname(arg->mon_name))
		res.res_stat = stat_succ;
	else {
		syslog(LOG_ERR, "invalid hostname to sm_stat: %s",
		    arg->mon_name);
		res.res_stat = stat_fail;
	}

	res.state = status_info.ourState;
	ALARM;
	return (&res);
}

/* sm_mon_1 ---------------------------------------------------------------- */
/*
 * Purpose:	RPC procedure to establish a monitor request
 * Returns:	Success, unless lack of resources prevents
 *		the necessary structures from being set up
 *		to record the request, or if the hostname is not
 *		valid (as judged by gethostbyname())
 */
struct sm_stat_res *
sm_mon_1_svc(mon *arg, struct svc_req *req)
{
	static sm_stat_res res;
	HostInfo *hp, h;
	MonList *lp;

	NO_ALARM;
	if (debug) {
		syslog(LOG_DEBUG, "monitor request for host %s",
		    arg->mon_id.mon_name);
		syslog(LOG_DEBUG, "recall host: %s prog: %d ver: %d proc: %d",
		    arg->mon_id.my_id.my_name, arg->mon_id.my_id.my_prog,
		    arg->mon_id.my_id.my_vers, arg->mon_id.my_id.my_proc);
	}
	res.res_stat = stat_fail;	/* Assume fail until set otherwise */
	res.state = status_info.ourState;

	/*
	 * Find existing host entry, or create one if not found.  If
	 * find_host() fails, it will have logged the error already.
	 */
	if (!gethostbyname(arg->mon_id.mon_name)) {
		syslog(LOG_ERR, "Invalid hostname to sm_mon: %s",
		    arg->mon_id.mon_name);
		return &res;
	}

	if ((hp = find_host(arg->mon_id.mon_name, &h)) == NULL)
		memset(hp = &h, 0, sizeof(h));

	lp = malloc(sizeof(MonList));
	if (!lp)
		syslog(LOG_ERR, "Out of memory");
	else {
		strncpy(lp->notifyHost, arg->mon_id.my_id.my_name,
		    SM_MAXSTRLEN);
		lp->notifyProg = arg->mon_id.my_id.my_prog;
		lp->notifyVers = arg->mon_id.my_id.my_vers;
		lp->notifyProc = arg->mon_id.my_id.my_proc;
		memcpy(lp->notifyData, arg->priv,
		    sizeof(lp->notifyData));

		lp->next = hp->monList;
		hp->monList = lp;
		change_host(arg->mon_id.mon_name, hp);
		sync_file();
		res.res_stat = stat_succ;	/* Report success */
	}
	ALARM;
	return (&res);
}

/* do_unmon ---------------------------------------------------------------- */
/*
 * Purpose:	Remove a monitor request from a host
 * Returns:	TRUE if found, FALSE if not found.
 * Notes:	Common code from sm_unmon_1_svc and sm_unmon_all_1_svc
 *		In the unlikely event of more than one identical monitor
 *		request, all are removed.
 */
int
do_unmon(char *name, HostInfo *hp, void *ptr)
{
	my_id *idp = ptr;
	MonList *lp, *next;
	MonList *last = NULL;
	int result = FALSE;

	lp = hp->monList;
	while (lp) {
		if (!strncasecmp(idp->my_name, lp->notifyHost, SM_MAXSTRLEN)
		    && (idp->my_prog == lp->notifyProg)
		    && (idp->my_proc == lp->notifyProc)
		    && (idp->my_vers == lp->notifyVers)) {
			/* found one.  Unhook from chain and free. */
			next = lp->next;
			if (last)
				last->next = next;
			else
				hp->monList = next;
			free(lp);
			lp = next;
			result = TRUE;
		} else {
			last = lp;
			lp = lp->next;
		}
	}
	return (result);
}

/* sm_unmon_1 -------------------------------------------------------------- */
/*
 * Purpose:	RPC procedure to release a monitor request.
 * Returns:	Local machine's status number
 * Notes:	The supplied mon_id should match the value passed in an
 *		earlier call to sm_mon_1
 */
struct sm_stat *
sm_unmon_1_svc(mon_id *arg, struct svc_req *req)
{
	static sm_stat res;
	HostInfo *hp, h;

	NO_ALARM;
	if (debug) {
		syslog(LOG_DEBUG, "un-monitor request for host %s",
		    arg->mon_name);
		syslog(LOG_DEBUG, "recall host: %s prog: %d ver: %d proc: %d",
		    arg->my_id.my_name, arg->my_id.my_prog,
		    arg->my_id.my_vers, arg->my_id.my_proc);
	}
	if ((hp = find_host(arg->mon_name, &h)) != NULL) {
		if (do_unmon(arg->mon_name, hp, &arg->my_id)) {
			change_host(arg->mon_name, hp);
			sync_file();
		} else
			syslog(LOG_ERR,
			    "unmon request from %s, no matching monitor",
			    arg->my_id.my_name);
	} else
		syslog(LOG_ERR, "unmon request from %s for unknown host %s",
		    arg->my_id.my_name, arg->mon_name);

	res.state = status_info.ourState;
	ALARM;

	return (&res);
}

/* sm_unmon_all_1 ---------------------------------------------------------- */
/*
 * Purpose:	RPC procedure to release monitor requests.
 * Returns:	Local machine's status number
 * Notes:	Releases all monitor requests (if any) from the specified
 *		host and program number.
 */
struct sm_stat *
sm_unmon_all_1_svc(my_id *arg, struct svc_req *req)
{
	static sm_stat res;

	NO_ALARM;
	if (debug) {
		syslog(LOG_DEBUG,
		    "unmon_all for host: %s prog: %d ver: %d proc: %d",
		    arg->my_name, arg->my_prog, arg->my_vers, arg->my_proc);
	}

	unmon_hosts();
	sync_file();

	res.state = status_info.ourState;
	ALARM;

	return (&res);
}

/* sm_simu_crash_1 --------------------------------------------------------- */
/*
 * Purpose:	RPC procedure to simulate a crash
 * Returns:	Nothing
 * Notes:	Standardised mechanism for debug purposes
 *		The specification says that we should drop all of our
 *		status information (apart from the list of monitored hosts
 *		on disc).  However, this would confuse the rpc.lockd
 *		which would be unaware that all of its monitor requests
 *		had been silently junked.  Hence we in fact retain all
 *		current requests and simply increment the status counter
 *		and inform all hosts on the monitor list.
 */
void *
sm_simu_crash_1_svc(void *v, struct svc_req *req)
{
	static char dummy;

	NO_ALARM;
	if (debug)
		syslog(LOG_DEBUG, "simu_crash called!!");

	reset_database();
	ALARM;
	notify_handler(0);

	return (&dummy);
}

/* sm_notify_1 ------------------------------------------------------------- */
/*
 * Purpose:	RPC procedure notifying local statd of the crash of another
 * Returns:	Nothing
 * Notes:	There is danger of deadlock, since it is quite likely that
 *		the client procedure that we call will in turn call us
 *		to remove or adjust the monitor request.
 *		We therefore fork() a process to do the notifications.
 *		Note that the main HostInfo structure is in a mmap()
 *		region and so will be shared with the child, but the
 *		monList pointed to by the HostInfo is in normal memory.
 *		Hence if we read the monList before forking, we are
 *		protected from the parent servicing other requests
 *		that modify the list.
 */
void   *
sm_notify_1_svc(stat_chge *arg, struct svc_req *req)
{
	struct timeval timeout = {20, 0};	/* 20 secs timeout */
	CLIENT *cli;
	static char dummy;
	status tx_arg;		/* arg sent to callback procedure */
	MonList *lp;
	HostInfo *hp, h;
	pid_t pid;

	if (debug)
		syslog(LOG_DEBUG, "notify from host %s, new state %d",
		    arg->mon_name, arg->state);

	hp = find_host(arg->mon_name, &h);
	if (!hp) {
		/* Never heard of this host - why is it notifying us? */
		syslog(LOG_DEBUG, "Unsolicited notification from host %s",
		    arg->mon_name);
		return (&dummy);
	}
	lp = hp->monList;
	if (!lp) /* We know this host, but have no outstanding requests. */
		return (&dummy);

	sync_file();
	pid = fork();
	if (pid == -1) {
		syslog(LOG_ERR, "Unable to fork notify process - %s",
		    strerror(errno));
		return (FALSE);
	}
	if (pid)
		return (&dummy); /* Parent returns */

	while (lp) {
		tx_arg.mon_name = arg->mon_name;
		tx_arg.state = arg->state;
		memcpy(tx_arg.priv, lp->notifyData, sizeof(tx_arg.priv));
		cli = clnt_create(lp->notifyHost, lp->notifyProg,
		    lp->notifyVers, "udp");
		if (!cli)
			syslog(LOG_ERR, "Failed to contact host %s%s",
			    lp->notifyHost, clnt_spcreateerror(""));
		else {
			if (clnt_call(cli, lp->notifyProc, xdr_status, &tx_arg,
			    xdr_void, &dummy, timeout) != RPC_SUCCESS)
				syslog(LOG_ERR,
				    "Failed to call rpc.statd client at host %s",
				    lp->notifyHost);
			clnt_destroy(cli);
		}
		lp = lp->next;
	}

	exit(0);		/* Child quits */
}
