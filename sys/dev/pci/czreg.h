/*	$OpenBSD: czreg.h,v 1.3 2022/01/09 05:42:45 jsg Exp $	*/
/*	$NetBSD$	*/

/*-
 * Copyright (c) 2000 Zembu Labs, Inc.
 * All rights reserved.
 *
 * Author: Jason R. Thorpe <thorpej@zembu.com>
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
 *	This product includes software developed by Zembu Labs, Inc.
 * 4. Neither the name of Zembu Labs nor the names of its employees may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ZEMBU LABS, INC. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAR-
 * RANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DIS-
 * CLAIMED.  IN NO EVENT SHALL ZEMBU LABS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Register and firmware communication definitions for the Cyclades
 * Z series of multi-port serial adapters.
 */

/*
 * The Cyclades-Z series is an intelligent multi-port serial controller
 * comprised of:
 *
 *	- PLX PCI9060ES PCI bus interface
 *	- Xilinx XC5204 FPGA
 *	- IDT R3052 MIPS CPU
 *
 * Communication is performed by modifying structures in board local
 * RAM or in host RAM.  We define offsets into these structures so
 * that either access method may be used.
 *
 * The Cyclades-Z comes in three basic flavors:
 *
 *	- Cyclades-8Zo rev 1 -- This is an older 8-port board with no
 *	  FPGA.
 *
 *	- Cyclades-8Zo rev 2 -- This is the newer 8-port board, which
 *	  uses an octopus cable.
 *
 *	- Cyclades-Ze -- This is the top-of-the-line of the Cyclades
 *	  multiport serial controllers.  It uses a SCSI-2 cable to
 *	  connect the card to a rack-mountable serial expansion box
 *	  (1U high).  Each box has 16 RJ45 serial ports, and up to
 *	  4 boxes can be chained together, for a total of 64 ports.
 *	  Up to 2 boxes can be used without an extra power supply.
 *	  Boxes 3 and 4 require their own external power supply,
 *	  otherwise the firmware will refuse to start (as it cannot
 *	  communicate with the UARTs in the boxes).
 *
 * The 8Zo flavors have not been tested, tho the programming interface
 * is identical (except for the firmware load phase of the 8Zo rev 1;
 * no FPGA load is done in that case), so they should work.
 */

/*
 * PLX Local Address Base values for the board RAM and FPGA registers.
 *
 * These values are specific to the Cyclades-Z.
 */
#define	LOCAL_ADDR0_RAM		(0x00000000 | LASBA_ENABLE)
#define	LOCAL_ADDR0_FPGA	(0x14000000 | LASBA_ENABLE)

/*
 * PLX Mailbox0 values.
 *
 * These values are specific to the Cyclades-Z.
 */
#define	MAILBOX0_8Zo_V1		0	/* Cyclades-8Zo ver. 1 */
#define	MAILBOX0_8Zo_V2		1	/* Cyclades-8Zo ver. 2 */
#define	MAILBOX0_Ze_V1		2	/* Cyclades-Ze ver. 1 */

/*
 * Bits in the PLX INIT_CTRL register.
 *
 * These values are specific to the Cyclades-Z.
 */
#define	CONTROL_FPGA_LOADED	CONTROL_GPI

/*
 * FPGA registers on the 8Zo boards.
 */
#define	FPGA_ID			0x00	/* FPGA ID */
#define	FPGA_VERSION		0x04	/* FPGA version */
#define	FPGA_CPU_START		0x08	/* CPU start */
#define	FPGA_CPU_STOP		0x0c	/* CPU stop */
#define	FPGA_MISC		0x10	/* Misc. register */
#define	FPGA_IDT_MODE		0x14	/* IDT MIPS R3000 mode */
#define	FPGA_UART_IRQ_STAT	0x18	/* UART interrupt status */
#define	FPGA_CLEAR_TIMER0_IRQ	0x1c	/* clear timer 0 interrupt */
#define	FPGA_CLEAR_TIMER1_IRQ	0x20	/* clear timer 1 interrupt */
#define	FPGA_CLEAR_TIMER2_IRQ	0x24	/* clear timer 3 interrupt */
#define	FPGA_TEST		0x28	/* test register */
#define	FPGA_TEST_COUNT		0x2c	/* test count register */
#define	FPGA_TIMER_SELECT	0x30	/* timer select */
#define	FPGA_PR_UART_IRQ_STAT	0x34	/* prioritized UART interrupt status */
#define	FPGA_RAM_WAIT_STATE	0x38	/* RAM wait state */
#define	FPGA_UART_WAIT_STATE	0x3c	/* UART wait state */
#define	FPGA_TIMER_WAIT_STATE	0x40	/* timer wait state */
#define	FPGA_ACK_WAIT_STATE	0x44	/* ACK wait state */

/*
 * FPGA registers on the Ze boards.  Note that the important registers
 * (FPGA_ID, FPGA_VERSION, FPGA_CPU_START, FPGA_CPU_STOP) are all in the
 * same place as on the 8Zo boards, and have the same meanings.
 */
#define	FPGA_ZE_ID		0x00	/* FPGA ID */
#define	FPGA_ZE_VERSION		0x04	/* FPGA version */
#define	FPGA_ZE_CPU_START	0x08	/* CPU start */
#define	FPGA_ZE_CPU_STOP	0x0c	/* CPU stop */
#define	FPGA_ZE_CTRL		0x10	/* CPU control */
#define	FPGA_ZE_ZBUS_WAIT	0x14	/* Z-Bus wait state */
#define	FPGA_ZE_TIMER_DIV	0x18	/* timer divisor */
#define	FPGA_ZE_TIMER_IRQ_ACK	0x1c	/* timer interrupt ACK */

/*
 * Values for FPGA ID.
 */
#define	FPGA_ID_8Zo_V1		0x95	/* Cyclades-8Zo ver. 1 */
#define	FPGA_ID_8Zo_V2		0x84	/* Cyclades-8Zo ver. 2 */
#define	FPGA_ID_Ze_V1		0x89	/* Cyclades-Ze ver. 1 */

/*
 * Values for Cyclades-Ze timer divisor.
 */
#define	ZE_TIMER_DIV_1M		0x00
#define	ZE_TIMER_DIV_256K	0x01
#define	ZE_TIMER_DIV_128K	0x02
#define	ZE_TIMER_DIV_32K	0x03

/*
 * Firmware interface starts here.
 *
 * These values are valid for the following Cyclades-Z firmware:
 *
 *	@(#) Copyright (c) Cyclades Corporation, 1996, 1999
 *	@(#) ZFIRM Cyclades-Z/PCI Firmware V_3.3.1 09/24/99
 */

/*
 * Structure of the firmware header.
 */
#define	ZFIRM_MAX_BLOCKS	16	/* max. # of firmware/FPGA blocks */
struct zfirm_header {
	u_int8_t zfh_name[64];
	u_int8_t zfh_date[32];
	u_int8_t zfh_aux[32];
	u_int32_t zfh_nconfig;
	u_int32_t zfh_configoff;
	u_int32_t zfh_nblocks;
	u_int32_t zfh_blockoff;
	u_int32_t zfh_reserved[9];
} __packed;

struct zfirm_config {
	u_int8_t zfc_name[64];
	u_int32_t zfc_mailbox;
	u_int32_t zfc_function;
	u_int32_t zfc_nblocks;
	u_int32_t zfc_blocklist[ZFIRM_MAX_BLOCKS];
} __packed;

#define	ZFC_FUNCTION_NORMAL	0	/* normal operation */
#define	ZFC_FUNCTION_TEST	1	/* test mode operation */

struct zfirm_block {
	u_int32_t zfb_type;
	u_int32_t zfb_fileoff;
	u_int32_t zfb_ramoff;
	u_int32_t zfb_size;
} __packed;

#define	ZFB_TYPE_FIRMWARE	0	/* MIPS firmware */
#define	ZFB_TYPE_FPGA		1	/* FPGA code */

#define	ZFIRM_MAX_CHANNELS	64	/* max. # channels per board */

/*
 * Firmware ID structure, which the firmware sets up after it boots.
 */
#define	ZFIRM_SIG_OFF	   0x00000180	/* offset of signature in board RAM */
#define	ZFIRM_CTRLADDR_OFF 0x00000184	/* offset of offset of control
					   structure */
#define	ZFIRM_SIG	0x5557465A	/* ZFIRM signature */
#define	ZFIRM_HLT	0x59505B5C	/* Halt due to power problem */
#define	ZFIRM_RST	0x56040674	/* Firmware reset */

/*
 * The firmware control structures are made up of the following:
 *
 *	BOARD CONTROL (64 bytes)
 *	CHANNEL CONTROL (96 bytes * ZFIRM_MAX_CHANNELS)
 *	BUFFER CONTROL (64 bytes * ZFIRM_MAX_CHANNELS)
 */

#define	ZFIRM_BRDCTL_SIZE	64
#define	ZFIRM_CHNCTL_SIZE	96
#define	ZFIRM_BUFCTL_SIZE	64

#define	ZFIRM_CHNCTL_OFF(chan, reg)					\
	(ZFIRM_BRDCTL_SIZE + ((chan) * ZFIRM_CHNCTL_SIZE) + (reg))
#define	ZFIRM_BUFCTL_OFF(chan, reg)					\
	(ZFIRM_CHNCTL_OFF(ZFIRM_MAX_CHANNELS, 0) + 			\
	 ((chan) * ZFIRM_BUFCTL_SIZE) + (reg))

/*
 * Offsets in the BOARD CONTROL structure.
 */
	/* static info provided by MIPS */
#define	BRDCTL_NCHANNEL		0x00	/* number of channels */
#define	BRDCTL_FWVERSION	0x04	/* firmware version */
	/* static info provided by driver */
#define	BRDCTL_C_OS		0x08	/* operating system ID */
#define	BRDCTL_DRVERSION	0x0c	/* driver version */
	/* board control area */
#define	BRDCTL_INACTIVITY	0x10	/* inactivity control */
	/* host to firmware commands */
#define	BRDCTL_HCMD_CHANNEL	0x14	/* channel number */
#define	BRDCTL_HCMD_PARAM	0x18	/* parameter */
	/* firmware to host commands */
#define	BRDCTL_FWCMD_CHANNEL	0x1c	/* channel number */
#define	BRDCTL_FWCMD_PARAM	0x20	/* parameter */
#define	BRDCTL_INT_QUEUE_OFF	0x24	/* offset to INT_QUEUE structure */

/*
 * Offsets in the CHANNEL CONTROL structure.
 */
#define	CHNCTL_OP_MODE		0x00	/* operation mode */
#define	CHNCTL_INTR_ENABLE	0x04	/* interrupt making for UART */
#define	CHNCTL_SW_FLOW		0x08	/* SW flow control */
#define	CHNCTL_FLOW_STATUS	0x0c	/* output flow status */
#define	CHNCTL_COMM_BAUD	0x10	/* baud rate -- numerically specified */
#define	CHNCTL_COMM_PARITY	0x14	/* parity */
#define	CHNCTL_COMM_DATA_L	0x18	/* data length/stop */
#define	CHNCTL_COMM_FLAGS	0x1c	/* other flags */
#define	CHNCTL_HW_FLOW		0x20	/* HW flow control */
#define	CHNCTL_RS_CONTROL	0x24	/* RS-232 outputs */
#define	CHNCTL_RS_STATUS	0x28	/* RS-232 inputs */
#define	CHNCTL_FLOW_XON		0x2c	/* XON character */
#define	CHNCTL_FLOW_XOFF	0x30	/* XOFF character */
#define	CHNCTL_HW_OVERFLOW	0x34	/* HW overflow counter */
#define	CHNCTL_SW_OVERFLOW	0x38	/* SW overflow counter */
#define	CHNCTL_COMM_ERROR	0x3c	/* frame/parity error counter */
#define	CHNCTL_ICHAR		0x40	/* special interrupt character */

/*
 * Offsets in the BUFFER CONTROL structure.
 */
#define	BUFCTL_FLAG_DMA		0x00	/* buffers are in Host memory */
#define	BUFCTL_TX_BUFADDR	0x04	/* address of Tx buffer */
#define	BUFCTL_TX_BUFSIZE	0x08	/* size of Tx buffer */
#define	BUFCTL_TX_THRESHOLD	0x0c	/* Tx low water mark */
#define	BUFCTL_TX_GET		0x10	/* tail index Tx buf */
#define	BUFCTL_TX_PUT		0x14	/* head index Tx buf */
#define	BUFCTL_RX_BUFADDR	0x18	/* address of Rx buffer */
#define	BUFCTL_RX_BUFSIZE	0x1c	/* size of Rx buffer */
#define	BUFCTL_RX_THRESHOLD	0x20	/* Rx high water mark */
#define	BUFCTL_RX_GET		0x24	/* tail index Rx buf */
#define	BUFCTL_RX_PUT		0x28	/* head index Rx buf */

/* Values for operating system ID (BOARD CONTROL) */
#define	C_OS_SVR3		0x00000010	/* generic SVR3 */
#define	C_OS_XENIX		0x00000011	/* SCO XENIX */
#define	C_OS_SCO		0x00000012	/* SCO SVR3 */
#define	C_OS_SVR4		0x00000020	/* generic SVR4 */
#define	C_OS_UXWARE		0x00000021	/* UnixWare */
#define	C_OS_LINUX		0x00000030	/* Linux */
#define	C_OS_SOLARIS		0x00000040	/* Solaris */
#define	C_OS_BSD		0x00000050	/* generic BSD */
#define	C_OS_DOS		0x00000070	/* generic DOS */
#define	C_OS_NT			0x00000080	/* Windows NT */
#define	C_OS_OS2		0x00000090	/* IBM OS/2 */
#define	C_OS_MACOS		0x000000a0	/* MacOS */
#define	C_OS_AIX		0x000000b0	/* IBM AIX */

/* Values for op_mode (CHANNEL CONTROL) */
#define	C_CH_DISABLE		0x00000000	/* channel is disabled */
#define	C_CH_TXENABLE		0x00000001	/* channel Tx enabled */
#define	C_CH_RXENABLE		0x00000002	/* channel Rx enabled */
#define	C_CH_ENABLE		0x00000003	/* channel Tx/Rx enabled */
#define	C_CH_LOOPBACK		0x00000004	/* Loopback mode */

/* Values for comm_parity (CHANNEL CONTROL) */
#define	C_PR_NONE		0x00000000	/* None */
#define	C_PR_ODD		0x00000001	/* Odd */
#define	C_PR_EVEN		0x00000002	/* Even */
#define	C_PR_MARK		0x00000004	/* Mark */
#define	C_PR_SPACE		0x00000008	/* Space */
#define	C_PR_PARITY		0x000000ff
#define	C_PR_DISCARD		0x00000100	/* discard char with
						   frame/parity error */
#define	C_PR_IGNORE		0x00000200	/* ignore frame/par error */

/* Values for comm_data_l (CHANNEL CONTROL) */
#define	C_DL_CS5		0x00000001
#define	C_DL_CS6		0x00000002
#define	C_DL_CS7		0x00000004
#define	C_DL_CS8		0x00000008
#define	C_DL_CS			0x0000000f
#define	C_DL_1STOP		0x00000010
#define	C_DL_15STOP		0x00000020
#define	C_DL_2STOP		0x00000040
#define	C_DL_STOP		0x000000f0

/* Values for intr_enable (CHANNEL CONTROL) */
#define	C_IN_DISABLE		0x00000000	/* zero, disable interrupts */
#define	C_IN_TXBEMPTY		0x00000001	/* tx buffer empty */
#define	C_IN_TXLOWWM		0x00000002	/* tx buffer below LWM */
#define	C_IN_TXFEMPTY		0x00000004	/* tx buffer + FIFO +
						   shift reg. empty */
#define	C_IN_RXHIWM		0x00000010	/* rx buffer above HWM */
#define	C_IN_RXNNDT		0x00000020	/* rx no new data timeout */
#define	C_IN_MDCD		0x00000100	/* modem DCD change */
#define	C_IN_MDSR		0x00000200	/* modem DSR change */
#define	C_IN_MRI		0x00000400	/* modem RI change */
#define	C_IN_MCTS		0x00000800	/* modem CTS change */
#define	C_IN_RXBRK		0x00001000	/* Break received */
#define	C_IN_PR_ERROR		0x00002000	/* parity error */
#define	C_IN_FR_ERROR		0x00004000	/* frame error */
#define	C_IN_OVR_ERROR		0x00008000	/* overrun error */
#define	C_IN_RXOFL		0x00010000	/* RX buffer overflow */
#define	C_IN_IOCTLW		0x00020000	/* I/O control w/ wait */
#define	C_IN_MRTS		0x00040000	/* modem RTS drop */
#define	C_IN_ICHAR		0x00080000	/* special intr. char
						   received */

/* Values for flow control (CHANNEL CONTROL) */
#define	C_FL_OXX		0x00000001	/* output Xon/Xoff flow
						   control */
#define	C_FL_IXX		0x00000002	/* input Xon/Xoff flow
						   control */
#define	C_FL_OIXANY		0x00000004	/* output Xon/Xoff (any xon) */
#define	C_FL_SWFLOW		0x0000000f

/* Values for flow status (CHANNEL CONTROL) */
#define	C_FS_TXIDLE		0x00000000	/* no Tx data in the buffer
						   or UART */
#define	C_FS_SENDING		0x00000001      /* UART is sending data */
#define C_FS_SWFLOW		0x00000002      /* Tx is stopped by received
						   Xoff */

/* Values for RS-232 signals (CHANNEL CONTROL) */
#define	C_RS_PARAM		0x80000000	/* indicates presence of
						   parameter in IOCTL command */
#define	C_RS_RTS		0x00000001	/* RTS */
#define	C_RS_DTR		0x00000004	/* DTR */
#define	C_RS_DCD		0x00000100	/* CD */
#define	C_RS_DSR		0x00000200	/* DSR */
#define	C_RS_RI			0x00000400	/* RI */
#define	C_RS_CTS		0x00000800	/* CTS */

/* Commands Host <--> Board */
#define	C_CM_RESET		0x01	/* resets/flushes buffers */
#define	C_CM_IOCTL		0x02	/* re-reads CH_CTRL */
#define	C_CM_IOCTLW		0x03	/* re-reads CH_CTRL, intr when done */
#define	C_CM_IOCTLM		0x04	/* RS-232 outputs change */
#define	C_CM_SENDXOFF		0x10	/* sends Xoff */
#define	C_CM_SENDXON		0x11	/* sends Xon */
#define	C_CM_CLFLOW		0x12	/* Clears flow control (resume) */
#define	C_CM_SENDBRK		0x41	/* sends break */
#define	C_CM_INTBACK		0x42	/* Interrupt back */
#define	C_CM_SET_BREAK		0x43	/* Tx break on */
#define	C_CM_CLR_BREAK		0x44	/* Tx break off */
#define	C_CM_CMD_DONE		0x45	/* Previous command done */
#define	C_CM_INTBACK2		0x46	/* Alternate Interrupt back */
#define	C_CM_TINACT		0x51	/* sets inactivity detection */
#define	C_CM_IRQ_ENBL		0x52	/* enables generation of interrupts */
#define	C_CM_IRQ_DSBL		0x53	/* disables generation of interrupts */
#define	C_CM_ACK_ENBL		0x54	/* enables acknowledged interrupt
					   mode */
#define	C_CM_ACK_DSBL		0x55	/* disables acknowledged intr mode */
#define	C_CM_FLUSH_RX		0x56	/* flushes Rx buffer */
#define	C_CM_FLUSH_TX		0x57	/* flushes Tx buffer */
#define	C_CM_Q_ENABLE		0x58	/* enables queue access from the
					   driver */
#define	C_CM_Q_DISABLE		0x59	/* disables queue access from the
					   driver */
#define	C_CM_TXBEMPTY		0x60	/* Tx buffer is empty */
#define	C_CM_TXLOWWM		0x61	/* Tx buffer low water mark */
#define	C_CM_RXHIWM		0x62	/* Rx buffer high water mark */
#define	C_CM_RXNNDT		0x63	/* rx no new data timeout */
#define	C_CM_TXFEMPTY		0x64	/* Tx buffer, FIFO and shift reg.
					   are empty */
#define	C_CM_ICHAR		0x65	/* Special Interrupt Character
					   received */
#define	C_CM_MDCD		0x70	/* modem DCD change */
#define	C_CM_MDSR		0x71	/* modem DSR change */
#define	C_CM_MRI		0x72	/* modem RI change */
#define	C_CM_MCTS		0x73	/* modem CTS change */
#define	C_CM_MRTS		0x74	/* modem RTS drop */
#define	C_CM_RXBRK		0x84	/* Break received */
#define	C_CM_PR_ERROR		0x85	/* Parity error */
#define	C_CM_FR_ERROR		0x86	/* Frame error */
#define	C_CM_OVR_ERROR		0x87	/* Overrun error */
#define	C_CM_RXOFL		0x88	/* RX buffer overflow */
#define	C_CM_CMDERROR		0x90	/* command error */
#define	C_CM_FATAL		0x91	/* fatal error */
#define	C_CM_HW_RESET		0x92	/* reset board */
