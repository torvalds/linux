/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) Comtrol Corporation <support@comtrol.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted prodived that the follwoing conditions
 * are met.
 * 1. Redistributions of source code must retain the above copyright 
 *    notive, this list of conditions and the following disclainer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials prodided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *       This product includes software developed by Comtrol Corporation.
 * 4. The name of Comtrol Corporation may not be used to endorse or 
 *    promote products derived from this software without specific 
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COMTROL CORPORATION ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL COMTROL CORPORATION BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, LIFE OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Begin OS-specific defines -  rpreg.h - for RocketPort FreeBSD
 */

typedef uint8_t Byte_t;
typedef uint8_t ByteIO_t;

typedef uint16_t Word_t;
typedef uint16_t WordIO_t;

typedef uint32_t DWord_t;
typedef uint32_t DWordIO_t;

#define rp_readio(size, ctlp, rid, offset) \
	(bus_read_##size(ctlp->io[rid], offset))
#define rp_readmultiio(size, ctlp, rid, offset, addr, count) \
	(bus_read_multi_##size(ctlp->io[rid], offset, addr, count))
#define rp_writeio(size, ctlp, rid, offset, data) \
	(bus_write_##size(ctlp->io[rid], offset, data))
#define rp_writemultiio(size, ctlp, rid, offset, addr, count) \
	(bus_write_multi_##size(ctlp->io[rid], offset, addr, count))

#define rp_readio1(ctlp, rid, offset)				rp_readio(1, ctlp, rid, offset)
#define rp_readio2(ctlp, rid, offset)				rp_readio(2, ctlp, rid, offset)
#define rp_readio4(ctlp, rid, offset)				rp_readio(4, ctlp, rid, offset)
#define rp_writeio1(ctlp, rid, offset, data)			rp_writeio(1, ctlp, rid, offset, data)
#define rp_writeio2(ctlp, rid, offset, data)			rp_writeio(2, ctlp, rid, offset, data)
#define rp_writeio4(ctlp, rid, offset, data)			rp_writeio(4, ctlp, rid, offset, data)
#define rp_readmultiio1(ctlp, rid, offset, addr, count)		rp_readmultiio(1, ctlp, rid, offset, addr, count) 
#define rp_readmultiio2(ctlp, rid, offset, addr, count)		rp_readmultiio(2, ctlp, rid, offset, addr, count) 
#define rp_readmultiio4(ctlp, rid, offset, addr, count)		rp_readmultiio(4, ctlp, rid, offset, addr, count) 
#define rp_writemultiio1(ctlp, rid, offset, addr, count)	rp_writemultiio(1, ctlp, rid, offset, addr, count) 
#define rp_writemultiio2(ctlp, rid, offset, addr, count)	rp_writemultiio(2, ctlp, rid, offset, addr, count) 
#define rp_writemultiio4(ctlp, rid, offset, addr, count)	rp_writemultiio(4, ctlp, rid, offset, addr, count) 

#define rp_readaiop1(ctlp, aiop, offset) \
	(rp_readio1((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset)))
#define rp_readaiop2(ctlp, aiop, offset) \
	(rp_readio2((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset)))
#define rp_readaiop4(ctlp, aiop, offset) \
	(rp_readio4((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset)))
#define rp_readmultiaiop1(ctlp, aiop, offset, addr, count) \
	(rp_readmultiio1((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), addr, count))
#define rp_readmultiaiop2(ctlp, aiop, offset, addr, count) \
	(rp_readmultiio2((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), addr, count))
#define rp_readmultiaiop4(ctlp, aiop, offset, addr, count) \
	(rp_readmultiio4((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), addr, count))
#define rp_writeaiop1(ctlp, aiop, offset, data) \
	(rp_writeio1((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), data))
#define rp_writeaiop2(ctlp, aiop, offset, data) \
	(rp_writeio2((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), data))
#define rp_writeaiop4(ctlp, aiop, offset, data) \
	(rp_writeio4((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), data))
#define rp_writemultiaiop1(ctlp, aiop, offset, addr, count) \
	(rp_writemultiio1((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), addr, count))
#define rp_writemultiaiop2(ctlp, aiop, offset, addr, count) \
	(rp_writemultiio2((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), addr, count))
#define rp_writemultiaiop4(ctlp, aiop, offset, addr, count) \
	(rp_writemultiio4((ctlp), (ctlp)->aiop2rid(aiop, offset), (ctlp)->aiop2off(aiop, offset), addr, count))

#define rp_readch1(chp, offset) \
	(rp_readaiop1((chp)->CtlP, (chp)->AiopNum, offset))
#define rp_readch2(chp, offset) \
	(rp_readaiop2((chp)->CtlP, (chp)->AiopNum, offset))
#define rp_readch4(chp, offset) \
	(rp_readaiop4((chp)->CtlP, (chp)->AiopNum, offset))
#define rp_readmultich1(chp, offset, addr, count) \
	(rp_readmultiaiop1((chp)->CtlP, (chp)->AiopNum, offset, addr, count))
#define rp_readmultich2(chp, offset, addr, count) \
	(rp_readmultiaiop2((chp)->CtlP, (chp)->AiopNum, offset, addr, count))
#define rp_readmultich4(chp, offset, addr, count) \
	(rp_readmultiaiop4((chp)->CtlP, (chp)->AiopNum, offset, addr, count))
#define rp_writech1(chp, offset, data) \
	(rp_writeaiop1((chp)->CtlP, (chp)->AiopNum, offset, data))
#define rp_writech2(chp, offset, data) \
	(rp_writeaiop2((chp)->CtlP, (chp)->AiopNum, offset, data))
#define rp_writech4(chp, offset, data) \
	(rp_writeaiop4((chp)->CtlP, (chp)->AiopNum, offset, data))
#define rp_writemultich1(chp, offset, addr, count) \
	(rp_writemultiaiop1((chp)->CtlP, (chp)->AiopNum, offset, addr, count))
#define rp_writemultich2(chp, offset, addr, count) \
	(rp_writemultiaiop2((chp)->CtlP, (chp)->AiopNum, offset, addr, count))
#define rp_writemultich4(chp, offset, addr, count) \
	(rp_writemultiaiop4((chp)->CtlP, (chp)->AiopNum, offset, addr, count))

/*
 * End of OS-specific defines
 */

#define ROCKET_H

#define CTL_SIZE 4
#define AIOP_CTL_SIZE 4
#define CHAN_AIOP_SIZE 8
#define MAX_PORTS_PER_AIOP 8
#define MAX_AIOPS_PER_BOARD 4
#define MAX_PORTS_PER_BOARD 32

/* Controller ID numbers */
#define CTLID_NULL  -1		    /* no controller exists */
#define CTLID_0001  0x0001	    /* controller release 1 */

/* AIOP ID numbers, identifies AIOP type implementing channel */
#define AIOPID_NULL -1		    /* no AIOP or channel exists */
#define AIOPID_0001 0x0001	    /* AIOP release 1 */

#define NULLDEV -1		    /* identifies non-existant device */
#define NULLCTL -1		    /* identifies non-existant controller */
#define NULLCTLPTR (CONTROLLER_T *)0 /* identifies non-existant controller */
#define NULLAIOP -1		    /* identifies non-existant AIOP */
#define NULLCHAN -1		    /* identifies non-existant channel */

/************************************************************************
 Global Register Offsets - Direct Access - Fixed values
************************************************************************/

#define _CMD_REG   0x38   /* Command Register		 8    Write */
#define _INT_CHAN  0x39   /* Interrupt Channel Register  8    Read */
#define _INT_MASK  0x3A   /* Interrupt Mask Register	 8    Read / Write */
#define _UNUSED    0x3B   /* Unused			 8 */
#define _INDX_ADDR 0x3C   /* Index Register Address	 16   Write */
#define _INDX_DATA 0x3E   /* Index Register Data	 8/16 Read / Write */

/************************************************************************
 Channel Register Offsets for 1st channel in AIOP - Direct Access
************************************************************************/
#define _TD0	   0x00  /* Transmit Data		16   Write */
#define _RD0	   0x00  /* Receive Data		16   Read */
#define _CHN_STAT0 0x20  /* Channel Status		8/16 Read / Write */
#define _FIFO_CNT0 0x10  /* Transmit/Receive FIFO Count 16   Read */
#define _INT_ID0   0x30  /* Interrupt Identification	8    Read */

/************************************************************************
 Tx Control Register Offsets - Indexed - External - Fixed
************************************************************************/
#define _TX_ENBLS  0x980    /* Tx Processor Enables Register 8 Read / Write */
#define _TXCMP1    0x988    /* Transmit Compare Value #1     8 Read / Write */
#define _TXCMP2    0x989    /* Transmit Compare Value #2     8 Read / Write */
#define _TXREP1B1  0x98A    /* Tx Replace Value #1 - Byte 1  8 Read / Write */
#define _TXREP1B2  0x98B    /* Tx Replace Value #1 - Byte 2  8 Read / Write */
#define _TXREP2    0x98C    /* Transmit Replace Value #2     8 Read / Write */

/************************************************************************
 Receive FIFO
************************************************************************/
#define RXFIFO_DATA	0x5f
#define RXFIFO_OUT	0x5c
#define RXFIFO_EN	0x08
#define RXFIFO_DIS	0xa7

/************************************************************************
Memory Controller Register Offsets - Indexed - External - Fixed
************************************************************************/
#define _RX_FIFO    0x000    /* Rx FIFO */
#define _TX_FIFO    0x800    /* Tx FIFO */
#define _RXF_OUTP   0x990    /* Rx FIFO OUT pointer	   16 Read / Write */
#define _RXF_INP    0x992    /* Rx FIFO IN pointer	   16 Read / Write */
#define _TXF_OUTP   0x994    /* Tx FIFO OUT pointer	   8  Read / Write */
#define _TXF_INP    0x995    /* Tx FIFO IN pointer	   8  Read / Write */
#define _TXP_CNT    0x996    /* Tx Priority Count	   8  Read / Write */
#define _TXP_PNTR   0x997    /* Tx Priority Pointer	   8  Read / Write */

#define PRI_PEND    0x80     /* Priority data pending (bit7, Tx pri cnt) */
#define TXFIFO_SIZE 255      /* size of Tx FIFO */
#define RXFIFO_SIZE 1023     /* size of Rx FIFO */

/************************************************************************
Tx Priority Buffer - Indexed - External - Fixed
************************************************************************/
#define _TXP_BUF    0x9C0    /* Tx Priority Buffer  32	Bytes	Read / Write */
#define TXP_SIZE    0x20     /* 32 bytes */

/************************************************************************
Channel Register Offsets - Indexed - Internal - Fixed
************************************************************************/

#define _TX_CTRL    0xFF0    /* Transmit Control	       16  Write */
#define _RX_CTRL    0xFF2    /* Receive Control 		8  Write */
#define _BAUD	    0xFF4    /* Baud Rate		       16  Write */
#define _CLK_PRE    0xFF6    /* Clock Prescaler 		8  Write */

#define CLOCK_PRESC 0x19	  /* mod 9 (divide by 10) prescale */

#define BRD50		  4607
#define BRD75		  3071
#define BRD110		  2094
#define BRD134		  1712
#define BRD150		  1535
#define BRD200		  1151
#define BRD300		  767
#define BRD600		  383
#define BRD1200 	  191
#define BRD1800 	  127
#define BRD2000 	  114
#define BRD2400 	  95
#define BRD3600 	  64
#define BRD4800 	  47
#define BRD7200 	  31
#define BRD9600 	  23
#define BRD14400	  15
#define BRD19200	  11
#define BRD38400	  5
#define BRD57600	  3
#define BRD76800	  2
#define BRD115200	  1
#define BRD230400	  0

#define STMBREAK   0x08        /* BREAK */
#define STMFRAME   0x04        /* framing error */
#define STMRCVROVR 0x02        /* receiver over run error */
#define STMPARITY  0x01        /* parity error */
#define STMERROR   (STMBREAK | STMFRAME | STMPARITY)
#define STMBREAKH   0x800      /* BREAK */
#define STMFRAMEH   0x400      /* framing error */
#define STMRCVROVRH 0x200      /* receiver over run error */
#define STMPARITYH  0x100      /* parity error */
#define STMERRORH   (STMBREAKH | STMFRAMEH | STMPARITYH)

#define CTS_ACT   0x20	      /* CTS input asserted */
#define DSR_ACT   0x10	      /* DSR input asserted */
#define CD_ACT	  0x08	      /* CD input asserted */
#define TXFIFOMT  0x04	      /* Tx FIFO is empty */
#define TXSHRMT   0x02	      /* Tx shift register is empty */
#define RDA	  0x01	      /* Rx data available */
#define DRAINED (TXFIFOMT | TXSHRMT)  /* indicates Tx is drained */

#define STATMODE  0x8000      /* status mode enable bit */
#define RXFOVERFL 0x2000      /* receive FIFO overflow */
#define RX2MATCH  0x1000      /* receive compare byte 2 match */
#define RX1MATCH  0x0800      /* receive compare byte 1 match */
#define RXBREAK   0x0400      /* received BREAK */
#define RXFRAME   0x0200      /* received framing error */
#define RXPARITY  0x0100      /* received parity error */
#define STATERROR (RXBREAK | RXFRAME | RXPARITY)

#define CTSFC_EN  0x80	      /* CTS flow control enable bit */
#define RTSTOG_EN 0x40	      /* RTS toggle enable bit */
#define TXINT_EN  0x10	      /* transmit interrupt enable */
#define STOP2	  0x08	      /* enable 2 stop bits (0 = 1 stop) */
#define PARITY_EN 0x04	      /* enable parity (0 = no parity) */
#define EVEN_PAR  0x02	      /* even parity (0 = odd parity) */
#define DATA8BIT  0x01	      /* 8 bit data (0 = 7 bit data) */

#define SETBREAK  0x10	      /* send break condition (must clear) */
#define LOCALLOOP 0x08	      /* local loopback set for test */
#define SET_DTR   0x04	      /* assert DTR */
#define SET_RTS   0x02	      /* assert RTS */
#define TX_ENABLE 0x01	      /* enable transmitter */

#define RTSFC_EN  0x40	      /* RTS flow control enable */
#define RXPROC_EN 0x20	      /* receive processor enable */
#define TRIG_NO   0x00	      /* Rx FIFO trigger level 0 (no trigger) */
#define TRIG_1	  0x08	      /* trigger level 1 char */
#define TRIG_1_2  0x10	      /* trigger level 1/2 */
#define TRIG_7_8  0x18	      /* trigger level 7/8 */
#define TRIG_MASK 0x18	      /* trigger level mask */
#define SRCINT_EN 0x04	      /* special Rx condition interrupt enable */
#define RXINT_EN  0x02	      /* Rx interrupt enable */
#define MCINT_EN  0x01	      /* modem change interrupt enable */

#define RXF_TRIG  0x20	      /* Rx FIFO trigger level interrupt */
#define TXFIFO_MT 0x10	      /* Tx FIFO empty interrupt */
#define SRC_INT   0x08	      /* special receive condition interrupt */
#define DELTA_CD  0x04	      /* CD change interrupt */
#define DELTA_CTS 0x02	      /* CTS change interrupt */
#define DELTA_DSR 0x01	      /* DSR change interrupt */

#define REP1W2_EN 0x10	      /* replace byte 1 with 2 bytes enable */
#define IGN2_EN   0x08	      /* ignore byte 2 enable */
#define IGN1_EN   0x04	      /* ignore byte 1 enable */
#define COMP2_EN  0x02	      /* compare byte 2 enable */
#define COMP1_EN  0x01	      /* compare byte 1 enable */

#define RESET_ALL 0x80	      /* reset AIOP (all channels) */
#define TXOVERIDE 0x40	      /* Transmit software off override */
#define RESETUART 0x20	      /* reset channel's UART */
#define RESTXFCNT 0x10	      /* reset channel's Tx FIFO count register */
#define RESRXFCNT 0x08	      /* reset channel's Rx FIFO count register */

#define INTSTAT0  0x01	      /* AIOP 0 interrupt status */
#define INTSTAT1  0x02	      /* AIOP 1 interrupt status */
#define INTSTAT2  0x04	      /* AIOP 2 interrupt status */
#define INTSTAT3  0x08	      /* AIOP 3 interrupt status */

#define INTR_EN   0x08	      /* allow interrupts to host */
#define INT_STROB 0x04	      /* strobe and clear interrupt line (EOI) */

#define CHAN3_EN  0x08	      /* enable AIOP 3 */
#define CHAN2_EN  0x04	      /* enable AIOP 2 */
#define CHAN1_EN  0x02	      /* enable AIOP 1 */
#define CHAN0_EN  0x01	      /* enable AIOP 0 */
#define FREQ_DIS  0x00
#define FREQ_274HZ 0x60
#define FREQ_137HZ 0x50
#define FREQ_69HZ  0x40
#define FREQ_34HZ  0x30
#define FREQ_17HZ  0x20
#define FREQ_9HZ   0x10
#define PERIODIC_ONLY 0x80    /* only PERIODIC interrupt */

#define CHANINT_EN 0x0100	    /* flags to enable/disable channel ints */

#define RDATASIZE 72
#define RREGDATASIZE 52

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

struct CONTROLLER_str;
struct CHANNEL_str;

/* The types of bus-specific methods */
typedef int rp_aiop2rid_t(int, int);
typedef int rp_aiop2off_t(int, int);
typedef unsigned char rp_ctlmask_t(struct CONTROLLER_str *);

/* Controller level information structure */
struct CONTROLLER_str
{
	int		CtlID;
	int		NumAiop;
	int		AiopID[AIOP_CTL_SIZE];
	int		AiopNumChan[AIOP_CTL_SIZE];

        struct mtx	hwmtx;     /* Spinlock protecting hardware. */
	int		hwmtx_init;
	int		free;
	int		num_ports;

	/* Device and resource management */
	device_t		dev;		/* device */
	int			io_num;		/* Number of IO resources */
	int			*io_rid;	/* IO resource IDs */
	struct resource		**io;		/* IO resources */

	struct rp_port		*rp;		/* port */

	/* Device nodes */
	struct cdev **dev_nodes;

	/* Bus-specific properties */
	void			*bus_ctlp;

	/* Bus-specific methods */
	rp_aiop2rid_t		*aiop2rid;	/* (aiop, offset) -> rid */
	rp_aiop2off_t		*aiop2off;	/* (aiop, offset) -> off */
	rp_ctlmask_t		*ctlmask;	/* Int status */
};
typedef struct CONTROLLER_str CONTROLLER_T;
typedef CONTROLLER_T CONTROLLER_t;

/* Channel level information structure */
struct CHANNEL_str
{
	CONTROLLER_t	*CtlP;
	int		AiopNum;
	int		ChanID;
	int		ChanNum;

	Word_t		TxFIFO;
	Word_t		TxFIFOPtrs;
	Word_t		RxFIFO;
	Word_t		RxFIFOPtrs;
	Word_t		TxPrioCnt;
	Word_t		TxPrioPtr;
	Word_t		TxPrioBuf;

	Byte_t		R[RREGDATASIZE];

	Byte_t		BaudDiv[4];
	Byte_t		TxControl[4];
	Byte_t		RxControl[4];
	Byte_t		TxEnables[4];
	Byte_t		TxCompare[4];
	Byte_t		TxReplace1[4];
	Byte_t		TxReplace2[4];
};

typedef struct CHANNEL_str CHANNEL_T;
typedef CHANNEL_T CHANNEL_t;
typedef CHANNEL_T * CHANPTR_T;

#define CHNOFF_TXRXDATA(chp)	((chp)->ChanNum * 2 + _TD0)
#define CHNOFF_CHANSTAT(chp)	((chp)->ChanNum * 2 + _CHN_STAT0)
#define CHNOFF_TXRXCOUNT(chp)	((chp)->ChanNum * 2 + _FIFO_CNT0)
#define CHNOFF_INTID(chp)	((chp)->ChanNum     + _INT_ID0)

/***************************************************************************
Function: sClrBreak
Purpose:  Stop sending a transmit BREAK signal
Call:	  sClrBreak(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrBreak(ChP) \
{ \
   (ChP)->TxControl[3] &= ~SETBREAK; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sClrDTR
Purpose:  Clr the DTR output
Call:	  sClrDTR(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrDTR(ChP) \
{ \
   (ChP)->TxControl[3] &= ~SET_DTR; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sClrRTS
Purpose:  Clr the RTS output
Call:	  sClrRTS(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrRTS(ChP) \
{ \
   (ChP)->TxControl[3] &= ~SET_RTS; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sClrTxXOFF
Purpose:  Clear any existing transmit software flow control off condition
Call:	  sClrTxXOFF(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrTxXOFF(ChP) \
{ \
   rp_writech1(ChP,_CMD_REG,TXOVERIDE | (Byte_t)(ChP)->ChanNum); \
   rp_writech1(ChP,_CMD_REG,(Byte_t)(ChP)->ChanNum); \
}

/***************************************************************************
Function: sDisCTSFlowCtl
Purpose:  Disable output flow control using CTS
Call:	  sDisCTSFlowCtl(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisCTSFlowCtl(ChP) \
{ \
   (ChP)->TxControl[2] &= ~CTSFC_EN; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: DisParity
Purpose:  Disable parity
Call:	  sDisParity(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
	  sDisParity(), sSetOddParity(), and sSetEvenParity().
*/
#define sDisParity(ChP) \
{ \
   (ChP)->TxControl[2] &= ~PARITY_EN; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sDisRxFIFO
Purpose:  Disable Rx FIFO
Call:	  sDisRxFIFO(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisRxFIFO(ChP) \
{ \
   (ChP)->R[0x32] = 0x0a; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->R + 0x30)); \
}

/***************************************************************************
Function: sDisRxStatusMode
Purpose:  Disable the Rx status mode
Call:	  sDisRxStatusMode(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: This takes the channel out of the receive status mode.  All
	  subsequent reads of receive data using sReadRxWord() will return
	  two data bytes.
*/
#define sDisRxStatusMode(ChP) rp_writech2(ChP,CHNOFF_CHANSTAT(ChP),0)

/***************************************************************************
Function: sDisTransmit
Purpose:  Disable transmit
Call:	  sDisTransmit(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
	  This disables movement of Tx data from the Tx FIFO into the 1 byte
	  Tx buffer.  Therefore there could be up to a 2 byte latency
	  between the time sDisTransmit() is called and the transmit buffer
	  and transmit shift register going completely empty.
*/
#define sDisTransmit(ChP) \
{ \
   (ChP)->TxControl[3] &= ~TX_ENABLE; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sDisTxSoftFlowCtl
Purpose:  Disable Tx Software Flow Control
Call:	  sDisTxSoftFlowCtl(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisTxSoftFlowCtl(ChP) \
{ \
   (ChP)->R[0x06] = 0x8a; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->R + 0x04)); \
}

/***************************************************************************
Function: sEnCTSFlowCtl
Purpose:  Enable output flow control using CTS
Call:	  sEnCTSFlowCtl(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnCTSFlowCtl(ChP) \
{ \
   (ChP)->TxControl[2] |= CTSFC_EN; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: EnParity
Purpose:  Enable parity
Call:	  sEnParity(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
	  sDisParity(), sSetOddParity(), and sSetEvenParity().

Warnings: Before enabling parity odd or even parity should be chosen using
	  functions sSetOddParity() or sSetEvenParity().
*/
#define sEnParity(ChP) \
{ \
   (ChP)->TxControl[2] |= PARITY_EN; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sEnRTSFlowCtl
Return: void
*/
#define sEnRTSFlowCtl(ChP) \
{ \
	(ChP)->TxControl[2] &= ~RTSTOG_EN; \
	(ChP)->TxControl[3] &= ~SET_RTS; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
	(ChP)->RxControl[2] |= RTSFC_EN; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->RxControl)); \
}

/***************************************************************************
Function: sDisRTSFlowCtl
Return: void
*/
#define sDisRTSFlowCtl(ChP) \
{ \
	(ChP)->RxControl[2] &= ~RTSFC_EN; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->RxControl)); \
}

/***************************************************************************
Function: sEnRxFIFO
Purpose:  Enable Rx FIFO
Call:	  sEnRxFIFO(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnRxFIFO(ChP) \
{ \
   (ChP)->R[0x32] = 0x08; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->R + 0x30)); \
}

/***************************************************************************
Function: sEnRxProcessor
Purpose:  Enable the receive processor
Call:	  sEnRxProcessor(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: This function is used to start the receive processor.  When
	  the channel is in the reset state the receive processor is not
	  running.  This is done to prevent the receive processor from
	  executing invalid microcode instructions prior to the
	  downloading of the microcode.

Warnings: This function must be called after valid microcode has been
	  downloaded to the AIOP, and it must not be called before the
	  microcode has been downloaded.
*/
#define sEnRxProcessor(ChP) \
{ \
   (ChP)->RxControl[2] |= RXPROC_EN; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->RxControl)); \
}

/***************************************************************************
Function: sEnRxStatusMode
Purpose:  Enable the Rx status mode
Call:	  sEnRxStatusMode(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: This places the channel in the receive status mode.  All subsequent
	  reads of receive data using sReadRxWord() will return a data byte
	  in the low word and a status byte in the high word.

*/
#define sEnRxStatusMode(ChP) rp_writech2(ChP,CHNOFF_CHANSTAT(ChP),STATMODE)

/***************************************************************************
Function: sEnTransmit
Purpose:  Enable transmit
Call:	  sEnTransmit(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnTransmit(ChP) \
{ \
   (ChP)->TxControl[3] |= TX_ENABLE; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sGetAiopIntStatus
Purpose:  Get the AIOP interrupt status
Call:	  sGetAiopIntStatus(CtlP,AiopNum)
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  int AiopNum; AIOP number
Return:   Byte_t: The AIOP interrupt status.  Bits 0 through 7
			 represent channels 0 through 7 respectively.  If a
			 bit is set that channel is interrupting.
*/
#define sGetAiopIntStatus(CtlP,AIOPNUM) rp_readaiop1(CtlP,AIOPNUM,_INT_CHAN)

/***************************************************************************
Function: sGetAiopNumChan
Purpose:  Get the number of channels supported by an AIOP
Call:	  sGetAiopNumChan(CtlP,AiopNum)
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  int AiopNum; AIOP number
Return:   int: The number of channels supported by the AIOP
*/
#define sGetAiopNumChan(CtlP,AIOPNUM) CtlP->AiopNumChan[AIOPNUM]

/***************************************************************************
Function: sGetChanIntID
Purpose:  Get a channel's interrupt identification byte
Call:	  sGetChanIntID(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   Byte_t: The channel interrupt ID.  Can be any
	     combination of the following flags:
		RXF_TRIG:     Rx FIFO trigger level interrupt
		TXFIFO_MT:    Tx FIFO empty interrupt
		SRC_INT:      Special receive condition interrupt
		DELTA_CD:     CD change interrupt
		DELTA_CTS:    CTS change interrupt
		DELTA_DSR:    DSR change interrupt
*/
#define sGetChanIntID(ChP) (rp_readch1(ChP,(ChP)->ChanNum+_INT_ID0) & (RXF_TRIG | TXFIFO_MT | SRC_INT | DELTA_CD | DELTA_CTS | DELTA_DSR))

/***************************************************************************
Function: sGetChanNum
Purpose:  Get the number of a channel within an AIOP
Call:	  sGetChanNum(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   int: Channel number within AIOP, or NULLCHAN if channel does
	       not exist.
*/
#define sGetChanNum(ChP) (ChP)->ChanNum

/***************************************************************************
Function: sGetChanStatus
Purpose:  Get the channel status
Call:	  sGetChanStatus(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   Word_t: The channel status.  Can be any combination of
	     the following flags:
		LOW BYTE FLAGS
		CTS_ACT:      CTS input asserted
		DSR_ACT:      DSR input asserted
		CD_ACT:       CD input asserted
		TXFIFOMT:     Tx FIFO is empty
		TXSHRMT:      Tx shift register is empty
		RDA:	      Rx data available

		HIGH BYTE FLAGS
		STATMODE:     status mode enable bit
		RXFOVERFL:    receive FIFO overflow
		RX2MATCH:     receive compare byte 2 match
		RX1MATCH:     receive compare byte 1 match
		RXBREAK:      received BREAK
		RXFRAME:      received framing error
		RXPARITY:     received parity error
Warnings: This function will clear the high byte flags in the Channel
	  Status Register.
*/
#define sGetChanStatus(ChP) rp_readch2(ChP,CHNOFF_CHANSTAT(ChP))

/***************************************************************************
Function: sGetChanStatusLo
Purpose:  Get the low byte only of the channel status
Call:	  sGetChanStatusLo(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   Byte_t: The channel status low byte.	Can be any combination
	     of the following flags:
		CTS_ACT:      CTS input asserted
		DSR_ACT:      DSR input asserted
		CD_ACT:       CD input asserted
		TXFIFOMT:     Tx FIFO is empty
		TXSHRMT:      Tx shift register is empty
		RDA:	      Rx data available
*/
#define sGetChanStatusLo(ChP) rp_readch1(ChP,CHNOFF_CHANSTAT(ChP))

/***************************************************************************
Function: sGetRxCnt
Purpose:  Get the number of data bytes in the Rx FIFO
Call:	  sGetRxCnt(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   int: The number of data bytes in the Rx FIFO.
Comments: Byte read of count register is required to obtain Rx count.

*/
#define sGetRxCnt(ChP) rp_readch2(ChP,CHNOFF_TXRXCOUNT(ChP))

/***************************************************************************
Function: sGetTxCnt
Purpose:  Get the number of data bytes in the Tx FIFO
Call:	  sGetTxCnt(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   Byte_t: The number of data bytes in the Tx FIFO.
Comments: Byte read of count register is required to obtain Tx count.

*/
#define sGetTxCnt(ChP) rp_readch1(ChP,CHNOFF_TXRXCOUNT(ChP))

/*****************************************************************************
Function: sGetTxRxDataIO
Purpose:  Get the offset of a channel's TxRx Data register
Call:	  sGetTxRxDataIO(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   WordIO_t: offset of a channel's TxRx Data register
*/
#define sGetTxRxDataIO(ChP) CHNOFF_TXRXDATA(ChP)

/***************************************************************************
Function: sInitChanDefaults
Purpose:  Initialize a channel structure to its default state.
Call:	  sInitChanDefaults(ChP)
	  CHANNEL_T *ChP; Ptr to the channel structure
Comments: This function must be called once for every channel structure
	  that exists before any other SSCI calls can be made.

*/
#define sInitChanDefaults(ChP) \
{ \
   (ChP)->CtlP = NULLCTLPTR; \
   (ChP)->AiopNum = NULLAIOP; \
   (ChP)->ChanID = AIOPID_NULL; \
   (ChP)->ChanNum = NULLCHAN; \
}

/***************************************************************************
Function: sResetAiopByNum
Purpose:  Reset the AIOP by number
Call:	  sResetAiopByNum(CTLP,AIOPNUM)
	CONTROLLER_T CTLP; Ptr to controller structure
	AIOPNUM; AIOP index
*/
#define sResetAiopByNum(CTLP,AIOPNUM) \
{ \
   rp_writeaiop1(CTLP,AIOPNUM,_CMD_REG,RESET_ALL); \
   rp_writeaiop1(CTLP,AIOPNUM,_CMD_REG,0x0); \
}

/***************************************************************************
Function: sSendBreak
Purpose:  Send a transmit BREAK signal
Call:	  sSendBreak(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSendBreak(ChP) \
{ \
   (ChP)->TxControl[3] |= SETBREAK; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetBaud
Purpose:  Set baud rate
Call:	  sSetBaud(ChP,Divisor)
	  CHANNEL_T *ChP; Ptr to channel structure
	  Word_t Divisor; 16 bit baud rate divisor for channel
*/
#define sSetBaud(ChP,DIVISOR) \
{ \
   (ChP)->BaudDiv[2] = (Byte_t)(DIVISOR); \
   (ChP)->BaudDiv[3] = (Byte_t)((DIVISOR) >> 8); \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->BaudDiv)); \
}

/***************************************************************************
Function: sSetData7
Purpose:  Set data bits to 7
Call:	  sSetData7(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetData7(ChP) \
{ \
   (ChP)->TxControl[2] &= ~DATA8BIT; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetData8
Purpose:  Set data bits to 8
Call:	  sSetData8(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetData8(ChP) \
{ \
   (ChP)->TxControl[2] |= DATA8BIT; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetDTR
Purpose:  Set the DTR output
Call:	  sSetDTR(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetDTR(ChP) \
{ \
   (ChP)->TxControl[3] |= SET_DTR; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetEvenParity
Purpose:  Set even parity
Call:	  sSetEvenParity(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
	  sDisParity(), sSetOddParity(), and sSetEvenParity().

Warnings: This function has no effect unless parity is enabled with function
	  sEnParity().
*/
#define sSetEvenParity(ChP) \
{ \
   (ChP)->TxControl[2] |= EVEN_PAR; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetOddParity
Purpose:  Set odd parity
Call:	  sSetOddParity(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
	  sDisParity(), sSetOddParity(), and sSetEvenParity().

Warnings: This function has no effect unless parity is enabled with function
	  sEnParity().
*/
#define sSetOddParity(ChP) \
{ \
   (ChP)->TxControl[2] &= ~EVEN_PAR; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetRTS
Purpose:  Set the RTS output
Call:	  sSetRTS(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetRTS(ChP) \
{ \
   (ChP)->TxControl[3] |= SET_RTS; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetRxTrigger
Purpose:  Set the Rx FIFO trigger level
Call:	  sSetRxProcessor(ChP,Level)
	  CHANNEL_T *ChP; Ptr to channel structure
	  Byte_t Level; Number of characters in Rx FIFO at which the
	     interrupt will be generated.  Can be any of the following flags:

	     TRIG_NO:	no trigger
	     TRIG_1:	1 character in FIFO
	     TRIG_1_2:	FIFO 1/2 full
	     TRIG_7_8:	FIFO 7/8 full
Comments: An interrupt will be generated when the trigger level is reached
	  only if function sEnInterrupt() has been called with flag
	  RXINT_EN set.  The RXF_TRIG flag in the Interrupt Idenfification
	  register will be set whenever the trigger level is reached
	  regardless of the setting of RXINT_EN.

*/
#define sSetRxTrigger(ChP,LEVEL) \
{ \
   (ChP)->RxControl[2] &= ~TRIG_MASK; \
   (ChP)->RxControl[2] |= LEVEL; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->RxControl)); \
}

/***************************************************************************
Function: sSetStop1
Purpose:  Set stop bits to 1
Call:	  sSetStop1(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetStop1(ChP) \
{ \
   (ChP)->TxControl[2] &= ~STOP2; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sSetStop2
Purpose:  Set stop bits to 2
Call:	  sSetStop2(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetStop2(ChP) \
{ \
   (ChP)->TxControl[2] |= STOP2; \
   rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->TxControl)); \
}

/***************************************************************************
Function: sStartRxProcessor
Purpose:  Start a channel's receive processor
Call:	  sStartRxProcessor(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Comments: This function is used to start a Rx processor after it was
	  stopped with sStopRxProcessor() or sStopSWInFlowCtl().  It
	  will restart both the Rx processor and software input flow control.

*/
#define sStartRxProcessor(ChP) rp_writech4(ChP,_INDX_ADDR,le32dec((ChP)->R))

/***************************************************************************
Function: sWriteTxByte
Purpose:  Write a transmit data byte to a channel.
	  CHANNEL_T *ChP; Ptr to channel structure
	  ByteIO_t io: Channel transmit register I/O address.  This can
			   be obtained with sGetTxRxDataIO().
	  Byte_t Data; The transmit data byte.
Warnings: This function writes the data byte without checking to see if
	  sMaxTxSize is exceeded in the Tx FIFO.
*/
#define sWriteTxByte(ChP,IO,DATA) rp_writech1(ChP,IO,DATA)

int sReadAiopID(CONTROLLER_T *CtlP, int aiop);
int sReadAiopNumChan(CONTROLLER_T *CtlP, int aiop);
int sInitChan(	CONTROLLER_T *CtlP,
		CHANNEL_T *ChP,
		int AiopNum,
		int ChanNum);
Byte_t sGetRxErrStatus(CHANNEL_T *ChP);
void sStopRxProcessor(CHANNEL_T *ChP);
void sStopSWInFlowCtl(CHANNEL_T *ChP);
void sFlushRxFIFO(CHANNEL_T *ChP);
void sFlushTxFIFO(CHANNEL_T *ChP);
int sWriteTxPrioByte(CHANNEL_T *ChP, Byte_t Data);
void sEnInterrupts(CHANNEL_T *ChP,Word_t Flags);
void sDisInterrupts(CHANNEL_T *ChP,Word_t Flags);
int rp_attachcommon(CONTROLLER_T *ctlp, int num_aiops, int num_ports);
void rp_releaseresource(CONTROLLER_t *ctlp);
static __inline void
rp_lock(CONTROLLER_T *CtlP)
{
        if (CtlP->hwmtx_init != 0)
                mtx_lock(&CtlP->hwmtx);
}
static __inline void
rp_unlock(CONTROLLER_T *CtlP)
{
        if (CtlP->hwmtx_init != 0)
                mtx_unlock(&CtlP->hwmtx);
}

#ifndef ROCKET_C
extern Byte_t R[RDATASIZE];
extern CONTROLLER_T sController[CTL_SIZE];
extern Byte_t sIRQMap[16];
#endif
extern Byte_t rp_sBitMapClrTbl[8];
extern Byte_t rp_sBitMapSetTbl[8];
