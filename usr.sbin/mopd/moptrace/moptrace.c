/*	$OpenBSD: moptrace.c,v 1.14 2023/03/08 04:43:14 guenther Exp $ */

/*
 * Copyright (c) 1993-95 Mats O Jansson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * moptrace - MOP Trace Utility
 *
 * Usage:	moptrace [-3 | -4] [-ad] interface
 */

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"
#include "common/device.h"
#include "common/print.h"
#include "common/pf.h"
#include "common/dl.h"
#include "common/rc.h"
#include "common/get.h"

/*
 * The list of all interfaces that are being listened to. 
 * "selects" on the descriptors in this list.
 */
struct if_info *iflist;

void   Usage(void);
void   mopProcess(struct if_info *, u_char *);

int     AllFlag = 0;		/* listen on "all" interfaces  */
int     DebugFlag = 0;		/* print debugging messages    */
int	Not3Flag = 0;		/* Ignore MOP V3 messages      */
int	Not4Flag = 0;		/* Ignore MOP V4 messages      */ 
int	promisc = 1;		/* Need promisc mode           */
extern char *__progname;

int
main(int argc, char *argv[])
{
	int     op;
	char   *interface;

	/* All error reporting is done through syslogs. */
	openlog(__progname, LOG_PID | LOG_CONS, LOG_DAEMON);

	opterr = 0;
	while ((op = getopt(argc, argv, "34ad")) != -1) {
		switch (op) {
		case '3':
			Not3Flag = 1;
			break;
		case '4':
			Not4Flag = 1;
			break;
		case 'a':
			AllFlag = 1;
			break;
		case 'd':
			DebugFlag++;
			break;
		default:
			Usage();
			/* NOTREACHED */
		}
	}

	interface = argv[optind++];
	
	if ((AllFlag && interface) ||
	    (!AllFlag && interface == 0) ||
	    (Not3Flag && Not4Flag))
		Usage();

	if (AllFlag)
 		deviceInitAll();
	else
		deviceInitOne(interface);

	Loop();
	/* NOTREACHED */
}

void
Usage()
{
	fprintf(stderr, "usage: %s [-3 | -4] [-ad] interface\n", __progname);
	exit(1);
}

/*
 * Process incoming packages.
 */
void
mopProcess(struct if_info *ii, u_char *pkt)
{
	int	 trans;

	/* We don't known which transport, Guess! */

	trans = mopGetTrans(pkt, 0);

	/* Ok, return if we don't want this message */

	if ((trans == TRANS_ETHER) && Not3Flag) return;
	if ((trans == TRANS_8023) && Not4Flag)	return;

	fprintf(stdout, "Interface    : %s", ii->if_name);
	mopPrintHeader(stdout, pkt, trans);
	mopPrintMopHeader(stdout, pkt, trans);
	
	mopDumpDL(stdout, pkt, trans);
	mopDumpRC(stdout, pkt, trans);

	fprintf(stdout, "\n");
	fflush(stdout);
}
