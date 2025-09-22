/*	$OpenBSD: i82596reg.h,v 1.6 2008/09/01 17:30:56 deraadt Exp $	*/
/*	$NetBSD: i82586reg.h,v 1.7 1998/02/28 01:07:45 pk Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1992, University of Vermont and State Agricultural College.
 * Copyright (c) 1992, Garrett A. Wollman.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	Vermont and State Agricultural College and Garrett A. Wollman.
 * 4. Neither the name of the University nor the name of the author
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR AUTHOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Intel 82586/82596 Ethernet chip
 * Register, bit, and structure definitions.
 *
 * Written by GAW with reference to the Clarkson Packet Driver code for this
 * chip written by Russ Nelson and others.
 */

/*
 * NOTE, the structure definitions in here are for reference only.
 * We use integer offsets exclusively to access the i82586/596 data structures.
 */

/*
 * This is the master configuration block.
 * It tells the hardware where all the rest of the stuff is.
 *-
struct __ie_sys_conf_ptr {
	u_int16_t	mbz;			// must be zero
	u_int8_t	ie_bus_use;		// true if 8-bit only
	u_int8_t	mbz2[5];		// must be zero
	u_int32_t	ie_iscp_ptr;		// 24-bit physaddr of ISCP
} __packed;

 */
#define IE_SCP_SZ		12	/* keep paragraph alignment */
#define IE_SCP_BUS_USE(base)	((base) + 2)
#define IE_SCP_TEST(base)	((base) + 4)
#define IE_SCP_ISCP(base)	((base) + 8)

/*
 * Note that this is wired in hardware; the SCP is always located here, no
 * matter what.
 */
#define IE_SCP_ADDR 0xfffff4

/*
 * The tells the hardware where all the rest of the stuff is, too.
 * FIXME: some of these should be re-commented after we figure out their
 * REAL function.
 *-
struct __ie_int_sys_conf_ptr {
	u_int8_t	ie_busy;	// zeroed after init
	u_int8_t	mbz;
	u_int16_t	ie_scb_offset;	// 16-bit physaddr of next struct
	caddr_t		ie_base;	// 24-bit physaddr for all 16-bit vars
} __packed;
 */
#define IE_ISCP_SZ		16
#define IE_ISCP_BUSY(base)	((base) + 0)
#define IE_ISCP_SCB(base)	((base) + 2)
#define IE_ISCP_BASE(base)	((base) + 4)

/*
 * SYSBUS byte definitions
 * bit0 is not checked
 * bit6 is always 1
 */
#define	IE_SYSBUS_82586		0x00
#define	IE_SYSBUS_32SEG		0x02
#define	IE_SYSBUS_LINEAR	0x04
#define	IE_SYSBUS_TRG		0x08
#define	IE_SYSBUS_LOCK		0x10
#define	IE_SYSBUS_INTLOW	0x20
#define	IE_SYSBUS_BE		0x80

/*
 * This FINALLY tells the hardware what to do and where to put it.
 *-
struct __ie_sys_ctl_block {
	u_int16_t ie_status;		// status word
	u_int16_t ie_command;		// command word
	u_int16_t ie_command_list;	// 16-pointer to command block list
	u_int16_t ie_recv_list;		// 16-pointer to receive frame list
	u_int16_t ie_err_crc;		// CRC errors
	u_int16_t ie_err_align;		// Alignment errors
	u_int16_t ie_err_resource;	// Resource errors
	u_int16_t ie_err_overrun;	// Overrun errors
} __packed;
 */
#define IE_SCB_SZ		16
#define IE_SCB_STATUS(base)	((base) + 0)
#define IE_SCB_CMD(base)	((base) + 2)
#define IE_SCB_CMDLST(base)	((base) + 4)
#define IE_SCB_RCVLST(base)	((base) + 6)
#define IE_SCB_ERRCRC(base)	((base) + 8)
#define IE_SCB_ERRALN(base)	((base) + 10)
#define IE_SCB_ERRRES(base)	((base) + 12)
#define IE_SCB_ERROVR(base)	((base) + 14)

/* Command values */
#define IE_RUC_MASK	0x0070	/* mask for RU command */
#define IE_RUC_NOP	0	/* for completeness */
#define IE_RUC_START	0x0010	/* start receive unit command */
#define IE_RUC_RESUME	0x0020	/* resume a suspended receiver command */
#define IE_RUC_SUSPEND	0x0030	/* suspend receiver command */
#define IE_RUC_ABORT	0x0040	/* abort current receive operation */

#define IE_CUC_MASK	0x0700	/* mask for CU command */
#define IE_CUC_NOP	0	/* included for completeness */
#define IE_CUC_START	0x0100	/* do-command command */
#define IE_CUC_RESUME	0x0200	/* resume a suspended cmd list */
#define IE_CUC_SUSPEND	0x0300	/* suspend current command */
#define IE_CUC_ABORT	0x0400	/* abort current command */

#define IE_ACK_COMMAND	0xf000	/* mask for ACK command */
#define IE_ACK_CX	0x8000	/* ack IE_ST_CX */
#define IE_ACK_FR	0x4000	/* ack IE_ST_FR */
#define IE_ACK_CNA	0x2000	/* ack IE_ST_CNA */
#define IE_ACK_RNR	0x1000	/* ack IE_ST_RNR */

#define IE_ACTION_COMMAND(x) (((x) & IE_CUC_MASK) == IE_CUC_START)
				/* is this command an action command? */

#define IE_ST_WHENCE	0xf000	/* mask for cause of interrupt */
#define IE_ST_CX	0x8000	/* command with I bit completed */
#define IE_ST_FR	0x4000	/* frame received */
#define IE_ST_CNA	0x2000	/* all commands completed */
#define IE_ST_RNR	0x1000	/* receive not ready */

#define IE_CUS_MASK	0x0700	/* mask for command unit status */
#define IE_CUS_ACTIVE	0x0200	/* command unit is active */
#define IE_CUS_SUSPEND	0x0100	/* command unit is suspended */

#define IE_RUS_MASK	0x0070	/* mask for receiver unit status */
#define IE_RUS_SUSPEND	0x0010	/* receiver is suspended */
#define IE_RUS_NOSPACE	0x0020	/* receiver has no resources */
#define IE_RUS_READY	0x0040	/* receiver is ready */

#define	IE_ST_BITS \
	"\020\020cx\017fr\016cna\015rnr\012cact\011csusp\07rrdy\06rnsp\05rsusp"

/*
 * This is filled in partially by the chip, partially by us.
 *-
struct __ie_recv_frame_desc {
	u_int16_t	ie_fd_status;	// status for this frame
	u_int16_t	ie_fd_last;	// end of frame list flag
	u_int16_t	ie_fd_next;	// 16-pointer to next RFD
	u_int16_t	ie_fd_buf_desc;	// 16-pointer to list of buffer descs
	struct __ie_en_addr dest;	// destination ether
	struct __ie_en_addr src;	// source ether
	u_int16_t	ie_length;	// 802 length/Ether type
	u_short		mbz;		// must be zero
} __packed;
 */
#define IE_RFRAME_SZ			24
#define IE_RFRAME_ADDR(base,i)		((base) + (i) * 64)
#define IE_RFRAME_STATUS(b,i)		(IE_RFRAME_ADDR(b,i) + 0)
#define IE_RFRAME_LAST(b,i)		(IE_RFRAME_ADDR(b,i) + 2)
#define IE_RFRAME_NEXT(b,i)		(IE_RFRAME_ADDR(b,i) + 4)
#define IE_RFRAME_BUFDESC(b,i)		(IE_RFRAME_ADDR(b,i) + 6)
#define IE_RFRAME_EDST(b,i)		(IE_RFRAME_ADDR(b,i) + 8)
#define IE_RFRAME_ESRC(b,i)		(IE_RFRAME_ADDR(b,i) + 14)
#define IE_RFRAME_ELEN(b,i)		(IE_RFRAME_ADDR(b,i) + 20)

/* "last" bits */
#define IE_FD_EOL	0x8000	/* last rfd in list */
#define IE_FD_SUSP	0x4000	/* suspend RU after receipt */

/* status field bits */
#define IE_FD_COMPLETE	0x8000	/* frame is complete */
#define IE_FD_BUSY	0x4000	/* frame is busy */
#define IE_FD_OK	0x2000	/* frame is ok */
#define IE_FD_CRC	0x0800	/* CRC error */
#define IE_FD_ALGN	0x0400	/* Alignment error */
#define IE_FD_RNR	0x0200	/* receiver out of resources here */
#define IE_FD_OVR	0x0100	/* DMA overrun */
#define IE_FD_SHORT	0x0080	/* Short frame */
#define IE_FD_NOEOF	0x0040	/* no EOF (?) */
#define IE_FD_ERRMASK		/* all error bits */ \
	(IE_FD_CRC|IE_FD_ALGN|IE_FD_RNR|IE_FD_OVR|IE_FD_SHORT|IE_FD_NOEOF)
#define IE_FD_STATUSBITS	\
	"\20\20complt\17busy\16ok\14crc\13algn\12rnr\11ovr\10short\7noeof"

/*
 * linked list of buffers...
 *-
struct __ie_recv_buf_desc {
	u_int16_t	ie_rbd_status;	// status for this buffer
	u_int16_t	ie_rbd_next;	// 16-pointer to next RBD
	caddr_t		ie_rbd_buffer;	// 24-pointer to buffer for this RBD
	u_int16_t	ie_rbd_length;	// length of the buffer
	u_int16_t	mbz;		// must be zero
} __packed;
 */
#define IE_RBD_SZ			12
#define IE_RBD_ADDR(base,i)		((base) + (i) * 32)
#define IE_RBD_STATUS(b,i)		(IE_RBD_ADDR(b,i) + 0)
#define IE_RBD_NEXT(b,i)		(IE_RBD_ADDR(b,i) + 2)
#define IE_RBD_BUFADDR(b,i)		(IE_RBD_ADDR(b,i) + 4)
#define IE_RBD_BUFLEN(b,i)		(IE_RBD_ADDR(b,i) + 8)

/* RBD status fields */
#define IE_RBD_LAST	0x8000		/* last buffer */
#define IE_RBD_USED	0x4000		/* this buffer has data */
#define IE_RBD_CNTMASK	0x3fff		/* byte count of buffer data */

/* RDB `End Of List' flag; encoded in `buffer length' field */
#define IE_RBD_EOL	0x8000		/* last buffer */


/*
 * All commands share this in common.
 *-
struct __ie_cmd_common {
	u_int16_t ie_cmd_status;	// status of this command 
	u_int16_t ie_cmd_cmd;		// command word
	u_int16_t ie_cmd_link;		// link to next command
} __packed;
 */
#define IE_CMD_COMMON_SZ		6
#define IE_CMD_COMMON_STATUS(base)	((base) + 0)
#define IE_CMD_COMMON_CMD(base)		((base) + 2)
#define IE_CMD_COMMON_LINK(base)	((base) + 4)

#define IE_STAT_COMPL	0x8000	/* command is completed */
#define IE_STAT_BUSY	0x4000	/* command is running now */
#define IE_STAT_OK	0x2000	/* command completed successfully */
#define IE_STAT_ABORT	0x1000  /* command was aborted */
#define	IE_STAT_BITS	"\020\020complete\017busy\016ok\015abort"

#define IE_CMD_NOP	0x0000	/* NOP */
#define IE_CMD_IASETUP	0x0001	/* initial address setup */
#define IE_CMD_CONFIG	0x0002	/* configure command */
#define IE_CMD_MCAST	0x0003	/* multicast setup command */
#define IE_CMD_XMIT	0x0004	/* transmit command */
#define IE_CMD_TDR	0x0005	/* time-domain reflectometer command */
#define IE_CMD_DUMP	0x0006	/* dump command */
#define IE_CMD_DIAGNOSE	0x0007	/* diagnostics command */

#define IE_CMD_LAST	0x8000	/* this is the last command in the list */
#define IE_CMD_SUSPEND	0x4000	/* suspend CU after this command */
#define IE_CMD_INTR	0x2000	/* post an interrupt after completion */

/*
 * No-op commands; just like COMMON but "indexable"
 */
#define IE_CMD_NOP_SZ			IE_CMD_COMMON_SZ
#define IE_CMD_NOP_ADDR(base,i)		((base) + (i) * 32)
#define IE_CMD_NOP_STATUS(b,i)		(IE_CMD_NOP_ADDR(b,i) + 0)
#define IE_CMD_NOP_CMD(b,i)		(IE_CMD_NOP_ADDR(b,i) + 2)
#define IE_CMD_NOP_LINK(b,i)		(IE_CMD_NOP_ADDR(b,i) + 4)


/*
 * This is the command to transmit a frame.
 *-
struct __ie_xmit_cmd {
	struct __ie_cmd_common	com;		// common part
#define __ie_xmit_status	com.ie_cmd_status

	u_int16_t	ie_xmit_desc;		// pointer to buffer descriptor
	struct __ie_en_addr ie_xmit_addr;	// destination address
	u_int16_t	ie_xmit_length;		// 802.3 length/Ether type field
} __packed;
 */
#define IE_CMD_XMIT_SZ			(IE_CMD_COMMON_SZ + 10)
#define IE_CMD_XMIT_ADDR(base,i)	((base) + (i) * 32)
#define IE_CMD_XMIT_STATUS(b,i)		(IE_CMD_XMIT_ADDR(b,i) + 0)
#define IE_CMD_XMIT_CMD(b,i)		(IE_CMD_XMIT_ADDR(b,i) + 2)
#define IE_CMD_XMIT_LINK(b,i)		(IE_CMD_XMIT_ADDR(b,i) + 4)
#define IE_CMD_XMIT_DESC(b,i)		\
	(IE_CMD_XMIT_ADDR(b,i) + IE_CMD_COMMON_SZ + 0)
#define IE_CMD_XMIT_EADDR(b,i)		\
	(IE_CMD_XMIT_ADDR(b,i) + IE_CMD_COMMON_SZ + 2)
#define IE_CMD_XMIT_LEN(b,i)		\
	(IE_CMD_XMIT_ADDR(b,i) + IE_CMD_COMMON_SZ + 8)

#define IE_XS_MAXCOLL  	0x000f	/* number of collisions during transmit */
#define IE_XS_EXCMAX	0x0020	/* exceeded maximum number of collisions */
#define IE_XS_SQE	0x0040	/* SQE positive */
#define IE_XS_DEFERRED	0x0080	/* transmission deferred */
#define IE_XS_UNDERRUN	0x0100	/* DMA underrun */
#define IE_XS_LOSTCTS	0x0200	/* Lost CTS */
#define IE_XS_NOCARRIER	0x0400	/* No Carrier */
#define	IE_XS_LATECOLL	0x0800	/* Late collision */

#define	IE_XS_BITS \
	"\020\014ltcoll\013nocar\012lostcts\011underrun\010xsdef\07sqe\06excoll"

/*
 * This is a buffer descriptor for a frame to be transmitted.
 *-
struct __ie_xmit_buf {
	u_int16_t ie_xmit_flags;	// see below
	u_int16_t ie_xmit_next;		// 16-pointer to next desc
	caddr_t ie_xmit_buf;		// 24-pointer to the actual buffer
} __packed;
 */
#define IE_XBD_SZ			8
#define IE_XBD_ADDR(base,i)		((base) + (i) * 32)
#define IE_XBD_FLAGS(b,i)		(IE_XBD_ADDR(b,i) + 0)
#define IE_XBD_NEXT(b,i)		(IE_XBD_ADDR(b,i) + 2)
#define IE_XBD_BUF(b,i)			(IE_XBD_ADDR(b,i) + 4)

#define IE_TBD_EOL	0x8000		/* this TBD is the last one */
#define IE_TBD_CNTMASK	0x3fff		/* The rest of the `flags' word
					   is actually the length. */


/*
 * Multicast setup command.
 *-
struct __ie_mcast_cmd {
	struct __ie_cmd_common	com;	// common part
#define ie_mcast_status		com.ie_cmd_status

	// size (in bytes) of multicast addresses
	u_int16_t		ie_mcast_bytes;
	struct __ie_en_addr ie_mcast_addrs[IE_MAXMCAST + 1];// space for them
} __packed;
 */
#define IE_CMD_MCAST_SZ			(IE_CMD_COMMON_SZ + 2 /* + XXX */)
#define IE_CMD_MCAST_BYTES(base)	((base) + IE_CMD_COMMON_SZ + 0)
#define IE_CMD_MCAST_MADDR(base)	((base) + IE_CMD_COMMON_SZ + 2)

/*
 * Time Domain Reflectometer command.
 *-
struct __ie_tdr_cmd {
	struct __ie_cmd_common com;	// common part
#define ie_tdr_status com.ie_cmd_status
	u_short ie_tdr_time;		// error bits and time
} __packed;
 */
#define IE_CMD_TDR_SZ		(IE_CMD_COMMON_SZ + 2)
#define IE_CMD_TDR_TIME(base)	((base) + IE_CMD_COMMON_SZ + 0)

#define IE_TDR_SUCCESS	0x8000	/* TDR succeeded without error */
#define IE_TDR_XCVR	0x4000	/* detected a transceiver problem */
#define IE_TDR_OPEN	0x2000	/* detected an open */
#define IE_TDR_SHORT	0x1000	/* TDR detected a short */
#define IE_TDR_TIME	0x07ff	/* mask for reflection time */

#define	IE_TDR_BITS	"\020\020success\017xcvr\016open\015short"

/*
 * Initial Address Setup command
 *-
struct __ie_iasetup_cmd {
	struct __ie_cmd_common com;
#define ie_iasetup_status com.ie_cmd_status
	struct __ie_en_addr ie_address;
} __packed;
 */
#define IE_CMD_IAS_SZ		(IE_CMD_COMMON_SZ + 6)
#define IE_CMD_IAS_EADDR(base)	((base) + IE_CMD_COMMON_SZ + 0)

/*
 * Configuration command
 *-
struct __ie_config_cmd {
	struct __ie_cmd_common com;	// common part
#define ie_config_status com.ie_cmd_status

	u_int8_t ie_config_count;	// byte count (0x0c)
	u_int8_t ie_fifo;		// fifo (8)
	u_int8_t ie_save_bad;		// save bad frames (0x40)
	u_int8_t ie_addr_len;		// address length (0x2e) (AL-LOC == 1)
	u_int8_t ie_priority;		// priority and backoff (0x0)
	u_int8_t ie_ifs;		// inter-frame spacing (0x60)
	u_int8_t ie_slot_low;		// slot time, LSB (0x0)
	u_int8_t ie_slot_high;		// slot time, MSN, and retries (0xf2)
	u_int8_t ie_promisc;		// 1 if promiscuous, else 0
	u_int8_t ie_crs_cdt;		// CSMA/CD parameters (0x0)
	u_int8_t ie_min_len;		// min frame length (0x40)
	u_int8_t ie_junk;		// stuff for 82596 (0xff)
} __packed;
 */
#define IE_CMD_CFG_SZ			(IE_CMD_COMMON_SZ + 12)
#define IE_CMD_CFG_CNT(base)		((base) + IE_CMD_COMMON_SZ + 0)
#define IE_CMD_CFG_FIFO(base)		((base) + IE_CMD_COMMON_SZ + 1)
#define IE_CMD_CFG_SAVEBAD(base)	((base) + IE_CMD_COMMON_SZ + 2)
#define IE_CMD_CFG_ADDRLEN(base)	((base) + IE_CMD_COMMON_SZ + 3)
#define IE_CMD_CFG_PRIORITY(base)	((base) + IE_CMD_COMMON_SZ + 4)
#define IE_CMD_CFG_IFS(base)		((base) + IE_CMD_COMMON_SZ + 5)
#define IE_CMD_CFG_SLOT_LOW(base)	((base) + IE_CMD_COMMON_SZ + 6)
#define IE_CMD_CFG_SLOT_HIGH(base)	((base) + IE_CMD_COMMON_SZ + 7)
#define IE_CMD_CFG_PROMISC(base)	((base) + IE_CMD_COMMON_SZ + 8)
#define IE_CMD_CFG_CRSCDT(base)		((base) + IE_CMD_COMMON_SZ + 9)
#define IE_CMD_CFG_MINLEN(base)		((base) + IE_CMD_COMMON_SZ + 10)
#define IE_CMD_CFG_JUNK(base)		((base) + IE_CMD_COMMON_SZ + 11)
