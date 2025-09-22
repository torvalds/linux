/*	$OpenBSD: safte.h,v 1.9 2020/09/12 15:54:51 krw Exp $ */

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
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

#ifndef _SCSI_SAFTE_H
#define _SCSI_SAFTE_H

/* scsi_inquiry_data.extra */
struct safte_inq {
	u_int8_t	uniqueid[7];
	u_int8_t	chanid;
	u_int8_t	ident[6];
#define SAFTE_IDENT		"SAF-TE"
};

struct safte_readbuf_cmd {
	u_int8_t	opcode;		/* READ_BUFFER */
	u_int8_t	flags;
#define SAFTE_RD_LUNMASK	0xe0	/* the lun should always be 0 */
#define SAFTE_RD_MODEMASK	0x07
#define SAFTE_RD_MODE		0x01	/* 0x01 is the SAF-TE command mode */
	u_int8_t	bufferid;
#define SAFTE_RD_CONFIG		0x00	/* enclosure configuration */
#define SAFTE_RD_ENCSTAT	0x01	/* enclosure status */
#define SAFTE_RD_USAGE		0x02	/* usage statistics */
#define SAFTE_RD_INSERTS	0x03	/* device insertions */
#define SAFTE_RD_SLOTSTAT	0x04	/* slot status */
#define SAFTE_RD_GLOBALS	0x05	/* global flags */
	u_int32_t	reserved1;
	u_int16_t	length;		/* transfer length (big endian) */
	u_int8_t	reserved2;
} __packed;

struct safte_writebuf_cmd {
	u_int8_t	opcode;		/* WRITE_BUFFER */
	u_int8_t	flags;
#define SAFTE_WR_LUNMASK	0xe0	/* the lun should always be 0 */
#define SAFTE_WR_MODEMASK	0x07
#define SAFTE_WR_MODE		0x01	/* 0x01 is the SAF-TE command mode */
	u_int8_t	reserved1[5];
	u_int16_t	length;		/* transfer length (big endian) */
	u_int8_t	reserved2;
} __packed;

#define	SAFTE_WRITE_SLOTSTAT	0x10	/* write device slot status */
#define SAFTE_WRITE_SETID	0x11	/* set scsi id */
#define	SAFTE_WRITE_SLOTOP	0x12	/* perform slot operation */
#define	SAFTE_WRITE_FANSPEED	0x13	/* set fan speed */
#define	SAFTE_WRITE_PWRSUP	0x14	/* activate power supply */
#define	SAFTE_WRITE_GLOBALS	0x15	/* global flags */


/* enclosure configuration */
struct safte_config {
	u_int8_t	nfans;		/* number of fans */
	u_int8_t	npwrsup;	/* number of power supplies */
	u_int8_t	nslots;		/* number of device slots */
	u_int8_t	doorlock;	/* door lock installed */
	u_int8_t	ntemps;		/* number of temp sensors */
	u_int8_t	alarm;		/* audible alarm installed */
	u_int8_t	therm;		/* temps in C and num of thermostats */
#define SAFTE_CFG_CELSIUSMASK	0x80
#define SAFTE_CFG_CELSIUS(a)	((a) & SAFTE_CFG_CELSIUSMASK ? 1 : 0)
#define SAFTE_CFG_NTHERMMASK	0x0f
#define SAFTE_CFG_NTHERM(a)	((a) & SAFTE_CFG_NTHERMMASK)
	u_int8_t	reserved[56]; /* 7 to 62 */
	u_int8_t	vendor_bytes;	/* number of vendor specific bytes */
} __packed;
#define SAFTE_CONFIG_LEN	sizeof(struct safte_config)

/* enclosure status fields */
/* fan status field */
#define SAFTE_FAN_OP		0x00	/* operational */
#define SAFTE_FAN_MF		0x01	/* malfunctioning */
#define SAFTE_FAN_NOTINST	0x02	/* not installed */
#define SAFTE_FAN_UNKNOWN	0x80	/* unknown status or unreportable */

/* power supply status field */
#define SAFTE_PWR_OP_ON		0x00	/* operational and on */
#define SAFTE_PWR_OP_OFF	0x01	/* operational and off */
#define SAFTE_PWR_MF_ON		0x10	/* malfunctioning and on */
#define SAFTE_PWR_MF_OFF	0x11	/* malfunctioning and off */
#define SAFTE_PWR_NOTINST	0x20	/* not present */
#define SAFTE_PWR_PRESENT	0x21	/* present */
#define SAFTE_PWR_UNKNOWN	0x80	/* unknown status or unreportable */

/* scsi id fields */
/* are integers, not bitfields */

/* door lock status */
#define SAFTE_DOOR_LOCKED	0x00	/* locked */
#define SAFTE_DOOR_UNLOCKED	0x01	/* unlocked or uncontrollable */
#define SAFTE_DOOR_UNKNOWN	0x80	/* unknown status or unreportable */

/* speaker status */
#define SAFTE_SPKR_OFF		0x00	/* off or not installed */
#define SAFTE_SPKR_ON		0x01	/* speaker is currently on */

/* temperature */
#define SAFTE_TEMP_OFFSET	-10	/* -10 to 245 degrees */

/* temp out of range */
#define SAFTE_TEMP_ETA		0x8000	/* any temp alert */


/* usage statistics */
struct safte_usage {
	u_int32_t	minutes;	/* total number of minutes on */
	u_int32_t	cycles;		/* total number of power cycles */
	u_int8_t	reserved[7];
	u_int8_t	vendor_bytes;	/* number of vendor specific bytes */
};


/* device insertions */
/* u_int16_t * nslots */


/* device slot status */
#define SAFTE_SLOTSTAT_INSERT	(1<<0)	/* inserted */
#define SAFTE_SLOTSTAT_SWAP	(1<<1)	/* ready to be inserted/removed */
#define SAFTE_SLOTSTAT_OPER	(1<<2)	/* ready for operation */


/* global flags */
struct safte_globals {
	u_int8_t	flags1;
#define SAFTE_GLOBAL_ALARM	(1<<0)	/* audible alarm */
#define SAFTE_GLOBAL_FAILURE	(1<<1)	/* global failure indication */
#define SAFTE_GLOBAL_WARNING	(1<<2)	/* global warning indication */
#define SAFTE_GLOBAL_POWER	(1<<3)	/* enclosure power */
#define	SAFTE_GLOBAL_COOLING	(1<<4)	/* cooling failure */
#define SAFTE_GLOBAL_PWRFAIL	(1<<5)	/* power failure */
#define SAFTE_GLOBAL_DRVFAIL	(1<<6)	/* drive failure */
#define SAFTE_GLOBAL_DRVWARN	(1<<6)	/* drive warning */
	u_int8_t	flags2;
#define SAFTE_GLOBAL_ARRAYFAIL	(1<<0)	/* array failure */
#define SAFTE_GLOBAL_ARRAYWARN	(1<<1)	/* array warning */
#define SAFTE_GLOBAL_LOCK	(1<<2)	/* enclosure lock */
#define SAFTE_GLOBAL_IDENTIFY	(1<<3)	/* identify enclosure */
	u_int8_t	flags3;
	u_int8_t	reserved[13];
};


/*  perform slot operation */
struct safte_slotop {
	u_int8_t	opcode;		/* SAFTE_WRITE_SLOTOP */
	u_int8_t	slot;
	u_int8_t	flags;
#define SAFTE_SLOTOP_OPERATE	(1<<0)	/* prepare for operation */
#define SAFTE_SLOTOP_INSREM	(1<<1)	/* prepare for insert/removal */
#define SAFTE_SLOTOP_IDENTIFY	(1<<2)	/* identify */
	u_int8_t	reserved[61];	/* zero these */
} __packed;

#endif /* _SCSI_SAFTE_H */
