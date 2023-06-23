/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_TERMBITS_COMMON_H
#define __ASM_GENERIC_TERMBITS_COMMON_H

typedef unsigned char	cc_t;
typedef unsigned int	speed_t;

/* c_iflag bits */
#define IGNBRK	0x001			/* Ignore break condition */
#define BRKINT	0x002			/* Signal interrupt on break */
#define IGNPAR	0x004			/* Ignore characters with parity errors */
#define PARMRK	0x008			/* Mark parity and framing errors */
#define INPCK	0x010			/* Enable input parity check */
#define ISTRIP	0x020			/* Strip 8th bit off characters */
#define INLCR	0x040			/* Map NL to CR on input */
#define IGNCR	0x080			/* Ignore CR */
#define ICRNL	0x100			/* Map CR to NL on input */
#define IXANY	0x800			/* Any character will restart after stop */

/* c_oflag bits */
#define OPOST	0x01			/* Perform output processing */
#define OCRNL	0x08
#define ONOCR	0x10
#define ONLRET	0x20
#define OFILL	0x40
#define OFDEL	0x80

/* c_cflag bit meaning */
/* Common CBAUD rates */
#define     B0		0x00000000	/* hang up */
#define    B50		0x00000001
#define    B75		0x00000002
#define   B110		0x00000003
#define   B134		0x00000004
#define   B150		0x00000005
#define   B200		0x00000006
#define   B300		0x00000007
#define   B600		0x00000008
#define  B1200		0x00000009
#define  B1800		0x0000000a
#define  B2400		0x0000000b
#define  B4800		0x0000000c
#define  B9600		0x0000000d
#define B19200		0x0000000e
#define B38400		0x0000000f
#define EXTA		B19200
#define EXTB		B38400

#define ADDRB		0x20000000	/* address bit */
#define CMSPAR		0x40000000	/* mark or space (stick) parity */
#define CRTSCTS		0x80000000	/* flow control */

#define IBSHIFT		16		/* Shift from CBAUD to CIBAUD */

/* tcflow() ACTION argument and TCXONC use these */
#define TCOOFF		0		/* Suspend output */
#define TCOON		1		/* Restart suspended output */
#define TCIOFF		2		/* Send a STOP character */
#define TCION		3		/* Send a START character */

/* tcflush() QUEUE_SELECTOR argument and TCFLSH use these */
#define TCIFLUSH	0		/* Discard data received but not yet read */
#define TCOFLUSH	1		/* Discard data written but not yet sent */
#define TCIOFLUSH	2		/* Discard all pending data */

#endif /* __ASM_GENERIC_TERMBITS_COMMON_H */
