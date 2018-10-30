/* SPDX-License-Identifier: GPL-1.0+ WITH Linux-syscall-note */
/* $Id: isdnif.h,v 1.43.2.2 2004/01/12 23:08:35 keil Exp $
 *
 * Linux ISDN subsystem
 * Definition of the interface between the subsystem and its low-level drivers.
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    Thinking Objects Software GmbH Wuerzburg
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef _UAPI__ISDNIF_H__
#define _UAPI__ISDNIF_H__


/*
 * Values for general protocol-selection
 */
#define ISDN_PTYPE_UNKNOWN   0   /* Protocol undefined   */
#define ISDN_PTYPE_1TR6      1   /* german 1TR6-protocol */
#define ISDN_PTYPE_EURO      2   /* EDSS1-protocol       */
#define ISDN_PTYPE_LEASED    3   /* for leased lines     */
#define ISDN_PTYPE_NI1       4   /* US NI-1 protocol     */
#define ISDN_PTYPE_MAX       7   /* Max. 8 Protocols     */

/*
 * Values for Layer-2-protocol-selection
 */
#define ISDN_PROTO_L2_X75I   0   /* X75/LAPB with I-Frames            */
#define ISDN_PROTO_L2_X75UI  1   /* X75/LAPB with UI-Frames           */
#define ISDN_PROTO_L2_X75BUI 2   /* X75/LAPB with UI-Frames           */
#define ISDN_PROTO_L2_HDLC   3   /* HDLC                              */
#define ISDN_PROTO_L2_TRANS  4   /* Transparent (Voice)               */
#define ISDN_PROTO_L2_X25DTE 5   /* X25/LAPB DTE mode                 */
#define ISDN_PROTO_L2_X25DCE 6   /* X25/LAPB DCE mode                 */
#define ISDN_PROTO_L2_V11096 7   /* V.110 bitrate adaption 9600 Baud  */
#define ISDN_PROTO_L2_V11019 8   /* V.110 bitrate adaption 19200 Baud */
#define ISDN_PROTO_L2_V11038 9   /* V.110 bitrate adaption 38400 Baud */
#define ISDN_PROTO_L2_MODEM  10  /* Analog Modem on Board */
#define ISDN_PROTO_L2_FAX    11  /* Fax Group 2/3         */
#define ISDN_PROTO_L2_HDLC_56K 12   /* HDLC 56k                          */
#define ISDN_PROTO_L2_MAX    15  /* Max. 16 Protocols                 */

/*
 * Values for Layer-3-protocol-selection
 */
#define ISDN_PROTO_L3_TRANS	0	/* Transparent */
#define ISDN_PROTO_L3_TRANSDSP	1	/* Transparent with DSP */
#define ISDN_PROTO_L3_FCLASS2	2	/* Fax Group 2/3 CLASS 2 */
#define ISDN_PROTO_L3_FCLASS1	3	/* Fax Group 2/3 CLASS 1 */
#define ISDN_PROTO_L3_MAX	7	/* Max. 8 Protocols */


#endif /* _UAPI__ISDNIF_H__ */
