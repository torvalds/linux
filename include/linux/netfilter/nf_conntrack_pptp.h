/* PPTP constants and structs */
#ifndef _NF_CONNTRACK_PPTP_H
#define _NF_CONNTRACK_PPTP_H

#include <linux/netfilter/nf_conntrack_common.h>

/* state of the control session */
enum pptp_ctrlsess_state {
	PPTP_SESSION_NONE,			/* no session present */
	PPTP_SESSION_ERROR,			/* some session error */
	PPTP_SESSION_STOPREQ,			/* stop_sess request seen */
	PPTP_SESSION_REQUESTED,			/* start_sess request seen */
	PPTP_SESSION_CONFIRMED,			/* session established */
};

/* state of the call inside the control session */
enum pptp_ctrlcall_state {
	PPTP_CALL_NONE,
	PPTP_CALL_ERROR,
	PPTP_CALL_OUT_REQ,
	PPTP_CALL_OUT_CONF,
	PPTP_CALL_IN_REQ,
	PPTP_CALL_IN_REP,
	PPTP_CALL_IN_CONF,
	PPTP_CALL_CLEAR_REQ,
};

/* conntrack private data */
struct nf_ct_pptp_master {
	enum pptp_ctrlsess_state sstate;	/* session state */
	enum pptp_ctrlcall_state cstate;	/* call state */
	__be16 pac_call_id;			/* call id of PAC */
	__be16 pns_call_id;			/* call id of PNS */

	/* in pre-2.6.11 this used to be per-expect. Now it is per-conntrack
	 * and therefore imposes a fixed limit on the number of maps */
	struct nf_ct_gre_keymap *keymap[IP_CT_DIR_MAX];
};

struct nf_nat_pptp {
	__be16 pns_call_id;			/* NAT'ed PNS call id */
	__be16 pac_call_id;			/* NAT'ed PAC call id */
};

#ifdef __KERNEL__

#define PPTP_CONTROL_PORT	1723

#define PPTP_PACKET_CONTROL	1
#define PPTP_PACKET_MGMT	2

#define PPTP_MAGIC_COOKIE	0x1a2b3c4d

struct pptp_pkt_hdr {
	__u16	packetLength;
	__be16	packetType;
	__be32	magicCookie;
};

/* PptpControlMessageType values */
#define PPTP_START_SESSION_REQUEST	1
#define PPTP_START_SESSION_REPLY	2
#define PPTP_STOP_SESSION_REQUEST	3
#define PPTP_STOP_SESSION_REPLY		4
#define PPTP_ECHO_REQUEST		5
#define PPTP_ECHO_REPLY			6
#define PPTP_OUT_CALL_REQUEST		7
#define PPTP_OUT_CALL_REPLY		8
#define PPTP_IN_CALL_REQUEST		9
#define PPTP_IN_CALL_REPLY		10
#define PPTP_IN_CALL_CONNECT		11
#define PPTP_CALL_CLEAR_REQUEST		12
#define PPTP_CALL_DISCONNECT_NOTIFY	13
#define PPTP_WAN_ERROR_NOTIFY		14
#define PPTP_SET_LINK_INFO		15

#define PPTP_MSG_MAX			15

/* PptpGeneralError values */
#define PPTP_ERROR_CODE_NONE		0
#define PPTP_NOT_CONNECTED		1
#define PPTP_BAD_FORMAT			2
#define PPTP_BAD_VALUE			3
#define PPTP_NO_RESOURCE		4
#define PPTP_BAD_CALLID			5
#define PPTP_REMOVE_DEVICE_ERROR	6

struct PptpControlHeader {
	__be16	messageType;
	__u16	reserved;
};

/* FramingCapability Bitmap Values */
#define PPTP_FRAME_CAP_ASYNC		0x1
#define PPTP_FRAME_CAP_SYNC		0x2

/* BearerCapability Bitmap Values */
#define PPTP_BEARER_CAP_ANALOG		0x1
#define PPTP_BEARER_CAP_DIGITAL		0x2

struct PptpStartSessionRequest {
	__be16	protocolVersion;
	__u16	reserved1;
	__be32	framingCapability;
	__be32	bearerCapability;
	__be16	maxChannels;
	__be16	firmwareRevision;
	__u8	hostName[64];
	__u8	vendorString[64];
};

/* PptpStartSessionResultCode Values */
#define PPTP_START_OK			1
#define PPTP_START_GENERAL_ERROR	2
#define PPTP_START_ALREADY_CONNECTED	3
#define PPTP_START_NOT_AUTHORIZED	4
#define PPTP_START_UNKNOWN_PROTOCOL	5

struct PptpStartSessionReply {
	__be16	protocolVersion;
	__u8	resultCode;
	__u8	generalErrorCode;
	__be32	framingCapability;
	__be32	bearerCapability;
	__be16	maxChannels;
	__be16	firmwareRevision;
	__u8	hostName[64];
	__u8	vendorString[64];
};

/* PptpStopReasons */
#define PPTP_STOP_NONE			1
#define PPTP_STOP_PROTOCOL		2
#define PPTP_STOP_LOCAL_SHUTDOWN	3

struct PptpStopSessionRequest {
	__u8	reason;
	__u8	reserved1;
	__u16	reserved2;
};

/* PptpStopSessionResultCode */
#define PPTP_STOP_OK			1
#define PPTP_STOP_GENERAL_ERROR		2

struct PptpStopSessionReply {
	__u8	resultCode;
	__u8	generalErrorCode;
	__u16	reserved1;
};

struct PptpEchoRequest {
	__be32 identNumber;
};

/* PptpEchoReplyResultCode */
#define PPTP_ECHO_OK			1
#define PPTP_ECHO_GENERAL_ERROR		2

struct PptpEchoReply {
	__be32	identNumber;
	__u8	resultCode;
	__u8	generalErrorCode;
	__u16	reserved;
};

/* PptpFramingType */
#define PPTP_ASYNC_FRAMING		1
#define PPTP_SYNC_FRAMING		2
#define PPTP_DONT_CARE_FRAMING		3

/* PptpCallBearerType */
#define PPTP_ANALOG_TYPE		1
#define PPTP_DIGITAL_TYPE		2
#define PPTP_DONT_CARE_BEARER_TYPE	3

struct PptpOutCallRequest {
	__be16	callID;
	__be16	callSerialNumber;
	__be32	minBPS;
	__be32	maxBPS;
	__be32	bearerType;
	__be32	framingType;
	__be16	packetWindow;
	__be16	packetProcDelay;
	__be16	phoneNumberLength;
	__u16	reserved1;
	__u8	phoneNumber[64];
	__u8	subAddress[64];
};

/* PptpCallResultCode */
#define PPTP_OUTCALL_CONNECT		1
#define PPTP_OUTCALL_GENERAL_ERROR	2
#define PPTP_OUTCALL_NO_CARRIER		3
#define PPTP_OUTCALL_BUSY		4
#define PPTP_OUTCALL_NO_DIAL_TONE	5
#define PPTP_OUTCALL_TIMEOUT		6
#define PPTP_OUTCALL_DONT_ACCEPT	7

struct PptpOutCallReply {
	__be16	callID;
	__be16	peersCallID;
	__u8	resultCode;
	__u8	generalErrorCode;
	__be16	causeCode;
	__be32	connectSpeed;
	__be16	packetWindow;
	__be16	packetProcDelay;
	__be32	physChannelID;
};

struct PptpInCallRequest {
	__be16	callID;
	__be16	callSerialNumber;
	__be32	callBearerType;
	__be32	physChannelID;
	__be16	dialedNumberLength;
	__be16	dialingNumberLength;
	__u8	dialedNumber[64];
	__u8	dialingNumber[64];
	__u8	subAddress[64];
};

/* PptpInCallResultCode */
#define PPTP_INCALL_ACCEPT		1
#define PPTP_INCALL_GENERAL_ERROR	2
#define PPTP_INCALL_DONT_ACCEPT		3

struct PptpInCallReply {
	__be16	callID;
	__be16	peersCallID;
	__u8	resultCode;
	__u8	generalErrorCode;
	__be16	packetWindow;
	__be16	packetProcDelay;
	__u16	reserved;
};

struct PptpInCallConnected {
	__be16	peersCallID;
	__u16	reserved;
	__be32	connectSpeed;
	__be16	packetWindow;
	__be16	packetProcDelay;
	__be32	callFramingType;
};

struct PptpClearCallRequest {
	__be16	callID;
	__u16	reserved;
};

struct PptpCallDisconnectNotify {
	__be16	callID;
	__u8	resultCode;
	__u8	generalErrorCode;
	__be16	causeCode;
	__u16	reserved;
	__u8	callStatistics[128];
};

struct PptpWanErrorNotify {
	__be16	peersCallID;
	__u16	reserved;
	__be32	crcErrors;
	__be32	framingErrors;
	__be32	hardwareOverRuns;
	__be32	bufferOverRuns;
	__be32	timeoutErrors;
	__be32	alignmentErrors;
};

struct PptpSetLinkInfo {
	__be16	peersCallID;
	__u16	reserved;
	__be32	sendAccm;
	__be32	recvAccm;
};

union pptp_ctrl_union {
	struct PptpStartSessionRequest	sreq;
	struct PptpStartSessionReply	srep;
	struct PptpStopSessionRequest	streq;
	struct PptpStopSessionReply	strep;
	struct PptpOutCallRequest	ocreq;
	struct PptpOutCallReply		ocack;
	struct PptpInCallRequest	icreq;
	struct PptpInCallReply		icack;
	struct PptpInCallConnected	iccon;
	struct PptpClearCallRequest	clrreq;
	struct PptpCallDisconnectNotify disc;
	struct PptpWanErrorNotify	wanerr;
	struct PptpSetLinkInfo		setlink;
};

/* crap needed for nf_conntrack_compat.h */
struct nf_conn;
struct nf_conntrack_expect;

extern int
(*nf_nat_pptp_hook_outbound)(struct sk_buff **pskb,
			     struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			     struct PptpControlHeader *ctlh,
			     union pptp_ctrl_union *pptpReq);

extern int
(*nf_nat_pptp_hook_inbound)(struct sk_buff **pskb,
			    struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			    struct PptpControlHeader *ctlh,
			    union pptp_ctrl_union *pptpReq);

extern void
(*nf_nat_pptp_hook_exp_gre)(struct nf_conntrack_expect *exp_orig,
			    struct nf_conntrack_expect *exp_reply);

extern void
(*nf_nat_pptp_hook_expectfn)(struct nf_conn *ct,
			     struct nf_conntrack_expect *exp);

#endif /* __KERNEL__ */
#endif /* _NF_CONNTRACK_PPTP_H */
