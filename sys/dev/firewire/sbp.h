/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $FreeBSD$
 *
 */

#define ORB_NOTIFY	(1U << 31)
#define	ORB_FMT_STD	(0 << 29)
#define	ORB_FMT_VED	(2 << 29)
#define	ORB_FMT_NOP	(3 << 29)
#define	ORB_FMT_MSK	(3 << 29)
#define	ORB_EXV		(1 << 28)
/* */
#define	ORB_CMD_IN	(1 << 27)
/* */
#define	ORB_CMD_SPD(x)	((x) << 24)
#define	ORB_CMD_MAXP(x)	((x) << 20)
#define	ORB_RCN_TMO(x)	((x) << 20)
#define	ORB_CMD_PTBL	(1 << 19)
#define	ORB_CMD_PSZ(x)	((x) << 16)

#define	ORB_FUN_LGI	(0 << 16)
#define	ORB_FUN_QLG	(1 << 16)
#define	ORB_FUN_RCN	(3 << 16)
#define	ORB_FUN_LGO	(7 << 16)
#define	ORB_FUN_ATA	(0xb << 16)
#define	ORB_FUN_ATS	(0xc << 16)
#define	ORB_FUN_LUR	(0xe << 16)
#define	ORB_FUN_RST	(0xf << 16)
#define	ORB_FUN_MSK	(0xf << 16)
#define	ORB_FUN_RUNQUEUE 0xffff

#define ORB_RES_CMPL 0
#define ORB_RES_FAIL 1
#define ORB_RES_ILLE 2
#define ORB_RES_VEND 3

#define SBP_DEBUG(x)	if (debug > x) {
#define END_DEBUG	}

struct ind_ptr {
	uint32_t hi,lo;
};


#define SBP_RECV_LEN 32

struct sbp_login_res {
	uint16_t	len;
	uint16_t	id;
	uint16_t	res0;
	uint16_t	cmd_hi;
	uint32_t	cmd_lo;
	uint16_t	res1;
	uint16_t	recon_hold;
};

struct sbp_status {
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t		src:2,
			resp:2,
			dead:1,
			len:3;
#else
	uint8_t		len:3,
			dead:1,
			resp:2,
			src:2;
#endif
	uint8_t		status;
	uint16_t	orb_hi;
	uint32_t	orb_lo;
	uint32_t	data[6];
};
/* src */
#define SRC_NEXT_EXISTS	0
#define SRC_NO_NEXT	1
#define SRC_UNSOL	2

/* resp */
#define SBP_REQ_CMP	0	/* request complete */
#define SBP_TRANS_FAIL	1	/* transport failure */
#define SBP_ILLE_REQ	2	/* illegal request */
#define SBP_VEND_DEP	3	/* vendor dependent */

/* status (resp == 0) */
/*   0: No additional Information to report */
/*   1: Request Type not supported */
/*   2: Speed not supported */
/*   3: Page size not supported */
/*   4: Access denied */
#define STATUS_ACCESS_DENY	4
#define STATUS_LUR		5
/*   6: Maximum payload too small */
/*   7: Reserved for future standardization */
/*   8: Resource unavailabe */
#define STATUS_RES_UNAVAIL	8
/*   9: Function Rejected */
/*  10: Login ID not recognized */
/*  11: Dummy ORB completed */
/*  12: Request aborted */
/* 255: Unspecified error */

/* status (resp == 1) */
/* Referenced object */
#define OBJ_ORB		(0 << 6)	/* 0: ORB */
#define OBJ_DATA	(1 << 6)	/* 1: Data buffer */
#define OBJ_PT		(2 << 6)	/* 2: Page table */
#define OBJ_UNSPEC	(3 << 6)	/* 3: Unable to specify */
/* Serial bus error */
/* 0: Missing acknowledge */
/* 1: Reserved; not to be used */
/* 2: Time-out error */
#define SBE_TIMEOUT 2
/* 3: Reserved; not to be used */
/* 4: Busy retry limit exceeded: ack_busy_X */
/* 5: Busy retry limit exceeded: ack_busy_A */
/* 6: Busy retry limit exceeded: ack_busy_B */
/* 7-A: Reserved for future standardization */
/* B: Tardy retry limit exceeded */
/* C: Confilict error */
/* D: Data error */
/* E: Type error */
/* F: Address error */


struct sbp_cmd_status {
#define SBP_SFMT_CURR 0
#define SBP_SFMT_DEFER 1
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t		sfmt:2,
			status:6;
	uint8_t		valid:1,
			mark:1,
			eom:1,
			ill_len:1,
			s_key:4;
#else
	uint8_t		status:6,
			sfmt:2;
	uint8_t		s_key:4,
			ill_len:1,
			eom:1,
			mark:1,
			valid:1;
#endif
	uint8_t		s_code;
	uint8_t		s_qlfr;
	uint32_t	info;
	uint32_t	cdb;
	uint8_t		fru;
	uint8_t		s_keydep[3];
	uint32_t	vend[2];
};

#define ORB_FUN_NAMES \
	/* 0 */ "LOGIN", \
	/* 1 */ "QUERY LOGINS", \
	/* 2 */ "Reserved", \
	/* 3 */ "RECONNECT", \
	/* 4 */ "SET PASSWORD", \
	/* 5 */ "Reserved", \
	/* 6 */ "Reserved", \
	/* 7 */ "LOGOUT", \
	/* 8 */ "Reserved", \
	/* 9 */ "Reserved", \
	/* A */ "Reserved", \
	/* B */ "ABORT TASK", \
	/* C */ "ABORT TASK SET", \
	/* D */ "Reserved", \
	/* E */ "LOGICAL UNIT RESET", \
	/* F */ "TARGET RESET"
