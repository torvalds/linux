/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _NETSMB_SMB_RQ_H_
#define	_NETSMB_SMB_RQ_H_

#ifndef MB_MSYSTEM
#include <sys/mchain.h>
#endif

#define	SMBR_ALLOCED		0x0001	/* structure was malloced */
#define	SMBR_SENT		0x0002	/* request successfully transmitted */
#define	SMBR_REXMIT		0x0004	/* request should be retransmitted */
#define	SMBR_INTR		0x0008	/* request interrupted */
#define	SMBR_RESTART		0x0010	/* request should be repeated if possible */
#define	SMBR_NORESTART		0x0020	/* request is not restartable */
#define	SMBR_MULTIPACKET	0x0040	/* multiple packets can be sent and received */
#define	SMBR_INTERNAL		0x0080	/* request is internal to smbrqd */
#define	SMBR_XLOCK		0x0100	/* request locked and can't be moved */
#define	SMBR_XLOCKWANT		0x0200	/* waiter on XLOCK */

#define SMBT2_ALLSENT		0x0001	/* all data and params are sent */
#define SMBT2_ALLRECV		0x0002	/* all data and params are received */
#define	SMBT2_ALLOCED		0x0004
#define	SMBT2_RESTART		0x0008
#define	SMBT2_NORESTART		0x0010
#define	SMBT2_SECONDARY		0x0020	/* secondary request */

#define SMBRQ_SLOCK(rqp)	smb_sl_lock(&(rqp)->sr_slock)
#define SMBRQ_SUNLOCK(rqp)	smb_sl_unlock(&(rqp)->sr_slock)
#define SMBRQ_SLOCKPTR(rqp)	(&(rqp)->sr_slock)


enum smbrq_state {
	SMBRQ_NOTSENT,		/* rq have data to send */
	SMBRQ_SENT,		/* send procedure completed */
	SMBRQ_REPLYRECEIVED,
	SMBRQ_NOTIFIED		/* owner notified about completion */
};

struct smb_vc;
struct smb_t2rq;

struct smb_rq {
	enum smbrq_state	sr_state;
	struct smb_vc * 	sr_vc;
	struct smb_share*	sr_share;
	u_short			sr_mid;
	u_int32_t		sr_seqno;
	u_int32_t		sr_rseqno;
	struct mbchain		sr_rq;
	u_int8_t		sr_rqflags;
	u_int16_t		sr_rqflags2;
	u_char *		sr_wcount;
	void *			sr_bcount;	/* Points to 2-byte buffer. */
	struct mdchain		sr_rp;
	int			sr_rpgen;
	int			sr_rplast;
	int			sr_flags;	/* SMBR_* */
	int			sr_rpsize;
	struct smb_cred *	sr_cred;
	int			sr_timo;
	int			sr_rexmit;
	int			sr_sendcnt;
	struct timespec 	sr_timesent;
	int			sr_lerror;
	u_int8_t *		sr_rqsig;
	void *			sr_rqtid;	/* Points to 2-byte buffer. */
	void *			sr_rquid;	/* Points to 2-byte buffer. */
	u_int8_t		sr_errclass;
	u_int16_t		sr_serror;
	u_int32_t		sr_error;
	u_int8_t		sr_rpflags;
	u_int16_t		sr_rpflags2;
	u_int16_t		sr_rptid;
	u_int16_t		sr_rppid;
	u_int16_t		sr_rpuid;
	u_int16_t		sr_rpmid;
	struct smb_slock	sr_slock;	/* short term locks */
	struct smb_t2rq *	sr_t2;
	TAILQ_ENTRY(smb_rq)	sr_link;
};

struct smb_t2rq {
	u_int16_t	t2_setupcount;
	u_int16_t *	t2_setupdata;
	u_int16_t	t2_setup[2];	/* most of rqs has setupcount of 1 */
	u_int8_t	t2_maxscount;	/* max setup words to return */
	u_int16_t	t2_maxpcount;	/* max param bytes to return */
	u_int16_t	t2_maxdcount;	/* max data bytes to return */
	u_int16_t	t2_fid;		/* for T2 request */
	char *		t_name;		/* for T request, should be zero for T2 */
	int		t2_flags;	/* SMBT2_ */
	struct mbchain	t2_tparam;	/* parameters to transmit */
	struct mbchain	t2_tdata;	/* data to transmit */
	struct mdchain	t2_rparam;	/* received parameters */
	struct mdchain	t2_rdata;	/* received data */
	struct smb_cred*t2_cred;
	struct smb_connobj *t2_source;
	struct smb_rq *	t2_rq;
	struct smb_vc * t2_vc;
};

int  smb_rq_alloc(struct smb_connobj *layer, u_char cmd,
	struct smb_cred *scred, struct smb_rq **rqpp);
int  smb_rq_init(struct smb_rq *rqp, struct smb_connobj *layer, u_char cmd,
	struct smb_cred *scred);
void smb_rq_done(struct smb_rq *rqp);
int  smb_rq_getrequest(struct smb_rq *rqp, struct mbchain **mbpp);
int  smb_rq_getreply(struct smb_rq *rqp, struct mdchain **mbpp);
void smb_rq_wstart(struct smb_rq *rqp);
void smb_rq_wend(struct smb_rq *rqp);
void smb_rq_bstart(struct smb_rq *rqp);
void smb_rq_bend(struct smb_rq *rqp);
int  smb_rq_intr(struct smb_rq *rqp);
int  smb_rq_simple(struct smb_rq *rqp);

int  smb_t2_alloc(struct smb_connobj *layer, u_short setup, struct smb_cred *scred,
	struct smb_t2rq **rqpp);
int  smb_t2_init(struct smb_t2rq *rqp, struct smb_connobj *layer, u_short setup,
	struct smb_cred *scred);
void smb_t2_done(struct smb_t2rq *t2p);
int  smb_t2_request(struct smb_t2rq *t2p);

#endif /* !_NETSMB_SMB_RQ_H_ */
