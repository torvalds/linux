/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * BER and PER decoding library for H.323 conntrack/NAT module.
 *
 * Copyright (c) 2006 by Jing Min Zhao <zhaojingmin@users.sourceforge.net>
 *
 * This library is based on H.225 version 4, H.235 version 2 and H.245
 * version 7. It is extremely optimized to decode only the absolutely
 * necessary objects in a signal for Linux kernel NAT module use, so don't
 * expect it to be a full ASN.1 library.
 *
 * Features:
 *
 * 1. Small. The total size of code plus data is less than 20 KB (IA32).
 * 2. Fast. Decoding Netmeeting's Setup signal 1 million times on a PIII 866
 *    takes only 3.9 seconds.
 * 3. No memory allocation. It uses a static object. No need to initialize or
 *    cleanup.
 * 4. Thread safe.
 * 5. Support embedded architectures that has no misaligned memory access
 *    support.
 *
 * Limitations:
 *
 * 1. At most 30 faststart entries. Actually this is limited by ethernet's MTU.
 *    If a Setup signal contains more than 30 faststart, the packet size will
 *    very likely exceed the MTU size, then the TPKT will be fragmented. I
 *    don't know how to handle this in a Netfilter module. Anybody can help?
 *    Although I think 30 is enough for most of the cases.
 * 2. IPv4 addresses only.
 *
 ****************************************************************************/

#ifndef _NF_CONNTRACK_HELPER_H323_ASN1_H_
#define _NF_CONNTRACK_HELPER_H323_ASN1_H_

/*****************************************************************************
 * H.323 Types
 ****************************************************************************/

#include <linux/types.h>
#include <linux/netfilter/nf_conntrack_h323_types.h>

typedef struct {
	enum {
		Q931_NationalEscape = 0x00,
		Q931_Alerting = 0x01,
		Q931_CallProceeding = 0x02,
		Q931_Connect = 0x07,
		Q931_ConnectAck = 0x0F,
		Q931_Progress = 0x03,
		Q931_Setup = 0x05,
		Q931_SetupAck = 0x0D,
		Q931_Resume = 0x26,
		Q931_ResumeAck = 0x2E,
		Q931_ResumeReject = 0x22,
		Q931_Suspend = 0x25,
		Q931_SuspendAck = 0x2D,
		Q931_SuspendReject = 0x21,
		Q931_UserInformation = 0x20,
		Q931_Disconnect = 0x45,
		Q931_Release = 0x4D,
		Q931_ReleaseComplete = 0x5A,
		Q931_Restart = 0x46,
		Q931_RestartAck = 0x4E,
		Q931_Segment = 0x60,
		Q931_CongestionCtrl = 0x79,
		Q931_Information = 0x7B,
		Q931_Notify = 0x6E,
		Q931_Status = 0x7D,
		Q931_StatusEnquiry = 0x75,
		Q931_Facility = 0x62
	} MessageType;
	H323_UserInformation UUIE;
} Q931;

/*****************************************************************************
 * Decode Functions Return Codes
 ****************************************************************************/

#define H323_ERROR_NONE 0	/* Decoded successfully */
#define H323_ERROR_STOP 1	/* Decoding stopped, not really an error */
#define H323_ERROR_BOUND -1
#define H323_ERROR_RANGE -2


/*****************************************************************************
 * Decode Functions
 ****************************************************************************/

int DecodeRasMessage(unsigned char *buf, size_t sz, RasMessage * ras);
int DecodeQ931(unsigned char *buf, size_t sz, Q931 * q931);
int DecodeMultimediaSystemControlMessage(unsigned char *buf, size_t sz,
					 MultimediaSystemControlMessage *
					 mscm);

#endif
