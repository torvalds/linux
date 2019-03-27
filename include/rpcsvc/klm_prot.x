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
 * Kernel/lock manager protocol definition
 * Copyright (C) 1986 Sun Microsystems, Inc.
 *
 * protocol used between the UNIX kernel (the "client") and the
 * local lock manager.  The local lock manager is a daemon running
 * above the kernel.
 */

#ifndef RPC_HDR
%#ifndef lint
%/*static char sccsid[] = "from: @(#)klm_prot.x 1.7 87/07/08 Copyr 1987 Sun Micro";*/
%/*static char sccsid[] = "from: @(#)klm_prot.x	2.1 88/08/01 4.0 RPCSRC";*/
%#endif /* not lint */
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

const	LM_MAXSTRLEN = 1024;

/*
 * lock manager status returns
 */
enum klm_stats {
	klm_granted = 0,	/* lock is granted */
	klm_denied = 1,		/* lock is denied */
	klm_denied_nolocks = 2, /* no lock entry available */
	klm_working = 3 	/* lock is being processed */
};

/*
 * lock manager lock identifier
 */
struct klm_lock {
	string server_name<LM_MAXSTRLEN>;
	netobj fh;		/* a counted file handle */
	int pid;		/* holder of the lock */
	unsigned l_offset;	/* beginning offset of the lock */
	unsigned l_len;		/* byte length of the lock;
				 * zero means through end of file */
};

/*
 * lock holder identifier
 */
struct klm_holder {
	bool exclusive;		/* FALSE if shared lock */
	int svid;		/* holder of the lock (pid) */
	unsigned l_offset;	/* beginning offset of the lock */
	unsigned l_len;		/* byte length of the lock;
				 * zero means through end of file */
};

/*
 * reply to KLM_LOCK / KLM_UNLOCK / KLM_CANCEL
 */
struct klm_stat {
	klm_stats stat;
};

/*
 * reply to a KLM_TEST call
 */
union klm_testrply switch (klm_stats stat) {
	case klm_denied:
		struct klm_holder holder;
	default: /* All other cases return no arguments */
		void;
};


/*
 * arguments to KLM_LOCK
 */
struct klm_lockargs {
	bool block;
	bool exclusive;
	struct klm_lock alock;
};

/*
 * arguments to KLM_TEST
 */
struct klm_testargs {
	bool exclusive;
	struct klm_lock alock;
};

/*
 * arguments to KLM_UNLOCK
 */
struct klm_unlockargs {
	struct klm_lock alock;
};

program KLM_PROG {
	version KLM_VERS {

		klm_testrply	KLM_TEST (struct klm_testargs) =	1;

		klm_stat	KLM_LOCK (struct klm_lockargs) =	2;

		klm_stat	KLM_CANCEL (struct klm_lockargs) =	3;
		/* klm_granted=> the cancel request fails due to lock is already granted */
		/* klm_denied=> the cancel request successfully aborts
lock request  */

		klm_stat	KLM_UNLOCK (struct klm_unlockargs) =	4;
	} = 1;
} = 100020;
