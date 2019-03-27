/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2010 Daniel Braniss <danny@cs.huji.ac.il>
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
/*
 | $Id: iscsi.h 743 2009-08-08 10:54:53Z danny $
 */
#define	TRUE	1
#define FALSE	0
#ifndef _KERNEL
typedef int boolean_t;
#endif

#include <cam/cam.h>

#define ISCSIDEV	"iscsi"
#define ISCSI_MAX_TARGETS	64
/*
 | iSCSI commands
 */

/*
 | Initiator Opcodes:
 */
#define ISCSI_NOP_OUT		0x00
#define ISCSI_SCSI_CMD		0x01
#define ISCSI_TASK_CMD		0x02
#define ISCSI_LOGIN_CMD		0x03
#define ISCSI_TEXT_CMD		0x04
#define ISCSI_WRITE_DATA	0x05
#define ISCSI_LOGOUT_CMD	0x06
#define ISCSI_SNACK		0x10
/*
 | Target Opcodes:
 */
#define ISCSI_NOP_IN		0x20
#define ISCSI_SCSI_RSP		0x21
#define ISCSI_TASK_RSP		0x22
#define ISCSI_LOGIN_RSP		0x23
#define ISCSI_TEXT_RSP		0x24
#define ISCSI_READ_DATA		0x25
#define ISCSI_LOGOUT_RSP	0x26
#define ISCSI_R2T		0x31
#define ISCSI_ASYNC		0x32
#define ISCSI_REJECT		0x3f
/*
 | PDU stuff
 */
/*
 | BHS Basic Header Segment
 */
typedef struct bhs {
     // the order is network byte order!
     u_char	opcode:6;
     u_char	I:1;
     u_char	_:1;
     u_char	__:7;
     u_char	F:1;			// Final bit
     u_char	___[2];

     u_int	AHSLength:8;		// in 4byte words
     u_int	DSLength:24;		// in bytes

     u_int	LUN[2];			// or Opcode-specific fields
     u_int	itt;
     u_int	OpcodeSpecificFields[7];
#define	CmdSN		OpcodeSpecificFields[1]
#define	ExpStSN		OpcodeSpecificFields[2]
#define MaxCmdSN	OpcodeSpecificFields[3]
} bhs_t;

typedef struct ahs {
     u_int	len:16;
     u_int	type:8;
     u_int	spec:8;
     char	data[0];
} ahs_t;

typedef struct {
     // Sequence Numbers
     // (computers were invented to count, right?)
     int	cmd;
     int	expcmd;
     int	maxcmd;
} req_sn_t;

typedef struct {
     // Sequence Numbers
     // (computers were invented to count, right?)
     int	stat;
     int	expcmd;
     int	maxcmd;
} rsp_sn_t;

typedef struct scsi_req {
     u_char	opcode:6; // 0x01
     u_char	I:1;
     u_char	_:1;

     u_char	attr:3;
     u_char	_0:2;
     u_char	W:1;
     u_char	R:1;
     u_char	F:1;
#define		iSCSI_TASK_UNTAGGED	0
#define		iSCSI_TASK_SIMPLE	1
#define		iSCSI_TASK_ORDER	2
#define		iSCSI_TASK_HOFQ		3
#define		iSCSI_TASK_ACA		4
     char	_1[2];
     int	len;
     int	lun[2];
     int	itt;
     int	edtlen;		// expectect data transfere length
     int	cmdSN;
     int	extStatSN;
     int	cdb[4];
} scsi_req_t;

typedef struct scsi_rsp {
     char	opcode;	// 0x21
     u_char	flag;
     u_char	response;
     u_char	status;

     int	len;
     int	_[2];
     int	itt;
     int	stag;
     rsp_sn_t	sn;
     int	expdatasn;
     int	bdrcnt;	// bidirectional residual count
     int	rcnt;	// residual count
} scsi_rsp_t;

typedef struct nop_out {
     // the order is network byte order!
     u_char	opcode:6;
     u_char	I:1;
     u_char	_:1;
     u_char	__:7;
     u_char	F:1;			// Final bit
     u_char	___[2];

     u_int	len;
     u_int	lun[2];
     u_int	itt;
     u_int	ttt;
     req_sn_t	sn;
     u_int	mbz[3];
} nop_out_t; 

typedef struct nop_in {
     // the order is network byte order!
     u_char	opcode:6;
     u_char	I:1;
     u_char	_:1;
     u_char	__:7;
     u_char	F:1;			// Final bit
     u_char	___[2];

     u_int	len;
     u_int	lun[2];
     u_int	itt;
     u_int	ttt;
     rsp_sn_t	sn;
     u_int	____[2];
     
} nop_in_t;

typedef struct r2t {
     u_char	opcode:6;
     u_char	I:1;
     u_char	_:1;
     u_char	__:7;
     u_char	F:1;			// Final bit
     u_char	___[2];  

     u_int	len;
     u_int	lun[2];
     u_int	itt;
     u_int	ttt;
     rsp_sn_t	sn;
     u_int	r2tSN;
     u_int	bo;
     u_int	ddtl;
} r2t_t;

typedef struct data_out {
     u_char	opcode:6;
     u_char	I:1;
     u_char	_:1;
     u_char	__:7;
     u_char	F:1;			// Final bit
     u_char	___[2];  

     u_int	len;
     u_int	lun[2];
     u_int	itt;
     u_int	ttt;
     rsp_sn_t	sn;
     u_int	dsn;	// data seq. number
     u_int	bo;
     u_int	____;
} data_out_t;

typedef struct data_in {
     u_char	opcode:6;
     u_char	I:1;
     u_char	_:1;

     u_char	S:1;
     u_char	U:1;
     u_char	O:1;
     u_char	__:3;
     u_char	A:1;
     u_char	F:1;			// Final bit
     u_char	___[1]; 
     u_char	status;

     u_int	len;
     u_int	lun[2];
     u_int	itt;
     u_int	ttt;
     rsp_sn_t	sn;
     u_int	dataSN;
     u_int	bo;
     u_int	____;
} data_in_t;

typedef struct reject {
     u_char	opcode:6;
     u_char	_:2;
     u_char	F:1;
     u_char	__:7;
     u_char	reason;
     u_char	___;

     u_int	len;
     u_int	____[2];
     u_int	tt[2];	// must be -1
     rsp_sn_t	sn;
     u_int	dataSN;	// or R2TSN or reserved
     u_int	_____[2];
} reject_t;

typedef struct async {
     u_char	opcode:6;
     u_char	_:2;
     u_char	F:1;
     u_char	__:7;
     u_char	___[2];

     u_int	len;
     u_int	lun[2];
     u_int	itt;	// must be -1
     u_int	____;
     rsp_sn_t	sn;

     u_char	asyncEvent;
     u_char	asyncVCode;
     u_char	param1[2];
     u_char	param2[2];
     u_char	param3[2];

     u_int	_____;
     
} async_t;  

typedef struct login_req {
     char	cmd;	// 0x03

     u_char	NSG:2;
     u_char	CSG:2;
     u_char	_:2;
     u_char	C:1;
     u_char	T:1;

     char	v_max;
     char	v_min;

     int	len;	// remapped via standard bhs
     char	isid[6];
     short	tsih;
     int	itt;	// Initiator Task Tag;

     int	CID:16;
     int	rsv:16;

     int	cmdSN;
     int	expStatSN;
     int	unused[4];
} login_req_t;

typedef struct login_rsp {
     char	cmd;	// 0x23
     u_char	NSG:2;
     u_char	CSG:2;
     u_char	_1:2;
     u_char	C:1;
     u_char	T:1;

     char	v_max;
     char	v_act;

     int	len;	// remapped via standard bhs
     char	isid[6];
     short	tsih;
     int	itt;	// Initiator Task Tag;
     int	_2;
     rsp_sn_t	sn;
     int	status:16;
     int	_3:16;
     int	_4[2];
} login_rsp_t;

typedef struct text_req {
     char	cmd;	// 0x04

     u_char	_1:6;
     u_char	C:1;	// Continuation 
     u_char	F:1;	// Final
     char	_2[2];

     int	len;
     int	itt;		// Initiator Task Tag
     int	LUN[2];
     int	ttt;		// Target Transfer Tag
     int	cmdSN;
     int	expStatSN;
     int	unused[4];
} text_req_t;

typedef struct logout_req {
     char	cmd;	// 0x06
     u_char	reason;	// 0 - close session
     			// 1 - close connection
     			// 2 - remove the connection for recovery
     char	_2[2];

     int	len;
     int	_r[2];
     int	itt;	// Initiator Task Tag;

     u_int	CID:16;
     u_int	rsv:16;

     int	cmdSN;
     int	expStatSN;
     int	unused[4];
} logout_req_t;

typedef struct logout_rsp {
     char	cmd;	// 0x26
     char	cbits;
     char	_1[2];
     int	len;
     int	_2[2];
     int	itt;
     int	_3;
     rsp_sn_t	sn;
     short	time2wait;
     short	time2retain;
     int	_4;
} logout_rsp_t;

union ipdu_u {
     bhs_t	bhs;
     scsi_req_t	scsi_req;
     scsi_rsp_t	scsi_rsp;
     nop_out_t	nop_out;
     nop_in_t	nop_in;
     r2t_t	r2t;
     data_out_t	data_out;
     data_in_t	data_in;
     reject_t	reject;
     async_t	async;
};

/*
 | Sequence Numbers
 */
typedef struct {
     u_int	itt;
     u_int      cmd;
     u_int      expCmd;
     u_int      maxCmd;
     u_int      stat;
     u_int      expStat;
     u_int      data;
} sn_t;

/*
 | in-core version of a Protocol Data Unit
 */
typedef struct {
     union ipdu_u	ipdu;
     u_int		hdr_dig;	// header digest

     ahs_t		*ahs_addr;
     u_int		ahs_len;
     u_int		ahs_size;	// the allocated size

     u_char		*ds_addr;
     u_int		ds_len;
     u_int		ds_size;	// the allocated size
     u_int		ds_dig;		// data digest
} pdu_t;

typedef struct opvals {
     int	port;
     int	tags;
     int	maxluns;
     int	sockbufsize;

     int	maxConnections;
     int	maxRecvDataSegmentLength;
     int	maxXmitDataSegmentLength; // pseudo ...
     int	maxBurstLength;
     int	firstBurstLength;
     int	defaultTime2Wait;
     int	defaultTime2Retain;
     int	maxOutstandingR2T;
     int	errorRecoveryLevel;
     int	targetPortalGroupTag;

     boolean_t	initialR2T;
     boolean_t	immediateData;
     boolean_t	dataPDUInOrder;
     boolean_t	dataSequenceInOrder;
     char	*headerDigest;
     char	*dataDigest;
     char	*sessionType;
     char	*sendTargets;
     char	*targetAddress;
     char	*targetAlias;
     char	*targetName;
     char	*initiatorName;
     char	*initiatorAlias;
     char	*authMethod;
     char	*chapSecret;
     char	*chapIName;
     char	*chapDigest;
     char	*tgtChapName;
     char	*tgtChapSecret;
     int	tgtChallengeLen;
     u_char	tgtChapID;
     char	*tgtChapDigest;
     char	*iqn;
     char	*pidfile;
} isc_opt_t;

/*
 | ioctl
 */
#define ISCSISETSES	_IOR('i', 1, int)
#define ISCSISETSOC	_IOW('i', 2, int)
#define ISCSISETOPT	_IOW('i', 5, isc_opt_t)
#define ISCSIGETOPT	_IOR('i', 6, isc_opt_t)

#define ISCSISEND	_IOW('i', 10, pdu_t)
#define ISCSIRECV	_IOWR('i', 11, pdu_t)

#define ISCSIPING	_IO('i', 20)
#define ISCSISIGNAL	_IOW('i', 21, int *)

#define ISCSISTART	_IO('i', 30)
#define ISCSIRESTART	_IO('i', 31)
#define ISCSISTOP	_IO('i', 32)

typedef struct iscsi_cam {
     path_id_t		path_id;
     target_id_t	target_id;
     int		target_nluns;
} iscsi_cam_t;

#define ISCSIGETCAM	_IOR('i', 33, iscsi_cam_t)
