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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* 
 * rp.c - for RocketPort FreeBSD
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/serial.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/bus.h>
#include <sys/rman.h>

#define ROCKET_C
#include <dev/rp/rpreg.h>
#include <dev/rp/rpvar.h>

static const char RocketPortVersion[] = "3.02";

static Byte_t RData[RDATASIZE] =
{
   0x00, 0x09, 0xf6, 0x82,
   0x02, 0x09, 0x86, 0xfb,
   0x04, 0x09, 0x00, 0x0a,
   0x06, 0x09, 0x01, 0x0a,
   0x08, 0x09, 0x8a, 0x13,
   0x0a, 0x09, 0xc5, 0x11,
   0x0c, 0x09, 0x86, 0x85,
   0x0e, 0x09, 0x20, 0x0a,
   0x10, 0x09, 0x21, 0x0a,
   0x12, 0x09, 0x41, 0xff,
   0x14, 0x09, 0x82, 0x00,
   0x16, 0x09, 0x82, 0x7b,
   0x18, 0x09, 0x8a, 0x7d,
   0x1a, 0x09, 0x88, 0x81,
   0x1c, 0x09, 0x86, 0x7a,
   0x1e, 0x09, 0x84, 0x81,
   0x20, 0x09, 0x82, 0x7c,
   0x22, 0x09, 0x0a, 0x0a
};

static Byte_t RRegData[RREGDATASIZE]=
{
   0x00, 0x09, 0xf6, 0x82,	       /* 00: Stop Rx processor */
   0x08, 0x09, 0x8a, 0x13,	       /* 04: Tx software flow control */
   0x0a, 0x09, 0xc5, 0x11,	       /* 08: XON char */
   0x0c, 0x09, 0x86, 0x85,	       /* 0c: XANY */
   0x12, 0x09, 0x41, 0xff,	       /* 10: Rx mask char */
   0x14, 0x09, 0x82, 0x00,	       /* 14: Compare/Ignore #0 */
   0x16, 0x09, 0x82, 0x7b,	       /* 18: Compare #1 */
   0x18, 0x09, 0x8a, 0x7d,	       /* 1c: Compare #2 */
   0x1a, 0x09, 0x88, 0x81,	       /* 20: Interrupt #1 */
   0x1c, 0x09, 0x86, 0x7a,	       /* 24: Ignore/Replace #1 */
   0x1e, 0x09, 0x84, 0x81,	       /* 28: Interrupt #2 */
   0x20, 0x09, 0x82, 0x7c,	       /* 2c: Ignore/Replace #2 */
   0x22, 0x09, 0x0a, 0x0a	       /* 30: Rx FIFO Enable */
};

#if 0
/* IRQ number to MUDBAC register 2 mapping */
Byte_t sIRQMap[16] =
{
   0,0,0,0x10,0x20,0x30,0,0,0,0x40,0x50,0x60,0x70,0,0,0x80
};
#endif

Byte_t rp_sBitMapClrTbl[8] =
{
   0xfe,0xfd,0xfb,0xf7,0xef,0xdf,0xbf,0x7f
};

Byte_t rp_sBitMapSetTbl[8] =
{
   0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80
};

static void rpfree(void *);

/***************************************************************************
Function: sReadAiopID
Purpose:  Read the AIOP idenfication number directly from an AIOP.
Call:	  sReadAiopID(CtlP, aiop)
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  int aiop: AIOP index
Return:   int: Flag AIOPID_XXXX if a valid AIOP is found, where X
		 is replace by an identifying number.
	  Flag AIOPID_NULL if no valid AIOP is found
Warnings: No context switches are allowed while executing this function.

*/
int sReadAiopID(CONTROLLER_T *CtlP, int aiop)
{
   Byte_t AiopID;		/* ID byte from AIOP */

   rp_writeaiop1(CtlP, aiop, _CMD_REG, RESET_ALL);     /* reset AIOP */
   rp_writeaiop1(CtlP, aiop, _CMD_REG, 0x0);
   AiopID = rp_readaiop1(CtlP, aiop, _CHN_STAT0) & 0x07;
   if(AiopID == 0x06)
      return(1);
   else 			       /* AIOP does not exist */
      return(-1);
}

/***************************************************************************
Function: sReadAiopNumChan
Purpose:  Read the number of channels available in an AIOP directly from
	  an AIOP.
Call:	  sReadAiopNumChan(CtlP, aiop)
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  int aiop: AIOP index
Return:   int: The number of channels available
Comments: The number of channels is determined by write/reads from identical
	  offsets within the SRAM address spaces for channels 0 and 4.
	  If the channel 4 space is mirrored to channel 0 it is a 4 channel
	  AIOP, otherwise it is an 8 channel.
Warnings: No context switches are allowed while executing this function.
*/
int sReadAiopNumChan(CONTROLLER_T *CtlP, int aiop)
{
   Word_t x, y;

   rp_writeaiop4(CtlP, aiop, _INDX_ADDR,0x12340000L); /* write to chan 0 SRAM */
   rp_writeaiop2(CtlP, aiop, _INDX_ADDR,0);	   /* read from SRAM, chan 0 */
   x = rp_readaiop2(CtlP, aiop, _INDX_DATA);
   rp_writeaiop2(CtlP, aiop, _INDX_ADDR,0x4000);  /* read from SRAM, chan 4 */
   y = rp_readaiop2(CtlP, aiop, _INDX_DATA);
   if(x != y)  /* if different must be 8 chan */
      return(8);
   else
      return(4);
}

/***************************************************************************
Function: sInitChan
Purpose:  Initialization of a channel and channel structure
Call:	  sInitChan(CtlP,ChP,AiopNum,ChanNum)
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  CHANNEL_T *ChP; Ptr to channel structure
	  int AiopNum; AIOP number within controller
	  int ChanNum; Channel number within AIOP
Return:   int: TRUE if initialization succeeded, FALSE if it fails because channel
	       number exceeds number of channels available in AIOP.
Comments: This function must be called before a channel can be used.
Warnings: No range checking on any of the parameters is done.

	  No context switches are allowed while executing this function.
*/
int sInitChan(	CONTROLLER_T *CtlP,
		CHANNEL_T *ChP,
		int AiopNum,
		int ChanNum)
{
   int i, ChOff;
   Byte_t *ChR;
   static Byte_t R[4];

   if(ChanNum >= CtlP->AiopNumChan[AiopNum])
      return(FALSE);		       /* exceeds num chans in AIOP */

   /* Channel, AIOP, and controller identifiers */
   ChP->CtlP = CtlP;
   ChP->ChanID = CtlP->AiopID[AiopNum];
   ChP->AiopNum = AiopNum;
   ChP->ChanNum = ChanNum;

   /* Initialize the channel from the RData array */
   for(i=0; i < RDATASIZE; i+=4)
   {
      R[0] = RData[i];
      R[1] = RData[i+1] + 0x10 * ChanNum;
      R[2] = RData[i+2];
      R[3] = RData[i+3];
      rp_writech4(ChP,_INDX_ADDR,le32dec(R));
   }

   ChR = ChP->R;
   for(i=0; i < RREGDATASIZE; i+=4)
   {
      ChR[i] = RRegData[i];
      ChR[i+1] = RRegData[i+1] + 0x10 * ChanNum;
      ChR[i+2] = RRegData[i+2];
      ChR[i+3] = RRegData[i+3];
   }

   /* Indexed registers */
   ChOff = (Word_t)ChanNum * 0x1000;

   ChP->BaudDiv[0] = (Byte_t)(ChOff + _BAUD);
   ChP->BaudDiv[1] = (Byte_t)((ChOff + _BAUD) >> 8);
   ChP->BaudDiv[2] = (Byte_t)BRD9600;
   ChP->BaudDiv[3] = (Byte_t)(BRD9600 >> 8);
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->BaudDiv));

   ChP->TxControl[0] = (Byte_t)(ChOff + _TX_CTRL);
   ChP->TxControl[1] = (Byte_t)((ChOff + _TX_CTRL) >> 8);
   ChP->TxControl[2] = 0;
   ChP->TxControl[3] = 0;
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->TxControl));

   ChP->RxControl[0] = (Byte_t)(ChOff + _RX_CTRL);
   ChP->RxControl[1] = (Byte_t)((ChOff + _RX_CTRL) >> 8);
   ChP->RxControl[2] = 0;
   ChP->RxControl[3] = 0;
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->RxControl));

   ChP->TxEnables[0] = (Byte_t)(ChOff + _TX_ENBLS);
   ChP->TxEnables[1] = (Byte_t)((ChOff + _TX_ENBLS) >> 8);
   ChP->TxEnables[2] = 0;
   ChP->TxEnables[3] = 0;
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->TxEnables));

   ChP->TxCompare[0] = (Byte_t)(ChOff + _TXCMP1);
   ChP->TxCompare[1] = (Byte_t)((ChOff + _TXCMP1) >> 8);
   ChP->TxCompare[2] = 0;
   ChP->TxCompare[3] = 0;
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->TxCompare));

   ChP->TxReplace1[0] = (Byte_t)(ChOff + _TXREP1B1);
   ChP->TxReplace1[1] = (Byte_t)((ChOff + _TXREP1B1) >> 8);
   ChP->TxReplace1[2] = 0;
   ChP->TxReplace1[3] = 0;
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->TxReplace1));

   ChP->TxReplace2[0] = (Byte_t)(ChOff + _TXREP2);
   ChP->TxReplace2[1] = (Byte_t)((ChOff + _TXREP2) >> 8);
   ChP->TxReplace2[2] = 0;
   ChP->TxReplace2[3] = 0;
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->TxReplace2));

   ChP->TxFIFOPtrs = ChOff + _TXF_OUTP;
   ChP->TxFIFO = ChOff + _TX_FIFO;

   rp_writech1(ChP,_CMD_REG,(Byte_t)ChanNum | RESTXFCNT); /* apply reset Tx FIFO count */
   rp_writech1(ChP,_CMD_REG,(Byte_t)ChanNum);  /* remove reset Tx FIFO count */
   rp_writech2(ChP,_INDX_ADDR,ChP->TxFIFOPtrs); /* clear Tx in/out ptrs */
   rp_writech2(ChP,_INDX_DATA,0);
   ChP->RxFIFOPtrs = ChOff + _RXF_OUTP;
   ChP->RxFIFO = ChOff + _RX_FIFO;

   rp_writech1(ChP,_CMD_REG,(Byte_t)ChanNum | RESRXFCNT); /* apply reset Rx FIFO count */
   rp_writech1(ChP,_CMD_REG,(Byte_t)ChanNum);  /* remove reset Rx FIFO count */
   rp_writech2(ChP,_INDX_ADDR,ChP->RxFIFOPtrs); /* clear Rx out ptr */
   rp_writech2(ChP,_INDX_DATA,0);
   rp_writech2(ChP,_INDX_ADDR,ChP->RxFIFOPtrs + 2); /* clear Rx in ptr */
   rp_writech2(ChP,_INDX_DATA,0);
   ChP->TxPrioCnt = ChOff + _TXP_CNT;
   rp_writech2(ChP,_INDX_ADDR,ChP->TxPrioCnt);
   rp_writech1(ChP,_INDX_DATA,0);
   ChP->TxPrioPtr = ChOff + _TXP_PNTR;
   rp_writech2(ChP,_INDX_ADDR,ChP->TxPrioPtr);
   rp_writech1(ChP,_INDX_DATA,0);
   ChP->TxPrioBuf = ChOff + _TXP_BUF;
   sEnRxProcessor(ChP); 	       /* start the Rx processor */

   return(TRUE);
}

/***************************************************************************
Function: sStopRxProcessor
Purpose:  Stop the receive processor from processing a channel.
Call:	  sStopRxProcessor(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure

Comments: The receive processor can be started again with sStartRxProcessor().
	  This function causes the receive processor to skip over the
	  stopped channel.  It does not stop it from processing other channels.

Warnings: No context switches are allowed while executing this function.

	  Do not leave the receive processor stopped for more than one
	  character time.

	  After calling this function a delay of 4 uS is required to ensure
	  that the receive processor is no longer processing this channel.
*/
void sStopRxProcessor(CHANNEL_T *ChP)
{
   Byte_t R[4];

   R[0] = ChP->R[0];
   R[1] = ChP->R[1];
   R[2] = 0x0a;
   R[3] = ChP->R[3];
   rp_writech4(ChP,_INDX_ADDR,le32dec(R));
}

/***************************************************************************
Function: sFlushRxFIFO
Purpose:  Flush the Rx FIFO
Call:	  sFlushRxFIFO(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   void
Comments: To prevent data from being enqueued or dequeued in the Tx FIFO
	  while it is being flushed the receive processor is stopped
	  and the transmitter is disabled.  After these operations a
	  4 uS delay is done before clearing the pointers to allow
	  the receive processor to stop.  These items are handled inside
	  this function.
Warnings: No context switches are allowed while executing this function.
*/
void sFlushRxFIFO(CHANNEL_T *ChP)
{
   int i;
   Byte_t Ch;			/* channel number within AIOP */
   int RxFIFOEnabled;		       /* TRUE if Rx FIFO enabled */

   if(sGetRxCnt(ChP) == 0)	       /* Rx FIFO empty */
      return;			       /* don't need to flush */

   RxFIFOEnabled = FALSE;
   if(ChP->R[0x32] == 0x08) /* Rx FIFO is enabled */
   {
      RxFIFOEnabled = TRUE;
      sDisRxFIFO(ChP);		       /* disable it */
      for(i=0; i < 2000/200; i++)	/* delay 2 uS to allow proc to disable FIFO*/
	 rp_readch1(ChP,_INT_CHAN);		/* depends on bus i/o timing */
   }
   sGetChanStatus(ChP); 	 /* clear any pending Rx errors in chan stat */
   Ch = (Byte_t)sGetChanNum(ChP);
   rp_writech1(ChP,_CMD_REG,Ch | RESRXFCNT);     /* apply reset Rx FIFO count */
   rp_writech1(ChP,_CMD_REG,Ch);		       /* remove reset Rx FIFO count */
   rp_writech2(ChP,_INDX_ADDR,ChP->RxFIFOPtrs); /* clear Rx out ptr */
   rp_writech2(ChP,_INDX_DATA,0);
   rp_writech2(ChP,_INDX_ADDR,ChP->RxFIFOPtrs + 2); /* clear Rx in ptr */
   rp_writech2(ChP,_INDX_DATA,0);
   if(RxFIFOEnabled)
      sEnRxFIFO(ChP);		       /* enable Rx FIFO */
}

/***************************************************************************
Function: sFlushTxFIFO
Purpose:  Flush the Tx FIFO
Call:	  sFlushTxFIFO(ChP)
	  CHANNEL_T *ChP; Ptr to channel structure
Return:   void
Comments: To prevent data from being enqueued or dequeued in the Tx FIFO
	  while it is being flushed the receive processor is stopped
	  and the transmitter is disabled.  After these operations a
	  4 uS delay is done before clearing the pointers to allow
	  the receive processor to stop.  These items are handled inside
	  this function.
Warnings: No context switches are allowed while executing this function.
*/
void sFlushTxFIFO(CHANNEL_T *ChP)
{
   int i;
   Byte_t Ch;			/* channel number within AIOP */
   int TxEnabled;		       /* TRUE if transmitter enabled */

   if(sGetTxCnt(ChP) == 0)	       /* Tx FIFO empty */
      return;			       /* don't need to flush */

   TxEnabled = FALSE;
   if(ChP->TxControl[3] & TX_ENABLE)
   {
      TxEnabled = TRUE;
      sDisTransmit(ChP);	       /* disable transmitter */
   }
   sStopRxProcessor(ChP);	       /* stop Rx processor */
   for(i = 0; i < 4000/200; i++)	 /* delay 4 uS to allow proc to stop */
      rp_readch1(ChP,_INT_CHAN);	/* depends on bus i/o timing */
   Ch = (Byte_t)sGetChanNum(ChP);
   rp_writech1(ChP,_CMD_REG,Ch | RESTXFCNT);     /* apply reset Tx FIFO count */
   rp_writech1(ChP,_CMD_REG,Ch);		       /* remove reset Tx FIFO count */
   rp_writech2(ChP,_INDX_ADDR,ChP->TxFIFOPtrs); /* clear Tx in/out ptrs */
   rp_writech2(ChP,_INDX_DATA,0);
   if(TxEnabled)
      sEnTransmit(ChP); 	       /* enable transmitter */
   sStartRxProcessor(ChP);	       /* restart Rx processor */
}

/***************************************************************************
Function: sWriteTxPrioByte
Purpose:  Write a byte of priority transmit data to a channel
Call:	  sWriteTxPrioByte(ChP,Data)
	  CHANNEL_T *ChP; Ptr to channel structure
	  Byte_t Data; The transmit data byte

Return:   int: 1 if the bytes is successfully written, otherwise 0.

Comments: The priority byte is transmitted before any data in the Tx FIFO.

Warnings: No context switches are allowed while executing this function.
*/
int sWriteTxPrioByte(CHANNEL_T *ChP, Byte_t Data)
{
   Byte_t DWBuf[4];		/* buffer for double word writes */

   if(sGetTxCnt(ChP) > 1)	       /* write it to Tx priority buffer */
   {
      rp_writech2(ChP,_INDX_ADDR,ChP->TxPrioCnt); /* get priority buffer status */
      if(rp_readch1(ChP,_INDX_DATA) & PRI_PEND) /* priority buffer busy */
	 return(0);		       /* nothing sent */

      le16enc(DWBuf,ChP->TxPrioBuf);   /* data byte address */

      DWBuf[2] = Data;		       /* data byte value */
      DWBuf[3] = 0;		       /* priority buffer pointer */
      rp_writech4(ChP,_INDX_ADDR,le32dec(DWBuf)); /* write it out */

      le16enc(DWBuf,ChP->TxPrioCnt);   /* Tx priority count address */

      DWBuf[2] = PRI_PEND + 1;	       /* indicate 1 byte pending */
      DWBuf[3] = 0;		       /* priority buffer pointer */
      rp_writech4(ChP,_INDX_ADDR,le32dec(DWBuf)); /* write it out */
   }
   else 			       /* write it to Tx FIFO */
   {
      sWriteTxByte(ChP,sGetTxRxDataIO(ChP),Data);
   }
   return(1);			       /* 1 byte sent */
}

/***************************************************************************
Function: sEnInterrupts
Purpose:  Enable one or more interrupts for a channel
Call:	  sEnInterrupts(ChP,Flags)
	  CHANNEL_T *ChP; Ptr to channel structure
	  Word_t Flags: Interrupt enable flags, can be any combination
	     of the following flags:
		TXINT_EN:   Interrupt on Tx FIFO empty
		RXINT_EN:   Interrupt on Rx FIFO at trigger level (see
			    sSetRxTrigger())
		SRCINT_EN:  Interrupt on SRC (Special Rx Condition)
		MCINT_EN:   Interrupt on modem input change
		CHANINT_EN: Allow channel interrupt signal to the AIOP's
			    Interrupt Channel Register.
Return:   void
Comments: If an interrupt enable flag is set in Flags, that interrupt will be
	  enabled.  If an interrupt enable flag is not set in Flags, that
	  interrupt will not be changed.  Interrupts can be disabled with
	  function sDisInterrupts().

	  This function sets the appropriate bit for the channel in the AIOP's
	  Interrupt Mask Register if the CHANINT_EN flag is set.  This allows
	  this channel's bit to be set in the AIOP's Interrupt Channel Register.

	  Interrupts must also be globally enabled before channel interrupts
	  will be passed on to the host.  This is done with function
	  sEnGlobalInt().

	  In some cases it may be desirable to disable interrupts globally but
	  enable channel interrupts.  This would allow the global interrupt
	  status register to be used to determine which AIOPs need service.
*/
void sEnInterrupts(CHANNEL_T *ChP,Word_t Flags)
{
   Byte_t Mask; 		/* Interrupt Mask Register */

   ChP->RxControl[2] |=
      ((Byte_t)Flags & (RXINT_EN | SRCINT_EN | MCINT_EN));

   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->RxControl));

   ChP->TxControl[2] |= ((Byte_t)Flags & TXINT_EN);

   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->TxControl));

   if(Flags & CHANINT_EN)
   {
      Mask = rp_readch1(ChP,_INT_MASK) | rp_sBitMapSetTbl[ChP->ChanNum];
      rp_writech1(ChP,_INT_MASK,Mask);
   }
}

/***************************************************************************
Function: sDisInterrupts
Purpose:  Disable one or more interrupts for a channel
Call:	  sDisInterrupts(ChP,Flags)
	  CHANNEL_T *ChP; Ptr to channel structure
	  Word_t Flags: Interrupt flags, can be any combination
	     of the following flags:
		TXINT_EN:   Interrupt on Tx FIFO empty
		RXINT_EN:   Interrupt on Rx FIFO at trigger level (see
			    sSetRxTrigger())
		SRCINT_EN:  Interrupt on SRC (Special Rx Condition)
		MCINT_EN:   Interrupt on modem input change
		CHANINT_EN: Disable channel interrupt signal to the
			    AIOP's Interrupt Channel Register.
Return:   void
Comments: If an interrupt flag is set in Flags, that interrupt will be
	  disabled.  If an interrupt flag is not set in Flags, that
	  interrupt will not be changed.  Interrupts can be enabled with
	  function sEnInterrupts().

	  This function clears the appropriate bit for the channel in the AIOP's
	  Interrupt Mask Register if the CHANINT_EN flag is set.  This blocks
	  this channel's bit from being set in the AIOP's Interrupt Channel
	  Register.
*/
void sDisInterrupts(CHANNEL_T *ChP,Word_t Flags)
{
   Byte_t Mask; 		/* Interrupt Mask Register */

   ChP->RxControl[2] &=
	 ~((Byte_t)Flags & (RXINT_EN | SRCINT_EN | MCINT_EN));
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->RxControl));
   ChP->TxControl[2] &= ~((Byte_t)Flags & TXINT_EN);
   rp_writech4(ChP,_INDX_ADDR,le32dec(ChP->TxControl));

   if(Flags & CHANINT_EN)
   {
      Mask = rp_readch1(ChP,_INT_MASK) & rp_sBitMapClrTbl[ChP->ChanNum];
      rp_writech1(ChP,_INT_MASK,Mask);
   }
}

/*********************************************************************
  Begin FreeBsd-specific driver code
**********************************************************************/

#define POLL_INTERVAL		(hz / 100)

#define RP_ISMULTIPORT(dev)	((dev)->id_flags & 0x1)
#define RP_MPMASTER(dev)	(((dev)->id_flags >> 8) & 0xff)
#define RP_NOTAST4(dev) 	((dev)->id_flags & 0x04)

/*
 * The top-level routines begin here
 */

static	void	rpclose(struct tty *tp);
static	void	rphardclose(struct tty *tp);
static	int	rpmodem(struct tty *, int, int);
static	int	rpparam(struct tty *, struct termios *);
static	void	rpstart(struct tty *);
static	int	rpioctl(struct tty *, u_long, caddr_t, struct thread *);
static	int	rpopen(struct tty *);

static void rp_do_receive(struct rp_port *rp, struct tty *tp,
			CHANNEL_t *cp, unsigned int ChanStatus)
{
	unsigned	int	CharNStat;
	int	ToRecv, ch, err = 0;

	ToRecv = sGetRxCnt(cp);
	if(ToRecv == 0)
		return;

/*	If status indicates there are errored characters in the
	FIFO, then enter status mode (a word in FIFO holds
	characters and status)
*/

	if(ChanStatus & (RXFOVERFL | RXBREAK | RXFRAME | RXPARITY)) {
		if(!(ChanStatus & STATMODE)) {
			ChanStatus |= STATMODE;
			sEnRxStatusMode(cp);
		}
	}
/*
	if we previously entered status mode then read down the
	FIFO one word at a time, pulling apart the character and
	the status. Update error counters depending on status.
*/
	tty_lock(tp);
	if(ChanStatus & STATMODE) {
		while(ToRecv) {
			CharNStat = rp_readch2(cp,sGetTxRxDataIO(cp));
			ch = CharNStat & 0xff;

			if((CharNStat & STMBREAK) || (CharNStat & STMFRAMEH))
				err |= TRE_FRAMING;
			else if (CharNStat & STMPARITYH)
				err |= TRE_PARITY;
			else if (CharNStat & STMRCVROVRH) {
				rp->rp_overflows++;
				err |= TRE_OVERRUN;
			}

			ttydisc_rint(tp, ch, err);
			ToRecv--;
		}
/*
	After emtying FIFO in status mode, turn off status mode
*/

		if(sGetRxCnt(cp) == 0) {
			sDisRxStatusMode(cp);
		}
	} else {
		ToRecv = sGetRxCnt(cp);
		while (ToRecv) {
			ch = rp_readch1(cp,sGetTxRxDataIO(cp));
			ttydisc_rint(tp, ch & 0xff, err);
			ToRecv--;
		}
	}
        ttydisc_rint_done(tp);
        tty_unlock(tp);
}

static void rp_handle_port(struct rp_port *rp)
{
	CHANNEL_t	*cp;
	struct	tty	*tp;
	unsigned	int	IntMask, ChanStatus;

	if(!rp)
		return;

	cp = &rp->rp_channel;
	tp = rp->rp_tty;
	IntMask = sGetChanIntID(cp);
	IntMask = IntMask & rp->rp_intmask;
	ChanStatus = sGetChanStatus(cp);
	if(IntMask & RXF_TRIG)
		rp_do_receive(rp, tp, cp, ChanStatus);
	if(IntMask & DELTA_CD) {
		if(ChanStatus & CD_ACT) {
			(void)ttydisc_modem(tp, 1);
		} else {
			(void)ttydisc_modem(tp, 0);
		}
	}
/*	oldcts = rp->rp_cts;
	rp->rp_cts = ((ChanStatus & CTS_ACT) != 0);
	if(oldcts != rp->rp_cts) {
		printf("CTS change (now %s)... on port %d\n", rp->rp_cts ? "on" : "off", rp->rp_port);
	}
*/
}

static void rp_do_poll(void *arg)
{
	CONTROLLER_t	*ctl;
	struct rp_port	*rp;
	struct tty	*tp;
	int	count;
	unsigned char	CtlMask, AiopMask;

	rp = arg;
	tp = rp->rp_tty;
	tty_lock_assert(tp, MA_OWNED);
	ctl = rp->rp_ctlp;
	CtlMask = ctl->ctlmask(ctl);
	if (CtlMask & (1 << rp->rp_aiop)) {
		AiopMask = sGetAiopIntStatus(ctl, rp->rp_aiop);
		if (AiopMask & (1 << rp->rp_chan)) {
			rp_handle_port(rp);
		}
	}

	count = sGetTxCnt(&rp->rp_channel);
	if (count >= 0  && (count <= rp->rp_restart)) {
		rpstart(tp);
	}
	callout_schedule(&rp->rp_timer, POLL_INTERVAL);
}

static struct ttydevsw rp_tty_class = {
	.tsw_flags	= TF_INITLOCK|TF_CALLOUT,
	.tsw_open	= rpopen,
	.tsw_close	= rpclose,
	.tsw_outwakeup	= rpstart,
	.tsw_ioctl	= rpioctl,
	.tsw_param	= rpparam,
	.tsw_modem	= rpmodem,
	.tsw_free	= rpfree,
};


static void
rpfree(void *softc)
{
	struct	rp_port *rp = softc;
	CONTROLLER_t *ctlp = rp->rp_ctlp;

	atomic_subtract_32(&ctlp->free, 1);
}

int
rp_attachcommon(CONTROLLER_T *ctlp, int num_aiops, int num_ports)
{
	int	unit;
	int	num_chan;
	int	aiop, chan, port;
	int	ChanStatus;
	int	retval;
	struct	rp_port *rp;
	struct tty *tp;

	unit = device_get_unit(ctlp->dev);

	printf("RocketPort%d (Version %s) %d ports.\n", unit,
		RocketPortVersion, num_ports);

	ctlp->num_ports = num_ports;
	ctlp->rp = rp = (struct rp_port *)
		malloc(sizeof(struct rp_port) * num_ports, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rp == NULL) {
		device_printf(ctlp->dev, "rp_attachcommon: Could not malloc rp_ports structures.\n");
		retval = ENOMEM;
		goto nogo;
	}

	port = 0;
	for(aiop=0; aiop < num_aiops; aiop++) {
		num_chan = sGetAiopNumChan(ctlp, aiop);
		for(chan=0; chan < num_chan; chan++, port++, rp++) {
			rp->rp_tty = tp = tty_alloc(&rp_tty_class, rp);
			callout_init_mtx(&rp->rp_timer, tty_getlock(tp), 0);
			rp->rp_port = port;
			rp->rp_ctlp = ctlp;
			rp->rp_unit = unit;
			rp->rp_chan = chan;
			rp->rp_aiop = aiop;

			rp->rp_intmask = RXF_TRIG | TXFIFO_MT | SRC_INT |
				DELTA_CD | DELTA_CTS | DELTA_DSR;
#ifdef notdef
			ChanStatus = sGetChanStatus(&rp->rp_channel);
#endif /* notdef */
			if(sInitChan(ctlp, &rp->rp_channel, aiop, chan) == 0) {
				device_printf(ctlp->dev, "RocketPort sInitChan(%d, %d, %d) failed.\n",
					      unit, aiop, chan);
				retval = ENXIO;
				goto nogo;
			}
			ChanStatus = sGetChanStatus(&rp->rp_channel);
			rp->rp_cts = (ChanStatus & CTS_ACT) != 0;
			tty_makedev(tp, NULL, "R%r%r", unit, port);
		}
	}

	mtx_init(&ctlp->hwmtx, "rp_hwmtx", NULL, MTX_DEF);
	ctlp->hwmtx_init = 1;
	return (0);

nogo:
	rp_releaseresource(ctlp);

	return (retval);
}

void
rp_releaseresource(CONTROLLER_t *ctlp)
{
	struct	rp_port *rp;
	int i;

	if (ctlp->rp != NULL) {
		for (i = 0; i < ctlp->num_ports; i++) {
			rp = ctlp->rp + i;
			atomic_add_32(&ctlp->free, 1);
			tty_lock(rp->rp_tty);
			tty_rel_gone(rp->rp_tty);
		}
                free(ctlp->rp, M_DEVBUF);
                ctlp->rp = NULL;
	}

	while (ctlp->free != 0) {
		pause("rpwt", hz / 10);
	}

	if (ctlp->hwmtx_init)
		mtx_destroy(&ctlp->hwmtx);
}

static int
rpopen(struct tty *tp)
{
	struct	rp_port *rp;
	int	flags;
	unsigned int	IntMask, ChanStatus;

	rp = tty_softc(tp);

	flags = 0;
	flags |= SET_RTS;
	flags |= SET_DTR;
	rp->rp_channel.TxControl[3] =
		((rp->rp_channel.TxControl[3]
		& ~(SET_RTS | SET_DTR)) | flags);
	rp_writech4(&rp->rp_channel,_INDX_ADDR,
		le32dec(rp->rp_channel.TxControl));
	sSetRxTrigger(&rp->rp_channel, TRIG_1);
	sDisRxStatusMode(&rp->rp_channel);
	sFlushRxFIFO(&rp->rp_channel);
	sFlushTxFIFO(&rp->rp_channel);

	sEnInterrupts(&rp->rp_channel,
		(TXINT_EN|MCINT_EN|RXINT_EN|SRCINT_EN|CHANINT_EN));
	sSetRxTrigger(&rp->rp_channel, TRIG_1);

	sDisRxStatusMode(&rp->rp_channel);
	sClrTxXOFF(&rp->rp_channel);

/*	sDisRTSFlowCtl(&rp->rp_channel);
	sDisCTSFlowCtl(&rp->rp_channel);
*/
	sDisTxSoftFlowCtl(&rp->rp_channel);

	sStartRxProcessor(&rp->rp_channel);

	sEnRxFIFO(&rp->rp_channel);
	sEnTransmit(&rp->rp_channel);

/*	sSetDTR(&rp->rp_channel);
	sSetRTS(&rp->rp_channel);
*/

	IntMask = sGetChanIntID(&rp->rp_channel);
	IntMask = IntMask & rp->rp_intmask;
	ChanStatus = sGetChanStatus(&rp->rp_channel);

	callout_reset(&rp->rp_timer, POLL_INTERVAL, rp_do_poll, rp);

	device_busy(rp->rp_ctlp->dev);
	return(0);
}

static void
rpclose(struct tty *tp)
{
	struct	rp_port	*rp;

	rp = tty_softc(tp);
	callout_stop(&rp->rp_timer);
	rphardclose(tp);
	device_unbusy(rp->rp_ctlp->dev);
}

static void
rphardclose(struct tty *tp)
{
	struct	rp_port	*rp;
	CHANNEL_t	*cp;

	rp = tty_softc(tp);
	cp = &rp->rp_channel;

	sFlushRxFIFO(cp);
	sFlushTxFIFO(cp);
	sDisTransmit(cp);
	sDisInterrupts(cp, TXINT_EN|MCINT_EN|RXINT_EN|SRCINT_EN|CHANINT_EN);
	sDisRTSFlowCtl(cp);
	sDisCTSFlowCtl(cp);
	sDisTxSoftFlowCtl(cp);
	sClrTxXOFF(cp);

#ifdef DJA
	if(tp->t_cflag&HUPCL || !(tp->t_state&TS_ISOPEN) || !tp->t_actout) {
		sClrDTR(cp);
	}
	if(ISCALLOUT(tp->t_dev)) {
		sClrDTR(cp);
	}
	tp->t_actout = FALSE;
	wakeup(&tp->t_actout);
	wakeup(TSA_CARR_ON(tp));
#endif /* DJA */
}

static int
rpioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
	struct rp_port	*rp;

	rp = tty_softc(tp);
	switch (cmd) {
	case TIOCSBRK:
		sSendBreak(&rp->rp_channel);
		return (0);
	case TIOCCBRK:
		sClrBreak(&rp->rp_channel);
		return (0);
	default:
		return ENOIOCTL;
	}
}

static int
rpmodem(struct tty *tp, int sigon, int sigoff)
{
	struct rp_port	*rp;
	int i, j, k;

	rp = tty_softc(tp);
	if (sigon != 0 || sigoff != 0) {
		i = j = 0;
		if (sigon & SER_DTR)
			i = SET_DTR;
		if (sigoff & SER_DTR)
			j = SET_DTR;
		if (sigon & SER_RTS)
			i = SET_RTS;
		if (sigoff & SER_RTS)
			j = SET_RTS;
		rp->rp_channel.TxControl[3] &= ~i;
		rp->rp_channel.TxControl[3] |= j;
		rp_writech4(&rp->rp_channel,_INDX_ADDR,
			le32dec(rp->rp_channel.TxControl));
	} else {
		i = sGetChanStatusLo(&rp->rp_channel);
		j = rp->rp_channel.TxControl[3];
		k = 0;
		if (j & SET_DTR)
			k |= SER_DTR;
		if (j & SET_RTS)
			k |= SER_RTS;
		if (i & CD_ACT)
			k |= SER_DCD;
		if (i & DSR_ACT)
			k |= SER_DSR;
		if (i & CTS_ACT)
			k |= SER_CTS;
		return(k);
	}
	return (0);
}

static struct
{
	int baud;
	int conversion;
} baud_table[] = {
	{B0,	0},		{B50,	BRD50},		{B75,	BRD75},
	{B110,	BRD110}, 	{B134,	BRD134}, 	{B150,	BRD150},
	{B200,	BRD200}, 	{B300,	BRD300}, 	{B600,	BRD600},
	{B1200,	BRD1200},	{B1800,	BRD1800},	{B2400,	BRD2400},
	{B4800,	BRD4800},	{B9600,	BRD9600},	{B19200, BRD19200},
	{B38400, BRD38400},	{B7200,	BRD7200},	{B14400, BRD14400},
				{B57600, BRD57600},	{B76800, BRD76800},
	{B115200, BRD115200},	{B230400, BRD230400},
	{-1,	-1}
};

static int rp_convert_baud(int baud) {
	int i;

	for (i = 0; baud_table[i].baud >= 0; i++) {
		if (baud_table[i].baud == baud)
			break;
	}

	return baud_table[i].conversion;
}

static int
rpparam(tp, t)
	struct tty *tp;
	struct termios *t;
{
	struct rp_port	*rp;
	CHANNEL_t	*cp;
	int	cflag, iflag, oflag, lflag;
	int	ospeed;
#ifdef RPCLOCAL
	int	devshift;
#endif

	rp = tty_softc(tp);
	cp = &rp->rp_channel;

	cflag = t->c_cflag;
#ifdef RPCLOCAL
	devshift = umynor / 32;
	devshift = 1 << devshift;
	if ( devshift & RPCLOCAL ) {
		cflag |= CLOCAL;
	}
#endif
	iflag = t->c_iflag;
	oflag = t->c_oflag;
	lflag = t->c_lflag;

	ospeed = rp_convert_baud(t->c_ispeed);
	if(ospeed < 0 || t->c_ispeed != t->c_ospeed)
		return(EINVAL);

	if(t->c_ospeed == 0) {
		sClrDTR(cp);
		return(0);
	}
	rp->rp_fifo_lw = ((t->c_ospeed*2) / 1000) +1;

	/* Set baud rate ----- we only pay attention to ispeed */
	sSetDTR(cp);
	sSetRTS(cp);
	sSetBaud(cp, ospeed);

	if(cflag & CSTOPB) {
		sSetStop2(cp);
	} else {
		sSetStop1(cp);
	}

	if(cflag & PARENB) {
		sEnParity(cp);
		if(cflag & PARODD) {
			sSetOddParity(cp);
		} else {
			sSetEvenParity(cp);
		}
	}
	else {
		sDisParity(cp);
	}
	if((cflag & CSIZE) == CS8) {
		sSetData8(cp);
		rp->rp_imask = 0xFF;
	} else {
		sSetData7(cp);
		rp->rp_imask = 0x7F;
	}

	if(iflag & ISTRIP) {
		rp->rp_imask &= 0x7F;
	}

	if(cflag & CLOCAL) {
		rp->rp_intmask &= ~DELTA_CD;
	} else {
		rp->rp_intmask |= DELTA_CD;
	}

	/* Put flow control stuff here */

	if(cflag & CCTS_OFLOW) {
		sEnCTSFlowCtl(cp);
	} else {
		sDisCTSFlowCtl(cp);
	}

	if(cflag & CRTS_IFLOW) {
		rp->rp_rts_iflow = 1;
	} else {
		rp->rp_rts_iflow = 0;
	}

	if(cflag & CRTS_IFLOW) {
		sEnRTSFlowCtl(cp);
	} else {
		sDisRTSFlowCtl(cp);
	}

	return(0);
}

static void
rpstart(struct tty *tp)
{
	struct rp_port	*rp;
	CHANNEL_t	*cp;
	char	flags;
	int	xmit_fifo_room;
	int	i, count, wcount;

	rp = tty_softc(tp);
	cp = &rp->rp_channel;
	flags = rp->rp_channel.TxControl[3];

	if(rp->rp_xmit_stopped) {
		sEnTransmit(cp);
		rp->rp_xmit_stopped = 0;
	}

	xmit_fifo_room = TXFIFO_SIZE - sGetTxCnt(cp);
	count = ttydisc_getc(tp, &rp->TxBuf, xmit_fifo_room);
	if(xmit_fifo_room > 0) {
		for( i = 0, wcount = count >> 1; wcount > 0; i += 2, wcount-- ) {
			rp_writech2(cp, sGetTxRxDataIO(cp), le16dec(&rp->TxBuf[i]));
		}
		if ( count & 1 ) {
			rp_writech1(cp, sGetTxRxDataIO(cp), rp->TxBuf[(count-1)]);
		}
	}
}
