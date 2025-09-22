/*	$OpenBSD: rc.c,v 1.9 2017/07/29 07:18:03 florian Exp $ */

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
#include "common/get.h"
#include "common/print.h"
#include "common/mopdef.h"

void
mopDumpRC(FILE *fd, u_char *pkt, int trans)
{
	int	i, idx = 0;
	long	tmpl;
	u_char	tmpc, code, control;
	u_short	len, tmps, moplen;

	len = mopGetLength(pkt, trans);

	switch (trans) {
	case TRANS_8023:
		idx = 22;
		moplen = len - 8;
		break;
	default:
		idx = 16;
		moplen = len;
	}
	code = mopGetChar(pkt, &idx);

	switch (code) {
	case MOP_K_CODE_RID:

		tmpc = mopGetChar(pkt, &idx);
		fprintf(fd, "Reserved     :   %02x\n", tmpc);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Receipt Nbr  : %04x\n", tmps);

		break;
	case MOP_K_CODE_BOT:

		if (moplen == 5) {
			tmps = mopGetShort(pkt, &idx);
			fprintf(fd, "Verification : %04x\n", tmps);
		} else {

			tmpl = mopGetLong(pkt, &idx);
			fprintf(fd, "Verification : %08lx\n", tmpl);

			tmpc = mopGetChar(pkt, &idx);	/* Processor */
			fprintf(fd, "Processor    :   %02x ", tmpc);
			mopPrintBPTY(fd, tmpc);  fprintf(fd, "\n");

			control = mopGetChar(pkt, &idx);	/* Control */
			fprintf(fd, "Control    :   %02x ", control);
			if ((control & (1<<MOP_K_BOT_CNTL_SERVER)))
				fprintf(fd, "Bootserver Requesting system ");
			else
				fprintf(fd, "Bootserver System default ");
			if ((control & (1<<MOP_K_BOT_CNTL_DEVICE)))
				fprintf(fd, "Bootdevice Specified device");
			else
				fprintf(fd, "Bootdevice System default");
			fprintf(fd, "\n");

			if ((control & (1<<MOP_K_BOT_CNTL_DEVICE))) {
				tmpc = mopGetChar(pkt, &idx);/* Device ID */
				fprintf(fd, "Device ID    :   %02x '", tmpc);
				for (i = 0; i < ((int) tmpc); i++)
					fprintf(fd, "%c",
					    mopGetChar(pkt, &idx));
				fprintf(fd, "'\n");
			}

			tmpc = mopGetChar(pkt, &idx);      /* Software ID */
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
					    mopGetChar(pkt, &idx));
				fprintf(fd, "'");
			}
			fprintf(fd, "'\n");

		}
		break;
	case MOP_K_CODE_SID:

		tmpc = mopGetChar(pkt, &idx);		/* Reserved */
		fprintf(fd, "Reserved     :   %02x\n", tmpc);

		tmps = mopGetShort(pkt, &idx);		/* Receipt # */
		fprintf(fd, "Receipt Nbr  : %04x\n", tmpc);

		mopPrintInfo(fd, pkt, &idx, moplen, code, trans);

		break;
	case MOP_K_CODE_RQC:

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Receipt Nbr  : %04x\n", tmps);

		break;
	case MOP_K_CODE_CNT:

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Receipt Nbr  : %04x %d\n", tmps, tmps);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Last Zeroed  : %04x %d\n", tmps, tmps);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Bytes rec    : %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Bytes snd    : %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Frames rec   : %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Frames snd   : %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Mcst Bytes re: %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Mcst Frame re: %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Frame snd, def: %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Frame snd, col: %08lx %ld\n", tmpl, tmpl);

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Frame snd, mcl: %08lx %ld\n", tmpl, tmpl);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Snd failure  : %04x %d\n", tmps, tmps);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Snd fail reas: %04x ", tmps);
		if (tmps & 1)
			fprintf(fd, "Excess col  ");
		if (tmps & 2)
			fprintf(fd, "Carrier chk fail  ");
		if (tmps & 4)
			fprintf(fd, "Short circ  ");
		if (tmps & 8)
			fprintf(fd, "Open circ  ");
		if (tmps & 16)
			fprintf(fd, "Frm to long  ");
		if (tmps & 32)
			fprintf(fd, "Rem fail to defer  ");
		fprintf(fd, "\n");

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Rec failure  : %04x %d\n", tmps, tmps);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Rec fail reas: %04x ", tmps);
		if (tmps & 1)
			fprintf(fd, "Block chk err  ");
		if (tmps & 2)
			fprintf(fd, "Framing err  ");
		if (tmps & 4)
			fprintf(fd, "Frm to long  ");
		fprintf(fd, "\n");

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Unrec frm dst: %04x %d\n", tmps, tmps);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Data overrun : %04x %d\n", tmps, tmps);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Sys Buf Unava: %04x %d\n", tmps, tmps);

		tmps = mopGetShort(pkt, &idx);
		fprintf(fd, "Usr Buf Unava: %04x %d\n", tmps, tmps);

		break;
	case MOP_K_CODE_RVC:

		tmpl = mopGetLong(pkt, &idx);
		fprintf(fd, "Verification : %08lx\n", tmpl);

		break;
	case MOP_K_CODE_RLC:

		/* Empty message */

		break;
	case MOP_K_CODE_CCP:
		tmpc = mopGetChar(pkt, &idx);
		fprintf(fd, "Control Flags: %02x Message %d ", tmpc, tmpc & 1);
		if (tmpc & 2)
			fprintf(fd, "Break");
		fprintf(fd, "\n");

		if (moplen > 2) {
			for (i = 0; i < (moplen - 2); i++) {
				if ((i % 16) == 0) {
					if ((i / 16) == 0)
						fprintf(fd,
						    "Image Data   : %04x ",
						    moplen-2);
					else
						fprintf(fd,
						    "                    ");
				}
				fprintf(fd, "%02x ", mopGetChar(pkt, &idx));
				if ((i % 16) == 15)
					fprintf(fd, "\n");
			}
			if ((i % 16) != 15)
				fprintf(fd, "\n");
		}

		break;
	case MOP_K_CODE_CRA:

		tmpc = mopGetChar(pkt, &idx);
		fprintf(fd, "Control Flags: %02x Message %d ", tmpc, tmpc & 1);
		if (tmpc & 2)
			fprintf(fd, "Cmd Data Lost ");
		if (tmpc & 4)
			fprintf(fd, "Resp Data Lost ");
		fprintf(fd, "\n");

		if (moplen > 2) {
			for (i = 0; i < (moplen - 2); i++) {
				if ((i % 16) == 0) {
					if ((i / 16) == 0)
						fprintf(fd,
						    "Image Data   : %04x ",
						    moplen-2);
					else
						fprintf(fd,
						    "                    ");
				}
				fprintf(fd, "%02x ", mopGetChar(pkt, &idx));
				if ((i % 16) == 15)
					fprintf(fd, "\n");
			}
			if ((i % 16) != 15)
				fprintf(fd, "\n");
		}

		break;
	default:
		break;
	}
}
