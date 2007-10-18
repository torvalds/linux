/* $Revision: 3.0 $$Date: 1998/11/02 14:20:59 $
 * linux/include/linux/cyclades.h
 *
 * This file was initially written by
 * Randolph Bentson <bentson@grieg.seaslug.org> and is maintained by
 * Ivan Passos <ivan@cyclades.com>.
 *
 * This file contains the general definitions for the cyclades.c driver
 *$Log: cyclades.h,v $
 *Revision 3.1  2002/01/29 11:36:16  henrique
 *added throttle field on struct cyclades_port to indicate whether the
 *port is throttled or not
 *
 *Revision 3.1  2000/04/19 18:52:52  ivan
 *converted address fields to unsigned long and added fields for physical
 *addresses on cyclades_card structure;
 *
 *Revision 3.0  1998/11/02 14:20:59  ivan
 *added nports field on cyclades_card structure;
 *
 *Revision 2.5  1998/08/03 16:57:01  ivan
 *added cyclades_idle_stats structure;
 * 
 *Revision 2.4  1998/06/01 12:09:53  ivan
 *removed closing_wait2 from cyclades_port structure;
 *
 *Revision 2.3  1998/03/16 18:01:12  ivan
 *changes in the cyclades_port structure to get it closer to the 
 *standard serial port structure;
 *added constants for new ioctls;
 *
 *Revision 2.2  1998/02/17 16:50:00  ivan
 *changes in the cyclades_port structure (addition of shutdown_wait and 
 *chip_rev variables);
 *added constants for new ioctls and for CD1400 rev. numbers.
 *
 *Revision 2.1	1997/10/24 16:03:00  ivan
 *added rflow (which allows enabling the CD1400 special flow control 
 *feature) and rtsdtr_inv (which allows DTR/RTS pin inversion) to 
 *cyclades_port structure;
 *added Alpha support
 *
 *Revision 2.0  1997/06/30 10:30:00  ivan
 *added some new doorbell command constants related to IOCTLW and
 *UART error signaling
 *
 *Revision 1.8  1997/06/03 15:30:00  ivan
 *added constant ZFIRM_HLT
 *added constant CyPCI_Ze_win ( = 2 * Cy_PCI_Zwin)
 *
 *Revision 1.7  1997/03/26 10:30:00  daniel
 *new entries at the end of cyclades_port struct to reallocate
 *variables illegally allocated within card memory.
 *
 *Revision 1.6  1996/09/09 18:35:30  bentson
 *fold in changes for Cyclom-Z -- including structures for
 *communicating with board as well modest changes to original
 *structures to support new features.
 *
 *Revision 1.5  1995/11/13 21:13:31  bentson
 *changes suggested by Michael Chastain <mec@duracef.shout.net>
 *to support use of this file in non-kernel applications
 *
 *
 */

#ifndef _LINUX_CYCLADES_H
#define _LINUX_CYCLADES_H

#include <linux/types.h>

struct cyclades_monitor {
        unsigned long           int_count;
        unsigned long           char_count;
        unsigned long           char_max;
        unsigned long           char_last;
};

/*
 * These stats all reflect activity since the device was last initialized.
 * (i.e., since the port was opened with no other processes already having it
 * open)
 */
struct cyclades_idle_stats {
    time_t	   in_use;	/* Time device has been in use (secs) */
    time_t	   recv_idle;	/* Time since last char received (secs) */
    time_t	   xmit_idle;	/* Time since last char transmitted (secs) */
    unsigned long  recv_bytes;	/* Bytes received */
    unsigned long  xmit_bytes;	/* Bytes transmitted */
    unsigned long  overruns;	/* Input overruns */
    unsigned long  frame_errs;	/* Input framing errors */
    unsigned long  parity_errs;	/* Input parity errors */
};

#define CYCLADES_MAGIC  0x4359

#define CYGETMON                0x435901
#define CYGETTHRESH             0x435902
#define CYSETTHRESH             0x435903
#define CYGETDEFTHRESH          0x435904
#define CYSETDEFTHRESH          0x435905
#define CYGETTIMEOUT            0x435906
#define CYSETTIMEOUT            0x435907
#define CYGETDEFTIMEOUT         0x435908
#define CYSETDEFTIMEOUT         0x435909
#define CYSETRFLOW		0x43590a
#define CYGETRFLOW		0x43590b
#define CYSETRTSDTR_INV		0x43590c
#define CYGETRTSDTR_INV		0x43590d
#define CYZSETPOLLCYCLE		0x43590e
#define CYZGETPOLLCYCLE		0x43590f
#define CYGETCD1400VER		0x435910
#define	CYSETWAIT		0x435912
#define	CYGETWAIT		0x435913

/*************** CYCLOM-Z ADDITIONS ***************/

#define CZIOC           ('M' << 8)
#define CZ_NBOARDS      (CZIOC|0xfa)
#define CZ_BOOT_START   (CZIOC|0xfb)
#define CZ_BOOT_DATA    (CZIOC|0xfc)
#define CZ_BOOT_END     (CZIOC|0xfd)
#define CZ_TEST         (CZIOC|0xfe)

#define CZ_DEF_POLL	(HZ/25)

#define MAX_BOARD       4       /* Max number of boards */
#define MAX_DEV         256     /* Max number of ports total */
#define	CYZ_MAX_SPEED	921600

#define	CYZ_FIFO_SIZE	16

#define CYZ_BOOT_NWORDS 0x100
struct CYZ_BOOT_CTRL {
        unsigned short  nboard;
        int             status[MAX_BOARD];
        int             nchannel[MAX_BOARD];
        int             fw_rev[MAX_BOARD];
        unsigned long   offset;
        unsigned long   data[CYZ_BOOT_NWORDS];
};


#ifndef DP_WINDOW_SIZE
/* #include "cyclomz.h" */
/****************** ****************** *******************/
/*
 *	The data types defined below are used in all ZFIRM interface
 *	data structures. They accomodate differences between HW
 *	architectures and compilers.
 */

#include <asm/types.h>

typedef __u64  ucdouble;		/* 64 bits, unsigned */
typedef __u32  uclong;			/* 32 bits, unsigned */
typedef __u16  ucshort;		/* 16 bits, unsigned */
typedef __u8   ucchar;			/* 8 bits, unsigned */

/*
 *	Memory Window Sizes
 */

#define	DP_WINDOW_SIZE		(0x00080000)	/* window size 512 Kb */
#define	ZE_DP_WINDOW_SIZE	(0x00100000)	/* window size 1 Mb (Ze and
						  8Zo V.2 */
#define	CTRL_WINDOW_SIZE	(0x00000080)	/* runtime regs 128 bytes */

/*
 *	CUSTOM_REG - Cyclom-Z/PCI Custom Registers Set. The driver
 *	normally will access only interested on the fpga_id, fpga_version,
 *	start_cpu and stop_cpu.
 */

struct	CUSTOM_REG {
	__u32	fpga_id;		/* FPGA Identification Register */
	__u32	fpga_version;		/* FPGA Version Number Register */
	__u32	cpu_start;		/* CPU start Register (write) */
	__u32	cpu_stop;		/* CPU stop Register (write) */
	__u32	misc_reg;		/* Miscelaneous Register */
	__u32	idt_mode;		/* IDT mode Register */
	__u32	uart_irq_status;	/* UART IRQ status Register */
	__u32	clear_timer0_irq;	/* Clear timer interrupt Register */
	__u32	clear_timer1_irq;	/* Clear timer interrupt Register */
	__u32	clear_timer2_irq;	/* Clear timer interrupt Register */
	__u32	test_register;		/* Test Register */
	__u32	test_count;		/* Test Count Register */
	__u32	timer_select;		/* Timer select register */
	__u32	pr_uart_irq_status;	/* Prioritized UART IRQ stat Reg */
	__u32	ram_wait_state;		/* RAM wait-state Register */
	__u32	uart_wait_state;	/* UART wait-state Register */
	__u32	timer_wait_state;	/* timer wait-state Register */
	__u32	ack_wait_state;		/* ACK wait State Register */
};

/*
 *	RUNTIME_9060 - PLX PCI9060ES local configuration and shared runtime
 *	registers. This structure can be used to access the 9060 registers
 *	(memory mapped).
 */

struct RUNTIME_9060 {
	__u32	loc_addr_range;	/* 00h - Local Address Range */
	__u32	loc_addr_base;	/* 04h - Local Address Base */
	__u32	loc_arbitr;	/* 08h - Local Arbitration */
	__u32	endian_descr;	/* 0Ch - Big/Little Endian Descriptor */
	__u32	loc_rom_range;	/* 10h - Local ROM Range */
	__u32	loc_rom_base;	/* 14h - Local ROM Base */
	__u32	loc_bus_descr;	/* 18h - Local Bus descriptor */
	__u32	loc_range_mst;	/* 1Ch - Local Range for Master to PCI */
	__u32	loc_base_mst;	/* 20h - Local Base for Master PCI */
	__u32	loc_range_io;	/* 24h - Local Range for Master IO */
	__u32	pci_base_mst;	/* 28h - PCI Base for Master PCI */
	__u32	pci_conf_io;	/* 2Ch - PCI configuration for Master IO */
	__u32	filler1;	/* 30h */
	__u32	filler2;	/* 34h */
	__u32	filler3;	/* 38h */
	__u32	filler4;	/* 3Ch */
	__u32	mail_box_0;	/* 40h - Mail Box 0 */
	__u32	mail_box_1;	/* 44h - Mail Box 1 */
	__u32	mail_box_2;	/* 48h - Mail Box 2 */
	__u32	mail_box_3;	/* 4Ch - Mail Box 3 */
	__u32	filler5;	/* 50h */
	__u32	filler6;	/* 54h */
	__u32	filler7;	/* 58h */
	__u32	filler8;	/* 5Ch */
	__u32	pci_doorbell;	/* 60h - PCI to Local Doorbell */
	__u32	loc_doorbell;	/* 64h - Local to PCI Doorbell */
	__u32	intr_ctrl_stat;	/* 68h - Interrupt Control/Status */
	__u32	init_ctrl;	/* 6Ch - EEPROM control, Init Control, etc */
};

/* Values for the Local Base Address re-map register */

#define	WIN_RAM		0x00000001L	/* set the sliding window to RAM */
#define	WIN_CREG	0x14000001L	/* set the window to custom Registers */

/* Values timer select registers */

#define	TIMER_BY_1M	0x00		/* clock divided by 1M */
#define	TIMER_BY_256K	0x01		/* clock divided by 256k */
#define	TIMER_BY_128K	0x02		/* clock divided by 128k */
#define	TIMER_BY_32K	0x03		/* clock divided by 32k */

/****************** ****************** *******************/
#endif

#ifndef ZFIRM_ID
/* #include "zfwint.h" */
/****************** ****************** *******************/
/*
 *	This file contains the definitions for interfacing with the
 *	Cyclom-Z ZFIRM Firmware.
 */

/* General Constant definitions */

#define	MAX_CHAN	64		/* max number of channels per board */

/* firmware id structure (set after boot) */

#define ID_ADDRESS	0x00000180L	/* signature/pointer address */
#define	ZFIRM_ID	0x5557465AL	/* ZFIRM/U signature */
#define	ZFIRM_HLT	0x59505B5CL	/* ZFIRM needs external power supply */
#define	ZFIRM_RST	0x56040674L	/* RST signal (due to FW reset) */

#define	ZF_TINACT_DEF	1000		/* default inactivity timeout 
					   (1000 ms) */
#define	ZF_TINACT	ZF_TINACT_DEF

struct	FIRM_ID {
	__u32	signature;		/* ZFIRM/U signature */
	__u32	zfwctrl_addr;		/* pointer to ZFW_CTRL structure */
};

/* Op. System id */

#define	C_OS_LINUX	0x00000030	/* generic Linux system */

/* channel op_mode */

#define	C_CH_DISABLE	0x00000000	/* channel is disabled */
#define	C_CH_TXENABLE	0x00000001	/* channel Tx enabled */
#define	C_CH_RXENABLE	0x00000002	/* channel Rx enabled */
#define	C_CH_ENABLE	0x00000003	/* channel Tx/Rx enabled */
#define	C_CH_LOOPBACK	0x00000004	/* Loopback mode */

/* comm_parity - parity */

#define	C_PR_NONE	0x00000000	/* None */
#define	C_PR_ODD	0x00000001	/* Odd */
#define C_PR_EVEN	0x00000002	/* Even */
#define C_PR_MARK	0x00000004	/* Mark */
#define C_PR_SPACE	0x00000008	/* Space */
#define C_PR_PARITY	0x000000ff

#define	C_PR_DISCARD	0x00000100	/* discard char with frame/par error */
#define C_PR_IGNORE	0x00000200	/* ignore frame/par error */

/* comm_data_l - data length and stop bits */

#define C_DL_CS5	0x00000001
#define C_DL_CS6	0x00000002
#define C_DL_CS7	0x00000004
#define C_DL_CS8	0x00000008
#define	C_DL_CS		0x0000000f
#define C_DL_1STOP	0x00000010
#define C_DL_15STOP	0x00000020
#define C_DL_2STOP	0x00000040
#define	C_DL_STOP	0x000000f0

/* interrupt enabling/status */

#define	C_IN_DISABLE	0x00000000	/* zero, disable interrupts */
#define	C_IN_TXBEMPTY	0x00000001	/* tx buffer empty */
#define	C_IN_TXLOWWM	0x00000002	/* tx buffer below LWM */
#define	C_IN_RXHIWM	0x00000010	/* rx buffer above HWM */
#define	C_IN_RXNNDT	0x00000020	/* rx no new data timeout */
#define	C_IN_MDCD	0x00000100	/* modem DCD change */
#define	C_IN_MDSR	0x00000200	/* modem DSR change */
#define	C_IN_MRI	0x00000400	/* modem RI change */
#define	C_IN_MCTS	0x00000800	/* modem CTS change */
#define	C_IN_RXBRK	0x00001000	/* Break received */
#define	C_IN_PR_ERROR	0x00002000	/* parity error */
#define	C_IN_FR_ERROR	0x00004000	/* frame error */
#define C_IN_OVR_ERROR  0x00008000      /* overrun error */
#define C_IN_RXOFL	0x00010000      /* RX buffer overflow */
#define C_IN_IOCTLW	0x00020000      /* I/O control w/ wait */
#define C_IN_MRTS	0x00040000	/* modem RTS drop */
#define C_IN_ICHAR	0x00080000
 
/* flow control */

#define	C_FL_OXX	0x00000001	/* output Xon/Xoff flow control */
#define	C_FL_IXX	0x00000002	/* output Xon/Xoff flow control */
#define C_FL_OIXANY	0x00000004	/* output Xon/Xoff (any xon) */
#define	C_FL_SWFLOW	0x0000000f

/* flow status */

#define	C_FS_TXIDLE	0x00000000	/* no Tx data in the buffer or UART */
#define	C_FS_SENDING	0x00000001	/* UART is sending data */
#define	C_FS_SWFLOW	0x00000002	/* Tx is stopped by received Xoff */

/* rs_control/rs_status RS-232 signals */

#define C_RS_PARAM	0x80000000	/* Indicates presence of parameter in 
					   IOCTLM command */
#define	C_RS_RTS	0x00000001	/* RTS */
#define	C_RS_DTR	0x00000004	/* DTR */
#define	C_RS_DCD	0x00000100	/* CD */
#define	C_RS_DSR	0x00000200	/* DSR */
#define	C_RS_RI		0x00000400	/* RI */
#define	C_RS_CTS	0x00000800	/* CTS */

/* commands Host <-> Board */

#define	C_CM_RESET	0x01		/* reset/flush buffers */
#define	C_CM_IOCTL	0x02		/* re-read CH_CTRL */
#define	C_CM_IOCTLW	0x03		/* re-read CH_CTRL, intr when done */
#define	C_CM_IOCTLM	0x04		/* RS-232 outputs change */
#define	C_CM_SENDXOFF	0x10		/* send Xoff */
#define	C_CM_SENDXON	0x11		/* send Xon */
#define C_CM_CLFLOW	0x12		/* Clear flow control (resume) */
#define	C_CM_SENDBRK	0x41		/* send break */
#define	C_CM_INTBACK	0x42		/* Interrupt back */
#define	C_CM_SET_BREAK	0x43		/* Tx break on */
#define	C_CM_CLR_BREAK	0x44		/* Tx break off */
#define	C_CM_CMD_DONE	0x45		/* Previous command done */
#define C_CM_INTBACK2	0x46		/* Alternate Interrupt back */
#define	C_CM_TINACT	0x51		/* set inactivity detection */
#define	C_CM_IRQ_ENBL	0x52		/* enable generation of interrupts */
#define	C_CM_IRQ_DSBL	0x53		/* disable generation of interrupts */
#define	C_CM_ACK_ENBL	0x54		/* enable acknowledged interrupt mode */
#define	C_CM_ACK_DSBL	0x55		/* disable acknowledged intr mode */
#define	C_CM_FLUSH_RX	0x56		/* flushes Rx buffer */
#define	C_CM_FLUSH_TX	0x57		/* flushes Tx buffer */
#define C_CM_Q_ENABLE	0x58		/* enables queue access from the 
					   driver */
#define C_CM_Q_DISABLE  0x59            /* disables queue access from the 
					   driver */

#define	C_CM_TXBEMPTY	0x60		/* Tx buffer is empty */
#define	C_CM_TXLOWWM	0x61		/* Tx buffer low water mark */
#define	C_CM_RXHIWM	0x62		/* Rx buffer high water mark */
#define	C_CM_RXNNDT	0x63		/* rx no new data timeout */
#define	C_CM_TXFEMPTY	0x64
#define	C_CM_ICHAR	0x65
#define	C_CM_MDCD	0x70		/* modem DCD change */
#define	C_CM_MDSR	0x71		/* modem DSR change */
#define	C_CM_MRI	0x72		/* modem RI change */
#define	C_CM_MCTS	0x73		/* modem CTS change */
#define C_CM_MRTS	0x74		/* modem RTS drop */
#define	C_CM_RXBRK	0x84		/* Break received */
#define	C_CM_PR_ERROR	0x85		/* Parity error */
#define	C_CM_FR_ERROR	0x86		/* Frame error */
#define C_CM_OVR_ERROR  0x87            /* Overrun error */
#define C_CM_RXOFL	0x88            /* RX buffer overflow */
#define	C_CM_CMDERROR	0x90		/* command error */
#define	C_CM_FATAL	0x91		/* fatal error */
#define	C_CM_HW_RESET	0x92		/* reset board */

/*
 *	CH_CTRL - This per port structure contains all parameters
 *	that control an specific port. It can be seen as the
 *	configuration registers of a "super-serial-controller".
 */

struct CH_CTRL {
	__u32	op_mode;	/* operation mode */
	__u32	intr_enable;	/* interrupt masking */
	__u32	sw_flow;	/* SW flow control */
	__u32	flow_status;	/* output flow status */
	__u32	comm_baud;	/* baud rate  - numerically specified */
	__u32	comm_parity;	/* parity */
	__u32	comm_data_l;	/* data length/stop */
	__u32	comm_flags;	/* other flags */
	__u32	hw_flow;	/* HW flow control */
	__u32	rs_control;	/* RS-232 outputs */
	__u32	rs_status;	/* RS-232 inputs */
	__u32	flow_xon;	/* xon char */
	__u32	flow_xoff;	/* xoff char */
	__u32	hw_overflow;	/* hw overflow counter */
	__u32	sw_overflow;	/* sw overflow counter */
	__u32	comm_error;	/* frame/parity error counter */
	__u32 ichar;
	__u32 filler[7];
};


/*
 *	BUF_CTRL - This per channel structure contains
 *	all Tx and Rx buffer control for a given channel.
 */

struct	BUF_CTRL	{
	__u32	flag_dma;	/* buffers are in Host memory */
	__u32	tx_bufaddr;	/* address of the tx buffer */
	__u32	tx_bufsize;	/* tx buffer size */
	__u32	tx_threshold;	/* tx low water mark */
	__u32	tx_get;		/* tail index tx buf */
	__u32	tx_put;		/* head index tx buf */
	__u32	rx_bufaddr;	/* address of the rx buffer */
	__u32	rx_bufsize;	/* rx buffer size */
	__u32	rx_threshold;	/* rx high water mark */
	__u32	rx_get;		/* tail index rx buf */
	__u32	rx_put;		/* head index rx buf */
	__u32	filler[5];	/* filler to align structures */
};

/*
 *	BOARD_CTRL - This per board structure contains all global 
 *	control fields related to the board.
 */

struct BOARD_CTRL {

	/* static info provided by the on-board CPU */
	__u32	n_channel;	/* number of channels */
	__u32	fw_version;	/* firmware version */

	/* static info provided by the driver */
	__u32	op_system;	/* op_system id */
	__u32	dr_version;	/* driver version */

	/* board control area */
	__u32	inactivity;	/* inactivity control */

	/* host to FW commands */
	__u32	hcmd_channel;	/* channel number */
	__u32	hcmd_param;	/* pointer to parameters */

	/* FW to Host commands */
	__u32	fwcmd_channel;	/* channel number */
	__u32	fwcmd_param;	/* pointer to parameters */
	__u32	zf_int_queue_addr; /* offset for INT_QUEUE structure */

	/* filler so the structures are aligned */
	__u32	filler[6];
};

/* Host Interrupt Queue */

#define QUEUE_SIZE	(10*MAX_CHAN)

struct	INT_QUEUE {
	unsigned char	intr_code[QUEUE_SIZE];
	unsigned long	channel[QUEUE_SIZE];
	unsigned long	param[QUEUE_SIZE];
	unsigned long	put;
	unsigned long	get;
};

/*
 *	ZFW_CTRL - This is the data structure that includes all other
 *	data structures used by the Firmware.
 */
 
struct ZFW_CTRL {
	struct BOARD_CTRL	board_ctrl;
	struct CH_CTRL		ch_ctrl[MAX_CHAN];
	struct BUF_CTRL		buf_ctrl[MAX_CHAN];
};

/****************** ****************** *******************/
#endif

#ifdef __KERNEL__

/* Per card data structure */
struct cyclades_card {
    void __iomem *base_addr;
    void __iomem *ctl_addr;
    int irq;
    int num_chips;	/* 0 if card absent, -1 if Z/PCI, else Y */
    int first_line;	/* minor number of first channel on card */
    int nports;		/* Number of ports in the card */
    int bus_index;	/* address shift - 0 for ISA, 1 for PCI */
    int	intr_enabled;	/* FW Interrupt flag - 0 disabled, 1 enabled */
    spinlock_t card_lock;
    struct cyclades_port *ports;
};

/***************************************
 * Memory access functions/macros      *
 * (required to support Alpha systems) *
 ***************************************/

#define cy_writeb(port,val)     do { writeb((val), (port)); mb(); } while (0)
#define cy_writew(port,val)     do { writew((val), (port)); mb(); } while (0)
#define cy_writel(port,val)     do { writel((val), (port)); mb(); } while (0)

/*
 * Statistics counters
 */
struct cyclades_icount {
	__u32	cts, dsr, rng, dcd, tx, rx;
	__u32	frame, parity, overrun, brk;
	__u32	buf_overrun;
};

/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct cyclades_port {
	int                     magic;
	struct cyclades_card	*card;
	int			line;
	int			flags; 		/* defined in tty.h */
	int                     type;		/* UART type */
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			xmit_fifo_size;
	int                     cor1,cor2,cor3,cor4,cor5;
	int                     tbpr,tco,rbpr,rco;
	int			baud;
	int			rflow;
	int			rtsdtr_inv;
	int			chip_rev;
	int			custom_divisor;
	int                     x_char; /* to be pushed out ASAP */
	int			close_delay;
	unsigned short		closing_wait;
	int			count;	/* # of fd on device */
	int                     breakon;
	int                     breakoff;
	int			blocked_open; /* # of blocked opens */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
        int                     default_threshold;
        int                     default_timeout;
	unsigned long		rflush_count;
	struct cyclades_monitor	mon;
	struct cyclades_idle_stats	idle_stats;
	struct cyclades_icount	icount;
	wait_queue_head_t       open_wait;
	wait_queue_head_t       close_wait;
	struct completion       shutdown_wait;
	wait_queue_head_t       delta_msr_wait;
	int throttle;
};

#define	CLOSING_WAIT_DELAY	30*HZ
#define CY_CLOSING_WAIT_NONE	65535
#define CY_CLOSING_WAIT_INF	0


#define CyMAX_CHIPS_PER_CARD	8
#define CyMAX_CHAR_FIFO		12
#define CyPORTS_PER_CHIP	4
#define	CD1400_MAX_SPEED	115200

#define	CyISA_Ywin	0x2000

#define CyPCI_Ywin 	0x4000
#define CyPCI_Yctl 	0x80
#define CyPCI_Zctl 	CTRL_WINDOW_SIZE
#define CyPCI_Zwin 	0x80000
#define CyPCI_Ze_win 	(2 * CyPCI_Zwin)

#define PCI_DEVICE_ID_MASK	0x06

/**** CD1400 registers ****/

#define CD1400_REV_G	0x46
#define CD1400_REV_J	0x48

#define CyRegSize  	0x0400
#define Cy_HwReset 	0x1400
#define Cy_ClrIntr 	0x1800
#define Cy_EpldRev 	0x1e00

/* Global Registers */

#define CyGFRCR		(0x40*2)
#define      CyRevE		(44)
#define CyCAR		(0x68*2)
#define      CyCHAN_0		(0x00)
#define      CyCHAN_1		(0x01)
#define      CyCHAN_2		(0x02)
#define      CyCHAN_3		(0x03)
#define CyGCR		(0x4B*2)
#define      CyCH0_SERIAL	(0x00)
#define      CyCH0_PARALLEL	(0x80)
#define CySVRR		(0x67*2)
#define      CySRModem		(0x04)
#define      CySRTransmit	(0x02)
#define      CySRReceive	(0x01)
#define CyRICR		(0x44*2)
#define CyTICR		(0x45*2)
#define CyMICR		(0x46*2)
#define      CyICR0		(0x00)
#define      CyICR1		(0x01)
#define      CyICR2		(0x02)
#define      CyICR3		(0x03)
#define CyRIR		(0x6B*2)
#define CyTIR		(0x6A*2)
#define CyMIR		(0x69*2)
#define      CyIRDirEq		(0x80)
#define      CyIRBusy		(0x40)
#define      CyIRUnfair		(0x20)
#define      CyIRContext	(0x1C)
#define      CyIRChannel	(0x03)
#define CyPPR 		(0x7E*2)
#define      CyCLOCK_20_1MS	(0x27)
#define      CyCLOCK_25_1MS	(0x31)
#define      CyCLOCK_25_5MS	(0xf4)
#define      CyCLOCK_60_1MS	(0x75)
#define      CyCLOCK_60_2MS	(0xea)

/* Virtual Registers */

#define CyRIVR		(0x43*2)
#define CyTIVR		(0x42*2)
#define CyMIVR		(0x41*2)
#define      CyIVRMask (0x07)
#define      CyIVRRxEx (0x07)
#define      CyIVRRxOK (0x03)
#define      CyIVRTxOK (0x02)
#define      CyIVRMdmOK (0x01)
#define CyTDR		(0x63*2)
#define CyRDSR		(0x62*2)
#define      CyTIMEOUT		(0x80)
#define      CySPECHAR		(0x70)
#define      CyBREAK		(0x08)
#define      CyPARITY		(0x04)
#define      CyFRAME		(0x02)
#define      CyOVERRUN		(0x01)
#define CyMISR		(0x4C*2)
/* see CyMCOR_ and CyMSVR_ for bits*/
#define CyEOSRR		(0x60*2)

/* Channel Registers */

#define CyLIVR		(0x18*2)
#define      CyMscsr		(0x01)
#define      CyTdsr		(0x02)
#define      CyRgdsr		(0x03)
#define      CyRedsr		(0x07)
#define CyCCR		(0x05*2)
/* Format 1 */
#define      CyCHAN_RESET	(0x80)
#define      CyCHIP_RESET	(0x81)
#define      CyFlushTransFIFO	(0x82)
/* Format 2 */
#define      CyCOR_CHANGE	(0x40)
#define      CyCOR1ch		(0x02)
#define      CyCOR2ch		(0x04)
#define      CyCOR3ch		(0x08)
/* Format 3 */
#define      CySEND_SPEC_1	(0x21)
#define      CySEND_SPEC_2	(0x22)
#define      CySEND_SPEC_3	(0x23)
#define      CySEND_SPEC_4	(0x24)
/* Format 4 */
#define      CyCHAN_CTL		(0x10)
#define      CyDIS_RCVR		(0x01)
#define      CyENB_RCVR		(0x02)
#define      CyDIS_XMTR		(0x04)
#define      CyENB_XMTR		(0x08)
#define CySRER		(0x06*2)
#define      CyMdmCh		(0x80)
#define      CyRxData		(0x10)
#define      CyTxRdy		(0x04)
#define      CyTxMpty		(0x02)
#define      CyNNDT		(0x01)
#define CyCOR1		(0x08*2)
#define      CyPARITY_NONE	(0x00)
#define      CyPARITY_0		(0x20)
#define      CyPARITY_1		(0xA0)
#define      CyPARITY_E		(0x40)
#define      CyPARITY_O		(0xC0)
#define      Cy_1_STOP		(0x00)
#define      Cy_1_5_STOP	(0x04)
#define      Cy_2_STOP		(0x08)
#define      Cy_5_BITS		(0x00)
#define      Cy_6_BITS		(0x01)
#define      Cy_7_BITS		(0x02)
#define      Cy_8_BITS		(0x03)
#define CyCOR2		(0x09*2)
#define      CyIXM		(0x80)
#define      CyTxIBE		(0x40)
#define      CyETC		(0x20)
#define      CyAUTO_TXFL	(0x60)
#define      CyLLM		(0x10)
#define      CyRLM		(0x08)
#define      CyRtsAO		(0x04)
#define      CyCtsAE		(0x02)
#define      CyDsrAE		(0x01)
#define CyCOR3		(0x0A*2)
#define      CySPL_CH_DRANGE	(0x80)  /* special character detect range */
#define      CySPL_CH_DET1	(0x40)  /* enable special character detection
                                                               on SCHR4-SCHR3 */
#define      CyFL_CTRL_TRNSP	(0x20)  /* Flow Control Transparency */
#define      CySPL_CH_DET2	(0x10)  /* Enable special character detection
                                                               on SCHR2-SCHR1 */
#define      CyREC_FIFO		(0x0F)  /* Receive FIFO threshold */
#define CyCOR4		(0x1E*2)
#define CyCOR5		(0x1F*2)
#define CyCCSR		(0x0B*2)
#define      CyRxEN		(0x80)
#define      CyRxFloff		(0x40)
#define      CyRxFlon		(0x20)
#define      CyTxEN		(0x08)
#define      CyTxFloff		(0x04)
#define      CyTxFlon		(0x02)
#define CyRDCR		(0x0E*2)
#define CySCHR1		(0x1A*2)
#define CySCHR2 	(0x1B*2)
#define CySCHR3		(0x1C*2)
#define CySCHR4		(0x1D*2)
#define CySCRL		(0x22*2)
#define CySCRH		(0x23*2)
#define CyLNC		(0x24*2)
#define CyMCOR1 	(0x15*2)
#define CyMCOR2		(0x16*2)
#define CyRTPR		(0x21*2)
#define CyMSVR1		(0x6C*2)
#define CyMSVR2		(0x6D*2)
#define      CyANY_DELTA	(0xF0)
#define      CyDSR		(0x80)
#define      CyCTS		(0x40)
#define      CyRI		(0x20)
#define      CyDCD		(0x10)
#define      CyDTR              (0x02)
#define      CyRTS              (0x01)
#define CyPVSR		(0x6F*2)
#define CyRBPR		(0x78*2)
#define CyRCOR		(0x7C*2)
#define CyTBPR		(0x72*2)
#define CyTCOR		(0x76*2)

/* Custom Registers */

#define	CyPLX_VER	(0x3400)
#define	PLX_9050	0x0b
#define	PLX_9060	0x0c
#define	PLX_9080	0x0d

/***************************************************************************/

#endif /* __KERNEL__ */
#endif /* _LINUX_CYCLADES_H */
