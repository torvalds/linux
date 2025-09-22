/*	$OpenBSD: process.c,v 1.25 2023/03/08 04:43:14 guenther Exp $ */

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

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"
#include "common/nmadef.h"
#include "common/get.h"
#include "common/put.h"
#include "common/print.h"
#include "common/pf.h"
#include "common/cmp.h"
#include "common/dl.h"
#include "common/rc.h"
#include "common/file.h"

extern int	DebugFlag;

struct dllist dllist[MAXDL];		/* dump/load list		*/

void
mopProcessInfo(u_char *pkt, int *idx, u_short moplen, struct dllist *dl_rpr,
    int trans)
{
	u_short	itype, tmps;
	u_char	ilen, tmpc, device;

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

	while (*idx < (int)(moplen)) {
		ilen  = mopGetChar(pkt, idx);
		switch (itype) {
		case 0:
			tmpc = mopGetChar(pkt, idx);
			*idx = *idx + tmpc;
			break;
		case MOP_K_INFO_VER:
			*idx = *idx + 3;
			break;
		case MOP_K_INFO_MFCT:
		case MOP_K_INFO_RTM:
		case MOP_K_INFO_CSZ:
		case MOP_K_INFO_RSZ:
			mopGetShort(pkt, idx);
			break;
		case MOP_K_INFO_CNU:
		case MOP_K_INFO_HWA:
			*idx = *idx + 6;
			break;
		case MOP_K_INFO_TIME:
			*idx = *idx + 10;
			break;
		case MOP_K_INFO_SOFD:
			device = mopGetChar(pkt, idx);
			break;
		case MOP_K_INFO_SFID:
			tmpc = mopGetChar(pkt, idx);
			*idx = *idx + tmpc;
			break;
		case MOP_K_INFO_PRTY:
		case MOP_K_INFO_DLTY:
			mopGetChar(pkt, idx);
			break;
		case MOP_K_INFO_DLBSZ:
			tmps = mopGetShort(pkt, idx);
			dl_rpr->dl_bsz = tmps;
			break;
		default:
			if (((device == NMA_C_SOFD_LCS) ||  /* DECserver 100 */
			     (device == NMA_C_SOFD_DS2) ||  /* DECserver 200 */
			     (device == NMA_C_SOFD_DP2) ||  /* DECserver 250 */
			     (device == NMA_C_SOFD_DS3)) && /* DECserver 300 */
			    ((itype > 101) && (itype < 107))) {
				switch (itype) {
				case 102:
				case 103:
				case 105:
				case 106:
					*idx = *idx + ilen;
					break;
				case 104:
					mopGetShort(pkt, idx);
					break;
				}
			} else
				*idx = *idx + ilen;
		}
		itype = mopGetShort(pkt, idx);
	}
}

void
mopSendASV(u_char *dst, u_char *src, struct if_info *ii, int trans)
{
	u_char	 pkt[200];
	int	 idx;

	idx = 0;
	mopPutHeader(pkt, &idx, dst, src, MOP_K_PROTO_DL, trans);

	mopPutChar(pkt, &idx, MOP_K_CODE_ASV);

	mopPutLength(pkt, trans, idx);

	if (DebugFlag == DEBUG_ONELINE)
		mopPrintOneline(stdout, pkt, trans);

	if (DebugFlag >= DEBUG_HEADER) {
		mopPrintHeader(stdout, pkt, trans);
		mopPrintMopHeader(stdout, pkt, trans);
	}

	if (DebugFlag >= DEBUG_INFO)
		mopDumpDL(stdout, pkt, trans);

	if (pfWrite(ii->fd, pkt, idx, trans) != idx)
		if (DebugFlag)
			warnx("pfWrite() error");
}

void
mopStartLoad(u_char *dst, u_char *src, struct dllist *dl_rpr, int trans)
{
	int	 len;
	int	 i, slot;
	u_char	 pkt[BUFSIZE];
	int	 idx;
	u_char	 mopcode = MOP_K_CODE_MLD;

	slot = -1;

	/* Look if we have a non terminated load, if so, use its slot */
	for (i = 0; i < MAXDL && slot == -1; i++)
		if (dllist[i].status != DL_STATUS_FREE)
			if (mopCmpEAddr(dllist[i].eaddr, dst) == 0)
				slot = i;

	/* If no slot yet, then find first free */
	for (i = 0; slot == -1 && i < MAXDL; i++)
		if (dllist[i].status == DL_STATUS_FREE) {
			slot = i;
			bcopy(dst,  dllist[i].eaddr, 6);
		}

	/* If no slot yet, then return. No slot is free */	
	if (slot == -1)
		return;

	/* Ok, save info from RPR */
	dllist[slot] = *dl_rpr;
	dllist[slot].status = DL_STATUS_READ_IMGHDR;

	/* Get Load and Transfer Address. */
	GetFileInfo(&dllist[slot], 0);

	dllist[slot].nloadaddr = dllist[slot].loadaddr;
	dllist[slot].lseek     = lseek(dllist[slot].ldfd, 0L, SEEK_CUR);
	dllist[slot].a_lseek   = 0;

	dllist[slot].count     = 0;
	if ((dllist[slot].dl_bsz >= 1492) || (dllist[slot].dl_bsz == 0))
		dllist[slot].dl_bsz = 1492;
	if (dllist[slot].dl_bsz == 1030)	/* VS/uVAX 2000 needs this */
		dllist[slot].dl_bsz = 1000;
	if (trans == TRANS_8023)
		dllist[slot].dl_bsz = dllist[slot].dl_bsz - 8;

	idx = 0;
	mopPutHeader(pkt, &idx, dst, src, MOP_K_PROTO_DL, trans);
	mopPutChar(pkt, &idx, mopcode);

	mopPutChar(pkt, &idx, dllist[slot].count);
	mopPutLong(pkt, &idx, dllist[slot].loadaddr);

	len = mopFileRead(&dllist[slot], &pkt[idx]);

	dllist[slot].nloadaddr = dllist[slot].loadaddr + len;
	idx = idx + len;

	mopPutLength(pkt, trans, idx);

	if (DebugFlag == DEBUG_ONELINE)
		mopPrintOneline(stdout, pkt, trans);

	if (DebugFlag >= DEBUG_HEADER) {
		mopPrintHeader(stdout, pkt, trans);
		mopPrintMopHeader(stdout, pkt, trans);
	}

	if (DebugFlag >= DEBUG_INFO)
		mopDumpDL(stdout, pkt, trans);

	if (pfWrite(dllist[slot].ii->fd, pkt, idx, trans) != idx)
		if (DebugFlag)
			warnx("pfWrite() error");

	dllist[slot].status = DL_STATUS_SENT_MLD;
}

void
mopNextLoad(u_char *dst, u_char *src, u_char new_count, int trans)
{
	int	 len;
	int	 i, slot;
	u_char	 pkt[BUFSIZE];
	int	 idx, pidx;
	char	 line[100],hname[17],*p;

	slot = -1;

	for (i = 0; i < MAXDL && slot == -1; i++)
		if (dllist[i].status != DL_STATUS_FREE) {
			if (mopCmpEAddr(dst, dllist[i].eaddr) == 0)
				slot = i;
		}

	/* If no slot yet, then return. No slot is free */	
	if (slot == -1)
		return;

	if (new_count == ((dllist[slot].count+1) % 256)) {
		dllist[slot].loadaddr = dllist[slot].nloadaddr;
		dllist[slot].count    = new_count;
	} else
		return;

	if (dllist[slot].status == DL_STATUS_SENT_PLT) {
		close(dllist[slot].ldfd);
		dllist[slot].ldfd = 0;
		dllist[slot].status = DL_STATUS_FREE;
		snprintf(line, sizeof(line),
		    "%x:%x:%x:%x:%x:%x Load completed",
		    dst[0], dst[1], dst[2], dst[3], dst[4], dst[5]);
		syslog(LOG_INFO, "%s", line);
		return;
	}

	dllist[slot].lseek = lseek(dllist[slot].ldfd, 0L, SEEK_CUR);

	if (dllist[slot].dl_bsz >= 1492)
		dllist[slot].dl_bsz = 1492;

	idx = 0;
	mopPutHeader(pkt, &idx, dst, src, MOP_K_PROTO_DL, trans);
	pidx = idx;
	mopPutChar(pkt, &idx, MOP_K_CODE_MLD);
	mopPutChar(pkt, &idx, dllist[slot].count);
	mopPutLong(pkt, &idx, dllist[slot].loadaddr);

	len = mopFileRead(&dllist[slot], &pkt[idx]);

	if (len > 0) {
		dllist[slot].nloadaddr = dllist[slot].loadaddr + len;
		idx = idx + len;

		mopPutLength(pkt, trans, idx);
	} else {
		if (len == 0) {
			i = gethostname(hname, sizeof(hname));
			p = strchr(hname, '.');
			if (p != NULL)
				*p = 0;

			idx = pidx;
			mopPutChar(pkt, &idx, MOP_K_CODE_PLT);
			mopPutChar(pkt, &idx, dllist[slot].count);
			mopPutChar(pkt, &idx, MOP_K_PLTP_HSN);
			mopPutChar(pkt, &idx, (int)strlen(hname));
			mopPutMulti(pkt, &idx, (u_char *)hname, (int)strlen(hname));
			mopPutChar(pkt, &idx, MOP_K_PLTP_HSA);
			mopPutChar(pkt, &idx, 6);
			mopPutMulti(pkt, &idx, src, 6);
			mopPutChar(pkt, &idx, MOP_K_PLTP_HST);
			mopPutTime(pkt, &idx, 0);
			mopPutChar(pkt, &idx, 0);
			mopPutLong(pkt, &idx, dllist[slot].xferaddr);

			mopPutLength(pkt, trans, idx);

			dllist[slot].status = DL_STATUS_SENT_PLT;
		} else {
			dllist[slot].status = DL_STATUS_FREE;
			return;
		}
	}

	if (DebugFlag == DEBUG_ONELINE)
		mopPrintOneline(stdout, pkt, trans);

	if (DebugFlag >= DEBUG_HEADER) {
		mopPrintHeader(stdout, pkt, trans);
		mopPrintMopHeader(stdout, pkt, trans);
	}

	if (DebugFlag >= DEBUG_INFO)
		mopDumpDL(stdout, pkt, trans);

	if (pfWrite(dllist[slot].ii->fd, pkt, idx, trans) != idx)
		if (DebugFlag)
			warnx("pfWrite() error");
}

void
mopProcessDL(FILE *fd, struct if_info *ii, u_char *pkt, int *idx, u_char *dst,
    u_char *src, int trans, u_short len)
{
	u_char		tmpc;
	u_short		moplen;
	u_char		pfile[129], mopcode;
	char		filename[FILENAME_MAX];
	char		line[100];
	int		i, nfd;
	struct dllist	dl, *dl_rpr;
	u_char		load;

	if (DebugFlag == DEBUG_ONELINE)
		mopPrintOneline(stdout, pkt, trans);

	if (DebugFlag >= DEBUG_HEADER) {
		mopPrintHeader(stdout, pkt, trans);
		mopPrintMopHeader(stdout, pkt, trans);
	}

	if (DebugFlag >= DEBUG_INFO)
		mopDumpDL(stdout, pkt, trans);

	moplen  = mopGetLength(pkt, trans);
	mopcode = mopGetChar(pkt, idx);

	switch (mopcode) {
	case MOP_K_CODE_MLT:
		break;
	case MOP_K_CODE_DCM:
		break;
	case MOP_K_CODE_MLD:
		break;
	case MOP_K_CODE_ASV:
		break;
	case MOP_K_CODE_RMD:
		break;
	case MOP_K_CODE_RPR:
		mopGetChar(pkt, idx);			/* Device Type */
		tmpc = mopGetChar(pkt, idx);		/* Format Version */
		if ((tmpc != MOP_K_RPR_FORMAT) &&
		    (tmpc != MOP_K_RPR_FORMAT_V3)) {
			fprintf(stderr, "mopd: Unknown RPR Format (%d) from ",
			    tmpc);
			mopPrintHWA(stderr, src);
			fprintf(stderr, "\n");
		}

		mopGetChar(pkt, idx);			/* Program Type */

		tmpc = mopGetChar(pkt, idx);		/* Software ID Len */
		if (tmpc > sizeof(pfile) - 1)
			return;
		for (i = 0; i < tmpc; i++) {
			pfile[i] = mopGetChar(pkt, idx);
			pfile[i+1] = '\0';
		}

		if (tmpc == 0) {
			/* In a normal implementation of a MOP Loader this */
			/* would cause a question to NML (DECnet) if this  */
			/* node is known and if so what image to load. But */
			/* we don't have DECnet so we don't have anybody   */
			/* to ask. My solution is to use the ethernet addr */
			/* as filename. Implementing a database would be   */
			/* overkill.					   */
			snprintf((char *)pfile, sizeof pfile,
			    "%02x%02x%02x%02x%02x%02x%c",
			    src[0], src[1], src[2], src[3], src[4], src[5], 0);
		}

		mopGetChar(pkt, idx);			/* Processor */

		dl_rpr = &dl;
		bzero(dl_rpr, sizeof(*dl_rpr));
		dl_rpr->ii = ii;
		bcopy(src, dl_rpr->eaddr, 6);
		mopProcessInfo(pkt, idx, moplen, dl_rpr, trans);

		snprintf(filename, sizeof(filename), "%s.SYS", pfile);
		if ((mopCmpEAddr(dst, dl_mcst) == 0)) {
			if ((nfd = open(filename, O_RDONLY)) != -1) {
				close(nfd);
				mopSendASV(src, ii->eaddr, ii, trans);
				snprintf(line, sizeof(line),
				    "%x:%x:%x:%x:%x:%x (%d) Do you have %s? "
				    "(Yes)", src[0], src[1], src[2], src[3],
				    src[4], src[5], trans, pfile);
			} else {
				snprintf(line, sizeof(line),
				    "%x:%x:%x:%x:%x:%x (%d) Do you have %s? "
				    "(No)", src[0], src[1], src[2], src[3],
				    src[4], src[5], trans, pfile);
			}
			syslog(LOG_INFO, "%s", line);
		} else {
			if ((mopCmpEAddr(dst, ii->eaddr) == 0)) {
				dl_rpr->ldfd = open(filename, O_RDONLY);
				mopStartLoad(src, ii->eaddr, dl_rpr, trans);
				snprintf(line, sizeof(line),
				    "%x:%x:%x:%x:%x:%x Send me %s",
				    src[0], src[1], src[2], src[3], src[4],
				    src[5], pfile);
				syslog(LOG_INFO, "%s", line);
			}
		}
		break;
	case MOP_K_CODE_RML:
		load = mopGetChar(pkt, idx);		/* Load Number	*/
		mopGetChar(pkt, idx);			/* Error	*/
		if ((mopCmpEAddr(dst, ii->eaddr) == 0))
			mopNextLoad(src, ii->eaddr, load, trans);
		break;
	case MOP_K_CODE_RDS:
		break;
	case MOP_K_CODE_MDD:
		break;
	case MOP_K_CODE_CCP:
		break;
	case MOP_K_CODE_PLT:
		break;
	default:
		break;
	}
}

void
mopProcessRC(FILE *fd, struct if_info *ii, u_char *pkt, int *idx, u_char dst,
    u_char *src, int trans, u_short len)
{
	u_char		tmpc;
	u_short		tmps, moplen = 0;
	u_char		mopcode;
	struct dllist	dl, *dl_rpr;

	if (DebugFlag == DEBUG_ONELINE)
		mopPrintOneline(stdout, pkt, trans);

	if (DebugFlag >= DEBUG_HEADER) {
		mopPrintHeader(stdout, pkt, trans);
		mopPrintMopHeader(stdout, pkt, trans);
	}

	if (DebugFlag >= DEBUG_INFO)
		mopDumpRC(stdout, pkt, trans);

	moplen  = mopGetLength(pkt, trans);
	mopcode = mopGetChar(pkt, idx);

	switch (mopcode) {
	case MOP_K_CODE_RID:
		break;
	case MOP_K_CODE_BOT:
		break;
	case MOP_K_CODE_SID:
		tmpc = mopGetChar(pkt, idx);		/* Reserved */

		if ((DebugFlag >= DEBUG_INFO))
			fprintf(stderr, "Reserved     :   %02x\n", tmpc);

		tmps = mopGetShort(pkt, idx);		/* Receipt # */
		if ((DebugFlag >= DEBUG_INFO))
			fprintf(stderr, "Receipt Nbr  : %04x\n", tmps);

		dl_rpr = &dl;
		bzero(dl_rpr, sizeof(*dl_rpr));
		dl_rpr->ii = ii;
		bcopy(src, dl_rpr->eaddr, 6);
		mopProcessInfo(pkt, idx, moplen, dl_rpr, trans);
		break;
	case MOP_K_CODE_RQC:
		break;
	case MOP_K_CODE_CNT:
		break;
	case MOP_K_CODE_RVC:
		break;
	case MOP_K_CODE_RLC:
		break;
	case MOP_K_CODE_CCP:
		break;
	case MOP_K_CODE_CRA:
		break;
	default:
		break;
	}
}
