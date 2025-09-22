/*	$OpenBSD: print-l2tp.c,v 1.11 2022/01/05 05:46:18 dlg Exp $	*/

/*
 * Copyright (c) 1991, 1993, 1994, 1995, 1996, 1997
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * L2TP support contributed by Motonori Shindo (mshindo@ascend.co.jp)
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "l2tp.h"
#include "interface.h"

static char tstr[] = " [|l2tp]";

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

static u_char *l2tp_message_type_string[] = {
	"RESERVED_0",		/* 0  Reserved */
	"SCCRQ",		/* 1  Start-Control-Connection-Request */
	"SCCRP",		/* 2  Start-Control-Connection-Reply */
	"SCCCN",		/* 3  Start-Control-Connection-Connected */
	"StopCCN",		/* 4  Stop-Control-Connection-Notification */
	"RESERVED_5",		/* 5  Reserved */
	"HELLO",		/* 6  Hello */
	"OCRQ",			/* 7  Outgoing-Call-Request */
	"OCRP",			/* 8  Outgoing-Call-Reply */
	"OCCN",			/* 9  Outgoing-Call-Connected */
	"ICRQ",			/* 10 Incoming-Call-Request */
	"ICRP",			/* 11 Incoming-Call-Reply */
	"ICCN",			/* 12 Incoming-Call-Connected */
	"RESERVED_13",		/* 13 Reserved */
	"CDN",			/* 14 Call-Disconnect-Notify */
	"WEN",			/* 15 WAN-Error-Notify */
	"SLI"			/* 16 Set-Link-Info */
#define L2TP_MAX_MSGTYPE_INDEX	17
};

static void l2tp_msgtype_print(const u_char *dat, u_int length);
static void l2tp_result_code_print(const u_char *dat, u_int length);
static void l2tp_proto_ver_print(const u_char *dat, u_int length);
static void l2tp_framing_cap_print(const u_char *dat, u_int length);
static void l2tp_bearer_cap_print(const u_char *dat, u_int length);
static void l2tp_tie_breaker_print(const u_char *dat, u_int length);
static void l2tp_firm_ver_print(const u_char *dat, u_int length);
static void l2tp_host_name_print(const u_char *dat, u_int length);
static void l2tp_vendor_name_print(const u_char *dat, u_int length);
static void l2tp_assnd_tun_id_print(const u_char *dat, u_int length);
static void l2tp_recv_win_size_print(const u_char *dat, u_int length);
static void l2tp_challenge_print(const u_char *dat, u_int length);
static void l2tp_q931_cc_print(const u_char *dat, u_int length);
static void l2tp_challenge_resp_print(const u_char *dat, u_int length);
static void l2tp_assnd_sess_id_print(const u_char *dat, u_int length);
static void l2tp_call_ser_num_print(const u_char *dat, u_int length);
static void l2tp_minimum_bps_print(const u_char *dat, u_int length);
static void l2tp_maximum_bps_print(const u_char *dat, u_int length);
static void l2tp_bearer_type_print(const u_char *dat, u_int length);
static void l2tp_framing_type_print(const u_char *dat, u_int length);
static void l2tp_packet_proc_delay_print(const u_char *dat, u_int length);
static void l2tp_called_number_print(const u_char *dat, u_int length);
static void l2tp_calling_number_print(const u_char *dat, u_int length);
static void l2tp_sub_address_print(const u_char *dat, u_int length);
static void l2tp_tx_conn_speed_print(const u_char *dat, u_int length);
static void l2tp_phy_channel_id_print(const u_char *dat, u_int length);
static void l2tp_ini_recv_lcp_print(const u_char *dat, u_int length);
static void l2tp_last_sent_lcp_print(const u_char *dat, u_int length);
static void l2tp_last_recv_lcp_print(const u_char *dat, u_int length);
static void l2tp_proxy_auth_type_print(const u_char *dat, u_int length);
static void l2tp_proxy_auth_name_print(const u_char *dat, u_int length);
static void l2tp_proxy_auth_chal_print(const u_char *dat, u_int length);
static void l2tp_proxy_auth_id_print(const u_char *dat, u_int length);
static void l2tp_proxy_auth_resp_print(const u_char *dat, u_int length);
static void l2tp_call_errors_print(const u_char *dat, u_int length);
static void l2tp_accm_print(const u_char *dat, u_int length);
static void l2tp_random_vector_print(const u_char *dat, u_int length);
static void l2tp_private_grp_id_print(const u_char *dat, u_int length);
static void l2tp_rx_conn_speed_print(const u_char *dat, u_int length);
static void l2tp_seq_required_print(const u_char *dat, u_int length);
static void l2tp_avp_print(const u_char *dat, u_int length);

static struct l2tp_avp_vec l2tp_avp[] = {
  {"MSGTYPE", l2tp_msgtype_print}, 		/* 0  Message Type */
  {"RESULT_CODE", l2tp_result_code_print},	/* 1  Result Code */
  {"PROTO_VER", l2tp_proto_ver_print},		/* 2  Protocol Version */
  {"FRAMING_CAP", l2tp_framing_cap_print},	/* 3  Framing Capabilities */
  {"BEARER_CAP", l2tp_bearer_cap_print},	/* 4  Bearer Capabilities */
  {"TIE_BREAKER", l2tp_tie_breaker_print},	/* 5  Tie Breaker */
  {"FIRM_VER", l2tp_firm_ver_print},		/* 6  Firmware Revision */
  {"HOST_NAME", l2tp_host_name_print},		/* 7  Host Name */
  {"VENDOR_NAME", l2tp_vendor_name_print},	/* 8  Vendor Name */
  {"ASSND_TUN_ID", l2tp_assnd_tun_id_print}, 	/* 9  Assigned Tunnel ID */
  {"RECV_WIN_SIZE", l2tp_recv_win_size_print},	/* 10 Receive Window Size */
  {"CHALLENGE", l2tp_challenge_print},		/* 11 Challenge */
  {"Q931_CC", l2tp_q931_cc_print},		/* 12 Q.931 Cause Code */
  {"CHALLENGE_RESP", l2tp_challenge_resp_print},/* 13 Challenge Response */
  {"ASSND_SESS_ID", l2tp_assnd_sess_id_print},  /* 14 Assigned Session ID */
  {"CALL_SER_NUM", l2tp_call_ser_num_print}, 	/* 15 Call Serial Number */
  {"MINIMUM_BPS",	l2tp_minimum_bps_print},/* 16 Minimum BPS */
  {"MAXIMUM_BPS", l2tp_maximum_bps_print},	/* 17 Maximum BPS */
  {"BEARER_TYPE",	l2tp_bearer_type_print},/* 18 Bearer Type */
  {"FRAMING_TYPE", l2tp_framing_type_print}, 	/* 19 Framing Type */
  {"PACKET_PROC_DELAY", l2tp_packet_proc_delay_print}, /* 20 Packet Processing Delay (OBSOLETE) */
  {"CALLED_NUMBER", l2tp_called_number_print},	/* 21 Called Number */
  {"CALLING_NUMBER", l2tp_calling_number_print},/* 22 Calling Number */
  {"SUB_ADDRESS",	l2tp_sub_address_print},/* 23 Sub-Address */
  {"TX_CONN_SPEED", l2tp_tx_conn_speed_print},	/* 24 (Tx) Connect Speed */
  {"PHY_CHANNEL_ID", l2tp_phy_channel_id_print},/* 25 Physical Channel ID */
  {"INI_RECV_LCP", l2tp_ini_recv_lcp_print}, 	/* 26 Initial Received LCP CONFREQ */
  {"LAST_SENT_LCP", l2tp_last_sent_lcp_print},	/* 27 Last Sent LCP CONFREQ */
  {"LAST_RECV_LCP", l2tp_last_recv_lcp_print},	/* 28 Last Received LCP CONFREQ */
  {"PROXY_AUTH_TYPE", l2tp_proxy_auth_type_print},/* 29 Proxy Authen Type */
  {"PROXY_AUTH_NAME", l2tp_proxy_auth_name_print},/* 30 Proxy Authen Name */
  {"PROXY_AUTH_CHAL", l2tp_proxy_auth_chal_print},/* 31 Proxy Authen Challenge */
  {"PROXY_AUTH_ID", l2tp_proxy_auth_id_print},	/* 32 Proxy Authen ID */
  {"PROXY_AUTH_RESP", l2tp_proxy_auth_resp_print},/* 33 Proxy Authen Response */
  {"CALL_ERRORS", l2tp_call_errors_print},	/* 34 Call Errors */
  {"ACCM", l2tp_accm_print},			/* 35 ACCM */
  {"RANDOM_VECTOR", l2tp_random_vector_print},	/* 36 Random Vector */
  {"PRIVATE_GRP_ID", l2tp_private_grp_id_print},/* 37 Private Group ID */
  {"RX_CONN_SPEED", l2tp_rx_conn_speed_print},	/* 38 (Rx) Connect Speed */
  {"SEQ_REQUIRED", l2tp_seq_required_print}, 	/* 39 Sequencing Required */
#define L2TP_MAX_AVP_INDEX	40
};

#if 0
static char *l2tp_result_code_StopCCN[] = {
         "Reserved",
         "General request to clear control connection",
         "General error--Error Code indicates the problem",
         "Control channel already exists",
         "Requester is not authorized to establish a control channel",
         "The protocol version of the requester is not supported",
         "Requester is being shut down",
         "Finite State Machine error"
#define L2TP_MAX_RESULT_CODE_STOPCC_INDEX	8
};
#endif

#if 0
static char *l2tp_result_code_CDN[] = {
	"Reserved",
	"Call disconnected due to loss of carrier",
	"Call disconnected for the reason indicated in error code",
	"Call disconnected for administrative reasons",
	"Call failed due to lack of appropriate facilities being " \
	"available (temporary condition)",
	"Call failed due to lack of appropriate facilities being " \
	"available (permanent condition)",
	"Invalid destination",
	"Call failed due to no carrier detected",
	"Call failed due to detection of a busy signal",
	"Call failed due to lack of a dial tone",
	"Call was not established within time allotted by LAC",
	"Call was connected but no appropriate framing was detected"
#define L2TP_MAX_RESULT_CODE_CDN_INDEX	12
};
#endif

#if 0
static char *l2tp_error_code_general[] = {
	"No general error",
	"No control connection exists yet for this LAC-LNS pair",
	"Length is wrong",
	"One of the field values was out of range or " \
	"reserved field was non-zero"
	"Insufficient resources to handle this operation now",
	"The Session ID is invalid in this context",
	"A generic vendor-specific error occurred in the LAC",
	"Try another"
#define L2TP_MAX_ERROR_CODE_GENERAL_INDEX	8
};
#endif

/******************************/
/* generic print out routines */
/******************************/
static void 
print_string(const u_char *dat, u_int length)
{
	int i;
	for (i=0; i<length; i++) {
		printf("%c", *dat++);
	}
}

static void 
print_octets(const u_char *dat, u_int length)
{
	int i;
	for (i=0; i<length; i++) {
		printf("%02x", *dat++);
	}
}

static void
print_short(const u_short *dat)
{
	printf("%u", ntohs(*dat));
}

static void
print_int(const u_int *dat)
{
	printf("%lu", (u_long)ntohl(*dat));
}

/**********************************/
/* AVP-specific print out routines*/
/**********************************/
static void
l2tp_msgtype_print(const u_char *dat, u_int length)
{
	u_short *ptr = (u_short *)dat;

	if (ntohs(*ptr) < L2TP_MAX_MSGTYPE_INDEX) {
		printf("%s", l2tp_message_type_string[ntohs(*ptr)]);
	}
}

static void
l2tp_result_code_print(const u_char *dat, u_int length)
{
	/* we just print out the result and error code number */
	u_short *ptr = (u_short *)dat;
	
	if (length == 2) {		/* result code */
		printf("%d", ntohs(*ptr));	
	} else if (length == 4) { 	/* result & error code */
		printf("%d/%d", ntohs(*ptr), ntohs(*(ptr+1)));
	} else if (length > 4) {	/* result & error code & msg */
		printf("%u/%u ", ntohs(*ptr), ntohs(*(ptr+1)));
		print_string((u_char *)(ptr+2), length - 4);
	}
}

static void
l2tp_proto_ver_print(const u_char *dat, u_int length)
{
	printf("%d.%d", *dat, *(dat+1));
}


static void
l2tp_framing_cap_print(const u_char *dat, u_int length)
{
	u_int *ptr = (u_int *)dat;

	if (ntohl(*ptr) &  L2TP_FRAMING_CAP_ASYNC_MASK) {
		printf("A");
	}
	if (ntohl(*ptr) &  L2TP_FRAMING_CAP_SYNC_MASK) {
		printf("S");
	}
}

static void
l2tp_bearer_cap_print(const u_char *dat, u_int length)
{
	u_int *ptr = (u_int *)dat;

	if (ntohl(*ptr) &  L2TP_BEARER_CAP_ANALOG_MASK) {
		printf("A");
	}
	if (ntohl(*ptr) &  L2TP_BEARER_CAP_DIGITAL_MASK) {
		printf("D");
	}
}

static void
l2tp_tie_breaker_print(const u_char *dat, u_int length)
{
	print_octets(dat, (length < 8 ? length : 8));
}

static void
l2tp_firm_ver_print(const u_char *dat, u_int length)
{
	print_short((u_short *)dat);
}

static void
l2tp_host_name_print(const u_char *dat, u_int length)
{
	print_string(dat, length);
}

static void
l2tp_vendor_name_print(const u_char *dat, u_int length)
{
	print_string(dat, length);
}

static void
l2tp_assnd_tun_id_print(const u_char *dat, u_int length)
{
	print_short((u_short *)dat);
}

static void
l2tp_recv_win_size_print(const u_char *dat, u_int length)
{
	print_short((u_short *)dat); 
}

static void
l2tp_challenge_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_q931_cc_print(const u_char *dat, u_int length)
{
	print_short((u_short *)dat);
	print_short((u_short *)(dat + 2));
	if (length > 3) {
		printf(" ");
		print_string(dat+3, length-3);
	} 
}

static void
l2tp_challenge_resp_print(const u_char *dat, u_int length)
{
	print_octets(dat, 16);		/* XXX length should be 16? */
}

static void
l2tp_assnd_sess_id_print(const u_char *dat, u_int length)
{
	print_short((u_short *)dat);
}

static void
l2tp_call_ser_num_print(const u_char *dat, u_int length)
{
	print_int((u_int *)dat);
}

static void
l2tp_minimum_bps_print(const u_char *dat, u_int length)
{
	print_int((u_int *)dat);
}

static void
l2tp_maximum_bps_print(const u_char *dat, u_int length)
{
	print_int((u_int *)dat);
}

static void
l2tp_bearer_type_print(const u_char *dat, u_int length)
{
	u_int *ptr = (u_int *)dat;

	if (ntohl(*ptr) &  L2TP_BEARER_TYPE_ANALOG_MASK) {
		printf("A");
	}
	if (ntohl(*ptr) &  L2TP_BEARER_TYPE_DIGITAL_MASK) {
		printf("D");
	}
}

static void
l2tp_framing_type_print(const u_char *dat, u_int length)
{
	u_int *ptr = (u_int *)dat;

	if (ntohl(*ptr) &  L2TP_FRAMING_TYPE_ASYNC_MASK) {
		printf("A");
	}
	if (ntohl(*ptr) &  L2TP_FRAMING_TYPE_SYNC_MASK) {
		printf("S");
	}
}

static void
l2tp_packet_proc_delay_print(const u_char *dat, u_int length)
{
	printf("obsolete");
}

static void
l2tp_called_number_print(const u_char *dat, u_int length)
{
	print_string(dat, length);
}

static void
l2tp_calling_number_print(const u_char *dat, u_int length)
{
	print_string(dat, length);
}

static void
l2tp_sub_address_print(const u_char *dat, u_int length)
{
	print_string(dat, length);
}

static void
l2tp_tx_conn_speed_print(const u_char *dat, u_int length)
{
	print_int((u_int *)dat);
}

static void
l2tp_phy_channel_id_print(const u_char *dat, u_int length)
{
	print_int((u_int *)dat);
}

static void
l2tp_ini_recv_lcp_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_last_sent_lcp_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_last_recv_lcp_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_proxy_auth_type_print(const u_char *dat, u_int length)
{
	u_short *ptr = (u_short *)dat;

	switch (ntohs(*ptr)) {
	case L2TP_AUTHEN_TYPE_RESERVED:
		printf("Reserved");
		break;
	case L2TP_AUTHEN_TYPE_TEXTUAL:
		printf("Textual");
		break;
	case L2TP_AUTHEN_TYPE_CHAP:
		printf("CHAP");
		break;
	case L2TP_AUTHEN_TYPE_PAP:
		printf("PAP");
		break;
	case L2TP_AUTHEN_TYPE_NO_AUTH:
		printf("No Auth");
		break;
	case L2TP_AUTHEN_TYPE_MSCHAP:
		printf("MS-CHAP");
		break;
	default:
		printf("unknown");
	}
}

static void
l2tp_proxy_auth_name_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_proxy_auth_chal_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_proxy_auth_id_print(const u_char *dat, u_int length)
{
	u_short *ptr = (u_short *)dat;

	printf("%d", ntohs(*ptr) & L2TP_PROXY_AUTH_ID_MASK);
}

static void
l2tp_proxy_auth_resp_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_call_errors_print(const u_char *dat, u_int length)
{
	struct l2tp_call_errors *ptr = (struct l2tp_call_errors *)dat;

	printf("CRCErr=%d FrameErr=%d HardOver=%d BufOver=%d ",
	       ptr->crc_errs,
	       ptr->framing_errs,
	       ptr->hardware_overruns,
	       ptr->buffer_overruns);
	printf("Timeout=%d AlingErr=%d",
	       ptr->timeout_errs,
	       ptr->alignment_errs);
}

static void
l2tp_accm_print(const u_char *dat, u_int length)
{
	struct l2tp_accm *ptr = (struct l2tp_accm *)dat;

	printf("send=%x recv=%x", ptr->send_accm, ptr->recv_accm);
}

static void
l2tp_random_vector_print(const u_char *dat, u_int length)
{
	print_octets(dat, length);
}

static void
l2tp_private_grp_id_print(const u_char *dat, u_int length)
{
	print_string(dat, length);	
	/* XXX print_octets is more appropriate?? */
}

static void
l2tp_rx_conn_speed_print(const u_char *dat, u_int length)
{
	print_int((u_int *)dat);
}

static void
l2tp_seq_required_print(const u_char *dat, u_int length)
{
	return;
}

static void
l2tp_avp_print(const u_char *dat, u_int length)
{
	u_int len;
	const u_short *ptr = (u_short *)dat;
	int hidden = FALSE;

	printf(" ");
	if (length > 0 && (snapend - dat) >= 2) {
		/* there must be at least two octets for the length
		   to be decoded */
		if ((len = (ntohs(*ptr) & L2TP_AVP_HDR_LEN_MASK)) <=
		    (snapend - dat)) {
			if (ntohs(*ptr) & L2TP_AVP_HDR_FLAG_MANDATORY) {
				printf("*");
			}
			if (ntohs(*ptr) & L2TP_AVP_HDR_FLAG_HIDDEN) {
				hidden = TRUE;
				printf("?");
			}
		} else {
			printf("|...");
			return;
		}
		ptr++;

		if (ntohs(*ptr)) {	/* IETF == 0 */
			printf("vendor=%04x", ntohs(*ptr));
		}
		ptr++;

		if (ntohs(*ptr) < L2TP_MAX_AVP_INDEX) {
			printf("%s", l2tp_avp[ntohs(*ptr)].name);
			printf("(");
			if (!hidden && len >= 6) {
				(l2tp_avp[ntohs(*ptr)].print)
					((u_char *)ptr+2, len-6);
			} else {
				printf("???");
			}
			printf(")");
		} else {
			printf(" invalid AVP %u", ntohs(*ptr));
		}

		if (length >= len && len > 0)
			l2tp_avp_print(dat + len, length - len);
	} else if (length == 0) {
		return;
	} else {
		printf("|...");
	}
}


void
l2tp_print(const u_char *dat, u_int length)
{
	u_int cnt = 0;			/* total octets consumed */
	u_short pad, val;
	int flag_t, flag_l, flag_s, flag_o, flag_p;

	flag_t = flag_l = flag_s = flag_o = flag_p = FALSE;

	if (length < 6 || snapend - dat < 6) { 
		/* flag/ver, tunnel_id, session_id must be present for
		   this packet to be properly decoded */
		printf("%s", tstr);
		return;
	}

	TCHECK2(*dat, sizeof(val));
	memcpy(&val, dat, sizeof(val));
	if ((ntohs(val) & L2TP_VERSION_MASK) == L2TP_VERSION_L2TP) {
		printf("l2tp:");
	} else if ((ntohs(val) & L2TP_VERSION_MASK) == L2TP_VERSION_L2F) {
		printf("l2f:");
		return;		/* nothing to do */
	} else {
		printf("Unknown Version, neither L2F(1) nor L2TP(2)");
		return;		/* nothing we can do */
	}

	printf("[");
	if (ntohs(val) & L2TP_FLAG_TYPE) {
		flag_t = TRUE;
		printf("T");
	}
	if (ntohs(val) & L2TP_FLAG_LENGTH) {
		flag_l = TRUE;
		printf("L");
	}
	if (ntohs(val) & L2TP_FLAG_SEQUENCE) {
		flag_s = TRUE;
		printf("S");
	}
	if (ntohs(val) & L2TP_FLAG_OFFSET) {
		flag_o = TRUE;
		printf("O");
	}
	if (ntohs(val) & L2TP_FLAG_PRIORITY) {
		flag_p = TRUE;
		printf("P");
	}
	printf("]");

	dat += 2;
	cnt += 2;
	
	if (flag_l) {
		TCHECK2(*dat, sizeof(val));
		memcpy(&val, dat, sizeof(val));
		dat += 2;
		cnt += 2;
	}

	TCHECK2(*dat, sizeof(val));
	memcpy(&val, dat, sizeof(val));
	printf("(%d/", ntohs(val));		/* Tunnel ID */
	dat += 2;
	cnt += 2;
	TCHECK2(*dat, sizeof(val));
	memcpy(&val, dat, sizeof(val));
	printf("%d)",  ntohs(val));		/* Session ID */
	dat += 2;
	cnt += 2;

	if (flag_s) {
		TCHECK2(*dat, sizeof(val));
		memcpy(&val, dat, sizeof(val));
		printf("Ns=%d,", ntohs(val));
		dat += 2;
		cnt += 2;
		TCHECK2(*dat, sizeof(val));
		memcpy(&val, dat, sizeof(val));
		printf("Nr=%d",  ntohs(val));
		dat += 2;
		cnt += 2;
	}

	if (flag_o) {
		TCHECK2(*dat, sizeof(val));
		memcpy(&val, dat, sizeof(val));
		pad =  ntohs(val);
		dat += 2;
		cnt += 2;
		TCHECK2(*dat, pad);
		dat += pad;
		cnt += pad;
	}

	if (flag_t) {
		if (length - cnt == 0) {
			printf(" ZLB");
		} else {
			if (length >= cnt)
				l2tp_avp_print(dat, length - cnt);
		}
	} else {
#if 0
		printf(" {");
		ppp_hdlc_print(dat, length - cnt);
		printf("}");
#else
		printf("[hdlc|]");
#endif
	}

trunc:
	printf("[|l2tp]");
}
