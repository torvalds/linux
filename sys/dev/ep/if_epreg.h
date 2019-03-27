/*-
 * Copyright (c) 1993 Herb Peyerl (hpeyerl@novatel.ca) All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2. The name
 * of the author may not be used to endorse or promote products derived from
 * this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * DELAY_MULTIPLE: How much to boost "base" delays, except
 * for the inter-bit delays in get_eeprom_data.  A cyrix Media GX needed this.
 */
#define DELAY_MULTIPLE 10
#define BIT_DELAY_MULTIPLE 10

/*
 * Some global constants
 */
#define TX_INIT_RATE         16
#define TX_INIT_MAX_RATE     64
#define RX_INIT_LATENCY      64
#define RX_INIT_EARLY_THRESH 208/* not less than MINCLSIZE */
#define RX_NEXT_EARLY_THRESH 500

#define EEPROMSIZE      0x40
#define MAX_EEPROMBUSY  1000
#define EP_LAST_TAG     0xd7
#define EP_MAX_BOARDS   16
#define EP_IOSIZE	16	/* 16 bytes of I/O space used. */

/*
 * Commands to read/write EEPROM trough EEPROM command register (Window 0,
 * Offset 0xa)
 */
#define EEPROM_CMD_RD    0x0080	/* Read:  Address required (5 bits) */
#define EEPROM_CMD_WR    0x0040	/* Write: Address required (5 bits) */
#define EEPROM_CMD_ERASE 0x00c0	/* Erase: Address required (5 bits) */
#define EEPROM_CMD_EWEN  0x0030	/* Erase/Write Enable: No data required */

#define EEPROM_BUSY		(1<<15)
#define EEPROM_TST_MODE		(1<<14)

/*
 * Some short functions, worth to let them be a macro
 */
#define is_eeprom_busy(sc) (CSR_READ_2(sc, EP_W0_EEPROM_COMMAND)&EEPROM_BUSY)
#define GO_WINDOW(sc, x)	CSR_WRITE_2(sc, EP_COMMAND, WINDOW_SELECT|(x))

/**************************************************************************
 *									  *
 * These define the EEPROM data structure.  They are used in the probe
 * function to verify the existence of the adapter after having sent
 * the ID_Sequence.
 *
 **************************************************************************/

#define EEPROM_NODE_ADDR_0	0x0	/* Word */
#define EEPROM_NODE_ADDR_1	0x1	/* Word */
#define EEPROM_NODE_ADDR_2	0x2	/* Word */
#define EEPROM_PROD_ID		0x3	/* 0x9[0-f]50 */
#define EEPROM_MFG_DATE         0x4	/* Manufacturing date */
#define EEPROM_MFG_DIVSION      0x5	/* Manufacturing division */
#define EEPROM_MFG_PRODUCT      0x6	/* Product code */
#define EEPROM_MFG_ID		0x7	/* 0x6d50 */
#define EEPROM_ADDR_CFG		0x8	/* Base addr */
#define ADDR_CFG_EISA		0x1f
#define ADDR_CFG_MASK		0x1f
#define EEPROM_RESOURCE_CFG	0x9	/* IRQ. Bits 12-15 */
#define EEPROM_OEM_ADDR0        0xa
#define EEPROM_OEM_ADDR1        0xb
#define EEPROM_OEM_ADDR2        0xc
#define EEPROM_SOFTINFO         0xd
#define EEPROM_COMPAT           0xe
#define EEPROM_SOFTINFO2        0xf
#define EEPROM_CAP              0x10
#define CAP_ISA		0x2083
#define CAP_PCMCIA		0x2082
#define EEPROM_INT_CONFIG_0	0x12
#define EEPROM_INT_CONFIG_1	0x13
/* RAM Partition TX FIFO/RX FIFO */
#define ICW1_RAM_PART_MASK	0x03
#define ICW1_RAM_PART_35	0x00	/* 2:5 (only legal if RAM size == 000b
					 * default power-up/reset */
#define ICW1_RAM_PART_13	0x01	/* 1:3 (only legal if RAM size ==
					 * 000b) */
#define ICW1_RAM_PART_11	0x10	/* 1:1		 */
#define ICW1_RAM_PART_RESV	0x11	/* Reserved	 */
/* ISA Adapter Selection */
#define ICW1_IAS_MASK		0x0c
#define ICW1_IAS_DIS		0x00	/* Both mechanisms disabled (default) */
#define ICW1_IAS_ISA		0x04	/* ISA contention only */
#define ICW1_IAS_PNP		0x08	/* ISA Plug and Play only */
#define ICW1_IAS_BOTH		0x0c	/* Both mechanisms enabled */

#define EEPROM_CHECKSUM_EL3     0x17

/**************************************************************************
 *									  *
 * These are the registers for the 3Com 3c509 and their bit patterns when *
 * applicable.  They have been taken out the "EtherLink III Parallel      *
 * Tasking EISA and ISA Technical Reference" "Beta Draft 10/30/92" manual *
 * from 3com.								  *
 *									  *
 **************************************************************************/

#define EP_COMMAND		0x0e	/* Write. BASE+0x0e is always a
					 * command reg. */
#define EP_STATUS		0x0e	/* Read. BASE+0x0e is always status
					 * reg. */
#define EP_WINDOW		0x0f	/* Read. BASE+0x0f is always window
					 * reg. */
/*
 * Window 0 registers. Setup.
 */
/* Write */
#define EP_W0_EEPROM_DATA	0x0c
#define EP_W0_EEPROM_COMMAND	0x0a
#define EP_W0_RESOURCE_CFG	0x08
#define EP_W0_ADDRESS_CFG	0x06
#define EP_W0_CONFIG_CTRL	0x04
	/* Read */
#define EP_W0_PRODUCT_ID	0x02
#define EP_W0_MFG_ID		0x00

/*
 * Window 1 registers. Operating Set.
 */
/* Write */
#define EP_W1_TX_PIO_WR_2	0x02
#define EP_W1_TX_PIO_WR_1	0x00
/* Read */
#define EP_W1_FREE_TX		0x0c
#define EP_W1_TX_STATUS		0x0b	/* byte */
#define EP_W1_TIMER		0x0a	/* byte */
#define EP_W1_RX_STATUS		0x08
#define EP_W1_RX_PIO_RD_2	0x02
#define EP_W1_RX_PIO_RD_1	0x00

/*
 * Window 2 registers. Station Address Setup/Read
 */
/* Read/Write */
#define EP_W2_PHY_MGMT		0x0c
#define EP_W2_ALT_EEPROM	0x0a
#define EP_W2_ADDR_5		0x05
#define EP_W2_ADDR_4		0x04
#define EP_W2_ADDR_3		0x03
#define EP_W2_ADDR_2		0x02
#define EP_W2_ADDR_1		0x01
#define EP_W2_ADDR_0		0x00

/*
 * Window 3 registers.  FIFO Management.
 */
/* Read */
#define EP_W3_FREE_TX		0x0c
#define EP_W3_FREE_RX		0x0a
#define EP_W3_OPTIONS		0x08

/*
 * Window 4 registers. Diagnostics.
 */
/* Read/Write */
#define EP_W4_MEDIA_TYPE	0x0a
#define EP_W4_CTRLR_STATUS	0x08
#define EP_W4_NET_DIAG		0x06
#define EP_W4_FIFO_DIAG		0x04
#define EP_W4_HOST_DIAG		0x02
#define EP_W4_TX_DIAG		0x00

/*
 * Window 5 Registers.  Results and Internal status.
 */
/* Read */
#define EP_W5_READ_0_MASK	0x0c
#define EP_W5_INTR_MASK		0x0a
#define EP_W5_RX_FILTER		0x08
#define EP_W5_RX_EARLY_THRESH	0x06
#define EP_W5_TX_AVAIL_THRESH	0x02
#define EP_W5_TX_START_THRESH	0x00

/*
 * Window 6 registers. Statistics.
 */
/* Read/Write */
#define TX_TOTAL_OK		0x0c
#define RX_TOTAL_OK		0x0a
#define TX_DEFERRALS		0x08
#define RX_FRAMES_OK		0x07
#define TX_FRAMES_OK		0x06
#define RX_OVERRUNS		0x05
#define TX_COLLISIONS		0x04
#define TX_AFTER_1_COLLISION	0x03
#define TX_AFTER_X_COLLISIONS	0x02
#define TX_NO_SQE		0x01
#define TX_CD_LOST		0x00

/****************************************
 *
 * Register definitions.
 *
 ****************************************/

/*
 * Command parameter that disables threshold interrupts
 *   PIO (3c509) cards use 2044.  The fifo word-oriented and 2044--2047 work.
 *  "busmastering" cards need 8188.
 * The implicit two-bit upshift done by busmastering cards means
 * a value of 2047 disables threshold interrupts on both.
 */
#define EP_THRESH_DISABLE    2047

/*
 * Command register. All windows.
 *
 * 16 bit register.
 *     15-11:  5-bit code for command to be executed.
 *     10-0:   11-bit arg if any. For commands with no args;
 *	      this can be set to anything.
 */
#define GLOBAL_RESET		(u_short) 0x0000	/* Wait at least 1ms
							 * after issuing */
#define WINDOW_SELECT		(u_short) (0x1<<11)
#define START_TRANSCEIVER	(u_short) (0x2<<11)	/* Read ADDR_CFG reg to
							 * determine whether
							 * this is needed. If
							 * so; wait 800 uSec
							 * before using trans-
							 * ceiver. */
#define RX_DISABLE		(u_short) (0x3<<11)	/* state disabled on
							 * power-up */
#define RX_ENABLE		(u_short) (0x4<<11)
#define RX_RESET		(u_short) (0x5<<11)
#define RX_DISCARD_TOP_PACK	(u_short) (0x8<<11)
#define TX_ENABLE		(u_short) (0x9<<11)
#define TX_DISABLE		(u_short) (0xa<<11)
#define TX_RESET		(u_short) (0xb<<11)
#define REQ_INTR		(u_short) (0xc<<11)
/*
 * The following C_* acknowledge the various interrupts. Some of them don't
 * do anything.  See the manual.
 */
#define ACK_INTR		(u_short) (0x6800)
#	define C_INTR_LATCH	(u_short) (ACK_INTR|0x1)
#	define C_CARD_FAILURE	(u_short) (ACK_INTR|0x2)
#	define C_TX_COMPLETE	(u_short) (ACK_INTR|0x4)
#	define C_TX_AVAIL	(u_short) (ACK_INTR|0x8)
#	define C_RX_COMPLETE	(u_short) (ACK_INTR|0x10)
#	define C_RX_EARLY	(u_short) (ACK_INTR|0x20)
#	define C_INT_RQD		(u_short) (ACK_INTR|0x40)
#	define C_UPD_STATS	(u_short) (ACK_INTR|0x80)
#define SET_INTR_MASK		(u_short) (0xe<<11)
#define SET_RD_0_MASK		(u_short) (0xf<<11)
#define SET_RX_FILTER		(u_short) (0x10<<11)
#	define FIL_INDIVIDUAL	(u_short) (0x1)
#	define FIL_MULTICAST     (u_short) (0x02)
#	define FIL_BRDCST        (u_short) (0x04)
#	define FIL_PROMISC       (u_short) (0x08)
#define SET_RX_EARLY_THRESH	(u_short) (0x11<<11)
#define SET_TX_AVAIL_THRESH	(u_short) (0x12<<11)
#define SET_TX_START_THRESH	(u_short) (0x13<<11)
#define STATS_ENABLE		(u_short) (0x15<<11)
#define STATS_DISABLE		(u_short) (0x16<<11)
#define STOP_TRANSCEIVER	(u_short) (0x17<<11)
#define TX_PLL_ENABLE		(u_short) (0x18<<11)
#define TX_PLL_DISABLE		(u_short) (0x19<<11)
#define POWER_UP		(u_short) (0x1b<<11)
#define POWER_DOWN		(u_short) (0x1b<<11)
#define POWER_AUTO		(u_short) (0x1b<<11)

/*
 * Status register. All windows.
 *
 *     15-13:  Window number(0-7).
 *     12:     Command_in_progress.
 *     11:     reserved.
 *     10:     reserved.
 *     9:      reserved.
 *     8:      reserved.
 *     7:      Update Statistics.
 *     6:      Interrupt Requested.
 *     5:      RX Early.
 *     4:      RX Complete.
 *     3:      TX Available.
 *     2:      TX Complete.
 *     1:      Adapter Failure.
 *     0:      Interrupt Latch.
 */
#define S_INTR_LATCH		(u_short) (0x1)
#define S_CARD_FAILURE		(u_short) (0x2)
#define S_TX_COMPLETE		(u_short) (0x4)
#define S_TX_AVAIL		(u_short) (0x8)
#define S_RX_COMPLETE		(u_short) (0x10)
#define S_RX_EARLY		(u_short) (0x20)
#define S_INT_RQD		(u_short) (0x40)
#define S_UPD_STATS		(u_short) (0x80)
#define S_MASK	(u_short) 0xFF	/* mask of S_* */
#define S_5_INTS                (S_CARD_FAILURE|S_TX_COMPLETE|\
				 S_TX_AVAIL|S_RX_COMPLETE|S_RX_EARLY)
#define S_COMMAND_IN_PROGRESS	(u_short) (0x1000)

#define EP_BUSY_WAIT(sc) while (CSR_READ_2(sc, EP_STATUS) & S_COMMAND_IN_PROGRESS)

/* Address Config. Register.
 * Window 0/Port 06
 */

#define ACF_CONNECTOR_BITS	14
#define ACF_CONNECTOR_UTP	0
#define ACF_CONNECTOR_AUI	1
#define ACF_CONNECTOR_BNC	3

/* Resource configuration register.
 * Window 0/Port 08
 *
 */

#define SET_IRQ(sc, irq) CSR_WRITE_2((sc), EP_W0_RESOURCE_CFG, \
			((CSR_READ_2((sc), EP_W0_RESOURCE_CFG) & 0x0fff) | \
			((u_short)(irq)<<12))  )	/* set IRQ i */

/*
 * FIFO Registers.
 * RX Status. Window 1/Port 08
 *
 *     15:     Incomplete or FIFO empty.
 *     14:     1: Error in RX Packet   0: Incomplete or no error.
 *     13-11:  Type of error.
 *	      1000 = Overrun.
 *	      1011 = Run Packet Error.
 *	      1100 = Alignment Error.
 *	      1101 = CRC Error.
 *	      1001 = Oversize Packet Error (>1514 bytes)
 *	      0010 = Dribble Bits.
 *	      (all other error codes, no errors.)
 *
 *     10-0:   RX Bytes (0-1514)
 */
#define ERR_RX_INCOMPLETE  (u_short) (0x1<<15)
#define ERR_RX		   (u_short) (0x1<<14)
#define ERR_RX_OVERRUN 	   (u_short) (0x8<<11)
#define ERR_RX_RUN_PKT	   (u_short) (0xb<<11)
#define ERR_RX_ALIGN	   (u_short) (0xc<<11)
#define ERR_RX_CRC	   (u_short) (0xd<<11)
#define ERR_RX_OVERSIZE	   (u_short) (0x9<<11)
#define ERR_RX_DRIBBLE	   (u_short) (0x2<<11)

/*
 * FIFO Registers.
 * TX Status. Window 1/Port 0B
 *
 *   Reports the transmit status of a completed transmission. Writing this
 *   register pops the transmit completion stack.
 *
 *   Window 1/Port 0x0b.
 *
 *     7:      Complete
 *     6:      Interrupt on successful transmission requested.
 *     5:      Jabber Error (TP Only, TX Reset required. )
 *     4:      Underrun (TX Reset required. )
 *     3:      Maximum Collisions.
 *     2:      TX Status Overflow.
 *     1-0:    Undefined.
 *
 */
#define TXS_COMPLETE		0x80
#define TXS_SUCCES_INTR_REQ		0x40
#define TXS_JABBER		0x20
#define TXS_UNDERRUN		0x10
#define TXS_MAX_COLLISION	0x8
#define TXS_STATUS_OVERFLOW	0x4

/*
 * Configuration control register.
 * Window 0/Port 04
 */
/* Read */
#define IS_AUI 				(1<<13)
#define IS_BNC 				(1<<12)
#define IS_UTP 				(1<<9)
/* Write */
#define ENABLE_DRQ_IRQ			0x0001
#define W0_P4_CMD_RESET_ADAPTER       0x4
#define W0_P4_CMD_ENABLE_ADAPTER      0x1

/*
 * Media type and status.
 * Window 4/Port 0A
 */
#define JABBER_GUARD_ENABLE	0x40
#define LINKBEAT_ENABLE		0x80
#define	ENABLE_UTP		(JABBER_GUARD_ENABLE | LINKBEAT_ENABLE)
#define DISABLE_UTP		0x0
#define	MT_CSD			0x0004	/* CRC Strip disable */
#define	MT_SQE			0x0008	/* SQE Enable */
#define	MT_C0			0x0010	/* Collison */
#define	MT_CS			0x0020	/* Carrier Sense */
#define MT_J			0x0200	/* Jabber detected */
#define MT_PL			0x0400	/* Plarity Inverted */
#define MT_LB			0x0800	/* Link Beat detected */
#define MT_SQ			0x1000	/* SQE Present */
#define MT_IN			0x2000	/* Reserved */
#define MT_CE			0x4000	/* Coax */
#define MT_TPE			0x8000

/*
 * Misc defines for various things.
 */
#define ACTIVATE_ADAPTER_TO_CONFIG 	0xff	/* to the id_port */
#define MFG_ID 				0x6d50	/* in EEPROM and W0
						 * ADDR_CONFIG */
#define PROD_ID 			0x9150

#define AUI 				0x1
#define BNC 				0x2
#define UTP 				0x4

#define RX_BYTES_MASK			(u_short) (0x07ff)
