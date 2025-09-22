/*	$OpenBSD: ldp.h,v 1.42 2017/03/04 00:21:48 renato Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2004, 2005, 2008 Esben Norby <norby@openbsd.org>
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

/* LDP protocol definitions */

#ifndef _LDP_H_
#define _LDP_H_

#include <sys/types.h>

/* misc */
#define LDP_VERSION		1
#define LDP_PORT		646
#define LDP_MAX_LEN		4096

/* All Routers on this Subnet group multicast addresses */
#define AllRouters_v4		"224.0.0.2"
#define AllRouters_v6		"ff02::2"

#define LINK_DFLT_HOLDTIME	15
#define TARGETED_DFLT_HOLDTIME	45
#define MIN_HOLDTIME		3
#define MAX_HOLDTIME		0xffff
#define	INFINITE_HOLDTIME	0xffff

#define DEFAULT_KEEPALIVE	180
#define MIN_KEEPALIVE		3
#define MAX_KEEPALIVE		0xffff
#define KEEPALIVE_PER_PERIOD	3
#define INIT_FSM_TIMEOUT	15

#define	DEFAULT_HELLO_INTERVAL	5
#define	MIN_HELLO_INTERVAL	1
#define	MAX_HELLO_INTERVAL	0xffff

#define	INIT_DELAY_TMR		15
#define	MAX_DELAY_TMR		120

#define	MIN_PWID_ID		1
#define	MAX_PWID_ID		0xffffffff

#define	DEFAULT_L2VPN_MTU	1500
#define	MIN_L2VPN_MTU		512
#define	MAX_L2VPN_MTU		0xffff

/* LDP message types */
#define MSG_TYPE_NOTIFICATION	0x0001
#define MSG_TYPE_HELLO		0x0100
#define MSG_TYPE_INIT		0x0200
#define MSG_TYPE_KEEPALIVE	0x0201
#define MSG_TYPE_CAPABILITY	0x0202 /* RFC 5561 */
#define MSG_TYPE_ADDR		0x0300
#define MSG_TYPE_ADDRWITHDRAW	0x0301
#define MSG_TYPE_LABELMAPPING	0x0400
#define MSG_TYPE_LABELREQUEST	0x0401
#define MSG_TYPE_LABELWITHDRAW	0x0402
#define MSG_TYPE_LABELRELEASE	0x0403
#define MSG_TYPE_LABELABORTREQ	0x0404

/* LDP TLV types */
#define TLV_TYPE_FEC		0x0100
#define TLV_TYPE_ADDRLIST	0x0101
#define TLV_TYPE_HOPCOUNT	0x0103
#define TLV_TYPE_PATHVECTOR	0x0104
#define TLV_TYPE_GENERICLABEL	0x0200
#define TLV_TYPE_ATMLABEL	0x0201
#define TLV_TYPE_FRLABEL	0x0202
#define TLV_TYPE_STATUS		0x0300
#define TLV_TYPE_EXTSTATUS	0x0301
#define TLV_TYPE_RETURNEDPDU	0x0302
#define TLV_TYPE_RETURNEDMSG	0x0303
#define TLV_TYPE_COMMONHELLO	0x0400
#define TLV_TYPE_IPV4TRANSADDR	0x0401
#define TLV_TYPE_CONFIG		0x0402
#define TLV_TYPE_IPV6TRANSADDR	0x0403
#define TLV_TYPE_COMMONSESSION	0x0500
#define TLV_TYPE_ATMSESSIONPAR	0x0501
#define TLV_TYPE_FRSESSION	0x0502
#define TLV_TYPE_LABELREQUEST	0x0600
/* RFC 4447 */
#define TLV_TYPE_MAC_LIST	0x8404
#define TLV_TYPE_PW_STATUS	0x896A
#define TLV_TYPE_PW_IF_PARAM	0x096B
#define TLV_TYPE_PW_GROUP_ID	0x096C
/* RFC 5561 */
#define TLV_TYPE_RETURNED_TLVS	0x8304
#define TLV_TYPE_DYNAMIC_CAP	0x8506
/* RFC 5918 */
#define TLV_TYPE_TWCARD_CAP	0x850B
/* RFC 5919 */
#define TLV_TYPE_UNOTIF_CAP	0x8603
/* RFC 7552 */
#define TLV_TYPE_DUALSTACK	0x8701

/* LDP header */
struct ldp_hdr {
	uint16_t	version;
	uint16_t	length;
	uint32_t	lsr_id;
	uint16_t	lspace_id;
} __packed;

#define	LDP_HDR_SIZE		10	/* actual size of the LDP header */
#define	LDP_HDR_PDU_LEN		6	/* minimum "PDU Length" */
#define LDP_HDR_DEAD_LEN	4

/* TLV record */
struct tlv {
	uint16_t	type;
	uint16_t	length;
};
#define	TLV_HDR_SIZE		4

struct ldp_msg {
	uint16_t	type;
	uint16_t	length;
	uint32_t	id;
	/* Mandatory Parameters */
	/* Optional Parameters */
} __packed;

#define LDP_MSG_SIZE		8	/* minimum size of LDP message */
#define LDP_MSG_LEN		4	/* minimum "Message Length" */
#define LDP_MSG_DEAD_LEN	4

#define	UNKNOWN_FLAG		0x8000
#define	FORWARD_FLAG		0xc000

struct hello_prms_tlv {
	uint16_t	type;
	uint16_t	length;
	uint16_t	holdtime;
	uint16_t	flags;
};
#define F_HELLO_TARGETED	0x8000
#define F_HELLO_REQ_TARG	0x4000
#define F_HELLO_GTSM		0x2000

struct hello_prms_opt4_tlv {
	uint16_t	type;
	uint16_t	length;
	uint32_t	value;
};

struct hello_prms_opt16_tlv {
	uint16_t	type;
	uint16_t	length;
	uint8_t		value[16];
};

#define DUAL_STACK_LDPOV4	4
#define DUAL_STACK_LDPOV6	6

#define F_HELLO_TLV_RCVD_ADDR	0x01
#define F_HELLO_TLV_RCVD_CONF	0x02
#define F_HELLO_TLV_RCVD_DS	0x04

#define	S_SUCCESS	0x00000000
#define	S_BAD_LDP_ID	0x80000001
#define	S_BAD_PROTO_VER	0x80000002
#define	S_BAD_PDU_LEN	0x80000003
#define	S_UNKNOWN_MSG	0x00000004
#define	S_BAD_MSG_LEN	0x80000005
#define	S_UNKNOWN_TLV	0x00000006
#define	S_BAD_TLV_LEN	0x80000007
#define	S_BAD_TLV_VAL	0x80000008
#define	S_HOLDTIME_EXP	0x80000009
#define	S_SHUTDOWN	0x8000000A
#define	S_LOOP_DETECTED	0x0000000B
#define	S_UNKNOWN_FEC	0x0000000C
#define	S_NO_ROUTE	0x0000000D
#define	S_NO_LABEL_RES	0x0000000E
#define	S_AVAILABLE	0x0000000F
#define	S_NO_HELLO	0x80000010
#define	S_PARM_ADV_MODE	0x80000011
#define	S_MAX_PDU_LEN	0x80000012
#define	S_PARM_L_RANGE	0x80000013
#define	S_KEEPALIVE_TMR	0x80000014
#define	S_LAB_REQ_ABRT	0x00000015
#define	S_MISS_MSG	0x00000016
#define	S_UNSUP_ADDR	0x00000017
#define	S_KEEPALIVE_BAD	0x80000018
#define	S_INTERN_ERR	0x80000019
/* RFC 4447 */
#define S_ILLEGAL_CBIT	0x00000024
#define S_WRONG_CBIT	0x00000025
#define S_INCPT_BITRATE	0x00000026
#define S_CEP_MISCONF	0x00000027
#define S_PW_STATUS	0x00000028
#define S_UNASSIGN_TAI	0x00000029
#define S_MISCONF_ERR	0x0000002A
#define S_WITHDRAW_MTHD	0x0000002B
/* RFC 5561 */
#define	S_UNSSUPORTDCAP	0x0000002E
/* RFC 5919 */
#define	S_ENDOFLIB	0x0000002F
/* RFC 7552 */
#define	S_TRANS_MISMTCH	0x80000032
#define	S_DS_NONCMPLNCE	0x80000033

struct sess_prms_tlv {
	uint16_t	type;
	uint16_t	length;
	uint16_t	proto_version;
	uint16_t	keepalive_time;
	uint8_t		reserved;
	uint8_t		pvlim;
	uint16_t	max_pdu_len;
	uint32_t	lsr_id;
	uint16_t	lspace_id;
} __packed;

#define SESS_PRMS_SIZE		18
#define SESS_PRMS_LEN		14

struct status_tlv {
	uint16_t	type;
	uint16_t	length;
	uint32_t	status_code;
	uint32_t	msg_id;
	uint16_t	msg_type;
} __packed;

#define STATUS_SIZE		14
#define STATUS_TLV_LEN		10
#define	STATUS_FATAL		0x80000000

struct capability_tlv {
	uint16_t	type;
	uint16_t	length;
	uint8_t		reserved;
};
#define STATE_BIT		0x80

#define F_CAP_TLV_RCVD_DYNAMIC	0x01
#define F_CAP_TLV_RCVD_TWCARD	0x02
#define F_CAP_TLV_RCVD_UNOTIF	0x04

#define CAP_TLV_DYNAMIC_SIZE	5
#define CAP_TLV_DYNAMIC_LEN	1

#define CAP_TLV_TWCARD_SIZE	5
#define CAP_TLV_TWCARD_LEN	1

#define CAP_TLV_UNOTIF_SIZE	5
#define CAP_TLV_UNOTIF_LEN	1

#define	AF_IPV4			0x1
#define	AF_IPV6			0x2

struct address_list_tlv {
	uint16_t	type;
	uint16_t	length;
	uint16_t	family;
	/* address entries */
} __packed;

#define ADDR_LIST_SIZE		6

#define FEC_ELM_WCARD_LEN	1
#define FEC_ELM_PREFIX_MIN_LEN	4
#define FEC_PWID_ELM_MIN_LEN	8
#define FEC_PWID_SIZE		4
#define FEC_ELM_TWCARD_MIN_LEN	3

#define	MAP_TYPE_WILDCARD	0x01
#define	MAP_TYPE_PREFIX		0x02
#define	MAP_TYPE_TYPED_WCARD	0x05
#define	MAP_TYPE_PWID		0x80
#define	MAP_TYPE_GENPWID	0x81

#define CONTROL_WORD_FLAG	0x8000
#define PW_TYPE_ETHERNET_TAGGED	0x0004
#define PW_TYPE_ETHERNET	0x0005
#define PW_TYPE_WILDCARD	0x7FFF
#define DEFAULT_PW_TYPE		PW_TYPE_ETHERNET

#define PW_TWCARD_RESERVED_BIT	0x8000

/* RFC 4447 Sub-TLV record */
struct subtlv {
	uint8_t		type;
	uint8_t		length;
};
#define	SUBTLV_HDR_SIZE		2

#define SUBTLV_IFMTU		0x01
#define SUBTLV_VLANID		0x06

#define FEC_SUBTLV_IFMTU_SIZE	4
#define FEC_SUBTLV_VLANID_SIZE	4

struct label_tlv {
	uint16_t	type;
	uint16_t	length;
	uint32_t	label;
};
#define LABEL_TLV_SIZE		8
#define LABEL_TLV_LEN		4

struct reqid_tlv {
	uint16_t	type;
	uint16_t	length;
	uint32_t	reqid;
};
#define REQID_TLV_SIZE		8
#define REQID_TLV_LEN		4

struct pw_status_tlv {
	uint16_t	type;
	uint16_t	length;
	uint32_t	value;
};
#define PW_STATUS_TLV_SIZE	8
#define PW_STATUS_TLV_LEN	4

#define PW_FORWARDING		0
#define PW_NOT_FORWARDING	(1 << 0)
#define PW_LOCAL_RX_FAULT	(1 << 1)
#define PW_LOCAL_TX_FAULT	(1 << 2)
#define PW_PSN_RX_FAULT		(1 << 3)
#define PW_PSN_TX_FAULT		(1 << 4)

#define	NO_LABEL		UINT32_MAX

#endif /* !_LDP_H_ */
