/*	$OpenBSD: l2tp.h,v 1.1 2000/01/16 10:54:58 jakob Exp $	*/

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


#define L2TP_FLAG_TYPE		0x8000	/* Type (0=Data, 1=Control) */
#define L2TP_FLAG_LENGTH	0x4000	/* Length */
#define L2TP_FLAG_SEQUENCE	0x0800	/* Sequence */
#define L2TP_FLAG_OFFSET	0x0200	/* Offset */
#define L2TP_FLAG_PRIORITY	0x0100	/* Priority */

#define L2TP_VERSION_MASK	0x000f	/* Version Mask */
#define L2TP_VERSION_L2F	0x0001	/* L2F */
#define L2TP_VERSION_L2TP	0x0002	/* L2TP */

#define L2TP_AVP_HDR_FLAG_MANDATORY	0x8000	/* Mandatory Flag */
#define L2TP_AVP_HDR_FLAG_HIDDEN	0x4000	/* Hidden Flag */
#define L2TP_AVP_HDR_LEN_MASK		0x03ff	/* Length Mask */

#define L2TP_FRAMING_CAP_SYNC_MASK	0x00000001	/* Synchronous */
#define L2TP_FRAMING_CAP_ASYNC_MASK	0x00000002	/* Asynchronous */

#define L2TP_FRAMING_TYPE_SYNC_MASK	0x00000001	/* Synchronous */
#define L2TP_FRAMING_TYPE_ASYNC_MASK	0x00000002	/* Asynchronous */

#define L2TP_BEARER_CAP_DIGITAL_MASK	0x00000001	/* Digital */
#define L2TP_BEARER_CAP_ANALOG_MASK	0x00000002	/* Analog */

#define L2TP_BEARER_TYPE_DIGITAL_MASK	0x00000001	/* Digital */
#define L2TP_BEARER_TYPE_ANALOG_MASK	0x00000002	/* Analog */

/* Authen Type */
#define L2TP_AUTHEN_TYPE_RESERVED	0x0000	/* Reserved */
#define L2TP_AUTHEN_TYPE_TEXTUAL	0x0001	/* Textual username/password exchange */
#define L2TP_AUTHEN_TYPE_CHAP		0x0002	/* PPP CHAP */
#define L2TP_AUTHEN_TYPE_PAP		0x0003	/* PPP PAP */
#define L2TP_AUTHEN_TYPE_NO_AUTH	0x0004	/* No Authentication */
#define L2TP_AUTHEN_TYPE_MSCHAP		0x0005	/* MSCHAPv1 */

#define L2TP_PROXY_AUTH_ID_MASK		0x00ff


struct l2tp_avp_vec {
	const char *name;
	void (*print)(const u_char *, u_int);	
};
		
struct l2tp_call_errors {
	u_short	reserved;
	u_int	crc_errs;
	u_int	framing_errs;	
	u_int	hardware_overruns;	
	u_int	buffer_overruns;
	u_int	timeout_errs;	
	u_int	alignment_errs;	
};

struct l2tp_accm {
	u_short reserved;
	u_int	send_accm;
	u_int	recv_accm;
};

