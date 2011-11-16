/*
 * sound\soc\sun4i\spdif\sun4i_spdif.h
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef SUN4I_SPDIF_H_
#define SUN4I_SPDIF_H_

/*------------------SPDIF register definition--------------------*/
#define SUN4I_SPDIFBASE 0x01C21000

#define	SUN4I_SPDIF_CTL	(0x00)
	#define SUN4I_SPDIF_CTL_MCLKDIV(v)		((v)<<4)		//v even
	#define SUN4I_SPDIF_CTL_MCLKOUTEN			(1<<2)
	#define SUN4I_SPDIF_CTL_GEN						(1<<1)
	#define SUN4I_SPDIF_CTL_RESET					(1<<0)
	
#define SUN4I_SPDIF_TXCFG (0x04)
	#define SUN4I_SPDIF_TXCFG_SINGLEMOD		(1<<31)
	#define SUN4I_SPDIF_TXCFG_ASS					(1<<17)
	#define SUN4I_SPDIF_TXCFG_NONAUDIO		(1<<16)
	#define SUN4I_SPDIF_TXCFG_TXRATIO(v)	((v)<<4)
	#define SUN4I_SPDIF_TXCFG_FMTRVD			(3<<2)
	#define SUN4I_SPDIF_TXCFG_FMT16BIT		(0<<2)
	#define SUN4I_SPDIF_TXCFG_FMT20BIT		(1<<2)
	#define SUN4I_SPDIF_TXCFG_FMT24BIT		(2<<2)
	#define SUN4I_SPDIF_TXCFG_CHSTMODE		(1<<1)
	#define SUN4I_SPDIF_TXCFG_TXEN				(1<<0)
	
#define SUN4I_SPDIF_RXCFG (0x08)
	#define SUN4I_SPDIF_RXCFG_LOCKFLAG		(1<<4)
	#define SUN4I_SPDIF_RXCFG_CHSTSRC			(1<<3)
	#define SUN4I_SPDIF_RXCFG_CHSTCP			(1<<1)
	#define SUN4I_SPDIF_RXCFG_RXEN				(1<<0)
	
#define SUN4I_SPDIF_TXFIFO (0x0C)

#define SUN4I_SPDIF_RXFIFO (0x10)

#define SUN4I_SPDIF_FCTL (0x14)
	#define SUN4I_SPDIF_FCTL_FIFOSRC			(1<<31)
	#define SUN4I_SPDIF_FCTL_FTX					(1<<17)
	#define SUN4I_SPDIF_FCTL_FRX					(1<<16)
	#define SUN4I_SPDIF_FCTL_TXTL(v)			((v)<<8)
	#define SUN4I_SPDIF_FCTL_RXTL(v)			(((v))<<3)
	#define SUN4I_SPDIF_FCTL_TXIM(v)			((v)<<2)
	#define SUN4I_SPDIF_FCTL_RXOM(v)			((v)<<0)
	
#define SUN4I_SPDIF_FSTA (0x18)
	#define SUN4I_SPDIF_FSTA_TXE					(1<<14)
	#define SUN4I_SPDIF_FSTA_TXECNTSHT		(8)	 
	#define SUN4I_SPDIF_FSTA_RXA					(1<<6)
	#define SUN4I_SPDIF_FSTA_RXACNTSHT		(0)
	
#define SUN4I_SPDIF_INT (0x1C)
	#define SUN4I_SPDIF_INT_RXLOCKEN			(1<<18)
	#define SUN4I_SPDIF_INT_RXUNLOCKEN		(1<<17)
	#define SUN4I_SPDIF_INT_RXPARERREN		(1<<16)
	#define SUN4I_SPDIF_INT_TXDRQEN				(1<<7)
	#define SUN4I_SPDIF_INT_TXUIEN				(1<<6)
	#define SUN4I_SPDIF_INT_TXOIEN				(1<<5)
	#define SUN4I_SPDIF_INT_TXEIEN				(1<<4)
	#define SUN4I_SPDIF_INT_RXDRQEN				(1<<2)
	#define SUN4I_SPDIF_INT_RXOIEN				(1<<1)
	#define SUN4I_SPDIF_INT_RXAIEN				(1<<0)
	
#define SUN4I_SPDIF_ISTA (0x20)
	#define SUN4I_SPDIF_ISTA_RXLOCKSTA		(1<<18)
	#define SUN4I_SPDIF_ISTA_RXUNLOCKSTA	(1<<17)
	#define SUN4I_SPDIF_ISTA_RXPARERRSTA	(1<<16)
	#define SUN4I_SPDIF_ISTA_TXUSTA				(1<<6)
	#define SUN4I_SPDIF_ISTA_TXOSTA				(1<<5)
	#define SUN4I_SPDIF_ISTA_TXESTA				(1<<4)
	#define SUN4I_SPDIF_ISTA_RXOSTA				(1<<1)
	#define SUN4I_SPDIF_ISTA_RXASTA				(1<<0)
	
#define SUN4I_SPDIF_TXCNT	(0x24)

#define SUN4I_SPDIF_RXCNT	(0x28)

#define SUN4I_SPDIF_TXCHSTA0 (0x2C)
	#define SUN4I_SPDIF_TXCHSTA0_CLK(v)					((v)<<28)
	#define SUN4I_SPDIF_TXCHSTA0_SAMFREQ(v)			((v)<<24)
	#define SUN4I_SPDIF_TXCHSTA0_CHNUM(v)				((v)<<20)
	#define SUN4I_SPDIF_TXCHSTA0_SRCNUM(v)			((v)<<16)
	#define SUN4I_SPDIF_TXCHSTA0_CATACOD(v)			((v)<<8)
	#define SUN4I_SPDIF_TXCHSTA0_MODE(v)				((v)<<6)
	#define SUN4I_SPDIF_TXCHSTA0_EMPHASIS(v)	  ((v)<<3)
	#define SUN4I_SPDIF_TXCHSTA0_CP							(1<<2)
	#define SUN4I_SPDIF_TXCHSTA0_AUDIO					(1<<1)
	#define SUN4I_SPDIF_TXCHSTA0_PRO						(1<<0)

#define SUN4I_SPDIF_TXCHSTA1 (0x30)
	#define SUN4I_SPDIF_TXCHSTA1_CGMSA(v)				((v)<<8)
	#define SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(v)	((v)<<4)
	#define SUN4I_SPDIF_TXCHSTA1_SAMWORDLEN(v)	((v)<<1)
	#define SUN4I_SPDIF_TXCHSTA1_MAXWORDLEN			(1<<0)
	
#define SUN4I_SPDIF_RXCHSTA0 (0x34)
	#define SUN4I_SPDIF_RXCHSTA0_CLK(v)					((v)<<28)
	#define SUN4I_SPDIF_RXCHSTA0_SAMFREQ(v)			((v)<<24)
	#define SUN4I_SPDIF_RXCHSTA0_CHNUM(v)				((v)<<20)
	#define SUN4I_SPDIF_RXCHSTA0_SRCNUM(v)			((v)<<16)
	#define SUN4I_SPDIF_RXCHSTA0_CATACOD(v)			((v)<<8)
	#define SUN4I_SPDIF_RXCHSTA0_MODE(v)				((v)<<6)
	#define SUN4I_SPDIF_RXCHSTA0_EMPHASIS(v)	  ((v)<<3)
	#define SUN4I_SPDIF_RXCHSTA0_CP							(1<<2)
	#define SUN4I_SPDIF_RXCHSTA0_AUDIO					(1<<1)
	#define SUN4I_SPDIF_RXCHSTA0_PRO						(1<<0)

#define SUN4I_SPDIF_RXCHSTA1 (0x38)
	#define SUN4I_SPDIF_RXCHSTA1_CGMSA(v)				((v)<<8)
	#define SUN4I_SPDIF_RXCHSTA1_ORISAMFREQ(v)	((v)<<4)
	#define SUN4I_SPDIF_RXCHSTA1_SAMWORDLEN(v)	((v)<<1)
	#define SUN4I_SPDIF_RXCHSTA1_MAXWORDLEN			(1<<0)

/*--------------------------------CCM register definition---------------------*/
#define SUN4I_CCMBASE (0x01C20000)

#define SUN4I_CCMBASE_AUDIOHOSCPLL (0x08)
	#define SUN4I_CCMBASE_AUDIOHOSCPLL_EN			(1<<31)
	#define SUN4I_CCMBASE_AUDIOHOSCPLL_24576M		(1<<27)
	#define SUN4I_CCMBASE_AUDIOHOSCPLL_225792M 		(0<<27)
	
#define SUN4I_CCMBASE_APBGATE	(0x68)
	#define SUN4I_CCMBASE_APBGATE_SPDIFGATE	(1<<1)

#define SUN4I_CCMBASE_AUDIOCLK (0xC0)
	#define SUN4I_CCMBASE_AUDIOCLK_SPDIFSPEGATE	(1<<31)
	#define SUN4I_CCMBASE_AUDIOCLK_DIV(v)			((v)<<16)

	/* Clock dividers */
	#define SUN4I_DIV_MCLK	0
	#define SUN4I_DIV_BCLK	1


struct sun4i_spdif_info {
	void __iomem   *regs;    /* IIS BASE */
	void __iomem   *ccmregs;  //CCM BASE
	void __iomem   *ioregs;   //IO BASE

};

extern struct sun4i_spdif_info sun4i_spdif;

unsigned int sun4i_spdif_get_clockrate(void);

extern void sun4i_snd_txctrl(struct snd_pcm_substream *substream, int on);
extern void sun4i_snd_rxctrl(int on);

#endif
