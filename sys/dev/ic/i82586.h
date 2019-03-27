/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 *
 * $FreeBSD$
 */

/*
 * Intel 82586 Ethernet chip
 * Register, bit, and structure definitions.
 *
 * Written by GAW with reference to the Clarkson Packet Driver code for this
 * chip written by Russ Nelson and others.
 */

struct ie_en_addr {
	u_char data[6];
};

/*
 * This is the master configuration block.  It tells the hardware where all
 * the rest of the stuff is.
 */
struct ie_sys_conf_ptr {
	u_short mbz;			/* must be zero */
	u_char ie_bus_use;		/* true if 8-bit only */
	u_char mbz2[5];			/* must be zero */
	caddr_t ie_iscp_ptr;		/* 24-bit physaddr of ISCP */
};

/*
 * Note that this is wired in hardware; the SCP is always located here, no
 * matter what.
 */
#define IE_SCP_ADDR 0xfffff4

/*
 * The tells the hardware where all the rest of the stuff is, too.
 * FIXME: some of these should be re-commented after we figure out their
 * REAL function.
 */
struct ie_int_sys_conf_ptr {
	u_char ie_busy;			/* zeroed after init */
	u_char mbz;
	u_short ie_scb_offset;		/* 16-bit physaddr of next struct */
	caddr_t ie_base;		/* 24-bit physaddr for all 16-bit vars */
};

/*
 * This FINALLY tells the hardware what to do and where to put it.
 */
struct ie_sys_ctl_block {
	u_short ie_status;		/* status word */
	u_short ie_command;		/* command word */
	u_short ie_command_list;	/* 16-pointer to command block list */
	u_short ie_recv_list;		/* 16-pointer to receive frame list */
	u_short ie_err_crc;		/* CRC errors */
	u_short ie_err_align;		/* Alignment errors */
	u_short ie_err_resource;	/* Resource errors */
	u_short ie_err_overrun;		/* Overrun errors */
};

/* Command values */
#define IE_RU_COMMAND	0x0070	/* mask for RU command */
#define IE_RU_NOP	0	/* for completeness */
#define IE_RU_START	0x0010	/* start receive unit command */
#define IE_RU_ENABLE	0x0020	/* enable receiver command */
#define IE_RU_DISABLE	0x0030	/* disable receiver command */
#define IE_RU_ABORT	0x0040	/* abort current receive operation */

#define IE_CU_COMMAND	0x0700	/* mask for CU command */
#define IE_CU_NOP	0	/* included for completeness */
#define IE_CU_START	0x0100	/* do-command command */
#define IE_CU_RESUME	0x0200	/* resume a suspended cmd list */
#define IE_CU_STOP	0x0300	/* SUSPEND was already taken */
#define IE_CU_ABORT	0x0400	/* abort current command */

#define IE_ACK_COMMAND	0xf000	/* mask for ACK command */
#define IE_ACK_CX	0x8000	/* ack IE_ST_DONE */
#define IE_ACK_FR	0x4000	/* ack IE_ST_RECV */
#define IE_ACK_CNA	0x2000	/* ack IE_ST_ALLDONE */
#define IE_ACK_RNR	0x1000	/* ack IE_ST_RNR */

#define IE_ACTION_COMMAND(x) (((x) & IE_CU_COMMAND) == IE_CU_START)
				/* is this command an action command? */

/* Status values */
#define IE_ST_WHENCE	0xf000	/* mask for cause of interrupt */
#define IE_ST_DONE	0x8000	/* command with I bit completed */
#define IE_ST_RECV	0x4000	/* frame received */
#define IE_ST_ALLDONE	0x2000	/* all commands completed */
#define IE_ST_RNR	0x1000	/* receive not ready */

#define IE_CU_STATUS	0x700	/* mask for command unit status */
#define IE_CU_ACTIVE	0x200	/* command unit is active */
#define IE_CU_SUSPEND	0x100	/* command unit is suspended */

#define IE_RU_STATUS	0x70	/* mask for receiver unit status */
#define IE_RU_SUSPEND	0x10	/* receiver is suspended */
#define IE_RU_NOSPACE	0x20	/* receiver has no resources */
#define IE_RU_READY	0x40	/* reveiver is ready */

/*
 * This is filled in partially by the chip, partially by us.
 */
struct ie_recv_frame_desc {
	u_short ie_fd_status;		/* status for this frame */
	u_short ie_fd_last;		/* end of frame list flag */
	u_short ie_fd_next;		/* 16-pointer to next RFD */
	u_short ie_fd_buf_desc;		/* 16-pointer to list of buffer desc's */
	struct ie_en_addr dest;		/* destination ether */
	struct ie_en_addr src;		/* source ether */
	u_short ie_length;		/* 802 length/Ether type */
	u_short mbz;			/* must be zero */
};

#define IE_FD_LAST	0x8000	/* last rfd in list */
#define IE_FD_SUSP	0x4000	/* suspend RU after receipt */

#define IE_FD_COMPLETE	0x8000	/* frame is complete */
#define IE_FD_BUSY	0x4000	/* frame is busy */
#define IE_FD_OK	0x2000	/* frame is bad */
#define IE_FD_RNR	0x0200	/* receiver out of resources here */

/*
 * linked list of buffers...
 */
struct ie_recv_buf_desc {
	u_short ie_rbd_actual;		/* status for this buffer */
	u_short ie_rbd_next;		/* 16-pointer to next RBD */
	caddr_t ie_rbd_buffer;		/* 24-pointer to buffer for this RBD */
	u_short ie_rbd_length;		/* length of the buffer */
	u_short mbz;			/* must be zero */
};

#define IE_RBD_LAST	0x8000	/* last buffer */
#define IE_RBD_USED	0x4000	/* this buffer has data */
/*
 * All commands share this in common.
 */
struct ie_cmd_common {
	u_short ie_cmd_status;		/* status of this command */
	u_short ie_cmd_cmd;		/* command word */
	u_short ie_cmd_link;		/* link to next command */
};

#define IE_STAT_COMPL	0x8000	/* command is completed */
#define IE_STAT_BUSY	0x4000	/* command is running now */
#define IE_STAT_OK	0x2000	/* command completed successfully */

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
 * This is the command to transmit a frame.
 */
struct ie_xmit_cmd {
	struct ie_cmd_common com;	/* common part */
#define ie_xmit_status com.ie_cmd_status

	u_short ie_xmit_desc;		/* 16-pointer to buffer descriptor */
	struct ie_en_addr ie_xmit_addr; /* destination address */

	u_short ie_xmit_length;		/* 802.3 length/Ether type field */
};

#define IE_XS_MAXCOLL  	0x000f	/* number of collisions during transmit */
#define IE_XS_EXCMAX	0x0020	/* exceeded maximum number of collisions */
#define IE_XS_SQE	0x0040	/* SQE positive */
#define IE_XS_DEFERRED	0x0080	/* transmission deferred */
#define IE_XS_UNDERRUN	0x0100	/* DMA underrun */
#define IE_XS_LOSTCTS	0x0200	/* Lost CTS */
#define IE_XS_NOCARRIER	0x0400	/* No Carrier */
#define IE_XS_LATECOLL	0x0800	/* Late collision */

/*
 * This is a buffer descriptor for a frame to be transmitted.
 */

struct ie_xmit_buf {
	u_short ie_xmit_flags;		/* see below */
	u_short ie_xmit_next;		/* 16-pointer to next desc. */
	caddr_t ie_xmit_buf;		/* 24-pointer to the actual buffer */
};

#define IE_XMIT_LAST 0x8000	/* this TBD is the last one */
/* The rest of the `flags' word is actually the length. */

/*
 * Multicast setup command.
 */

#define MAXMCAST 50		/* must fit in transmit buffer */

struct ie_mcast_cmd {
	struct ie_cmd_common com;	/* common part */
#define ie_mcast_status com.ie_cmd_status

	u_short ie_mcast_bytes;	/* size (in bytes) of multicast addresses */
	struct ie_en_addr ie_mcast_addrs[MAXMCAST + 1];	/* space for them */
};

/*
 * Time Domain Reflectometer command.
 */

struct ie_tdr_cmd {
	struct ie_cmd_common com;	/* common part */
#define ie_tdr_status com.ie_cmd_status

	u_short ie_tdr_time;		/* error bits and time */
};

#define IE_TDR_SUCCESS	0x8000	/* TDR succeeded without error */
#define IE_TDR_XCVR	0x4000	/* detected a transceiver problem */
#define IE_TDR_OPEN	0x2000	/* detected an open */
#define IE_TDR_SHORT	0x1000	/* TDR detected a short */
#define IE_TDR_TIME	0x07ff	/* mask for reflection time */

/*
 * Initial Address Setup command
 */
struct ie_iasetup_cmd {
	struct ie_cmd_common com;
#define ie_iasetup_status com.ie_cmd_status

	struct ie_en_addr ie_address;
};

/*
 * Configuration command
 */
struct ie_config_cmd {
	struct ie_cmd_common com;	/* common part */
#define ie_config_status com.ie_cmd_status

	u_char ie_config_count;		/* byte count (0x0c) */
	u_char ie_fifo;			/* fifo (8) */
	u_char ie_save_bad;		/* save bad frames (0x40) */
	u_char ie_addr_len;		/* address length (0x2e) (AL-LOC == 1) */
	u_char ie_priority;		/* priority and backoff (0x0) */
	u_char ie_ifs;			/* inter-frame spacing (0x60) */
	u_char ie_slot_low;		/* slot time, LSB (0x0) */
	u_char ie_slot_high;		/* slot time, MSN, and retries (0xf2) */
	u_char ie_promisc;		/* 1 if promiscuous, else 0 */
	u_char ie_crs_cdt;		/* CSMA/CD parameters (0x0) */
	u_char ie_min_len;		/* min frame length (0x40) */
	u_char ie_junk;			/* stuff for 82596 (0xff) */
};

/*
 * Here are a few useful functions.  We could have done these as macros,
 * but since we have the inline facility, it makes sense to use that
 * instead.
 */
static __inline void
ie_setup_config(volatile struct ie_config_cmd *cmd,
		int promiscuous, int manchester) {
	cmd->ie_config_count = 0x0c;
	cmd->ie_fifo = 8;
	cmd->ie_save_bad = 0x40;
	cmd->ie_addr_len = 0x2e;
	cmd->ie_priority = 0;
	cmd->ie_ifs = 0x60;
	cmd->ie_slot_low = 0;
	cmd->ie_slot_high = 0xf2;
	cmd->ie_promisc = !!promiscuous | manchester << 2;
	cmd->ie_crs_cdt = 0;
	cmd->ie_min_len = 64;
	cmd->ie_junk = 0xff;
}

static __inline void *
Align(void *ptr) {
	uintptr_t l = (uintptr_t)ptr;
	l = (l + 3) & ~3L;
	return (void *)l;
}

static __inline volatile void *
Alignvol(volatile void *ptr) {
	uintptr_t l = (uintptr_t)ptr;
	l = (l + 3) & ~3L;
	return (volatile void *)l;
}

#if 0
static __inline void
ie_ack(volatile struct ie_sys_ctl_block *scb,
				  u_int mask, int unit,
				  void (*ca)(int)) {
	scb->ie_command = scb->ie_status & mask;
	(*ca)(unit);
}
#endif
