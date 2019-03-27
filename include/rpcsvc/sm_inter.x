/* @(#)sm_inter.x	2.2 88/08/01 4.0 RPCSRC */
/* @(#)sm_inter.x 1.7 87/06/24 Copyr 1987 Sun Micro */

/*-
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Status monitor protocol specification
 * Copyright (C) 1986 Sun Microsystems, Inc.
 *
 */

#ifndef RPC_HDR
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

program SM_PROG { 
	version SM_VERS  {
		/* res_stat = stat_succ if status monitor agrees to monitor */
		/* res_stat = stat_fail if status monitor cannot monitor */
		/* if res_stat == stat_succ, state = state number of site sm_name */
		struct sm_stat_res			 SM_STAT(struct sm_name) = 1;

		/* res_stat = stat_succ if status monitor agrees to monitor */
		/* res_stat = stat_fail if status monitor cannot monitor */
		/* stat consists of state number of local site */
		struct sm_stat_res			 SM_MON(struct mon) = 2;

		/* stat consists of state number of local site */
		struct sm_stat				 SM_UNMON(struct mon_id) = 3;

		/* stat consists of state number of local site */
		struct sm_stat				 SM_UNMON_ALL(struct my_id) = 4;

		void					 SM_SIMU_CRASH(void) = 5;
		void					 SM_NOTIFY(struct stat_chge) = 6;

	} = 1;
} = 100024;

const	SM_MAXSTRLEN = 1024;

struct sm_name {
	string mon_name<SM_MAXSTRLEN>;
};

struct my_id {
	string	 my_name<SM_MAXSTRLEN>;		/* name of the site iniates the monitoring request*/
	int	my_prog;			/* rpc program # of the requesting process */
	int	my_vers;			/* rpc version # of the requesting process */
	int	my_proc;			/* rpc procedure # of the requesting process */
};

struct mon_id {
	string	mon_name<SM_MAXSTRLEN>;		/* name of the site to be monitored */
	struct my_id my_id;
};


struct mon{
	struct mon_id mon_id;
	opaque priv[16]; 		/* private information to store at monitor for requesting process */
};

struct stat_chge {
	string  mon_name<SM_MAXSTRLEN>;         /* name of the site that had the state change */
	int state;
};

/*
 * state # of status monitor monitonically increases each time
 * status of the site changes:
 * an even number (>= 0) indicates the site is down and
 * an odd number (> 0) indicates the site is up;
 */
struct sm_stat {
	int state;		/* state # of status monitor */
};

enum sm_res {
	stat_succ = 0,		/* status monitor agrees to monitor */
	stat_fail = 1		/* status monitor cannot monitor */
};

struct sm_stat_res {
	sm_res res_stat;
	int state;
};

/* 
 * structure of the status message sent back by the status monitor
 * when monitor site status changes
 */
struct sm_status {
	string mon_name<SM_MAXSTRLEN>;
	int state;
	opaque priv[16];		/* stored private information */
};
