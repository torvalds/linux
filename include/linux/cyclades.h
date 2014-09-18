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

#include <uapi/linux/cyclades.h>


/* Per card data structure */
struct cyclades_card {
	void __iomem *base_addr;
	union {
		void __iomem *p9050;
		struct RUNTIME_9060 __iomem *p9060;
	} ctl_addr;
	struct BOARD_CTRL __iomem *board_ctrl;	/* cyz specific */
	int irq;
	unsigned int num_chips;	/* 0 if card absent, -1 if Z/PCI, else Y */
	unsigned int first_line;	/* minor number of first channel on card */
	unsigned int nports;	/* Number of ports in the card */
	int bus_index;		/* address shift - 0 for ISA, 1 for PCI */
	int intr_enabled;		/* FW Interrupt flag - 0 disabled, 1 enabled */
	u32 hw_ver;
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
	struct tty_port		port;
	struct cyclades_card	*card;
	union {
		struct {
			void __iomem *base_addr;
		} cyy;
		struct {
			struct CH_CTRL __iomem	*ch_ctrl;
			struct BUF_CTRL __iomem	*buf_ctrl;
		} cyz;
	} u;
	int			line;
	int			flags; 		/* defined in tty.h */
	int                     type;		/* UART type */
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
	u8			x_char; /* to be pushed out ASAP */
	int                     breakon;
	int                     breakoff;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
        int                     default_threshold;
        int                     default_timeout;
	unsigned long		rflush_count;
	struct cyclades_monitor	mon;
	struct cyclades_idle_stats	idle_stats;
	struct cyclades_icount	icount;
	struct completion       shutdown_wait;
	int throttle;
};

#define	CLOSING_WAIT_DELAY	30*HZ
#define CY_CLOSING_WAIT_NONE	ASYNC_CLOSING_WAIT_NONE
#define CY_CLOSING_WAIT_INF	ASYNC_CLOSING_WAIT_INF


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

#endif /* _LINUX_CYCLADES_H */
