/*	$OpenBSD: uowreg.h,v 1.5 2006/10/08 20:04:23 grange Exp $	*/

/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
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

#ifndef _DEV_USB_UOWREG_H_
#define _DEV_USB_UOWREG_H_

/*
 * Maxim/Dallas DS2490 USB 1-Wire adapter register definitions.
 */

/* USB core interface */
#define DS2490_USB_CONFIG	1	/* configuration */
#define DS2490_USB_IFACE	0	/* interface */

/* Command type codes */
#define DS2490_CONTROL_CMD	0x00	/* control */
#define DS2490_COMM_CMD		0x01	/* communication */
#define DS2490_MODE_CMD		0x02	/* mode */

/* Control command codes */
#define DS2490_CTL_RESET_DEVICE		0x0000
#define DS2490_CTL_START_EXE		0x0001
#define DS2490_CTL_RESUME_EXE		0x0002
#define DS2490_CTL_HALT_EXE_IDLE	0x0003
#define DS2490_CTL_HALT_EXE_DONE	0x0004
#define DS2490_CTL_FLUSH_COMM_CMDS	0x0007
#define DS2490_CTL_FLUSH_RCV_BUFFER	0x0008
#define DS2490_CTL_FLUSH_XMT_BUFFER	0x0009
#define DS2490_CTL_GET_COMM_CMDS	0x000a

/* Communication command codes */
#define DS2490_COMM_SET_DURATION	0x0012
#define DS2490_COMM_PULSE		0x0030
#define DS2490_COMM_1WIRE_RESET		0x0042
#define DS2490_COMM_BIT_IO		0x0020
#define DS2490_COMM_BYTE_IO		0x0052
#define DS2490_COMM_BLOCK_IO		0x0074
#define DS2490_COMM_MATCH_ACCESS	0x0064
#define DS2490_COMM_READ_STRAIGHT	0x0080
#define DS2490_COMM_DO_RELEASE		0x6092
#define DS2490_COMM_SET_PATH		0x00a2
#define DS2490_COMM_WRITE_SRAM_PAGE	0x00b2
#define DS2490_COMM_WRITE_EPROM		0x00c4
#define DS2490_COMM_READ_CRC_PROT_PAGE	0x00d4
#define DS2490_COMM_READ_REDIR_PAGE_CRC	0x21e4
#define DS2490_COMM_SEARCH_ACCESS	0x00f4

/* Communication command embedded command parameter bits */
#define DS2490_BIT_IM			(1 << 0)
#define DS2490_BIT_D			(1 << 3)
#define DS2490_BIT_CH			(1 << 3)
#define DS2490_BIT_R			(1 << 3)
#define DS2490_BIT_SE			(1 << 3)
#define DS2490_BIT_SM			(1 << 3)
#define DS2490_BIT_TYPE			(1 << 3)
#define DS2490_BIT_Z			(1 << 3)
#define DS2490_BIT_RST			(1 << 8)
#define DS2490_BIT_ICP			(1 << 9)
#define DS2490_BIT_NTF			(1 << 10)
#define DS2490_BIT_F			(1 << 11)
#define DS2490_BIT_SPU			(1 << 12)
#define DS2490_BIT_DT			(1 << 13)
#define DS2490_BIT_CIB			(1 << 14)
#define DS2490_BIT_PS			(1 << 14)
#define DS2490_BIT_PST			(1 << 14)
#define DS2490_BIT_RTS			(1 << 14)

/* Mode command codes */
#define DS2490_MOD_PULSE_EN		0x0000
#define DS2490_MOD_SPEED_CHANGE_EN	0x0001
#define DS2490_MOD_1WIRE_SPEED		0x0002
#define DS2490_MOD_STRONG_PU_DURATION	0x0003
#define DS2490_MOD_PULLDOWN_SLEWRATE	0x0004
#define DS2490_MOD_PROG_PULSE_DURATION	0x0005
#define DS2490_MOD_WRITE1_LOWTIME	0x0006
#define DS2490_MOD_DSOW0_TREC		0x0007

/* State registers */
#define DS2490_ST_BEGIN		0x00
#define DS2490_ST_ENFL		0x00	/* enabled flags */
#define DS2490_ST_ENFL_SPUE		(1 << 0)	/* strong pullup */
#define DS2490_ST_ENFL_PRGE		(1 << 1)	/* programming pulse */
#define DS2490_ST_ENFL_SPCE		(1 << 2)	/* speed change */
#define DS2490_ST_SPEED		0x01	/* bus speed */
#define DS2490_ST_SPUDUR	0x02	/* strong pullup duration */
#define DS2490_ST_PRGDUR	0x03	/* programming pullup duration */
#define DS2490_ST_PDSRC		0x04	/* pulldown slew rate control */
#define DS2490_ST_W1LT		0x05	/* write-1 low time */
#define DS2490_ST_DSO		0x06	/* data sample offset */
#define DS2490_ST_STFL		0x08	/* status flags */
#define DS2490_ST_STFL_SPUA		(1 << 0)	/* strong pullup */
#define DS2490_ST_STFL_PRGA		(1 << 1)	/* programming pulse */
#define DS2490_ST_STFL_12VP		(1 << 2)	/* 12V prog voltage */
#define DS2490_ST_STFL_PMOD		(1 << 3)	/* ext power */
#define DS2490_ST_STFL_HALT		(1 << 4)	/* halted */
#define DS2490_ST_STFL_IDLE		(1 << 5)	/* idle */
#define DS2490_ST_STFL_EP0F		(1 << 7)	/* EP0 FIFO status */
#define DS2490_ST_STFL_BITS		"\020\001SPUA\002PRGA\00312VP\004PMOD\005HALT\006IDLE\010EP0F"

#define DS2490_ST_CC1		0x09	/* communication command byte 1 */
#define DS2490_ST_CC2		0x0a	/* communication command byte 2 */
#define DS2490_ST_CCBUF		0x0b	/* communication command buf status */
#define DS2490_ST_OBUF		0x0c	/* data out buf status */
#define DS2490_ST_IBUF		0x0d	/* data in buf status */
#define DS2490_ST_END		0x0f

/* Result registers */
#define DS2490_RES_BEGIN	0x10
#define DS2490_RES_END		0x1f
#define DS2490_RES_DETECT	0xa5	/* device detect */

#define DS2490_NREGS		(DS2490_RES_END + 1)

#define DS2490_CMDFIFOSIZE	16	/* command FIFO size */
#define DS2490_DATAFIFOSIZE	128	/* data FIFO size */

#endif	/* !_DEV_USB_UOWREG_H_ */
