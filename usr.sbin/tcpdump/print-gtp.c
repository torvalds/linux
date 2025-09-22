/*	$OpenBSD: print-gtp.c,v 1.13 2020/10/26 23:19:18 jca Exp $ */
/*
 * Copyright (c) 2009, 2010 Joel Sing <jsing@openbsd.org>
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

/*
 * Decoder for the GPRS Trunking Protocol (GTP).
 *
 * This work has been kindly sponsored by SystemNet (www.systemnet.no).
 *
 * GTPv0 standards are available from the ETSI website:
 *
 *     http://pda.etsi.org/pda/
 *
 * GTPv1 standards are available from the 3GPP website:
 *
 *     http://www.3gpp.org/specifications
 *
 * The following standards have been referenced to create this decoder:
 *
 *     ETSI GSM 09.60 - GPRS Tunnelling Protocol (GTPv0)
 *     ETSI GSM 12.15 - GPRS Charging (GTPv0')
 *
 *     3GPP TS 23.003 - Numbering, addressing and identification
 *     3GPP TS 24.008 - Core network protocols
 *     3GPP TS 29.002 - Mobile Application Part (MAP) specification
 *     3GPP TS 29.060 - GPRS Tunnelling Protocol (GTPv1-C/GTPv1-U)
 *     3GPP TS 32.295 - Charging Data Record (CDR) transfer (GTPv1')
 */

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addrtoname.h"
#include "interface.h"
#include "gtp.h"

#ifndef nitems
#define nitems(_a)  (sizeof((_a)) / sizeof((_a)[0]))
#endif

void	gtp_print(const u_char *, u_int, u_short, u_short);
void	gtp_decode_ie(const u_char *, u_short, int);
void	gtp_print_tbcd(const u_char *, u_int);
void	gtp_print_user_address(const u_char *, u_int);
void	gtp_print_apn(const u_char *, u_int);
void	gtp_print_str(const char **, u_int, u_int);

void	gtp_v0_print(const u_char *, u_int, u_short, u_short);
void	gtp_v0_print_prime(const u_char *);
int	gtp_v0_print_tv(const u_char *, u_int);
int	gtp_v0_print_tlv(const u_char *, u_int);

void	gtp_v1_print(const u_char *, u_int, u_short, u_short);
void	gtp_v1_print_ctrl(const u_char *, u_int, struct gtp_v1_hdr *);
void	gtp_v1_print_user(const u_char *, u_int, struct gtp_v1_hdr *);
void	gtp_v1_print_prime(const u_char *, struct gtp_v1_prime_hdr *);
int	gtp_v1_print_tv(const u_char *, u_int);
int	gtp_v1_print_tlv(const u_char *, u_int);

/* GTPv0 message types. */
static struct tok gtp_v0_msgtype[] = {

	{ 1,	"Echo Request" },
	{ 2,	"Echo Response" },
	{ 3,	"Version Not Supported" },
	{ 4,	"Node Alive Request" },
	{ 5,	"Node Alive Response" },
	{ 6,	"Redirection Request" },
	{ 7,	"Redirection Response" },
	{ 16,	"Create PDP Context Request" },
	{ 17,	"Create PDP Context Response" },
	{ 18,	"Update PDP Context Request" },
	{ 19,	"Update PDP Context Response" },
	{ 20,	"Delete PDP Context Request" },
	{ 21,	"Delete PDP Context Response" },
	{ 22,	"Create AA PDP Context Request" },
	{ 23,	"Create AA PDP Context Response" },
	{ 24,	"Delete AA PDP Context Request" },
	{ 25,	"Delete AA PDP Context Response" },
	{ 26,	"Error Indication" },
	{ 27,	"PDU Notification Request" },
	{ 28,	"PDU Notification Response" },
	{ 29,	"PDU Notification Reject Request" },
	{ 30,	"PDU Notification Reject Response" },
	{ 32,	"Send Routeing Information Request" },
	{ 33,	"Send Routeing Information Response" },
	{ 34,	"Failure Report Request" },
	{ 35,	"Failure Report Response" },
	{ 36,	"MS GPRS Present Request" },
	{ 37,	"MS GPRS Present Response" },
	{ 48,	"Identification Request" },
	{ 49,	"Identification Response" },
	{ 50,	"SGSN Context Request" },
	{ 51,	"SGSN Context Response" },
	{ 52,	"SGSN Context Acknowledge" },
	{ 240,	"Data Record Transfer Request" },
	{ 241,	"Data Record Transfer Response" },
	{ 255,	"T-PDU" },

	{ 0,	NULL }
};

/* GTPv0 causes. */
static struct tok gtp_v0_cause[] = {

	{ 0,	"Request IMSI" },
	{ 1,	"Request IMEI" },
	{ 2,	"Request IMSI and IMEI" },
	{ 3,	"No identity needed" },
	{ 4,	"MS refuses" },
	{ 5,	"MS is not GPRS responding" },
	{ 128,	"Request accepted" },
	{ 192,	"Non-existent" },
	{ 193,	"Invalid message format" },
	{ 194,	"IMSI not known" },
	{ 195,	"MS is GPRS detached" },
	{ 196,	"MS is not GPRS responding" },
	{ 197,	"MS refuses" },
	{ 198,	"Version not supported" },
	{ 199,	"No resources available" },
	{ 200,	"Service not supported" },
	{ 201,	"Mandatory IE incorrect" },
	{ 202,	"Mandatory IE missing" },
	{ 203,	"Optional IE incorrect" },
	{ 204,	"System failure" },
	{ 205,	"Roaming restriction" },
	{ 206,	"P-TMSI signature mismatch" },
	{ 207,	"GPRS connection suspended" },
	{ 208,	"Authentication failure" },
	{ 209,	"User authentication failed" },

	{ 0,	NULL }
};

/* GTPv1 message types. */
static struct tok gtp_v1_msgtype[] = {

	{ 1,	"Echo Request" },
	{ 2,	"Echo Response" },
	{ 3,	"Version Not Supported" },
	{ 4,	"Node Alive Request" },
	{ 5,	"Node Alive Response" },
	{ 6,	"Redirection Request" },
	{ 7,	"Redirection Response" },
	{ 16,	"Create PDP Context Request" },
	{ 17,	"Create PDP Context Response" },
	{ 18,	"Update PDP Context Request" },
	{ 19,	"Update PDP Context Response" },
	{ 20,	"Delete PDP Context Request" },
	{ 21,	"Delete PDP Context Response" },
	{ 22,	"Initiate PDP Context Activiation Request" },
	{ 23,	"Initiate PDP Context Activiation Response" },
	{ 26,	"Error Indication" },
	{ 27,	"PDU Notification Request" },
	{ 28,	"PDU Notification Response" },
	{ 29,	"PDU Notification Reject Request" },
	{ 30,	"PDU Notification Reject Response" },
	{ 31,	"Supported Extension Headers Notification" },
	{ 32,	"Send Routeing Information for GPRS Request" },
	{ 33,	"Send Routeing Information for GPRS Response" },
	{ 34,	"Failure Report Request" },
	{ 35,	"Failure Report Response" },
	{ 36,	"Note MS GPRS Present Request" },
	{ 37,	"Note MS GPRS Present Response" },
	{ 48,	"Identification Request" },
	{ 49,	"Identification Response" },
	{ 50,	"SGSN Context Request" },
	{ 51,	"SGSN Context Response" },
	{ 52,	"SGSN Context Acknowledge" },
	{ 53,	"Forward Relocation Request" },
	{ 54,	"Forward Relocation Response" },
	{ 55,	"Forward Relocation Complete" },
	{ 56,	"Relocation Cancel Request" },
	{ 57,	"Relocation Cancel Response" },
	{ 58,	"Forward SRNS Context" },
	{ 59,	"Forward Relocation Complete Acknowledge" },
	{ 60,	"Forward SRNS Context Acknowledge" },
	{ 70,	"RAN Information Relay" },
	{ 96,	"MBMS Notification Request" },
	{ 97,	"MBMS Notification Response" },
	{ 98,	"MBMS Notification Reject Request" },
	{ 99,	"MBMS Notification Reject Response" },
	{ 100,	"Create MBMS Context Request" },
	{ 101,	"Create MBMS Context Response" },
	{ 102,	"Update MBMS Context Request" },
	{ 103,	"Update MBMS Context Response" },
	{ 104,	"Delete MBMS Context Request" },
	{ 105,	"Delete MBMS Context Response" },
	{ 112,	"MBMS Registration Request" },
	{ 113,	"MBMS Registration Response" },
	{ 114,	"MBMS De-Registration Request" },
	{ 115,	"MBMS De-Registration Response" },
	{ 116,	"MBMS Session Start Request" },
	{ 117,	"MBMS Session Start Response" },
	{ 118,	"MBMS Session Stop Request" },
	{ 119,	"MBMS Session Stop Response" },
	{ 120,	"MBMS Session Update Request" },
	{ 121,	"MBMS Session Update Response" },
	{ 128,	"MBMS Info Change Notification Request" },
	{ 129,	"MBMS Info Change Notification Response" },
	{ 240,	"Data Record Transfer Request" },
	{ 241,	"Data Record Transfer Response" },
	{ 255,	"G-PDU" },

	{ 0,	NULL }
};

/* GTPv1 Causes. */
static struct tok gtp_v1_cause[] = {

	/* GTPv1-C. */
	{ 0,	"Request IMSI" },
	{ 1,	"Request IMEI" },
	{ 2,	"Request IMSI and IMEI" },
	{ 3,	"No identity needed" },
	{ 4,	"MS refuses" },
	{ 5,	"MS is not GPRS responding" },
	{ 128,	"Request accepted" },
	{ 192,	"Non-existent" },
	{ 193,	"Invalid message format" },
	{ 194,	"IMSI not known" },
	{ 195,	"MS is GPRS detached" },
	{ 196,	"MS is not GPRS responding" },
	{ 197,	"MS refuses" },
	{ 198,	"Version not supported" },
	{ 199,	"No resources available" },
	{ 200,	"Service not supported" },
	{ 201,	"Mandatory IE incorrect" },
	{ 202,	"Mandatory IE missing" },
	{ 203,	"Optional IE incorrect" },
	{ 204,	"System failure" },
	{ 205,	"Roaming restriction" },
	{ 206,	"P-TMSI signature mismatch" },
	{ 207,	"GPRS connection suspended" },
	{ 208,	"Authentication failure" },
	{ 209,	"User authentication failed" },
	{ 210,	"Context not found" },
	{ 211,	"All dynamic PDP addresses are occupied" },
	{ 212,	"No memory is available" },
	{ 213,	"Relocation failure" },
	{ 214,	"Unknown mandatory extension header" },
	{ 215,	"Semantic error in the TFT operation" },
	{ 216,	"Syntactic error in the TFT operation" },
	{ 217,	"Semantic errors in packet filter(s)" },
	{ 218,	"Syntactic errors in packet filter(s)" },
	{ 219,	"Missing or unknown APN" },
	{ 220,	"Unknown PDP address or PDP type" },
	{ 221,	"PDP context without TFT already activated" },
	{ 222,	"APN access denied - no subscription" },
	{ 223,	"APN restriction type incompatibility with currently "
		"active PDP contexts" },
	{ 224,	"MS MBMS capabilities insufficient" },
	{ 225,	"Invalid correlation-ID" },
	{ 226,	"MBMS bearer context superseded" },

	/* GTP'v1. */
	{ 59,	"System failure" },
	{ 60,	"The transmit buffers are becoming full" },
	{ 61,	"The receive buffers are becoming full" },
	{ 62,	"Another node is about to go down" },
	{ 63,	"This node is about to go down" },
	{ 177,	"CDR decoding error" },
	{ 252,	"Request related to possibly duplicated packets already "
		"fulfilled" },
	{ 253,	"Request already fulfilled" },
	{ 254,	"Sequence numbers of released/cancelled packets IE incorrect" },
	{ 255,	"Request not fulfilled" },

	{ 0,	NULL }
};

static int gtp_proto = -1;

void
gtp_print(const u_char *cp, u_int length, u_short sport, u_short dport)
{
	int version;

	/* Decode GTP version. */
	TCHECK(cp[0]);
	version = cp[0] >> GTP_VERSION_SHIFT;

	if (version == GTP_VERSION_0)
		gtp_v0_print(cp, length, sport, dport);
	else if (version == GTP_VERSION_1)
		gtp_v1_print(cp, length, sport, dport);
	else
		printf("GTP (version %i)", version);

	return;

trunc:
	printf("[|GTP]");
}

/*
 * Decode and print information elements from message. The actual work is
 * handled in the appropriate Tag/Value (TV) or Tag/Length/Value (TLV)
 * decoding routine.
 */
void
gtp_decode_ie(const u_char *cp, u_short version, int len)
{
	int val, ielen, iecount = 0;

	if (len <= 0)
		return;

	printf(" {");

	while (len > 0) {

		iecount++;
		if (iecount > 1)
			printf(" ");

		TCHECK(cp[0]);
		val = (u_int)cp[0];
		cp++;

		printf("[");

		switch (version) {
		case GTP_VERSION_0:
			if ((val & GTPV0_IE_TYPE_MASK) == 0)
				ielen = gtp_v0_print_tv(cp, val);
			else
				ielen = gtp_v0_print_tlv(cp, val);
			break;

		case GTP_VERSION_1:
			if ((val & GTPV1_IE_TYPE_MASK) == 0)
				ielen = gtp_v1_print_tv(cp, val);
			else
				ielen = gtp_v1_print_tlv(cp, val);
			break;

		default:
			/* Version not supported... */
			ielen = -1;
			break;
		}

		printf("]");

		if (ielen < 0)
			goto trunc;

		len -= ielen;
		cp += ielen - 1;
	}

	if (iecount > 0)
		printf("}");

	return;

trunc:
	printf(" [|%s]", tok2str(gtp_type, "GTP", gtp_proto));
}

/*
 * Decode and print telephony binary coded decimal.
 */
void
gtp_print_tbcd(const u_char *cp, u_int len)
{
	u_int8_t *data, bcd;
	int i;

	data = (u_int8_t *)cp;
	for (i = 0; i < len; i++) {
		bcd = *data & 0xf;
		if (bcd != 0xf)
			printf("%u", bcd);
		bcd = *data >> 4;
		if (bcd != 0xf)
			printf("%u", bcd);
		data++;
	}
}

/*
 * Decode and print an end user address. Format is detailed in
 * GSM 09.60 section 7.9.18 and 3GPP 29.060 section 7.7.27.
 */
void
gtp_print_user_address(const u_char *cp, u_int len)
{
	u_int8_t org, type;

	if (len < 2)
		return;

	org = (u_int8_t)cp[0] & 0xf;
	type = (u_int8_t)cp[1];

	cp += 2;

	if (org == 0x0 && type == 0x1)
		printf(": PPP");
	else if (org == 0x1 && type == 0x21) {
		if (len == 6)
			printf(": %s", ipaddr_string(cp));
		else
			printf(": IPv4");
	} else if (org == 0x1 && type == 0x57) {
		if (len == 18)
			printf(": %s", ip6addr_string(cp));
		else
			printf(": IPv6");
	} else
		printf(" (org 0x%x, type 0x%x)", org, type);
}

/*
 * Decode and print an Access Point Name. Format is detailed in
 * 3GPP 24.008 section 10.5.6.1 and 3GPP 23.003 section 9.1.
 */
void
gtp_print_apn(const u_char *cp, u_int len)
{
	u_char label[100];
	u_int8_t llen;

	if (len < 1 || len > 100)
		return;

	while (len > 0) {

		llen = (u_int8_t)cp[0];
		if (llen > 99)
			return;

		bcopy(cp + 1, label, llen);
		label[llen] = '\0';
		printf("%s", label);

		cp += llen + 1;
		len -= llen + 1;

		if (len > 0)
			printf(".");

	}	
}

/* Print string from array. */
void
gtp_print_str(const char **strs, u_int bound, u_int index)
{
	if (index >= bound)
		printf(": %u", index);
	else if (strs[index] != NULL)
		printf(": %s", strs[index]);
}

/*
 * Decoding routines for GTP version 0.
 */
void
gtp_v0_print(const u_char *cp, u_int length, u_short sport, u_short dport)
{
	struct gtp_v0_hdr *gh = (struct gtp_v0_hdr *)cp;
	int len, version;
	u_int64_t tid;

	gtp_proto = GTP_V0_PROTO;

	/* Check if this is GTP prime. */
	TCHECK(gh->flags);
	if ((gh->flags & GTPV0_HDR_PROTO_TYPE) == 0) {
		gtp_proto = GTP_V0_PRIME_PROTO;
		gtp_v0_print_prime(cp);
		return;
	}

	/* Print GTP header. */
	TCHECK(*gh);
	cp += sizeof(struct gtp_v0_hdr);
	len = ntohs(gh->length);
	bcopy(&gh->tid, &tid, sizeof(tid));
	printf("GTPv0 (len %u, seqno %u, flow %u, N-PDU %u, tid 0x%llx) ",
	    ntohs(gh->length), ntohs(gh->seqno), ntohs(gh->flow),
	    ntohs(gh->npduno), betoh64(tid));

	/* Decode GTP message. */
	printf("%s", tok2str(gtp_v0_msgtype, "Message Type %u", gh->msgtype));

	if (!vflag)
		return;

	if (gh->msgtype == GTPV0_T_PDU) {

		TCHECK(cp[0]);
		version = cp[0] >> 4;

		printf(" { ");

		if (version == 4)
			ip_print(cp, len);
		else if (version == 6)
			ip6_print(cp, len);
		else
			printf("Unknown IP version %u", version);

		printf(" }");
	} else 
		gtp_decode_ie(cp, GTP_VERSION_0, len);

	return;

trunc:
	printf(" [|%s]", tok2str(gtp_type, "GTP", gtp_proto));
}

void
gtp_v0_print_prime(const u_char *cp)
{
	struct gtp_v0_prime_hdr *gph = (struct gtp_v0_prime_hdr *)cp;
	int len;
	
	/* Decode GTP prime header. */
	TCHECK(*gph);
	cp += sizeof(*gph);

	len = ntohs(gph->length);
	printf("GTPv0' (len %u, seq %u) ", len, ntohs(gph->seqno));

	/* Decode GTP message. */
	printf("%s", tok2str(gtp_v0_msgtype, "Message Type %u", gph->msgtype));

	if (vflag)
		gtp_decode_ie(cp, GTP_VERSION_0, len);

	return;

trunc:
	printf(" [|%s]", tok2str(gtp_type, "GTP", gtp_proto));
}

int
gtp_v0_print_tv(const u_char *cp, u_int value)
{
	u_int32_t *dpl;
	u_int16_t *dps;
	u_int8_t data;
	int ielen = -1;

	switch (value) {
	case GTPV0_TV_CAUSE:

		/* 09.60 7.9.1 - Cause. */
		TCHECK(cp[0]);
		data = (u_int8_t)cp[0];
		ielen = GTPV0_TV_CAUSE_LENGTH;
		printf("Cause: %s", tok2str(gtp_v0_cause, "#%u", data));
		break;

	case GTPV0_TV_IMSI:

		/* 09.60 7.9.2 - International Mobile Subscriber Identity. */
		TCHECK2(cp[0], GTPV0_TV_IMSI_LENGTH - 1);
		printf("IMSI ");
		gtp_print_tbcd(cp, GTPV0_TV_IMSI_LENGTH - 1);
		ielen = GTPV0_TV_IMSI_LENGTH;
		break;

	case GTPV0_TV_RAI:

		/* 09.60 7.9.3 - Routing Area Identity (RAI). */
		TCHECK2(cp[0], GTPV0_TV_RAI_LENGTH - 1);
		printf("RAI: MCC ");
		data = cp[1] | 0xf0;
		gtp_print_tbcd(cp, 1);
		gtp_print_tbcd(&data, 1);
		printf(", MNC ");
		data = (cp[1] >> 4) | 0xf0;
		gtp_print_tbcd(cp + 2, 1);
		gtp_print_tbcd(&data, 1);
		printf(", LAC 0x%x%x", cp[3], cp[4]);
		printf(", RAC 0x%x", cp[5]);
		ielen = GTPV0_TV_RAI_LENGTH;
		break;

	case GTPV0_TV_TLLI:

		/* 09.60 7.9.4 - Temporary Logical Link Identity (TLLI). */
		TCHECK2(cp[0], GTPV0_TV_TLLI_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("TLLI 0x%x", ntohl(*dpl));
		ielen = GTPV0_TV_TLLI_LENGTH;
		break;

	case GTPV0_TV_PTMSI:

		/* 09.60 7.9.5 - Packet TMSI (P-TMSI). */
		TCHECK2(cp[0], GTPV0_TV_PTMSI_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("P-TMSI 0x%x", ntohl(*dpl));
		ielen = GTPV0_TV_PTMSI_LENGTH;
		break;

	case GTPV0_TV_QOS:

		/* 09.60 7.9.6 - Quality of Service (QoS) Profile. */
		TCHECK2(cp[0], GTPV0_TV_QOS_LENGTH - 1);
		printf("QoS Profile");				/* XXX */
		ielen = GTPV0_TV_QOS_LENGTH;
		break;

	case GTPV0_TV_REORDER:

		/* 09.60 7.9.7 - Reordering Required. */
		TCHECK2(cp[0], GTPV0_TV_REORDER_LENGTH - 1);
		printf("Reordering Required: ");
		if (cp[0] & 0x1)
			printf("yes");
		else
			printf("no");
		ielen = GTPV0_TV_REORDER_LENGTH;
		break;

	case GTPV0_TV_AUTH_TRIPLET:

		/* 09.60 7.9.8 - Authentication Triplet. */
		TCHECK2(cp[0], GTPV0_TV_AUTH_TRIPLET_LENGTH - 1);
		printf("Authentication");			/* XXX */
		ielen = GTPV0_TV_AUTH_TRIPLET_LENGTH;
		break;

	case GTPV0_TV_MAP_CAUSE:

		/* 09.60 7.9.9 - MAP Cause. */
		TCHECK2(cp[0], GTPV0_TV_MAP_CAUSE_LENGTH - 1);
		printf("MAP Cause: %u", cp[0]);
		ielen = GTPV0_TV_MAP_CAUSE_LENGTH;
		break;

	case GTPV0_TV_PTMSI_SIGNATURE:

		/* 09.60 7.9.10 - P-TMSI Signature. */
		/* Signature defined in GSM 04.08. */
		TCHECK2(cp[0], GTPV0_TV_PTMSI_SIGNATURE_LENGTH - 1);
		printf("PTMSI Signature: 0x%x%x%x", cp[0], cp[1], cp[2]);
		ielen = GTPV0_TV_PTMSI_SIGNATURE_LENGTH;
		break;

	case GTPV0_TV_MS_VALIDATED:

		/* 09.60 7.9.11 - MS Validated. */
		TCHECK2(cp[0], GTPV0_TV_MS_VALIDATED_LENGTH - 1);
		printf("MS Validated");
		if (cp[0] & 0x1)
			printf("yes");
		else
			printf("no");
		ielen = GTPV0_TV_MS_VALIDATED_LENGTH;
		break;

	case GTPV0_TV_RECOVERY:

		/* 09.60 7.9.12 - Recovery. */
		TCHECK2(cp[0], GTPV0_TV_RECOVERY_LENGTH - 1);
		printf("Recovery: Restart counter %u", cp[0]);
		ielen = GTPV0_TV_RECOVERY_LENGTH;
		break;

	case GTPV0_TV_SELECTION_MODE:

		/* 09.60 7.9.13 - Selection Mode. */
		TCHECK2(cp[0], GTPV0_TV_SELECTION_MODE_LENGTH - 1);
		printf("Selection Mode");			/* XXX */
		ielen = GTPV0_TV_SELECTION_MODE_LENGTH;
		break;

	case GTPV0_TV_FLOW_LABEL_DATA_I:

		/* 09.60 7.9.14 - Flow Label Data I. */
		TCHECK2(cp[0], GTPV0_TV_FLOW_LABEL_DATA_I_LENGTH - 1);
		dps = (u_int16_t *)cp;
		printf("Flow Label Data I: %u", ntohs(*dps));
		ielen = GTPV0_TV_FLOW_LABEL_DATA_I_LENGTH;
		break;

	case GTPV0_TV_FLOW_LABEL_SIGNALLING:

		/* 09.60 7.9.15 - Flow Label Signalling. */
		TCHECK2(cp[0], GTPV0_TV_FLOW_LABEL_SIGNALLING_LENGTH - 1);
		dps = (u_int16_t *)cp;
		printf("Flow Label Signalling: %u", ntohs(*dps));
		ielen = GTPV0_TV_FLOW_LABEL_SIGNALLING_LENGTH;
		break;

	case GTPV0_TV_FLOW_LABEL_DATA_II:

		/* 09.60 7.9.16 - Flow Label Data II. */
		TCHECK2(cp[0], GTPV0_TV_FLOW_LABEL_DATA_II_LENGTH - 1);
		data = cp[0] & 0xf;
		dps = (u_int16_t *)(cp + 1);
		printf("Flow Label Data II: %u, NSAPI %u", ntohs(*dps), data);
		ielen = GTPV0_TV_FLOW_LABEL_DATA_II_LENGTH;
		break;

	case GTPV0_TV_PACKET_XFER_CMD:

		/* 12.15 7.3.4.5.3 - Packet Transfer Command. */
		TCHECK2(cp[0], GTPV0_TV_PACKET_XFER_CMD_LENGTH - 1);
		printf("Packet Transfer Command");
		gtp_print_str(gtp_packet_xfer_cmd, nitems(gtp_packet_xfer_cmd),
		    cp[0]);
		ielen = GTPV0_TV_PACKET_XFER_CMD_LENGTH;
		break;
		
	case GTPV0_TV_CHARGING_ID:

		/* 09.60 7.9.17 - Charging ID. */
		TCHECK2(cp[0], GTPV0_TV_CHARGING_ID_LENGTH - 1);
		dps = (u_int16_t *)cp;
		printf("Charging ID: %u", ntohs(*dps));
		ielen = GTPV0_TV_CHARGING_ID_LENGTH;
		break;

	default:
		printf("TV %u", value);
	}

trunc:
	return ielen;
}

int
gtp_v0_print_tlv(const u_char *cp, u_int value)
{
	u_int8_t data;
	u_int16_t *lenp, *seqno, len;
	int ielen = -1;

	/* Get length of IE. */
	TCHECK2(cp[0], 2);
	lenp = (u_int16_t *)cp;
	cp += 2;
	len = ntohs(*lenp);
	TCHECK2(cp[0], len);
	ielen = sizeof(data) + sizeof(len) + len;

	switch (value) {

	case GTPV0_TLV_END_USER_ADDRESS:

		/* 09.60 7.9.18 - End User Address. */
		printf("End User Address");
		gtp_print_user_address(cp, len);
		break;

	case GTPV0_TLV_MM_CONTEXT:

		/* 09.60 7.9.19 - MM Context. */
		printf("MM Context");				/* XXX */
		break;

	case GTPV0_TLV_PDP_CONTEXT:

		/* 09.60 7.9.20 - PDP Context. */
		printf("PDP Context");				/* XXX */
		break;

	case GTPV0_TLV_ACCESS_POINT_NAME:

		/* 09.60 7.9.21 - Access Point Name. */
		printf("AP Name: ");
		gtp_print_apn(cp, len);
		break;

	case GTPV0_TLV_PROTOCOL_CONFIG_OPTIONS:

		/* 09.60 7.9.22 - Protocol Configuration Options. */
		printf("Protocol Configuration Options");	/* XXX */
		break;

	case GTPV0_TLV_GSN_ADDRESS:

		/* 09.60 7.9.23 - GSN Address. */
		printf("GSN Address");
		if (len == 4)
			printf(": %s", ipaddr_string(cp));
		else if (len == 16)
			printf(": %s", ip6addr_string(cp));
		break;

	case GTPV0_TLV_MS_ISDN:

		/* 09.60 7.9.24 - MS International PSTN/ISDN Number. */
		printf("MSISDN ");
		data = (u_int8_t)cp[0];		/* XXX - Number type. */
		gtp_print_tbcd(cp + 1, len - 1);
		break;

	case GTPV0_TLV_CHARGING_GATEWAY_ADDRESS:

		/* 09.60 7.9.25 - Charging Gateway Address. */
		printf("Charging Gateway");
		if (len == 4)
			printf(": %s", ipaddr_string(cp));
		break;

	case GTPV0_TLV_DATA_RECORD_PACKET:

		/* 12.15 7.3.4.5.4 - Data Record Packet. */
		printf("Data Record: Records %u, Format %u, Format Version %u",
		    cp[0], cp[1], ntohs(*(u_int16_t *)(cp + 2)));
		break;

	case GTPV0_TLV_REQUESTS_RESPONDED:

		/* 12.15 7.3.4.6 - Requests Responded. */
		printf("Requests Responded:");
		seqno = (u_int16_t *)cp;
		while (len > 0) {
			printf(" %u", ntohs(*seqno));
			seqno++;
			len -= sizeof(*seqno);
		}
		break;

	case GTPV0_TLV_RECOMMENDED_NODE:

		/* 12.15 7.3.4.3 - Address of Recommended Node. */
		printf("Recommended Node");
		if (len == 4)
			printf(": %s", ipaddr_string(cp));
		else if (len == 16)
			printf(": %s", ip6addr_string(cp));
		break;

	case GTPV0_TLV_PRIVATE_EXTENSION:

		printf("Private Extension");
		break;

	default:
		printf("TLV %u (len %u)", value, len);
	}

	return ielen;

trunc:
	return -1;
}

/*
 * Decoding for GTP version 1, which consists of GTPv1-C, GTPv1-U and GTPv1'.
 */
void
gtp_v1_print(const u_char *cp, u_int length, u_short sport, u_short dport)
{
	struct gtp_v1_hdr *gh = (struct gtp_v1_hdr *)cp;
	struct gtp_v1_hdr_ext *ghe = NULL;
	int nexthdr, hlen;
	u_char *p = (u_char *)cp;

	TCHECK(gh->flags);
	if ((gh->flags & GTPV1_HDR_PROTO_TYPE) == 0) {
		gtp_proto = GTP_V1_PRIME_PROTO;
		printf(" GTPv1'");
		gtp_v1_print_prime(p, (struct gtp_v1_prime_hdr *)gh);
		return;
	}

	if (dport == GTPV1_C_PORT || sport == GTPV1_C_PORT) {
		gtp_proto = GTP_V1_CTRL_PROTO;
		printf(" GTPv1-C");
	} else if (dport == GTPV1_U_PORT || sport == GTPV1_U_PORT) {
		gtp_proto = GTP_V1_USER_PROTO;
		printf(" GTPv1-U");
	} else if (dport == GTPV1_PRIME_PORT || sport == GTPV1_PRIME_PORT) {
		gtp_proto = GTP_V1_PRIME_PROTO;
		printf(" GTPv1'");
	}

	/* Decode GTP header. */
	TCHECK(*gh);
	p += sizeof(struct gtp_v1_hdr);

	printf(" (teid %u, len %u)", ntohl(gh->teid), ntohs(gh->length));

	if (gh->flags & GTPV1_HDR_EXT) {
		ghe = (struct gtp_v1_hdr_ext *)cp;
		TCHECK(*ghe);
		p += sizeof(struct gtp_v1_hdr_ext) - sizeof(struct gtp_v1_hdr);
	}

	if (gh->flags & GTPV1_HDR_SN_FLAG)
		printf(" [seq %u]", ntohs(ghe->seqno));

	if (gh->flags & GTPV1_HDR_NPDU_FLAG)
		printf(" [N-PDU %u]", ghe->npduno);

	if (gh->flags & GTPV1_HDR_EH_FLAG) {

		/* Process next header... */
		nexthdr = ghe->nexthdr;
		while (nexthdr != GTPV1_EH_NONE) {

			/* Header length is a 4 octet multiplier. */
			hlen = (int)p[0] * 4;
			if (hlen == 0) {
				printf(" [Invalid zero-length header %u]",
				    nexthdr);
				goto trunc;
			}
			TCHECK2(p[0], hlen);

			switch (nexthdr) {
			case GTPV1_EH_MBMS_SUPPORT:
				printf(" [MBMS Support]");
				break;

			case GTPV1_EH_MSI_CHANGE_RPT:
				printf(" [MS Info Change Reporting]");
				break;

			case GTPV1_EH_PDCP_PDU_NO:
				printf(" [PDCP PDU %u]",
				    ntohs(*(u_int16_t *)(p + 1)));
				break;

			case GTPV1_EH_SUSPEND_REQUEST:
				printf(" [Suspend Request]");
				break;

			case GTPV1_EH_SUSPEND_RESPONSE:
				printf(" [Suspend Response]");
				break;

			default:
				printf(" [Unknown Header %u]", nexthdr);
			}

			p += hlen - 1;	
			nexthdr = (int)p[0];
			p++;
		}

	}

	hlen = p - cp;

	if (dport == GTPV1_C_PORT || sport == GTPV1_C_PORT)
		gtp_v1_print_ctrl(p, hlen, gh);
	else if (dport == GTPV1_U_PORT || sport == GTPV1_U_PORT)
		gtp_v1_print_user(p, hlen, gh);

	return;

trunc:
	printf(" [|%s]", tok2str(gtp_type, "GTP", gtp_proto));
}

void
gtp_v1_print_ctrl(const u_char *cp, u_int hlen, struct gtp_v1_hdr *gh)
{
	int len;

	/* Decode GTP control message. */
	printf(" %s", tok2str(gtp_v1_msgtype, "Message Type %u", gh->msgtype));

	len = ntohs(gh->length) - hlen + sizeof(*gh);
	if (vflag)
		gtp_decode_ie(cp, GTP_VERSION_1, len);
}

void
gtp_v1_print_user(const u_char *cp, u_int hlen, struct gtp_v1_hdr *gh)
{
	int len, version;

	/* Decode GTP user message. */
	printf(" %s", tok2str(gtp_v1_msgtype, "Message Type %u", gh->msgtype));

	if (!vflag)
		return;

	len = ntohs(gh->length) - hlen + sizeof(*gh);

	if (gh->msgtype == GTPV1_G_PDU) {

		TCHECK(cp[0]);
		version = cp[0] >> 4;

		printf(" { ");

		if (version == 4)
			ip_print(cp, len);
		else if (version == 6)
			ip6_print(cp, len);
		else
			printf("Unknown IP version %u", version);

		printf(" }");

	} else
		gtp_decode_ie(cp, GTP_VERSION_1, len);

	return;

trunc:
	printf(" [|%s]", tok2str(gtp_type, "GTP", gtp_proto));
}

void
gtp_v1_print_prime(const u_char *cp, struct gtp_v1_prime_hdr *gph)
{
	int len;
	
	/* Decode GTP prime header. */
	TCHECK(*gph);
	cp += sizeof(struct gtp_v1_prime_hdr);

	len = ntohs(gph->length);
	printf(" (len %u, seq %u) ", len, ntohs(gph->seqno));

	/* Decode GTP message. */
	printf("%s", tok2str(gtp_v1_msgtype, "Message Type %u", gph->msgtype));

	if (vflag)
		gtp_decode_ie(cp, GTP_VERSION_1, len);

	return;

trunc:
	printf(" [|%s]", tok2str(gtp_type, "GTP", gtp_proto));
}

int
gtp_v1_print_tv(const u_char *cp, u_int value)
{
	u_int32_t *dpl;
	u_int16_t *dps;
	u_int8_t data;
	int ielen = -1;

	switch (value) {
	case GTPV1_TV_CAUSE:

		/* 29.060 - 7.7.1 Cause. */
		TCHECK(cp[0]);
		data = (u_int8_t)cp[0];
		ielen = GTPV1_TV_CAUSE_LENGTH;
		printf("Cause: %s", tok2str(gtp_v1_cause, "#%u", data));
		break;

	case GTPV1_TV_IMSI:

		/* 29.060 7.7.2 - International Mobile Subscriber Identity. */
		TCHECK2(cp[0], GTPV1_TV_IMSI_LENGTH - 1);
		printf("IMSI ");
		gtp_print_tbcd(cp, GTPV1_TV_IMSI_LENGTH - 1);
		ielen = GTPV1_TV_IMSI_LENGTH;
		break;

	case GTPV1_TV_RAI:

		/* 29.060 7.7.3 - Routing Area Identity (RAI). */
		TCHECK2(cp[0], GTPV1_TV_RAI_LENGTH - 1);
		printf("RAI: MCC ");
		data = cp[1] | 0xf0;
		gtp_print_tbcd(cp, 1);
		gtp_print_tbcd(&data, 1);
		printf(", MNC ");
		data = (cp[1] >> 4) | 0xf0;
		gtp_print_tbcd(cp + 2, 1);
		gtp_print_tbcd(&data, 1);
		printf(", LAC 0x%x%x", cp[3], cp[4]);
		printf(", RAC 0x%x", cp[5]);
		ielen = GTPV1_TV_RAI_LENGTH;
		break;

	case GTPV1_TV_TLLI:

		/* 29.060 7.7.4 - Temporary Logical Link Identity (TLLI). */
		TCHECK2(cp[0], GTPV1_TV_TLLI_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("TLLI 0x%x", ntohl(*dpl));
		ielen = GTPV1_TV_TLLI_LENGTH;
		break;

	case GTPV1_TV_PTMSI:

		/* 29.060 7.7.5 - Packet TMSI (P-TMSI). */
		TCHECK2(cp[0], GTPV1_TV_PTMSI_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("P-TMSI 0x%x", ntohl(*dpl));
		ielen = GTPV1_TV_PTMSI_LENGTH;
		break;

	case GTPV1_TV_REORDER:

		/* 29.060 7.7.6 - Reordering Required. */
		TCHECK2(cp[0], GTPV1_TV_REORDER_LENGTH - 1);
		printf("Reordering Required: ");
		if (cp[0] & 0x1)
			printf("yes");
		else
			printf("no");
		ielen = GTPV1_TV_REORDER_LENGTH;
		break;

	case GTPV1_TV_AUTH:

		/* 29.060 7.7.7 - Authentication Triplet. */
		TCHECK2(cp[0], GTPV1_TV_AUTH_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("Auth: RAND 0x%x%x%x%x, SRES 0x%x, Kc 0x%x%x",
		    ntohl(dpl[0]), ntohl(dpl[1]), ntohl(dpl[2]), ntohl(dpl[3]),
		    ntohl(dpl[4]), ntohl(dpl[5]), ntohl(dpl[6]));
		ielen = GTPV1_TV_AUTH_LENGTH;
		break;

	case GTPV1_TV_MAP_CAUSE:

		/* 29.060 7.7.8 - MAP Cause. */
		/* Cause defined in 3GPP TS 29.002. */
		TCHECK2(cp[0], GTPV1_TV_MAP_CAUSE_LENGTH - 1);
		printf("Map Cause: %u", cp[0]);
		ielen = GTPV1_TV_MAP_CAUSE_LENGTH;
		break;

	case GTPV1_TV_PTMSI_SIGNATURE:

		/* 29.060 7.7.9 - P-TMSI Signature. */
		/* Signature defined in 3GPP TS 24.008. */
		TCHECK2(cp[0], GTPV1_TV_PTMSI_SIGNATURE_LENGTH - 1);
		printf("PTMSI Signature: 0x%x%x%x", cp[0], cp[1], cp[2]);
		ielen = GTPV1_TV_PTMSI_SIGNATURE_LENGTH;
		break;

	case GTPV1_TV_MS_VALIDATED:

		/* 29.060 7.7.10 - MS Validated. */
		TCHECK2(cp[0], GTPV1_TV_MS_VALIDATED_LENGTH - 1);
		printf("MS Validated: ");
		if (cp[0] & 0x1)
			printf("yes");
		else
			printf("no");
		ielen = GTPV1_TV_MS_VALIDATED_LENGTH;
		break;

	case GTPV1_TV_RECOVERY:

		/* 29.060 7.7.11 - Recovery. */
		TCHECK2(cp[0], GTPV1_TV_RECOVERY_LENGTH - 1);
		printf("Recovery: Restart counter %u", cp[0]);
		ielen = GTPV1_TV_RECOVERY_LENGTH;
		break;

	case GTPV1_TV_SELECTION_MODE:

		/* 29.060 7.7.12 - Selection Mode. */
		TCHECK2(cp[0], GTPV1_TV_SELECTION_MODE_LENGTH - 1);
		data = (u_int8_t)cp[0];
		printf("Selection Mode: %u", data & 0x2);
		ielen = GTPV1_TV_SELECTION_MODE_LENGTH;
		break;

	case GTPV1_TV_TEID_DATA_I:

		/* 29.060 7.7.13 - Tunnel Endpoint Identifier Data I. */
		TCHECK2(cp[0], GTPV1_TV_TEID_DATA_I_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("TEI Data I: %u", ntohl(*dpl));
		ielen = GTPV1_TV_TEID_DATA_I_LENGTH;
		break;

	case GTPV1_TV_TEID_CTRL:

		/* 29.060 7.7.14 - Tunnel Endpoint Identifier Control Plane. */
		TCHECK2(cp[0], GTPV1_TV_TEID_CTRL_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("TEI Control Plane: %u", ntohl(*dpl));
		ielen = GTPV1_TV_TEID_CTRL_LENGTH;
		break;

	case GTPV1_TV_TEID_DATA_II:

		/* 29.060 7.7.15 - Tunnel Endpoint Identifier Data II. */
		TCHECK2(cp[0], GTPV1_TV_TEID_DATA_II_LENGTH - 1);
		data = cp[0] & 0xf;
		dpl = (u_int32_t *)(cp + 1);
		printf("TEI Data II: %u, NSAPI %u", ntohl(*dpl), data);
		ielen = GTPV1_TV_TEID_DATA_II_LENGTH;
		break;

	case GTPV1_TV_TEARDOWN:

		/* 29.060 7.7.16 - Teardown Indicator. */
		TCHECK2(cp[0], GTPV1_TV_TEARDOWN_LENGTH - 1);
		printf("Teardown: ");
		if (cp[0] & 0x1)
			printf("yes");
		else
			printf("no");
		ielen = GTPV1_TV_TEARDOWN_LENGTH;
		break;

	case GTPV1_TV_NSAPI:

		/* 29.060 7.7.17 - NSAPI. */
		TCHECK2(cp[0], GTPV1_TV_NSAPI_LENGTH - 1);
		data = (u_int8_t)cp[0];
		printf("NSAPI %u", data & 0xf);
		ielen = GTPV1_TV_NSAPI_LENGTH;
		break;

	case GTPV1_TV_RANAP:

		/* 29.060 7.7.18 - RANAP Cause. */
		TCHECK2(cp[0], GTPV1_TV_RANAP_LENGTH - 1);
		printf("RANAP Cause: %u", cp[0]);
		ielen = GTPV1_TV_RANAP_LENGTH;
		break;

	case GTPV1_TV_RAB_CONTEXT:

		/* 29.060 7.7.19 - RAB Context. */
		TCHECK2(cp[0], GTPV1_TV_RAB_CONTEXT_LENGTH - 1);
		data = cp[0] & 0xf;
		dps = (u_int16_t *)(cp + 1);
		printf("RAB Context: NSAPI %u, DL GTP-U Seq No %u,"
		    "UL GTP-U Seq No %u, DL PDCP Seq No %u, UL PDCP Seq No %u",
		    data, ntohs(dps[0]), ntohs(dps[1]), ntohs(dps[2]),
		    ntohs(dps[3]));
		ielen = GTPV1_TV_RAB_CONTEXT_LENGTH;
		break;

	case GTPV1_TV_RADIO_PRIORITY_SMS:

		/* 29.060 7.7.20 - Radio Priority SMS. */
		TCHECK2(cp[0], GTPV1_TV_RADIO_PRI_SMS_LENGTH - 1);
		printf("Radio Priority SMS: %u", cp[0] & 0x7);
		ielen = GTPV1_TV_RADIO_PRI_SMS_LENGTH;
		break;

	case GTPV1_TV_RADIO_PRIORITY:

		/* 29.060 7.7.21 - Radio Priority. */
		TCHECK2(cp[0], GTPV1_TV_RADIO_PRI_LENGTH - 1);
		data = cp[0] >> 4;
		printf("Radio Priority: %u, NSAPI %u", cp[0] & 0x7, data);
		ielen = GTPV1_TV_RADIO_PRI_LENGTH;
		break;

	case GTPV1_TV_PACKET_FLOW_ID:

		/* 29.060 7.7.22 - Packet Flow ID. */
		TCHECK2(cp[0], GTPV1_TV_PACKET_FLOW_ID_LENGTH - 1);
		printf("Packet Flow ID: %u, NSAPI %u", cp[1], cp[0] & 0xf);
		ielen = GTPV1_TV_PACKET_FLOW_ID_LENGTH;
		break;

	case GTPV1_TV_CHARGING:

		/* 29.060 7.7.23 - Charging Characteristics. */
		/* Charging defined in 3GPP TS 32.298. */
		TCHECK2(cp[0], GTPV1_TV_CHARGING_LENGTH - 1);
		printf("Charging Characteristics");		/* XXX */
		ielen = GTPV1_TV_CHARGING_LENGTH;
		break;

	case GTPV1_TV_TRACE_REFERENCE:

		/* 29.060 7.7.24 - Trace Reference. */
		TCHECK2(cp[0], GTPV1_TV_TRACE_REFERENCE_LENGTH - 1);
		dps = (u_int16_t *)cp;
		printf("Trace Reference: %u", ntohs(*dps));
		ielen = GTPV1_TV_TRACE_REFERENCE_LENGTH;
		break;

	case GTPV1_TV_TRACE_TYPE:

		/* 29.060 7.7.25 - Trace Type. */
		/* Trace type defined in GSM 12.08. */
		TCHECK2(cp[0], GTPV1_TV_TRACE_TYPE_LENGTH - 1);
		dps = (u_int16_t *)cp;
		printf("Trace Type: %u", ntohs(*dps));
		ielen = GTPV1_TV_TRACE_TYPE_LENGTH;
		break;

	case GTPV1_TV_MSNRR:

		/* 29.060 7.7.26 - MS Not Reachable Reason. */
		/* Reason defined in 3GPP TS 23.040. */
		TCHECK2(cp[0], GTPV1_TV_MSNRR_LENGTH - 1);
		printf("MS NRR: %u", cp[0]);
		ielen = GTPV1_TV_MSNRR_LENGTH;
		break;

	case GTPV1_TV_PACKET_XFER_CMD:

		/* 32.295 6.2.4.5.2 - Packet Transfer Command. */
		TCHECK2(cp[0], GTPV1_TV_PACKET_XFER_CMD_LENGTH - 1);
		printf("Packet Transfer Command");
		gtp_print_str(gtp_packet_xfer_cmd, nitems(gtp_packet_xfer_cmd),
		    cp[0]);
		ielen = GTPV1_TV_PACKET_XFER_CMD_LENGTH;
		break;

	case GTPV1_TV_CHARGING_ID:

		/* 29.060 7.7.26 - Charging ID. */
		TCHECK2(cp[0], GTPV1_TV_CHARGING_ID_LENGTH - 1);
		dpl = (u_int32_t *)cp;
		printf("Charging ID: %u", ntohl(*dpl));
		ielen = GTPV1_TV_CHARGING_ID_LENGTH;
		break;

	default:
		printf("TV %u", value);
	}

trunc:
	return ielen;
}

int
gtp_v1_print_tlv(const u_char *cp, u_int value)
{
	u_int8_t data;
	u_int16_t *lenp, *seqno, len;
	int ielen = -1;

	/* Get length of IE. */
	TCHECK2(cp[0], 2);
	lenp = (u_int16_t *)cp;
	cp += 2;
	len = ntohs(*lenp);
	TCHECK2(cp[0], len);
	ielen = sizeof(data) + sizeof(len) + len;

	switch (value) {
	case GTPV1_TLV_END_USER_ADDRESS:

		/* 3GPP 29.060 - 7.7.27 End User Address. */
		printf("End User Address");
		gtp_print_user_address(cp, len);
		break;

	case GTPV1_TLV_MM_CONTEXT:

		/* 29.060 7.7.28 - MM Context. */
		printf("MM Context");				/* XXX */
		break;

	case GTPV1_TLV_PDP_CONTEXT:

		/* 29.260 7.7.29 - PDP Context. */
		printf("PDP Context");				/* XXX */
		break;

	case GTPV1_TLV_ACCESS_POINT_NAME:

		/* 29.060 7.7.30 - Access Point Name. */
		printf("AP Name: ");
		gtp_print_apn(cp, len);
		break;

	case GTPV1_TLV_PROTOCOL_CONFIG_OPTIONS:

		/* 29.060 7.7.31 - Protocol Configuration Options. */
		/* Defined in 3GPP TS 24.008. */
		printf("Config Options");			/* XXX */
		break;

	case GTPV1_TLV_GSN_ADDRESS:

		/* 29.060 7.7.32 - GSN Address. */
		/* Defined in 3GPP TS 23.003. */
		printf("GSN Address");
		if (len == 4)
			printf(": %s", ipaddr_string(cp));
		else if (len == 16)
			printf(": %s", ip6addr_string(cp));
		break;

	case GTPV1_TLV_MSISDN:

		/* 29.060 7.7.33 - MS International PSTN/ISDN Number. */
		printf("MSISDN ");
		data = (u_int8_t)cp[0];		/* XXX - Number type. */
		gtp_print_tbcd(cp + 1, len - 1);
		break;

	case GTPV1_TLV_QOS_PROFILE:

		/* 29.060 7.7.34 - QoS Profile. */
		/* QoS profile defined in 3GPP TS 24.008 10.5.6.5. */
		printf("QoS Profile: ");
		data = (u_int8_t)cp[0];
		printf("Delay Class %u, ", (data >> 3) & 0x7);
		printf("Reliability Class %u", data & 0x7);
		if (vflag > 1) {
			printf(", ");
			data = (u_int8_t)cp[1];
			printf("Precedence Class %u", data & 0x7);
			/* XXX - Decode more QoS fields. */
		}
		break;

	case GTPV1_TLV_AUTHENTICATION:

		/* 29.060 7.7.35 - Authentication. */
		printf("Authentication");			/* XXX */
		break;

	case GTPV1_TLV_TRAFFIC_FLOW:

		/* 29.060 7.7.36 - Traffic Flow Template. */
		printf("Traffic Flow Template");		/* XXX */
		break;

	case GTPV1_TLV_TARGET_IDENTIFICATION:

		/* 29.060 7.7.37 - Target Identification. */
		printf("Target ID");				/* XXX */
		break;

	case GTPV1_TLV_UTRAN_CONTAINER:

		/* 29.060 7.7.38 - UTRAN Transparent Container. */
		printf("UTRAN Container");			/* XXX */
		break;

	case GTPV1_TLV_RAB_SETUP_INFORMATION:

		/* 29.060 7.7.39 - RAB Setup Information. */
		printf("RAB Setup");				/* XXX */
		break;

	case GTPV1_TLV_EXT_HEADER_TYPE_LIST:

		/* 29.060 7.7.40 - Extension Header Type List. */
		printf("Extension Header List");		/* XXX */
		break;

	case GTPV1_TLV_TRIGGER_ID:

		/* 29.060 7.7.41 - Trigger ID. */
		printf("Trigger ID");				/* XXX */
		break;

	case GTPV1_TLV_OMC_IDENTITY:

		/* 29.060 7.7.42 - OMC Identity. */
		printf("OMC Identity");				/* XXX */
		break;

	case GTPV1_TLV_RAN_CONTAINER:

		/* 29.060 7.7.43 - RAN Transparent Container. */
		printf("RAN Container");			/* XXX */
		break;

	case GTPV1_TLV_PDP_CONTEXT_PRIORITIZATION:

		/* 29.060 7.7.45 - PDP Context Prioritization. */
		printf("PDP Context Prioritization");		/* XXX */
		break;

	case GTPV1_TLV_ADDITIONAL_RAB_SETUP_INFO:

		/* 29.060 7.7.45A - Additional RAB Setup Information. */
		printf("Additional RAB Setup");			/* XXX */
		break;

	case GTPV1_TLV_SGSN_NUMBER:

		/* 29.060 7.7.47 - SGSN Number. */
		printf("SGSN Number");				/* XXX */
		break;

	case GTPV1_TLV_COMMON_FLAGS:

		/* 29.060 7.7.48 - Common Flags. */
		printf("Common Flags");				/* XXX */
		break;

	case GTPV1_TLV_APN_RESTRICTION:

		/* 29.060 7.7.49 - APN Restriction. */
		data = (u_int8_t)cp[0];
		printf("APN Restriction: %u", data);
		break;

	case GTPV1_TLV_RADIO_PRIORITY_LCS:

		/* 29.060 7.7.25B - Radio Priority LCS. */
		printf("Radio Priority LCS: %u", cp[0] & 0x7);
		break;

	case GTPV1_TLV_RAT_TYPE:

		/* 29.060 7.7.50 - RAT Type. */
		printf("RAT");
		gtp_print_str(gtp_rat_type, nitems(gtp_rat_type), cp[0]);
		break;

	case GTPV1_TLV_USER_LOCATION_INFO:

		/* 29.060 7.7.51 - User Location Information. */
		printf("ULI");					/* XXX */
		break;

	case GTPV1_TLV_MS_TIME_ZONE:

		/* 29.060 7.7.52 - MS Time Zone. */
		printf("MSTZ");					/* XXX */
		break;

	case GTPV1_TLV_IMEI_SV:

		/* 29.060 7.7.53 - IMEI(SV). */
		printf("IMEI(SV) ");
		gtp_print_tbcd(cp, len);
		break;

	case GTPV1_TLV_CAMEL_CHARGING_CONTAINER:

		/* 29.060 7.7.54 - CAMEL Charging Information Container. */
		printf("CAMEL Charging");			/* XXX */
		break;

	case GTPV1_TLV_MBMS_UE_CONTEXT:

		/* 29.060 7.7.55 - MBMS UE Context. */
		printf("MBMS UE Context");			/* XXX */
		break;

	case GTPV1_TLV_TMGI:

		/* 29.060 7.7.56 - Temporary Mobile Group Identity. */
		printf("TMGI");					/* XXX */
		break;

	case GTPV1_TLV_RIM_ROUTING_ADDRESS:

		/* 29.060 7.7.57 - RIM Routing Address. */
		printf("RIM Routing Address");			/* XXX */
		break;

	case GTPV1_TLV_MBMS_PROTOCOL_CONFIG_OPTIONS:

		/* 29.060 7.7.58 - MBMS Protocol Configuration Options. */
		printf("MBMS Protocol Config Options");		/* XXX */
		break;

	case GTPV1_TLV_MBMS_SERVICE_AREA:

		/* 29.060 7.7.60 - MBMS Service Area. */
		printf("MBMS Service Area");			/* XXX */
		break;

	case GTPV1_TLV_SOURCE_RNC_PDCP_CONTEXT_INFO:

		/* 29.060 7.7.61 - Source RNC PDCP Context Information. */
		printf("Source RNC PDCP Context");		/* XXX */
		break;

	case GTPV1_TLV_ADDITIONAL_TRACE_INFO:

		/* 29.060 7.7.62 - Additional Trace Information. */
		printf("Additional Trace Info");		/* XXX */
		break;

	case GTPV1_TLV_HOP_COUNTER:

		/* 29.060 7.7.63 - Hop Counter. */
		printf("Hop Counter: %u", cp[0]);
		break;

	case GTPV1_TLV_SELECTED_PLMN_ID:

		/* 29.060 7.7.64 - Selected PLMN ID. */
		printf("Selected PLMN ID");			/* XXX */
		break;

	case GTPV1_TLV_MBMS_SESSION_IDENTIFIER:

		/* 29.060 7.7.65 - MBMS Session Identifier. */
		printf("MBMS Session ID: %u", cp[0]);
		break;

	case GTPV1_TLV_MBMS_2G_3G_INDICATOR:

		/* 29.060 7.7.66 - MBMS 2G/3G Indicator. */
		printf("MBMS 2G/3G Indicator");
		gtp_print_str(mbms_2g3g_indicator, nitems(mbms_2g3g_indicator),
		    cp[0]);
		break;

	case GTPV1_TLV_ENHANCED_NSAPI:

		/* 29.060 7.7.67 - Enhanced NSAPI. */
		printf("Enhanced NSAPI");			/* XXX */
		break;

	case GTPV1_TLV_MBMS_SESSION_DURATION:

		/* 29.060 7.7.59 - MBMS Session Duration. */
		printf("MBMS Session Duration");		/* XXX */
		break;

	case GTPV1_TLV_ADDITIONAL_MBMS_TRACE_INFO:

		/* 29.060 7.7.68 - Additional MBMS Trace Info. */
		printf("Additional MBMS Trace Info");		/* XXX */
		break;

	case GTPV1_TLV_MBMS_SESSION_REPITITION_NO:

		/* 29.060 7.7.69 - MBMS Session Repetition Number. */
		printf("MBMS Session Repetition No: %u", cp[0]);
		break;

	case GTPV1_TLV_MBMS_TIME_TO_DATA_TRANSFER:

		/* 29.060 7.7.70 - MBMS Time to Data Transfer. */
		printf("MBMS Time to Data Transfer: %u", cp[0]);
		break;

	case GTPV1_TLV_PS_HANDOVER_REQUEST_CONTEXT:

		/* 29.060 7.7.71 - PS Handover Request Context (Void). */
		break;

	case GTPV1_TLV_BSS_CONTAINER:

		/* 29.060 7.7.72 - BSS Container. */
		printf("BSS Container");			/* XXX */
		break;

	case GTPV1_TLV_CELL_IDENTIFICATION:

		/* 29.060 7.7.73 - Cell Identification. */
		printf("Cell Identification");			/* XXX */
		break;

	case GTPV1_TLV_PDU_NUMBERS:

		/* 29.060 7.7.74 - PDU Numbers. */
		printf("PDU Numbers");				/* XXX */
		break;

	case GTPV1_TLV_BSSGP_CAUSE:

		/* 29.060 7.7.75 - BSSGP Cause. */
		printf("BSSGP Cause: %u", cp[0]);
		break;

	case GTPV1_TLV_REQUIRED_MBMS_BEARER_CAP:

		/* 29.060 7.7.76 - Required MBMS Bearer Cap. */
		printf("Required MBMS Bearer Cap");		/* XXX */
		break;

	case GTPV1_TLV_RIM_ROUTING_ADDRESS_DISC:

		/* 29.060 7.7.77 - RIM Routing Address Discriminator. */
		printf("RIM Routing Address Discriminator: %u", cp[0] & 0xf);
		break;

	case GTPV1_TLV_LIST_OF_SETUP_PFCS:

		/* 29.060 7.7.78 - List of Setup PFCs. */
		printf("List of Setup PFCs");			/* XXX */
		break;

	case GTPV1_TLV_PS_HANDOVER_XID_PARAMETERS:

		/* 29.060 7.7.79 - PS Handover XID Parameters. */
		printf("PS Handover XID Parameters");		/* XXX */
		break;

	case GTPV1_TLV_MS_INFO_CHANGE_REPORTING:

		/* 29.060 7.7.80 - MS Info Change Reporting. */
		printf("MS Info Change Reporting");
		gtp_print_str(ms_info_change_rpt, nitems(ms_info_change_rpt),
		    cp[0]);
		break;

	case GTPV1_TLV_DIRECT_TUNNEL_FLAGS:

		/* 29.060 7.7.81 - Direct Tunnel Flags. */
		printf("Direct Tunnel Flags");			/* XXX */
		break;

	case GTPV1_TLV_CORRELATION_ID:

		/* 29.060 7.7.82 - Correlation ID. */
		printf("Correlation ID");			/* XXX */
		break;

	case GTPV1_TLV_BEARER_CONTROL_MODE:

		/* 29.060 7.7.83 - Bearer Control Mode. */
		printf("Bearer Control Mode");			/* XXX */
		break;

	case GTPV1_TLV_MBMS_FLOW_IDENTIFIER:

		/* 29.060 7.7.84 - MBMS Flow Identifier. */
		printf("MBMS Flow Identifier");			/* XXX */
		break;

	case GTPV1_TLV_RELEASED_PACKETS:

		/* 32.295 6.2.4.5.4 - Sequence Numbers of Released Packets. */
		printf("Released Packets:");
		seqno = (u_int16_t *)cp;
		while (len > 0) {
			printf(" %u", ntohs(*seqno));
			seqno++;
			len -= sizeof(*seqno);
		}
		break;

	case GTPV1_TLV_CANCELLED_PACKETS:

		/* 32.295 6.2.4.5.5 - Sequence Numbers of Cancelled Packets. */
		printf("Cancelled Packets:");
		seqno = (u_int16_t *)cp;
		while (len > 0) {
			printf(" %u", ntohs(*seqno));
			seqno++;
			len -= sizeof(*seqno);
		}
		break;

	case GTPV1_TLV_CHARGING_GATEWAY_ADDRESS:

		/* 29.060 7.7.44 - Charging Gateway Address. */
		printf("Charging Gateway");
		if (len == 4)
			printf(": %s", ipaddr_string(cp));
		else if (len == 16)
			printf(": %s", ip6addr_string(cp));
		break;

	case GTPV1_TLV_DATA_RECORD_PACKET:

		/* 32.295 6.2.4.5.3 - Data Record Packet. */
		printf("Data Record: Records %u, Format %u, Format Version %u",
		    cp[0], cp[1], ntohs(*(u_int16_t *)(cp + 2)));
		break;

	case GTPV1_TLV_REQUESTS_RESPONDED:

		/* 32.295 6.2.4.6 - Requests Responded. */
		printf("Requests Responded:");
		seqno = (u_int16_t *)cp;
		while (len > 0) {
			printf(" %u", ntohs(*seqno));
			seqno++;
			len -= sizeof(*seqno);
		}
		break;

	case GTPV1_TLV_ADDRESS_OF_RECOMMENDED_NODE:

		/* 32.295 6.2.4.3 - Address of Recommended Node. */
		printf("Address of Recommended Node");
		if (len == 4)
			printf(": %s", ipaddr_string(cp));
		else if (len == 16)
			printf(": %s", ip6addr_string(cp));
		break;

	case GTPV1_TLV_PRIVATE_EXTENSION:

		/* 29.060 7.7.46 - Private Extension. */
		printf("Private Extension");
		break;

	default:
		printf("TLV %u (len %u)", value, len);
	}

	return ielen;

trunc:
	return -1;
}
