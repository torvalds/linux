/*	$OpenBSD: mopprobe.c,v 1.15 2015/02/09 23:00:14 deraadt Exp $ */

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
 * mopprobe - MOP Probe Utility
 *
 * Usage:	mopprobe [-3 | -4] [-aov] interface
 */

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"
#include "common/device.h"
#include "common/print.h"
#include "common/get.h"
#include "common/cmp.h"
#include "common/pf.h"
#include "common/nmadef.h"

/*
 * The list of all interfaces that are being listened to.
 */
struct if_info *iflist;

void   Usage(void);
void   mopProcess(struct if_info *, u_char *);

struct once {
	u_char	eaddr[6];		/* Ethernet addr */
	struct once *next;		/* Next one */
};

int     AllFlag = 0;		/* listen on "all" interfaces  */
int	Not3Flag = 0;		/* Not MOP V3 messages         */
int	Not4Flag = 0;		/* Not MOP V4 messages         */
int	VerboseFlag = 0;	/* Print All Announces	       */
int     OnceFlag = 0;		/* print only once             */
int	promisc = 1;		/* Need promisc mode           */
extern char *__progname;
struct once *root = NULL;

int
main(int argc, char *argv[])
{
	int     op;
	char   *interface;

	/* All error reporting is done through syslogs. */
	openlog(__progname, LOG_PID | LOG_CONS, LOG_DAEMON);

	opterr = 0;
	while ((op = getopt(argc, argv, "34aov")) != -1) {
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
		case 'o':
			OnceFlag = 1;
			break;
		case 'v':
			VerboseFlag = 1;
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
	fprintf(stderr, "usage: %s [-3 | -4] [-aov] interface\n", __progname);
	exit(1);
}

/*
 * Process incoming packages.
 */
void
mopProcess(struct if_info *ii, u_char *pkt)
{
	u_char	*dst, *src, mopcode, tmpc, device, ilen;
	u_short	 ptype, moplen = 0, itype;
	int	 idx, trans, len, i, hwa = 0;
	struct once *o = NULL;
	
	/* We don't known with transport, Guess! */

	trans = mopGetTrans(pkt, 0);

	/* Ok, return if we don't wan't this message */

	if ((trans == TRANS_ETHER) && Not3Flag) return;
	if ((trans == TRANS_8023) && Not4Flag)	return;

	idx = 0;
	mopGetHeader(pkt, &idx, &dst, &src, &ptype, &len, trans);

	/* Ignore our own transmissions */

	if (mopCmpEAddr(ii->eaddr,src) == 0)
		return;

	/* Just check multicast */

	if (mopCmpEAddr(rc_mcst,dst) != 0) {
		return;
	}
	
	switch(ptype) {
	case MOP_K_PROTO_RC:
		break;
	default:
		return;
	}
	
	if (OnceFlag) {
		o = root;
		while (o != NULL) {
			if (mopCmpEAddr(o->eaddr,src) == 0)
				return;
			o = o->next;
		}
		o = (struct once *)malloc(sizeof(*o));
		o->eaddr[0] = src[0];
		o->eaddr[1] = src[1];
		o->eaddr[2] = src[2];
		o->eaddr[3] = src[3];
		o->eaddr[4] = src[4];
		o->eaddr[5] = src[5];
		o->next = root;
		root = o;
	}

	moplen  = mopGetLength(pkt, trans);
	mopcode	= mopGetChar(pkt,&idx);

	/* Just process System Information */

	if (mopcode != MOP_K_CODE_SID) {
		return;
	}
	
	mopGetChar(pkt,&idx);			/* Reserved */
	mopGetShort(pkt,&idx);			/* Receipt # */
		
	device = 0;

	switch(trans) {
	case TRANS_ETHER:
		moplen = moplen + 16;
		break;
	case TRANS_8023:
		moplen = moplen + 14;
		break;
	}

	itype = mopGetShort(pkt,&idx); 

	while (idx < (int)(moplen)) {
		ilen  = mopGetChar(pkt,&idx);
		switch (itype) {
		case 0:
			tmpc  = mopGetChar(pkt,&idx);
			idx = idx + tmpc;
			break;
		case MOP_K_INFO_VER:
			idx = idx + 3;
			break;
		case MOP_K_INFO_MFCT:
		case MOP_K_INFO_RTM:
		case MOP_K_INFO_CSZ:
		case MOP_K_INFO_RSZ:
			idx = idx + 2;
			break;
		case MOP_K_INFO_HWA:
			hwa = idx;
			/* FALLTHROUGH */
		case MOP_K_INFO_CNU:
			idx = idx + 6;
			break;
		case MOP_K_INFO_TIME:
			idx = idx + 10;
			break;
	        case MOP_K_INFO_SOFD:
			device = mopGetChar(pkt,&idx);
			if (VerboseFlag && 
			    (device != NMA_C_SOFD_LCS) &&   /* DECserver 100 */
			    (device != NMA_C_SOFD_DS2) &&   /* DECserver 200 */
			    (device != NMA_C_SOFD_DP2) &&   /* DECserver 250 */
			    (device != NMA_C_SOFD_DS3))     /* DECserver 300 */
			{
				mopPrintHWA(stdout, src);
				fprintf(stdout," # ");
				mopPrintDevice(stdout, device);
				fprintf(stdout," ");
				mopPrintHWA(stdout, &pkt[hwa]);
				fprintf(stdout,"\n");
			}
			break;
		case MOP_K_INFO_SFID:
			tmpc = mopGetChar(pkt,&idx);
			if ((tmpc > 0) && (tmpc < 17)) 
				idx = idx + tmpc;
			break;
		case MOP_K_INFO_PRTY:
			idx = idx + 1;
			break;
		case MOP_K_INFO_DLTY:
			idx = idx + 1;
			break;
	        case MOP_K_INFO_DLBSZ:
			idx = idx + 2;
			break;
		default:
			if (((device == NMA_C_SOFD_LCS) ||  /* DECserver 100 */
			     (device == NMA_C_SOFD_DS2) ||  /* DECserver 200 */
			     (device == NMA_C_SOFD_DP2) ||  /* DECserver 250 */
			     (device == NMA_C_SOFD_DS3)) && /* DECserver 300 */
			    ((itype > 101) && (itype < 107)))
			{
				switch (itype) {
				case 102:
				case 103:
				case 106:
					idx = idx + ilen;
					break;
				case 104:
					idx = idx + 2;
					break;
				case 105:
					mopPrintHWA(stdout, src);
					fprintf(stdout," ");
					for (i = 0; i < ilen; i++) {
						fprintf(stdout, "%c",pkt[idx+i]);
					}
					idx = idx + ilen;
					fprintf(stdout, "\n");
					break;
				};
			} else {
				idx = idx + ilen;
			};
		}
		itype = mopGetShort(pkt,&idx); 
	}
}
