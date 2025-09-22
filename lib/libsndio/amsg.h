/*	$OpenBSD: amsg.h,v 1.16 2024/05/24 15:16:09 ratchov Exp $	*/
/*
 * Copyright (c) 2008 Alexandre Ratchov <alex@caoua.org>
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
#ifndef AMSG_H
#define AMSG_H

#include <stdint.h>

/*
 * unix-domain socket name is:
 *
 * DIR [ '-' UID ] '/' FILE UNIT
 *
 * example: "/tmp/sndio-1000/sock0"
 *
 */
#define SOCKPATH_DIR	"/tmp/sndio"
#define SOCKPATH_FILE	"sock"
#define SOCKPATH_MAX	(1 +		\
	sizeof(SOCKPATH_DIR) - 1 +	\
	sizeof(char) +			\
	sizeof(int) * 3 +		\
	sizeof(char) +			\
	sizeof(SOCKPATH_FILE) - 1 +	\
	sizeof(int) * 3)

/*
 * server TCP base port number
 */
#define AUCAT_PORT		11025

/*
 * limits
 */
#define AMSG_CTL_NAMEMAX	16	/* max name length */
#define AMSG_CTL_DISPLAYMAX	32	/* max display string length */

/*
 * Size of the struct amsg_ctl_desc expected by clients
 * using the AMSG_CTLSUB_OLD request
 */
#define AMSG_OLD_DESC_SIZE	92

/*
 * WARNING: since the protocol may be simultaneously used by static
 * binaries or by different versions of a shared library, we are not
 * allowed to change the packet binary representation in a backward
 * incompatible way.
 *
 * Especially, make sure the amsg_xxx structures are not larger
 * than 32 bytes.
 */
struct amsg {
#define AMSG_ACK	0	/* ack for START/STOP */
#define AMSG_GETPAR	1	/* get the current parameters */
#define AMSG_SETPAR	2	/* set the current parameters */
#define AMSG_START	3	/* request the server to start the stream */
#define AMSG_STOP	4	/* request the server to stop the stream */
#define AMSG_DATA	5	/* data block */
#define AMSG_FLOWCTL	6	/* feedback about buffer usage */
#define AMSG_MOVE	7	/* position changed */
#define AMSG_SETVOL	9	/* set volume */
#define AMSG_HELLO	10	/* say hello, check versions and so ... */
#define AMSG_BYE	11	/* ask server to drop connection */
#define AMSG_AUTH	12	/* send authentication cookie */
#define AMSG_CTLSUB_OLD	13	/* amsg_ctl_desc with no "display" attribute */
#define AMSG_CTLSET	14	/* set control value */
#define AMSG_CTLSYNC	15	/* end of controls descriptions */
#define AMSG_CTLSUB	16	/* ondesc/onctl subscription */
	uint32_t cmd;
	uint32_t __pad;
	union {
		struct amsg_par {
			uint8_t legacy_mode;	/* compat for old libs */
			uint8_t xrun;		/* one of above */
			uint8_t bps;		/* bytes per sample */
			uint8_t bits;		/* actually used bits */
			uint8_t msb;		/* 1 if MSB justified */
			uint8_t le;		/* 1 if little endian */
			uint8_t sig;		/* 1 if signed */
			uint8_t __pad1;
			uint16_t pchan;		/* play channels */
			uint16_t rchan;		/* record channels */
			uint32_t rate;		/* frames per second */
			uint32_t bufsz;		/* total buffered frames */
			uint32_t round;
			uint32_t appbufsz;	/* client side bufsz */
			uint32_t _reserved[1];	/* for future use */
		} par;
		struct amsg_data {
#define AMSG_DATAMAX	0x1000
			uint32_t size;
		} data;
		struct amsg_stop {
			uint8_t drain;
		} stop;
		struct amsg_ts {
			int32_t delta;
		} ts;
		struct amsg_vol {
			uint32_t ctl;
		} vol;
		struct amsg_hello {
			uint16_t mode;		/* bitmap of MODE_XXX */
#define AMSG_VERSION	7
			uint8_t version;	/* protocol version */
#define AMSG_NODEV	255
			uint8_t devnum;		/* device number */
			uint32_t id;		/* client id */
#define AMSG_OPTMAX	12
			char opt[AMSG_OPTMAX];	/* profile name */
			char who[12];		/* hint for leases */
		} hello;
		struct amsg_auth {
#define AMSG_COOKIELEN	16
			uint8_t cookie[AMSG_COOKIELEN];
		} auth;
		struct amsg_ctlsub {
			uint8_t desc, val;
		} ctlsub;
		struct amsg_ctlset {
			uint16_t addr, val;
		} ctlset;
	} u;
};

/*
 * network representation of sioctl_node structure
 */
struct amsg_ctl_node {
	char name[AMSG_CTL_NAMEMAX];
	int16_t unit;
	uint8_t __pad[2];
};

/*
 * network representation of sioctl_desc structure
 */
struct amsg_ctl_desc {
	struct amsg_ctl_node node0;	/* affected channels */
	struct amsg_ctl_node node1;	/* dito for AMSG_CTL_{SEL,VEC,LIST} */
	char func[AMSG_CTL_NAMEMAX];	/* parameter function name */
	char group[AMSG_CTL_NAMEMAX];	/* group of the control */
	uint8_t type;			/* see sioctl_desc structure */
	uint8_t __pad1[1];
	uint16_t addr;			/* control address */
	uint16_t maxval;
	uint16_t curval;
	uint32_t __pad2[4];
	char display[AMSG_CTL_DISPLAYMAX];	/* free-format hint */
};

/*
 * Initialize an amsg structure: fill all fields with 0xff, so the read
 * can test which fields were set.
 */
#define AMSG_INIT(m) do { memset((m), 0xff, sizeof(struct amsg)); } while (0)

/*
 * Since the structure is memset to 0xff, the MSB can be used to check
 * if any field was set.
 */
#define AMSG_ISSET(x) (((x) & (1 << (8 * sizeof(x) - 1))) == 0)

#endif /* !defined(AMSG_H) */
