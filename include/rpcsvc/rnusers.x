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
 * Find out about remote users
 */

#ifndef RPC_HDR
%#ifndef lint
%/*static char sccsid[] = "from: @(#)rnusers.x 1.2 87/09/20 Copyr 1987 Sun Micro";*/
%/*static char sccsid[] = "from: @(#)rnusers.x	2.1 88/08/01 4.0 RPCSRC";*/
%#endif /* not lint */
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

const MAXUSERS = 100;
const MAXUTLEN = 256;

struct utmp {
	string ut_line<MAXUTLEN>;
	string ut_name<MAXUTLEN>;
	string ut_host<MAXUTLEN>;
	int ut_time;
};


struct utmpidle {
	utmp ui_utmp;
	unsigned int ui_idle;
};

typedef utmp utmparr<MAXUSERS>;

typedef utmpidle utmpidlearr<MAXUSERS>;

const RUSERS_MAXUSERLEN = 32;
const RUSERS_MAXLINELEN = 32;
const RUSERS_MAXHOSTLEN = 257;

struct rusers_utmp {
	string ut_user<RUSERS_MAXUSERLEN>;	/* aka ut_name */
	string ut_line<RUSERS_MAXLINELEN>;	/* device */
	string ut_host<RUSERS_MAXHOSTLEN>;	/* host user logged on from */
	int ut_type;				/* type of entry */
	int ut_time;				/* time entry was made */
	unsigned int ut_idle;			/* minutes idle */
};

typedef rusers_utmp utmp_array<>;

program RUSERSPROG {
	/*
	 * Old version does not include idle information
	 */
	version RUSERSVERS_ORIG {
		int
		RUSERSPROC_NUM(void) = 1;

		utmparr
		RUSERSPROC_NAMES(void) = 2;

		utmparr
		RUSERSPROC_ALLNAMES(void) = 3;
	} = 1;

	/*
	 * Includes idle information
	 */
	version RUSERSVERS_IDLE {
		int
		RUSERSPROC_NUM(void) = 1;

		utmpidlearr
		RUSERSPROC_NAMES(void) = 2;

		utmpidlearr
		RUSERSPROC_ALLNAMES(void) = 3;
	} = 2;

	/*
	 * Version 3 rusers procedures (from Solaris).
	 * (Thanks a lot Sun.)
	 */
	version RUSERSVERS_3 {
		int
		RUSERSPROC_NUM(void) = 1;

		utmp_array
		RUSERSPROC_NAMES(void) = 2;

		utmp_array
		RUSERSPROC_ALLNAMES(void) = 3;
	} = 3;

} = 100002;

