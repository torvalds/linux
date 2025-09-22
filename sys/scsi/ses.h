/* $OpenBSD: ses.h,v 1.11 2024/09/04 07:54:53 mglocker Exp $ */
/*
 * Copyright (c) 2005 Marco Peereboom
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _SCSI_SES_H_
#define _SCSI_SES_H_

/* the scsi command */
struct ses_scsi_diag {
	u_int8_t	opcode; /* SEND_DIAGNOSTIC or RECEIVE_DIAGNOSTIC */
	u_int8_t	flags;
#define SES_DIAG_PCV		(1<<0)	/* page code valid */
#define SES_DIAG_PF		(1<<4)	/* set this bit if page code is valid */
	u_int8_t	pgcode;
#define SES_PAGE_CONFIG		0x01	/* Configuration */
#define SES_PAGE_STATUS		0x02	/* Enclosure Status */
#define SES_PAGE_EDESC		0x07	/* Element Descriptor */
	u_int16_t	length;
	u_int8_t	control;
} __packed;


/* all the different sensor types */
#define SES_T_UNSPEC		0x00
#define SES_T_DEVICE		0x01
#define SES_T_POWERSUPPLY	0x02
#define SES_T_COOLING		0x03
#define SES_T_TEMP		0x04
#define SES_T_DOORLOCK		0x05
#define SES_T_ALARM		0x06
#define SES_T_ENC_SRV_CTRL	0x07
#define SES_T_SCC_CTRL		0x08
#define SES_T_NONVOL_CACHE	0x09
#define SES_T_INV_OP_REASON	0x0a
#define SES_T_UPS		0x0b
#define SES_T_DISPLAY		0x0c
#define SES_T_KEYPAD		0x0d
#define SES_T_ENCLOSURE		0x0e
#define SES_T_SCSI_PORT_TRANS	0x0f
#define SES_T_LANGUAGE		0x10
#define SES_T_COMM_PORT		0x11
#define SES_T_VOLTAGE		0x12
#define SES_T_CURRENT		0x13
#define SES_T_SCSI_TARGET_PORT	0x14
#define SES_T_SCSI_INIT_PORT	0x15
#define SES_T_SIMP_SUBENC	0x16
#define SES_T_ARRAY_DEVICE	0x17

#define SES_NUM_TYPES		256

/* diagnostic page header */
struct ses_config_hdr {
	u_int8_t	pgcode; /* SES_PAGE_CONFIG */
	u_int8_t	n_subenc;
	u_int16_t	length;
	u_int32_t	gencode;
} __packed;
#define SES_CFG_HDRLEN		sizeof(struct ses_config_hdr)

/* enclosure descriptor header */
struct ses_enc_hdr {
	u_int8_t	enc_id;
	u_int8_t	subenc_id;
	u_int8_t	n_types;
	u_int8_t	vendor_len;
} __packed;
#define SES_ENC_HDRLEN		sizeof(struct ses_enc_hdr)

/* enclosure descriptor strings */
struct ses_enc_desc {
	u_int8_t	logical_id[8]; /* this isn't a string */
	u_int8_t	vendor_id[8];
	u_int8_t	prod_id[16];
	u_int8_t	prod_rev[4];
	u_int8_t	vendor[0];
} __packed;

/* type descriptor header */
struct ses_type_desc {
	u_int8_t	type;
	u_int8_t	n_elem;
	u_int8_t	subenc_id;
	u_int8_t	desc_len;
} __packed;
#define SES_TYPE_DESCLEN	sizeof(struct ses_type_desc)

/* status page header */
struct ses_status_hdr {
	u_int8_t	pgcode;		/* SES_PAGE_STATUS */
	u_int8_t	flags;
#define SES_STAT_UNRECOV	(1<<0)	/* unrecoverable error */
#define SES_STAT_CRIT		(1<<1)	/* critical error */
#define SES_STAT_NONCRIT	(1<<2)	/* noncritical error */
#define SES_STAT_INFO		(1<<3)	/* info available */
#define SES_STAT_INVOP		(1<<4)	/* invalid operation */
	u_int16_t	length;
	u_int32_t	gencode;
} __packed;
#define SES_STAT_HDRLEN		sizeof(struct ses_status_hdr)

struct ses_status {
	u_int8_t	com;
#define SES_STAT_CODE_MASK	0x0f
#define SES_STAT_CODE(x)	((x) & SES_STAT_CODE_MASK)
#define SES_STAT_CODE_UNSUP	0x00 /* unsupported */
#define SES_STAT_CODE_OK	0x01 /* installed and ok */
#define SES_STAT_CODE_CRIT	0x02 /* critical */
#define SES_STAT_CODE_NONCRIT	0x03 /* warning */
#define SES_STAT_CODE_UNREC	0x04 /* unrecoverable */
#define SES_STAT_CODE_NOTINST	0x05 /* not installed */
#define SES_STAT_CODE_UNKNOWN	0x06 /* unknown */
#define SES_STAT_CODE_NOTAVAIL	0x07 /* not available */
#define SES_STAT_SWAP		(1<<4)	/* element has been swapped */
#define SES_STAT_DISABLED	(1<<5)	/* disabled */
#define SES_STAT_PRDFAIL	(1<<6)	/* predicted failure */
#define SES_STAT_SELECT		(1<<7)	/* set to modify element */

	u_int8_t	f1;	/* use of these flags depends on the SES_T */
	u_int8_t	f2;
	u_int8_t	f3;
} __packed;
#define SES_STAT_ELEMLEN	sizeof(struct ses_status)

/* device status */
/* f1 is the device address */
/* f2 */
#define SES_S_DEV_REPORT	(1<<0)	/* enc report in progress */
#define SES_S_DEV_IDENT		(1<<1)	/* currently identifying */
#define SES_S_DEV_REMOVE	(1<<2)	/* ready to remove */
#define SES_S_DEV_INSERT	(1<<3)	/* ready to insert */
#define SES_S_DEV_ENCBYPB	(1<<4)	/* port B bypassed by enc */
#define SES_S_DEV_ENCBYPA	(1<<5)	/* port A bypassed by enc */
#define SES_S_DEV_DONOTREM	(1<<6)	/* do not remove */
#define SES_S_DEV_APPCLBYPA	(1<<7)	/* port A bypassed by app */
/* f3 */
#define SES_S_DEV_DEVBYPB	(1<<0)	/* port B bypassed by dev */
#define SES_S_DEV_DEVBYPA	(1<<1)	/* port A bypassed by dev */
#define SES_S_DEV_BYPB		(1<<2)
#define SES_S_DEV_BYPA		(1<<3)
#define SES_S_DEV_OFF		(1<<4)	/* device is off */
#define SES_S_DEV_FAULTRQST	(1<<5)	/* fault indicator rqsted */
#define SES_S_DEV_FAULTSENSE	(1<<6)	/* fault sensed */
#define SES_S_DEV_APPCLBYPB	(1<<7)	/* port B bypassed by app */

/* device configuration */
/* f1 is reserved */
/* f2 */
#define SES_C_DEV_IDENT		(1<<1)	/* ident */
#define SES_C_DEV_REMOVE	(1<<2)	/* remove */
#define SES_C_DEV_INSERT	(1<<3)	/* insert */
#define SES_C_DEV_DONOTREM	(1<<6)	/* do not remove */
#define SES_C_DEV_ACTIVE	(1<<7)	/* active indicator */
#define SES_C_DEV_F2MASK	(SES_C_DEV_IDENT | SES_C_DEV_REMOVE | \
    SES_C_DEV_INSERT | SES_C_DEV_DONOTREM | SES_C_DEV_ACTIVE )
/* f3 */
#define SES_C_DEV_BYPB		(1<<2)	/* port B bypass */
#define SES_C_DEV_BYPA		(1<<3)	/* port A bypass */
#define SES_C_DEV_OFF		(1<<4)	/* off */
#define SES_C_DEV_FAULT		(1<<5)	/* fault indicator */
#define SES_C_DEV_F3MASK	(SES_C_DEV_BYPB | SES_C_DEV_BYPA | \
    SES_C_DEV_OFF | SES_C_DEV_FAULT)

/* power supply element */
#define SES_S_PSU_IDENT(d)	((d)->f1 & (1<<6)) /* identify */
#define SES_S_PSU_DCOC(d)	((d)->f2 & (1<<1)) /* DC over current */
#define SES_S_PSU_DCUV(d)	((d)->f2 & (1<<2)) /* DC under voltage */
#define SES_S_PSU_DCOV(d)	((d)->f2 & (1<<3)) /* DC over voltage */
#define SES_S_PSU_DCFAIL(d)	((d)->f3 & (1<<0)) /* DC fail */
#define SES_S_PSU_ACFAIL(d)	((d)->f3 & (1<<1)) /* AC fail */
#define SES_S_PSU_TEMPWARN(d)	((d)->f3 & (1<<2)) /* Temp warn */
#define SES_S_PSU_OVERTEMP(d)	((d)->f3 & (1<<3)) /* over temp fail */
#define SES_S_PSU_OFF(d)	((d)->f3 & (1<<4)) /* is the unit off */
#define SES_S_PSU_RQSTON(d)	((d)->f3 & (1<<5)) /* manually on */
#define SES_S_PSU_FAIL(d)	((d)->f3 & (1<<6)) /* fail is set on */

/* cooling element */
#define SES_S_COOL_IDENT(d)	((d)->f1 & (1<<6)) /* identify */
#define SES_S_COOL_SPEED_MASK	0x03
#define SES_S_COOL_SPEED(d)	((d)->f2 + \
    ((u_int16_t)((d)->f2 & SES_S_COOL_SPEED_MASK) << 8))
#define SES_S_COOL_FACTOR	10
#define SES_S_COOL_CODE(d)	((d)->f3 & 0x7) /* actual speed code */
#define SES_S_COOL_C_STOPPED	0x0 /* stopped */
#define SES_S_COOL_C_LOW1	0x1 /* lowest speed */
#define SES_S_COOL_C_LOW2	0x2 /* second lowest speed */
#define SES_S_COOL_C_LOW3	0x3 /* third lowest speed */
#define SES_S_COOL_C_INTER	0x4 /* intermediate speed */
#define SES_S_COOL_C_HI3	0x5 /* third highest speed */
#define SES_S_COOL_C_HI2	0x6 /* second highest speed */
#define SES_S_COOL_C_HI1	0x7 /* highest speed */
#define SES_S_COOL_OFF		((d)->f3 & (1<<4)) /* not cooling */
#define SES_S_COOL_RQSTON	((d)->f3 & (1<<5)) /* manually on */
#define SES_S_COOL_FAIL		((d)->f3 & (1<<6)) /* fail indic is on */

/* temperature sensor */
#define SES_S_TEMP_IDENT(d)	((d)->f1 & (1<<7)) /* identify */
#define SES_S_TEMP(d)		((d)->f2)
#define SES_S_TEMP_OFFSET	(-20)
#define SES_S_TEMP_UTWARN	((d)->f3 & (1<<0)) /* under temp warning */
#define SES_S_TEMP_UTFAIL	((d)->f3 & (1<<1)) /* under temp failure */
#define SES_S_TEMP_OTWARN	((d)->f3 & (1<<2)) /* over temp warning */
#define SES_S_TEMP_OTFAIL	((d)->f3 & (1<<3)) /* over temp failure */

/*
 * the length of the status page is the header and a status element for
 * each type plus the number of elements for each type
 */
#define SES_STAT_LEN(t, e)	\
    (SES_STAT_HDRLEN + SES_STAT_ELEMLEN * ((t)+(e)))

#endif /* _SCSI_SES_H_ */
