/*	$OpenBSD: iscsi.h,v 1.9 2019/09/27 23:07:42 krw Exp $ */

/*
 * Copyright (c) 2008 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SCSI_ISCSI_H
#define _SCSI_ISCSI_H

struct iscsi_pdu {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int8_t	_reserved2[4];

	u_int32_t	cmdsn;

	u_int32_t	expstatsn;

	u_int8_t	_reserved3[16];
} __packed;

/*
 * Initiator opcodes
 */

#define ISCSI_OP_I_NOP			0x00
#define ISCSI_OP_SCSI_REQUEST		0x01
#define ISCSI_OP_TASK_REQUEST		0x02
#define ISCSI_OP_LOGIN_REQUEST		0x03
#define ISCSI_OP_TEXT_REQUEST		0x04
#define ISCSI_OP_DATA_OUT		0x05
#define ISCSI_OP_LOGOUT_REQUEST		0x06
#define ISCSI_OP_SNACK_REQUEST		0x10

/*
 * Target opcodes
 */

#define ISCSI_OP_T_NOP			0x20
#define ISCSI_OP_SCSI_RESPONSE		0x21
#define ISCSI_OP_TASK_RESPONSE		0x22
#define ISCSI_OP_LOGIN_RESPONSE		0x23
#define ISCSI_OP_TEXT_RESPONSE		0x24
#define ISCSI_OP_DATA_IN		0x25
#define ISCSI_OP_LOGOUT_RESPONSE	0x26
#define ISCSI_OP_R2T			0x31
#define ISCSI_OP_ASYNC			0x32
#define ISCSI_OP_REJECT			0x3f

#define ISCSI_PDU_OPCODE(_o)		((_o) & 0x3f)
#define ISCSI_PDU_I(_h)			((_h)->opcode & 0x40)
#define ISCSI_PDU_F(_h)			((_h)->flags & 0x80)

#define ISCSI_OP_F_IMMEDIATE		0x40

/*
 * various other flags and values
 */
#define ISCSI_ISID_OUI			0x00000000
#define ISCSI_ISID_EN			0x40000000
#define ISCSI_ISID_RAND			0x80000000

struct iscsi_pdu_scsi_request {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	bytes;

	u_int32_t	cmdsn;

	u_int32_t	expstatsn;

	u_int8_t	cdb[16];
} __packed;

struct iscsi_pdu_scsi_response {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	response;
	u_int8_t	status;

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	_reserved[8];

	u_int32_t	itt;

	u_int32_t	snack;

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int32_t	expdatasn;

	u_int32_t	birescount;

	u_int32_t	rescount;
} __packed;

#define ISCSI_SCSI_F_F			0x80
#define ISCSI_SCSI_F_R			0x40
#define ISCSI_SCSI_F_W			0x20

#define ISCSI_SCSI_ATTR_UNTAGGED	0
#define ISCSI_SCSI_ATTR_SIMPLE		1
#define ISCSI_SCSI_ATTR_ORDERED		2
#define ISCSI_SCSI_ATTR_HEAD_OF_Q	3
#define ISCSI_SCSI_ATTR_ACA		4

#define ISCSI_SCSI_STAT_GOOD		0x00
#define ISCSI_SCSI_STAT_CHCK_COND	0x02
/* we don't care about the type of the other error conditions */

struct iscsi_pdu_task_request {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	reserved[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	tag;

	u_int32_t	cmdsn;

	u_int32_t	expstatsn;

	u_int32_t	refcmdsn;

	u_int32_t	expdatasn;

	u_int8_t	_reserved[8];
} __packed;

struct iscsi_pdu_task_response {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	response;
	u_int8_t	_reserved1;

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	_reserved[8];

	u_int32_t	itt;

	u_int8_t	_reserved2[4];

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int8_t	_reserved3[12];
} __packed;

struct iscsi_pdu_data_out {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int8_t	_reserved2[4];

	u_int32_t	expstatsn;

	u_int8_t	_reserved3[4];

	u_int32_t	datasn;

	u_int32_t	buffer_offs;

	u_int8_t	_reserved4[4];
} __packed;

struct iscsi_pdu_data_in {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved;
	u_int8_t	status;

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int32_t	datasn;

	u_int32_t	buffer_offs;

	u_int32_t	residual;
} __packed;

struct iscsi_pdu_rt2 {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int32_t	r2tsn;

	u_int32_t	buffer_offs;

	u_int32_t	desired_datalen;
} __packed;

struct iscsi_pdu_async {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	ffffffff;

	u_int8_t	_reserved2[4];

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int8_t	event;
	u_int8_t	vcode;
	u_int16_t	param[3];

	u_int8_t	_reserved3[4];
} __packed;

struct iscsi_pdu_text_request {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int32_t	cmdsn;

	u_int32_t	expstatsn;

	u_int8_t	_reserved2[16];
} __packed;

struct iscsi_pdu_text_response {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int32_t	cmdsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int8_t	_reserved2[12];
} __packed;

#define ISCSI_TEXT_F_F	0x80
#define ISCSI_TEXT_F_C	0x40

struct iscsi_pdu_login_request {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	version_max;
	u_int8_t	version_min;

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int32_t	isid_base;
	u_int16_t	isid_qual;
	u_int16_t	tsih;

	u_int32_t	itt;

	u_int16_t	cid;
	u_int8_t	_reserved1[2];

	u_int32_t	cmdsn;

	u_int32_t	expstatsn;

	u_int8_t	_reserved2[16];
} __packed;

#define ISCSI_LOGIN_F_T		0x80
#define ISCSI_LOGIN_F_C		0x40
#define ISCSI_LOGIN_F_CSG(x)	(((x) & 0x3) << 2)
#define ISCSI_LOGIN_F_NSG(x)	((x) & 0x3)
#define ISCSI_LOGIN_STG_SECNEG	0
#define ISCSI_LOGIN_STG_OPNEG	1
#define ISCSI_LOGIN_STG_FULL	3

struct iscsi_pdu_login_response {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	version_max;
	u_int8_t	version_active;

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int32_t	isid_base;
	u_int16_t	isid_qual;
	u_int16_t	tsih;

	u_int32_t	itt;

	u_int8_t	_reserved1[4];

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int8_t	status_class;
	u_int8_t	status_detail;

	u_int8_t	_reserved2[10];
} __packed;

struct iscsi_pdu_logout_request {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	_reserved2[8];

	u_int32_t	itt;

	u_int16_t	cid;
	u_int8_t	_reserved3[2];

	u_int32_t	cmdsn;

	u_int32_t	expstatsn;

	u_int8_t	_reserved4[16];
} __packed;

#define ISCSI_LOGOUT_F		0x80
#define ISCSI_LOGOUT_CLOSE_SESS	0
#define ISCSI_LOGOUT_CLOSE_CONN	1
#define ISCSI_LOGOUT_RCVRY_CONN	2

#define ISCSI_LOGOUT_RESP_SUCCESS	0
#define ISCSI_LOGOUT_RESP_UNKN_CID	1
#define ISCSI_LOGOUT_RESP_NO_SUPPORT	2
#define ISCSI_LOGOUT_RESP_ERROR		3

struct iscsi_pdu_logout_response {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	response;
	u_int8_t	_reserved1;

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	_reserved2[8];

	u_int32_t	itt;

	u_int8_t	_reserved3[4];

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int8_t	_reserved4[4];

	u_int16_t	time2wait;
	u_int16_t	time2retain;

	u_int8_t	_reserved5[4];
} __packed;

struct iscsi_pdu_snack_request {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int8_t	_reserved2[4];

	u_int32_t	expstatsn;

	u_int8_t	_reserved3[8];

	u_int32_t	begrun;

	u_int32_t	runlength;
} __packed;

struct iscsi_pdu_reject {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	reason;
	u_int8_t	_reserved1;

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	_reserved2[8];

	u_int32_t	ffffffff;

	u_int8_t	_reserved3[4];

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int32_t	datasn_r2tsn;

	u_int8_t	_reserved4[8];
} __packed;

struct iscsi_pdu_nop_out {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int32_t	cmdsn;

	u_int32_t	expstatsn;

	u_int8_t	_reserved2[16];
} __packed;

struct iscsi_pdu_nop_in {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int8_t	_reserved1[2];

	u_int8_t	ahslen;
	u_int8_t	datalen[3];

	u_int8_t	lun[8];

	u_int32_t	itt;

	u_int32_t	ttt;

	u_int32_t	statsn;

	u_int32_t	expcmdsn;

	u_int32_t	maxcmdsn;

	u_int8_t	_reserved2[12];
} __packed;

#endif /* _SCSI_ISCSI_H */
