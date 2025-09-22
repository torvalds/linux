/*	$OpenBSD: l2tp_subr.c,v 1.5 2023/09/11 07:33:07 yasuoka Exp $	*/

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */
/* $Id: l2tp_subr.c,v 1.5 2023/09/11 07:33:07 yasuoka Exp $ */
/**@file L2TP related sub-routines */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <event.h>

#ifdef USE_LIBSOCKUTIL
#include <seil/sockfromto.h>
#endif

#include "debugutil.h"
#include "hash.h"
#include "bytebuf.h"
#include "slist.h"
#include "l2tp.h"
#include "l2tp_subr.h"
#include "l2tp_local.h"

#ifdef	L2TP_SUBR_DEBUG
#define	L2TP_SUBR_ASSERT(x)	ASSERT(x)
#else
#define	L2TP_SUBR_ASSERT(x)
#endif

/*
 * AVP
 */
int
avp_enum(struct l2tp_avp *avp, const u_char *pkt, int pktlen, int filldata)
{
	uint16_t flags;

	L2TP_SUBR_ASSERT(pktlen >= 6);

	if (pktlen < 6)
		return -1;

	GETSHORT(flags, pkt);

	avp->is_mandatory = ((flags & 0x8000) != 0)? 1 : 0;
	avp->is_hidden = ((flags & 0x4000) != 0)? 1 : 0;
	avp->length = flags & 0x03ff;

	GETSHORT(avp->vendor_id, pkt);

	avp->attr_type = *pkt << 8;
	avp->attr_type |= *(pkt + 1);
	pkt += 2;

	if (avp->length < 6 || avp->length > pktlen)
		return -1;

	if (avp->length > 6 && filldata != 0)
		memcpy(avp->attr_value, pkt, avp->length - 6);

	return avp->length;
}

#define	NAME_VAL(x)	{ x, #x }
static struct _label_name {
	int		label;
	const char	*name;
}
l2tp_mes_type_names[] = {
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_SCCRQ),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_SCCRP),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_SCCCN),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_StopCCN),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_HELLO),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_OCRQ),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_OCRP),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_OCCN),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_ICRQ),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_ICRP),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_ICCN),
	NAME_VAL(L2TP_AVP_MESSAGE_TYPE_CDN),
},
l2tp_avp_attribute_names[] = {
	NAME_VAL(L2TP_AVP_TYPE_MESSAGE_TYPE),
	NAME_VAL(L2TP_AVP_TYPE_RESULT_CODE),
	NAME_VAL(L2TP_AVP_TYPE_PROTOCOL_VERSION),
	NAME_VAL(L2TP_AVP_TYPE_FRAMING_CAPABILITIES),
	NAME_VAL(L2TP_AVP_TYPE_BEARER_CAPABILITIES),
	NAME_VAL(L2TP_AVP_TYPE_TIE_BREAKER),
	NAME_VAL(L2TP_AVP_TYPE_FIRMWARE_REVISION),
	NAME_VAL(L2TP_AVP_TYPE_HOST_NAME),
	NAME_VAL(L2TP_AVP_TYPE_VENDOR_NAME),
	NAME_VAL(L2TP_AVP_TYPE_ASSINGED_TUNNEL_ID),
	NAME_VAL(L2TP_AVP_TYPE_RECV_WINDOW_SIZE),
	NAME_VAL(L2TP_AVP_TYPE_CHALLENGE),
	NAME_VAL(L2TP_AVP_TYPE_CAUSE_CODE),
	NAME_VAL(L2TP_AVP_TYPE_CHALLENGE_RESPONSE),
	NAME_VAL(L2TP_AVP_TYPE_ASSIGNED_SESSION_ID),
	NAME_VAL(L2TP_AVP_TYPE_CALL_SERIAL_NUMBER),
	NAME_VAL(L2TP_AVP_TYPE_MINIMUM_BPS),
	NAME_VAL(L2TP_AVP_TYPE_MAXIMUM_BPS),
	NAME_VAL(L2TP_AVP_TYPE_BEARER_TYPE),
	NAME_VAL(L2TP_AVP_TYPE_FRAMING_TYPE),
	NAME_VAL(L2TP_AVP_TYPE_CALLED_NUMBER),
	NAME_VAL(L2TP_AVP_TYPE_CALLING_NUMBER),
	NAME_VAL(L2TP_AVP_TYPE_SUB_ADDRESS),
	NAME_VAL(L2TP_AVP_TYPE_TX_CONNECT_SPEED),
	NAME_VAL(L2TP_AVP_TYPE_PHYSICAL_CHANNEL_ID),
	NAME_VAL(L2TP_AVP_TYPE_INITIAL_RECV_LCP_CONFREQ),
	NAME_VAL(L2TP_AVP_TYPE_LAST_SENT_LCP_CONFREQ),
	NAME_VAL(L2TP_AVP_TYPE_LAST_RECV_LCP_CONFREQ),
	NAME_VAL(L2TP_AVP_TYPE_PROXY_AUTHEN_TYPE),
	NAME_VAL(L2TP_AVP_TYPE_PROXY_AUTHEN_NAME),
	NAME_VAL(L2TP_AVP_TYPE_PROXY_AUTHEN_CHALLENGE),
	NAME_VAL(L2TP_AVP_TYPE_PROXY_AUTHEN_ID),
	NAME_VAL(L2TP_AVP_TYPE_PROXY_AUTHEN_RESPONSE),
	NAME_VAL(L2TP_AVP_TYPE_CALL_ERRORS),
	NAME_VAL(L2TP_AVP_TYPE_ACCM),
	NAME_VAL(L2TP_AVP_TYPE_RANDOM_VECTOR),
	NAME_VAL(L2TP_AVP_TYPE_PRIVATE_GROUP_ID),
	NAME_VAL(L2TP_AVP_TYPE_RX_CONNECT_SPEED),
	NAME_VAL(L2TP_AVP_TYPE_SEQUENCING_REQUIRED),
	NAME_VAL(L2TP_AVP_TYPE_TX_MINIMUM),
	NAME_VAL(L2TP_AVP_TYPE_CALLING_SUB_ADDRESS),
	NAME_VAL(L2TP_AVP_TYPE_PPP_DISCONNECT_CAUSE_CODE),
	NAME_VAL(L2TP_AVP_TYPE_CCDS),
	NAME_VAL(L2TP_AVP_TYPE_SDS),
	NAME_VAL(L2TP_AVP_TYPE_LCP_WANT_OPTIONS),
	NAME_VAL(L2TP_AVP_TYPE_LCP_ALLOW_OPTIONS),
	NAME_VAL(L2TP_AVP_TYPE_LNS_LAST_SENT_LCP_CONFREQ),
	NAME_VAL(L2TP_AVP_TYPE_LNS_LAST_RECV_LCP_CONFREQ),
	NAME_VAL(L2TP_AVP_TYPE_MODEM_ON_HOLD_CAPABLE),
	NAME_VAL(L2TP_AVP_TYPE_MODEM_ON_HOLD_STATUS),
	NAME_VAL(L2TP_AVP_TYPE_PPPOE_RELAY),
	NAME_VAL(L2TP_AVP_TYPE_PPPOE_RELAY_RESP_CAP),
	NAME_VAL(L2TP_AVP_TYPE_PPPOE_RELAY_FORW_CAP),
	NAME_VAL(L2TP_AVP_TYPE_EXTENDED_VENDOR_ID),
	NAME_VAL(L2TP_AVP_TYPE_PSEUDOWIRE_CAP_LIST),
	NAME_VAL(L2TP_AVP_TYPE_LOCAL_SESSION_ID),
	NAME_VAL(L2TP_AVP_TYPE_REMOTE_SESSION_ID),
	NAME_VAL(L2TP_AVP_TYPE_ASSIGNED_COOKIE),
	NAME_VAL(L2TP_AVP_TYPE_REMOTE_END_ID),
	NAME_VAL(L2TP_AVP_TYPE_APPLICATION_CODE),
	NAME_VAL(L2TP_AVP_TYPE_PSEUDOWIRE_TYPE),
	NAME_VAL(L2TP_AVP_TYPE_L2_SPECIFIC_SUBLAYER),
	NAME_VAL(L2TP_AVP_TYPE_DATA_SEQUENCING),
	NAME_VAL(L2TP_AVP_TYPE_CIRCUIT_STATUS),
	NAME_VAL(L2TP_AVP_TYPE_PREFERRED_LANGUAGE),
	NAME_VAL(L2TP_AVP_TYPE_CTRL_MSG_AUTH_NONCE),
	NAME_VAL(L2TP_AVP_TYPE_TX_CONNECT_SPEED),
	NAME_VAL(L2TP_AVP_TYPE_RX_CONNECT_SPEED),
	NAME_VAL(L2TP_AVP_TYPE_FAILOVER_CAPABILITY),
	NAME_VAL(L2TP_AVP_TYPE_TUNNEL_RECOVERY),
	NAME_VAL(L2TP_AVP_TYPE_SUGGESTED_CTRL_SEQUENCE),
	NAME_VAL(L2TP_AVP_TYPE_FAILOVER_SESSION_STATE),
	NAME_VAL(L2TP_AVP_TYPE_MULTICAST_CAPABILITY),
	NAME_VAL(L2TP_AVP_TYPE_NEW_OUTGOING_SESSIONS),
	NAME_VAL(L2TP_AVP_TYPE_NEW_OUTGOING_SESSIONS_ACK),
	NAME_VAL(L2TP_AVP_TYPE_WITHDRAW_OUTGOING_SESSIONS),
	NAME_VAL(L2TP_AVP_TYPE_MULTICAST_PACKETS_PRIORITY),
},
l2tp_stopccn_rcode_names[] = {
	NAME_VAL(L2TP_STOP_CCN_RCODE_GENERAL),
	NAME_VAL(L2TP_STOP_CCN_RCODE_GENERAL_ERROR),
	NAME_VAL(L2TP_STOP_CCN_RCODE_ALREADY_EXISTS),
	NAME_VAL(L2TP_STOP_CCN_RCODE_UNAUTHORIZED),
	NAME_VAL(L2TP_STOP_CCN_RCODE_BAD_PROTOCOL_VERSION),
	NAME_VAL(L2TP_STOP_CCN_RCODE_SHUTTING_DOWN),
	NAME_VAL(L2TP_STOP_CCN_RCODE_FSM_ERROR),
},
l2tp_cdn_rcode_names[] = {
	NAME_VAL(L2TP_CDN_RCODE_LOST_CARRIER),
	NAME_VAL(L2TP_CDN_RCODE_ERROR_CODE),
	NAME_VAL(L2TP_CDN_RCODE_ADMINISTRATIVE_REASON),
	NAME_VAL(L2TP_CDN_RCODE_TEMP_NOT_AVALIABLE),
	NAME_VAL(L2TP_CDN_RCODE_PERM_NOT_AVALIABLE),
	NAME_VAL(L2TP_CDN_RCODE_INVALID_DESTINATION),
	NAME_VAL(L2TP_CDN_RCODE_NO_CARRIER),
	NAME_VAL(L2TP_CDN_RCODE_BUSY),
	NAME_VAL(L2TP_CDN_RCODE_NO_DIALTONE),
	NAME_VAL(L2TP_CDN_RCODE_CALL_TIMEOUT_BY_LAC),
	NAME_VAL(L2TP_CDN_RCODE_NO_FRAMING_DETECTED),
},
l2tp_ecode_names[] = {
	NAME_VAL(L2TP_ECODE_NO_CONTROL_CONNECTION),
	NAME_VAL(L2TP_ECODE_WRONG_LENGTH),
	NAME_VAL(L2TP_ECODE_INVALID_MESSAGE),
	NAME_VAL(L2TP_ECODE_NO_RESOURCE),
	NAME_VAL(L2TP_ECODE_INVALID_SESSION_ID),
	NAME_VAL(L2TP_ECODE_GENERIC_ERROR),
	NAME_VAL(L2TP_ECODE_TRY_ANOTHER),
	NAME_VAL(L2TP_ECODE_UNKNOWN_MANDATORY_AVP),
};
#undef	NAME_VAL

const char *
avp_attr_type_string(int attr_type)
{
	int i;

	for (i = 0; i < countof(l2tp_avp_attribute_names); i++) {
		if (attr_type == l2tp_avp_attribute_names[i].label)
			return l2tp_avp_attribute_names[i].name + 14;
	}
	return "UNKNOWN_AVP";
}

const char *
l2tp_stopccn_rcode_string(int rcode)
{
	int i;

	for (i = 0; i < countof(l2tp_stopccn_rcode_names); i++) {
		if (rcode == l2tp_stopccn_rcode_names[i].label)
			return l2tp_stopccn_rcode_names[i].name + 20;
	}
	return "UNKNOWN";
}

const char *
l2tp_cdn_rcode_string(int rcode)
{
	int i;

	for (i = 0; i < countof(l2tp_cdn_rcode_names); i++) {
		if (rcode == l2tp_cdn_rcode_names[i].label)
			return l2tp_cdn_rcode_names[i].name + 15;
	}
	return "UNKNOWN";
}

const char *
l2tp_ecode_string(int ecode)
{
	int i;

	if (ecode == 0)
		return "none";
	for (i = 0; i < countof(l2tp_ecode_names); i++) {
		if (ecode == l2tp_ecode_names[i].label)
			return l2tp_ecode_names[i].name + 11;
	}
	return "UNKNOWN";
}

/**
 * Search the AVP that matches given vendor_id and attr_type and return it
 * In case the "fill_data" is specified (non 0 value is specified as the
 * "fill_data"), the memory space of the "avp" must be larger than or equal
 * to L2TP_AVP_MAXSIZ (1024).
 */
struct l2tp_avp *
avp_find(struct l2tp_avp *avp, const u_char *pkt, int pktlen,
    uint16_t vendor_id, uint16_t attr_type, int fill_data)
{
	int avpsz;

	while (pktlen >= 6 &&
	    (avpsz = avp_enum(avp, pkt, pktlen, fill_data)) > 0) {
		L2TP_SUBR_ASSERT(avpsz >= 6);
		if (avp->vendor_id != vendor_id || avp->attr_type != attr_type) {
			pkt += avpsz;
			pktlen -= avpsz;
			continue;
		}
		return avp;
	}

	return NULL;
}

/**
 * Search the Message-Type AVP and return it.  The memory space of the "avp"
 * must be larger than or equal to L2TP_AVP_MAXSIZ (1024).
 */
struct l2tp_avp *
avp_find_message_type_avp(struct l2tp_avp *avp, const u_char *pkt, int pktlen)
{
	return avp_find(avp, pkt, pktlen, 0, L2TP_AVP_TYPE_MESSAGE_TYPE, 1);
}

/**
 * add an AVP to bytebuffer
 */
int
bytebuf_add_avp(bytebuffer *bytebuf, struct l2tp_avp *avp, int value_len)
{
	struct l2tp_avp avp1;

	memcpy(&avp1, avp, sizeof(struct l2tp_avp));

	avp1.length = value_len + 6;
	avp1.vendor_id = htons(avp->vendor_id);
	avp1.attr_type = htons(avp->attr_type);
	*(uint16_t *)&avp1 = htons(*(uint16_t *)&avp1);

	if (bytebuffer_put(bytebuf, &avp1, 6) == NULL)
		return -1;
	if (bytebuffer_put(bytebuf, avp->attr_value, value_len) == NULL)
		return -1;

	return 0;
}

const char *
avp_mes_type_string(int mes_type)
{
	int i;

	for (i = 0; i < countof(l2tp_mes_type_names); i++) {
		if (mes_type == l2tp_mes_type_names[i].label)
			return l2tp_mes_type_names[i].name + 22;
	}
	return "Unknown";
}
