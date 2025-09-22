/*	$OpenBSD: print.c,v 1.16 2024/09/20 02:00:46 jsg Exp $ */

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

#include <sys/types.h>
#include <stdio.h>

#include "os.h"
#include "common/mopdef.h"
#include "common/nmadef.h"
#include "common/nma.h"
#include "common/cmp.h"
#include "common/get.h"

#define SHORT_PRINT

void
mopPrintHWA(FILE *fd, u_char *ap)
{
	fprintf(fd, "%x:%x:%x:%x:%x:%x", ap[0], ap[1], ap[2], ap[3], ap[4],
	    ap[5]);
	if (ap[0] < 16) fprintf(fd, " ");
	if (ap[1] < 16) fprintf(fd, " ");
	if (ap[2] < 16) fprintf(fd, " ");
	if (ap[3] < 16) fprintf(fd, " ");
	if (ap[4] < 16) fprintf(fd, " ");
	if (ap[5] < 16) fprintf(fd, " ");
}

void
mopPrintBPTY(FILE *fd, u_char bpty)
{
	switch (bpty) {
	case MOP_K_BPTY_SYS:
		fprintf(fd, "System Processor");
		break;
	case MOP_K_BPTY_COM:
		fprintf(fd, "Communication Processor");
		break;
	default:
		fprintf(fd, "Unknown");
		break;
	}
}

void
mopPrintPGTY(FILE *fd, u_char pgty)
{
	switch (pgty) {
	case MOP_K_PGTY_SECLDR:
		fprintf(fd, "Secondary Loader");
		break;
	case MOP_K_PGTY_TERLDR:
		fprintf(fd, "Tertiary Loader");
		break;
	case MOP_K_PGTY_OPRSYS:
		fprintf(fd, "Operating System");
		break;
	case MOP_K_PGTY_MGNTFL:
		fprintf(fd, "Management File");
		break;
	default:
		fprintf(fd, "Unknown");
		break;
	}
}

void
mopPrintOneline(FILE *fd, u_char *pkt, int trans)
{
	int	 idx = 0;
	u_char	*dst, *src, code;
	u_short	 proto;
	int	 len;

	trans = mopGetTrans(pkt, trans);
	mopGetHeader(pkt, &idx, &dst, &src, &proto, &len, trans);
	code = mopGetChar(pkt, &idx);

	switch (proto) {
	case MOP_K_PROTO_DL:
		fprintf(fd, "MOP DL ");
		break;
	case MOP_K_PROTO_RC:
		fprintf(fd, "MOP RC ");
		break;
	case MOP_K_PROTO_LP:
		fprintf(fd, "MOP LP ");
		break;
	default:
		switch ((proto % 256) * 256 + (proto / 256)) {
		case MOP_K_PROTO_DL:
			fprintf(fd, "MOP DL ");
			proto = MOP_K_PROTO_DL;
			break;
		case MOP_K_PROTO_RC:
			fprintf(fd, "MOP RC ");
			proto = MOP_K_PROTO_RC;
			break;
		case MOP_K_PROTO_LP:
			fprintf(fd, "MOP LP ");
			proto = MOP_K_PROTO_LP;
			break;
		default:
			fprintf(fd, "MOP ?? ");
			break;
		}
	}

	if (trans == TRANS_8023)
		fprintf(fd, "802.3 ");

	mopPrintHWA(fd, src); fprintf(fd, " > ");
	mopPrintHWA(fd, dst);
	if (len < 1600)
		fprintf(fd, " len %4d code %02x ", len, code);
	else
		fprintf(fd, " len %4d code %02x ",
		    (len % 256)*256 + (len /256), code);

	switch (proto) {
	case MOP_K_PROTO_DL:
	switch (code) {
		case MOP_K_CODE_MLT:
			fprintf(fd, "MLT ");
			break;
		case MOP_K_CODE_DCM:
			fprintf(fd, "DCM ");
			break;
		case MOP_K_CODE_MLD:
			fprintf(fd, "MLD ");
			break;
		case MOP_K_CODE_ASV:
			fprintf(fd, "ASV ");
			break;
		case MOP_K_CODE_RMD:
			fprintf(fd, "RMD ");
			break;
		case MOP_K_CODE_RPR:
			fprintf(fd, "RPR ");
			break;
		case MOP_K_CODE_RML:
			fprintf(fd, "RML ");
			break;
		case MOP_K_CODE_RDS:
			fprintf(fd, "RDS ");
			break;
		case MOP_K_CODE_MDD:
			fprintf(fd, "MDD ");
			break;
		case MOP_K_CODE_PLT:
			fprintf(fd, "PLT ");
			break;
		default:
			fprintf(fd, "??? ");
			break;
		}
		break;
	case MOP_K_PROTO_RC:
		switch (code) {
		case MOP_K_CODE_RID:
			fprintf(fd, "RID ");
			break;
		case MOP_K_CODE_BOT:
			fprintf(fd, "BOT ");
			break;
		case MOP_K_CODE_SID:
			fprintf(fd, "SID ");
			break;
		case MOP_K_CODE_RQC:
			fprintf(fd, "RQC ");
			break;
		case MOP_K_CODE_CNT:
			fprintf(fd, "CNT ");
			break;
		case MOP_K_CODE_RVC:
			fprintf(fd, "RVC ");
			break;
		case MOP_K_CODE_RLC:
			fprintf(fd, "RLC ");
			break;
		case MOP_K_CODE_CCP:
			fprintf(fd, "CCP ");
			break;
		case MOP_K_CODE_CRA:
			fprintf(fd, "CRA ");
			break;
		default:
			fprintf(fd, "??? ");
			break;
		}
		break;
	case MOP_K_PROTO_LP:
		switch (code) {
		case MOP_K_CODE_ALD:
			fprintf(fd, "ALD ");
			break;
		case MOP_K_CODE_PLD:
			fprintf(fd, "PLD ");
			break;
		default:
			fprintf(fd, "??? ");
			break;
		}
		break;
	default:
		fprintf(fd, "??? ");
		break;
	}
	fprintf(fd, "\n");
}

void
mopPrintHeader(FILE *fd, u_char *pkt, int trans)
{
	u_char	*dst, *src;
	u_short	 proto;
	int	 len, idx = 0;

	trans = mopGetTrans(pkt, trans);
	mopGetHeader(pkt, &idx, &dst, &src, &proto, &len, trans);

	fprintf(fd, "\nDst          : ");
	mopPrintHWA(fd, dst);
	if (mopCmpEAddr(dl_mcst, dst) == 0)
		fprintf(fd, " MOP Dump/Load Multicast");
	if (mopCmpEAddr(rc_mcst, dst) == 0)
		fprintf(fd, " MOP Remote Console Multicast");
	fprintf(fd, "\n");

	fprintf(fd, "Src          : ");
	mopPrintHWA(fd, src);
	fprintf(fd, "\n");
	fprintf(fd, "Proto        : %04x ", proto);

	switch (proto) {
	case MOP_K_PROTO_DL:
		switch (trans) {
		case TRANS_8023:
			fprintf(fd, "MOP Dump/Load (802.3)\n");
			break;
		default:
			fprintf(fd, "MOP Dump/Load\n");
		}
		break;
	case MOP_K_PROTO_RC:
		switch (trans) {
		case TRANS_8023:
			fprintf(fd, "MOP Remote Console (802.3)\n");
			break;
		default:
			fprintf(fd, "MOP Remote Console\n");
		}
		break;
	case MOP_K_PROTO_LP:
		switch (trans) {
		case TRANS_8023:
			fprintf(fd, "MOP Loopback (802.3)\n");
			break;
		default:
			fprintf(fd, "MOP Loopback\n");
		}
		break;
	default:
		fprintf(fd, "\n");
		break;
	}

	fprintf(fd, "Length       : %04x (%d)\n", len, len);
}

void
mopPrintMopHeader(FILE *fd, u_char *pkt, int trans)
{
	u_char	*dst, *src;
	u_short	 proto;
	int	 len, idx = 0;
	u_char   code;

	trans = mopGetTrans(pkt, trans);
	mopGetHeader(pkt, &idx, &dst, &src, &proto, &len, trans);

	code = mopGetChar(pkt, &idx);

	fprintf(fd, "Code         :   %02x ", code);

	switch (proto) {
	case MOP_K_PROTO_DL:
		switch (code) {
		case MOP_K_CODE_MLT:
			fprintf(fd, "Memory Load with transfer address\n");
			break;
		case MOP_K_CODE_DCM:
			fprintf(fd, "Dump Complete\n");
			break;
		case MOP_K_CODE_MLD:
			fprintf(fd, "Memory Load\n");
			break;
		case MOP_K_CODE_ASV:
			fprintf(fd, "Assistance volunteer\n");
			break;
		case MOP_K_CODE_RMD:
			fprintf(fd, "Request memory dump\n");
			break;
		case MOP_K_CODE_RPR:
			fprintf(fd, "Request program\n");
			break;
		case MOP_K_CODE_RML:
			fprintf(fd, "Request memory load\n");
			break;
		case MOP_K_CODE_RDS:
			fprintf(fd, "Request Dump Service\n");
			break;
		case MOP_K_CODE_MDD:
			fprintf(fd, "Memory dump data\n");
			break;
		case MOP_K_CODE_PLT:
			fprintf(fd, "Parameter load with transfer address\n");
			break;
		default:
			fprintf(fd, "(unknown)\n");
			break;
		}
		break;
	case MOP_K_PROTO_RC:
		switch (code) {
		case MOP_K_CODE_RID:
			fprintf(fd, "Request ID\n");
			break;
		case MOP_K_CODE_BOT:
			fprintf(fd, "Boot\n");
			break;
		case MOP_K_CODE_SID:
			fprintf(fd, "System ID\n");
			break;
		case MOP_K_CODE_RQC:
			fprintf(fd, "Request Counters\n");
			break;
		case MOP_K_CODE_CNT:
			fprintf(fd, "Counters\n");
			break;
		case MOP_K_CODE_RVC:
			fprintf(fd, "Reserve Console\n");
			break;
		case MOP_K_CODE_RLC:
			fprintf(fd, "Release Console\n");
			break;
		case MOP_K_CODE_CCP:
			fprintf(fd, "Console Command and Poll\n");
			break;
		case MOP_K_CODE_CRA:
			fprintf(fd, "Console Response and Acknnowledge\n");
			break;
		default:
			fprintf(fd, "(unknown)\n");
			break;
		}
		break;
	case MOP_K_PROTO_LP:
		switch (code) {
		case MOP_K_CODE_ALD:
			fprintf(fd, "Active loop data\n");
			break;
		case MOP_K_CODE_PLD:
			fprintf(fd, "Passive looped data\n");
			break;
		default:
			fprintf(fd, "(unknown)\n");
			break;
		}
		break;
	default:
		fprintf(fd, "(unknown)\n");
		break;
	}
}

void
mopPrintDevice(FILE *fd, u_char device)
{
	char	*sname, *name;

	sname = nmaGetShort((int) device);
	name = nmaGetDevice((int) device);

	fprintf(fd, "%s '%s'", sname, name);
}

void
mopPrintTime(FILE *fd, u_char *ap)
{
	fprintf(fd, "%04d-%02d-%02d %02d:%02d:%02d.%02d %d:%02d",
	    ap[0] * 100 + ap[1], ap[2], ap[3], ap[4], ap[5], ap[6], ap[7],
	    ap[8], ap[9]);
}

void
mopPrintInfo(FILE *fd, u_char *pkt, int *idx, u_short moplen, u_char mopcode,
    int trans)
{
	u_short itype, tmps;
	u_char  ilen, tmpc, device;
	u_char  uc1, uc2, uc3, *ucp;
	int     i;

	device = 0;

	switch (trans) {
	case TRANS_ETHER:
		moplen = moplen + 16;
		break;
	case TRANS_8023:
		moplen = moplen + 14;
		break;
	}

	itype = mopGetShort(pkt, idx);

	while (*idx < (moplen + 2)) {
		ilen = mopGetChar(pkt, idx);
		switch (itype) {
		case 0:
			tmpc  = mopGetChar(pkt, idx);
			*idx = *idx + tmpc;
			break;
		case MOP_K_INFO_VER:
			uc1 = mopGetChar(pkt, idx);
			uc2 = mopGetChar(pkt, idx);
			uc3 = mopGetChar(pkt, idx);
			fprintf(fd, "Maint Version: %d.%d.%d\n", uc1, uc2, uc3);
			break;
		case MOP_K_INFO_MFCT:
			tmps = mopGetShort(pkt, idx);
			fprintf(fd, "Maint Function: %04x ( ", tmps);
			if (tmps &   1) fprintf(fd, "Loop ");
			if (tmps &   2) fprintf(fd, "Dump ");
			if (tmps &   4) fprintf(fd, "Pldr ");
			if (tmps &   8) fprintf(fd, "MLdr ");
			if (tmps &  16) fprintf(fd, "Boot ");
			if (tmps &  32) fprintf(fd, "CC ");
			if (tmps &  64) fprintf(fd, "DLC ");
			if (tmps & 128) fprintf(fd, "CCR ");
			fprintf(fd, ")\n");
			break;
		case MOP_K_INFO_CNU:
			ucp = pkt + *idx;
			*idx = *idx + 6;
			fprintf(fd, "Console User : ");
			mopPrintHWA(fd, ucp);
			fprintf(fd, "\n");
			break;
		case MOP_K_INFO_RTM:
			tmps = mopGetShort(pkt, idx);
			fprintf(fd, "Reserv Timer : %04x (%d)\n", tmps, tmps);
			break;
		case MOP_K_INFO_CSZ:
			tmps = mopGetShort(pkt, idx);
			fprintf(fd, "Cons Cmd Size: %04x (%d)\n", tmps, tmps);
			break;
		case MOP_K_INFO_RSZ:
			tmps = mopGetShort(pkt, idx);
			fprintf(fd, "Cons Res Size: %04x (%d)\n", tmps, tmps);
			break;
		case MOP_K_INFO_HWA:
			ucp = pkt + *idx;
			*idx = *idx + 6;
			fprintf(fd, "Hardware Addr: ");
			mopPrintHWA(fd, ucp);
			fprintf(fd, "\n");
			break;
		case MOP_K_INFO_TIME:
			ucp = pkt + *idx;
			*idx = *idx + 10;
			fprintf(fd, "System Time: ");
			mopPrintTime(fd, ucp);
			fprintf(fd, "\n");
			break;
		case MOP_K_INFO_SOFD:
			device = mopGetChar(pkt, idx);
			fprintf(fd, "Comm Device  :   %02x ", device);
			mopPrintDevice(fd, device);
			fprintf(fd, "\n");
			break;
		case MOP_K_INFO_SFID:
			tmpc = mopGetChar(pkt, idx);
			fprintf(fd, "Software ID  :   %02x ", tmpc);
			if (tmpc == 0)
				fprintf(fd, "No software id");
			if (tmpc == 254) {
				fprintf(fd, "Maintenance system");
				tmpc = 0;
			}
			if (tmpc == 255) {
				fprintf(fd, "Standard operating system");
				tmpc = 0;
			}
			if (tmpc > 0) {
				fprintf(fd, "'");
				for (i = 0; i < ((int) tmpc); i++)
					fprintf(fd, "%c",
					    mopGetChar(pkt, idx));
				fprintf(fd, "'");
			}
			fprintf(fd, "\n");
			break;
		case MOP_K_INFO_PRTY:
			tmpc = mopGetChar(pkt, idx);
			fprintf(fd, "System Proc  :   %02x ", tmpc);
			switch (tmpc) {
			case MOP_K_PRTY_11:
				fprintf(fd, "PDP-11\n");
				break;
			case MOP_K_PRTY_CMSV:
				fprintf(fd, "Communication Server\n");
				break;
			case MOP_K_PRTY_PRO:
				fprintf(fd, "Professional\n");
				break;
			case MOP_K_PRTY_SCO:
				fprintf(fd, "Scorpio\n");
				break;
			case MOP_K_PRTY_AMB:
				fprintf(fd, "Amber\n");
				break;
			case MOP_K_PRTY_BRI:
				fprintf(fd, "XLII Bridge\n");
				break;
			default:
				fprintf(fd, "Unknown\n");
				break;
			}
			break;
		case MOP_K_INFO_DLTY:
			tmpc = mopGetChar(pkt, idx);
			fprintf(fd, "DLnk Type    :   %02x ", tmpc);
			switch (tmpc) {
			case MOP_K_DLTY_NI:
				fprintf(fd, "Ethernet\n");
				break;
			case MOP_K_DLTY_DDCMP:
				fprintf(fd, "DDCMP\n");
				break;
			case MOP_K_DLTY_LAPB:
				fprintf(fd, "LAPB (X.25)\n");
				break;
			default:
				fprintf(fd, "Unknown\n");
				break;
			}
			break;
		case MOP_K_INFO_DLBSZ:
			tmps = mopGetShort(pkt, idx);
			fprintf(fd, "DLnk Buf Size: %04x (%d)\n", tmps, tmps);
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
					ucp = pkt + *idx;
					*idx = *idx + ilen;
					fprintf(fd, "ROM SW Ver   :   %02x '",
					    ilen);
					for (i = 0; i < ilen; i++)
						fprintf(fd, "%c", ucp[i]);
					fprintf(fd, "'\n");
					break;
				case 103:
					ucp = pkt + *idx;
					*idx = *idx + ilen;
					fprintf(fd, "Loaded SW Ver:   %02x '",
					    ilen);
					for (i = 0; i < ilen; i++)
						fprintf(fd, "%c", ucp[i]);
					fprintf(fd, "'\n");
					break;
				case 104:
					tmps = mopGetShort(pkt, idx);
					fprintf(fd,
					    "DECnet Addr  : %d.%d (%d)\n",
					    tmps / 1024, tmps % 1024, tmps);
					break;
				case 105:
					ucp = pkt + *idx;
					*idx = *idx + ilen;
					fprintf(fd, "Node Name    :   %02x '",
					    ilen);
					for (i = 0; i < ilen; i++)
						fprintf(fd, "%c", ucp[i]);
					fprintf(fd, "'\n");
					break;
				case 106:
					ucp = pkt + *idx;
					*idx = *idx + ilen;
					fprintf(fd, "Node Ident   :   %02x '",
					    ilen);
					for (i = 0; i < ilen; i++)
						fprintf(fd, "%c", ucp[i]);
					fprintf(fd, "'\n");
					break;
				}
			} else {
				ucp = pkt + *idx;
				*idx = *idx + ilen;
				fprintf(fd, "Info Type    : %04x (%d)\n",
				    itype, itype);
				fprintf(fd, "Info Data    :   %02x ", ilen);
				for (i = 0; i < ilen; i++) {
					if ((i % 16) == 0)
						if ((i / 16) != 0)
							fprintf(fd,
						     "\n                    ");
					fprintf(fd, "%02x ", ucp[i]);
				}
				fprintf(fd, "\n");
			}
		}
		itype = mopGetShort(pkt, idx);
	}
}
