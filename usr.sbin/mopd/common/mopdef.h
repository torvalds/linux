/*	$OpenBSD: mopdef.h,v 1.6 2017/01/21 08:33:51 krw Exp $ */

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
 *
 *	$OpenBSD: mopdef.h,v 1.6 2017/01/21 08:33:51 krw Exp $
 *
 */

#ifndef _MOPDEF_H_
#define _MOPDEF_H_

#define MOP_K_PROTO_DL          0x6001  /* MOP Load/Dump Protocol */
#define MOP_K_PROTO_RC          0x6002  /* MOP Remote Console Protocol */
#define MOP_K_PROTO_LP          0x9000  /* MOP Loopback Protocol */

#define MOP_K_PROTO_802_DL	{ 0x08, 0x00, 0x2b, 0x60, 0x01 }
#define MOP_K_PROTO_802_RC	{ 0x08, 0x00, 0x2b, 0x60, 0x02 }
#define MOP_K_PROTO_802_LP	{ 0x08, 0x00, 0x2b, 0x90, 0x00 }

#define MOP_K_PROTO_802_DSAP	0xaa
#define MOP_K_PROTO_802_SSAP	0xaa
#define MOP_K_PROTO_802_CNTL	0x03

#define TRANS_ETHER		1	/* Packet in Ethernet format */
#define TRANS_8023		2	/* Packet in 802.3 format */
#define TRANS_AND		0x1000	/* Both Ethernet and 802.3 */

/* The following constants are defined in module MOPDEF.SDL in MOM */

#define MOP_K_CODE_MIN          0
#define MOP_K_CODE_MLT          0       /* Memory Load with transfer address */
#define MOP_K_CODE_DCM          1       /* Dump Complete */
#define MOP_K_CODE_MLD          2       /* Memory Load */
#define MOP_K_CODE_ASV          3       /* Assistance volunteer (NI only */
#define MOP_K_CODE_RMD          4       /* Request memory dump */
#define MOP_K_CODE_RID          5       /* Request ID */
#define MOP_K_CODE_BOT          6       /* Boot */
#define MOP_K_CODE_SID          7       /* System ID */
#define MOP_K_CODE_RPR          8       /* Request program */
#define MOP_K_CODE_RQC          9       /* Request Counters */
#define MOP_K_CODE_RML          10      /* Request memory load */
#define MOP_K_CODE_CNT          11      /* Counters */
#define MOP_K_CODE_RDS          12      /* Request Dump Service */
#define MOP_K_CODE_MMR          12      /* MOP Mode Running */
#define MOP_K_CODE_RVC          13      /* Reserve Console */
#define MOP_K_CODE_MDD          14      /* Memory dump data */
#define MOP_K_CODE_RLC          15      /* Release Console */
#define MOP_K_CODE_CCP          17      /* Console Command and Poll */
#define MOP_K_CODE_CRA          19      /* Console Response and Acknnowledge */
#define MOP_K_CODE_PLT          20      /* Parameter load with transfer address*/
#define MOP_K_CODE_ALD          24      /* Active loop data */
#define MOP_K_CODE_PLD          26      /* Passive looped data */
#define MOP_K_CODE_MAX          26

#define MOP_K_PGTY_MIN          0
#define MOP_K_PGTY_SECLDR       0       /* Secondary Loader */
#define MOP_K_PGTY_TERLDR       1       /* Tertiary Loader */
#define MOP_K_PGTY_OPRSYS       2       /* Operating System */
#define MOP_K_PGTY_MGNTFL       3       /* Management File */
#define MOP_K_PGTY_MAX          3

#define MOP_K_BPTY_MIN          0
#define MOP_K_BPTY_SYS          0       /* System Processor */
#define MOP_K_BPTY_COM          1       /* Communication Processor */
#define MOP_K_BPTY_MAX          1

#define MOP_K_RML_ERROR_MIN     0
#define MOP_K_RML_ERROR_NONE    0       /* None */
#define MOP_K_RML_ERROR_NO_LOAD 1       /* Image data not properly loaded */
#define MOP_K_RML_ERROR_MAX     1

#define MOP_K_PLTP_MIN          0
#define MOP_K_PLTP_END          0       /* End Mark */
#define MOP_K_PLTP_TSN          1       /* Target System Name */
#define MOP_K_PLTP_TSA          2       /* Target System Address */
#define MOP_K_PLTP_HSN          3       /* Host System Name */
#define MOP_K_PLTP_HSA          4       /* Host System Address */
#define MOP_K_PLTP_HST          5       /* Host System Time */
#define MOP_K_PLTP_MAX          5

#define MOP_K_BOT_CNTL_MIN      0
#define MOP_K_BOT_CNTL_SERVER   0       /* Boot-Server */
#define MOP_K_BOT_CNTL_DEVICE   1       /* Boot-Device */
#define MOP_K_BOT_CNTL_MAX      1

#define MOP_K_INFO_VER          1       /* Maintenance Version */
#define MOP_K_INFO_MFCT         2       /* Maintenance Functions */
#define MOP_K_INFO_CNU          3       /* Console User */
#define MOP_K_INFO_RTM          4       /* Reservation Timer */
#define MOP_K_INFO_CSZ          5       /* Console Command Size */
#define MOP_K_INFO_RSZ          6       /* Console Response Size */
#define MOP_K_INFO_HWA          7       /* Hardware Address */
#define MOP_K_INFO_TIME         8       /* System Time */
#define MOP_K_INFO_SOFD         100     /* Communication Device */
#define MOP_K_INFO_SFID         200     /* Software ID */
#define MOP_K_INFO_PRTY         300     /* System Processor */
#define MOP_K_INFO_DLTY         400     /* Data Link Type */
#define MOP_K_INFO_DLBSZ        401     /* Data Link Buffer Size */

#define MOP_K_DLTY_MIN          1
#define MOP_K_DLTY_NI           1       /* Ethernet */
#define MOP_K_DLTY_DDCMP        2       /* DDCMP */
#define MOP_K_DLTY_LAPB         3       /* LAPB (frame level of X.25) */
#define MOP_K_DLTY_MAX          3

#define MOP_K_PRTY_MIN          0
#define MOP_K_PRTY_11           1       /* PDP-11 (UNIBUS) */
#define MOP_K_PRTY_CMSV         2       /* Communication Server */
#define MOP_K_PRTY_PRO          3       /* Professional */
#define MOP_K_PRTY_SCO          4       /* Scorpio */
#define MOP_K_PRTY_AMB          5       /* Amber */
#define MOP_K_PRTY_BRI          6       /* XLII Bridge */
#define MOP_K_PRTY_MAX          6

#define MOP_K_SFID_FORM_MIN     -2
#define MOP_K_SFID_FORM_MAINT   -2      /* Maintenance System */
#define MOP_K_SFID_FORM_OPRSYS  -1      /* Standard Operating System */
#define MOP_K_SFID_FORM_NONE    0       /* None */
#define MOP_K_SFID_FORM_MAX     0

#define MOP_K_SFID_CUST         'CP'    /* Customer product */
#define MOP_K_SFID_DEC          'DP'    /* DEC product */
#define MOP_K_SFID_DELIM_ID     '#'     /* Delimiter identifier */

#define MOP_K_DLBSZ_DEFAULT     262     /* Buffersize */

#define MOP_K_NILOOP_REPLY      1       /* Response */
#define MOP_K_NILOOP_FORWARD    2       /* Forward Data */

#define MOP_DL_MULTICAST	{ 0xab, 0x00, 0x00, 0x01, 0x00, 0x00 }
#define MOP_RC_MULTICAST	{ 0xab, 0x00, 0x00, 0x02, 0x00, 0x00 }
#define MOP_LP_MULTICAST	{ 0xcf, 0x00, 0x00, 0x00, 0x00, 0x00 }

#define MOP_K_RPR_FORMAT_V3	1	/* Format Version of RPR */
#define MOP_K_RPR_FORMAT	4	/* Format Version of RPR */

#define IHD_C_MINCODE		-1	/* Low bound of ALIAS value */
#define IHD_C_NATIVE		-1	/* Native mode image */
#define IHD_C_RSX		0	/* RSX image produced by TKB */
#define IHD_C_BPA		1	/* BASIC plus analog */
#define IHD_C_ALIAS		2	/* Last 126 bytes contains ASCIC of image to activate */
#define IHD_C_CLI		3	/* Image is CLI, run LOGINOUT */
#define IHD_C_PMAX		4	/* PMAX system image */
#define IHD_C_ALPHA		5	/* ALPHA system image */
#define IHD_C_MAXCODE		5	/* High bound of ALIAS value */

#define IHD_W_SIZE		0
#define IHD_W_ACTIVOFF		2
#define IHD_B_HDRBLKCNT		16
#define IHD_W_ALIAS		510
#define	ISD_W_PAGCNT		2
#define ISD_V_VPN		4
#define ISD_M_VPN		0x1fffff
#define IHA_L_TFRADR1		0
#define EISD_L_SECSIZE		12
#define EIHD_L_ISDOFF		12
#define EIHD_L_HDRBLKCNT	76

#define L_BSA			0x08	/* RSX base address */
#define L_BLDZ			0x0e	/* RSX image size (* 64) */
#define L_BXFR			0xe8	/* RSX transfer address */
#define L_BBLK			0xf0	/* RSX header block count */

#ifndef MOPDEF_SUPRESS_EXTERN
extern u_char dl_mcst[];
extern u_char rc_mcst[];
extern u_char dl_802_proto[];
extern u_char rc_802_proto[];
extern u_char lp_802_proto[];
#endif /* MOPDEF_SUPRESS_EXTERN */

#endif /* _MOPDEF_H_ */
