/*	$OpenBSD: mopd.c,v 1.19 2015/02/09 23:00:14 deraadt Exp $ */

/*
 * Copyright (c) 1993-96 Mats O Jansson.  All rights reserved.
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
 * mopd - MOP Dump/Load Daemon
 *
 * Usage:	mopd [-3 | -4] [-adfv] interface
 */

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"
#include "common/device.h"
#include "common/print.h"
#include "common/pf.h"
#include "common/cmp.h"
#include "common/get.h"
#include "common/dl.h"
#include "common/rc.h"
#include "process.h"

#include "pwd.h"

/*
 * The list of all interfaces that are being listened to. 
 * "selects" on the descriptors in this list.
 */
struct if_info	*iflist;

void		Usage(void);
void		mopProcess(struct if_info *, u_char *);

int	 AllFlag = 0;		/* listen on "all" interfaces */
int	 DebugFlag = 0;		/* print debugging messages   */
int	 ForegroundFlag = 0;	/* run in foreground          */
int	 VersionFlag = 0;	/* print version              */
int	 Not3Flag = 0;		/* Not MOP V3 messages.       */
int	 Not4Flag = 0;		/* Not MOP V4 messages.       */
int	 promisc = 1;		/* Need promisc mode    */

extern char *__progname;

int
main(int argc, char *argv[])
{
	int		 c;
	char		*interface;
	struct passwd	*pw;

	extern char version[];

	while ((c = getopt(argc, argv, "34adfv")) != -1)
		switch (c) {
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
		case 'f':
			ForegroundFlag = 1;
			break;
		case 'v':
			VersionFlag = 1;
			break;
		default:
			Usage();
			/* NOTREACHED */
		}

	if (VersionFlag) {
		fprintf(stdout,"%s: version %s\n", __progname, version);
		exit(0);
	}

	interface = argv[optind++];

	if ((AllFlag && interface) || (!AllFlag && interface == 0) ||
	    (argc > optind) || (Not3Flag && Not4Flag))
		Usage();

	/* All error reporting is done through syslogs. */
	openlog(__progname, LOG_PID | LOG_CONS, LOG_DAEMON);
	tzset();

	if ((pw = getpwnam("_mopd")) == NULL)
		err(1, "getpwnam");

	if ((!ForegroundFlag) && DebugFlag)
		fprintf(stdout, "%s: not running as daemon, -d given.\n",
		    __progname);

	if ((!ForegroundFlag) && (!DebugFlag))
		if (daemon(0, 0) == -1)
			err(1, NULL);

	syslog(LOG_INFO, "%s %s started.", __progname, version);

	if (AllFlag)
		deviceInitAll();
	else
		deviceInitOne(interface);

	if (chroot(MOP_FILE_PATH) == -1) {
		syslog(LOG_CRIT, "chroot %s: %m", MOP_FILE_PATH);
		exit(1);
	}
	if (chdir("/") == -1) {
		syslog(LOG_CRIT, "chdir(\"/\"): %m");
		exit(1);
	}
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid)) {
		syslog(LOG_CRIT, "can't drop privileges: %m");
		exit(1);
	}
	endpwent();

	Loop();
	/* NOTREACHED */
}

void
Usage()
{
	fprintf(stderr, "usage: %s [-3 | -4] [-adfv] interface\n",
	    __progname);
	exit(1);
}

/*
 * Process incoming packages.
 */
void
mopProcess(struct if_info *ii, u_char *pkt)
{
	u_char	*dst, *src;
	u_short  ptype;
	int	 idx, trans, len;

	/* We don't known with transport, Guess! */
	trans = mopGetTrans(pkt, 0);

	/* Ok, return if we don't wan't this message */
	if ((trans == TRANS_ETHER) && Not3Flag) return;
	if ((trans == TRANS_8023) && Not4Flag)	return;

	idx = 0;
	mopGetHeader(pkt, &idx, &dst, &src, &ptype, &len, trans);

	/*
	 * Ignore our own transmissions
	 *
	 */	
	if (mopCmpEAddr(ii->eaddr,src) == 0)
		return;

	switch (ptype) {
	case MOP_K_PROTO_DL:
		mopProcessDL(stdout, ii, pkt, &idx, dst, src, trans, len);
		break;
	case MOP_K_PROTO_RC:
		mopProcessRC(stdout, ii, pkt, &idx, dst, src, trans, len);
		break;
	default:
		break;
	}
}
