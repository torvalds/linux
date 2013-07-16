/*
 * sound\soc\sunxi\i2s\sunxi-i2s.h
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

#ifndef SUNXI_I2S_H_
#define SUNXI_I2S_H_

/*------------------------------------------------------------*/
/* REGISTER definition */

/* IIS REGISTER */
#define SUNXI_IISBASE 		(0x01C22400)

#define SUNXI_IISCTL 	  (0x00)
	#define SUNXI_IISCTL_SDO3EN		(1<<11)
	#define SUNXI_IISCTL_SDO2EN		(1<<10)
	#define SUNXI_IISCTL_SDO1EN		(1<<9)
	#define SUNXI_IISCTL_SDO0EN		(1<<8)
	#define SUNXI_IISCTL_ASS			(1<<6)
	#define SUNXI_IISCTL_MS			(1<<5)
	#define SUNXI_IISCTL_PCM			(1<<4)
	#define SUNXI_IISCTL_LOOP			(1<<3)
	#define SUNXI_IISCTL_TXEN			(1<<2)
	#define SUNXI_IISCTL_RXEN			(1<<1)
	#define SUNXI_IISCTL_GEN			(1<<0)

#define SUNXI_IISFAT0 		(0x04)
	#define SUNXI_IISFAT0_LRCP					(1<<7)
	#define SUNXI_IISFAT0_BCP					(1<<6)
	#define SUNXI_IISFAT0_SR_RVD				(3<<4)
	#define SUNXI_IISFAT0_SR_16BIT				(0<<4)
	#define	SUNXI_IISFAT0_SR_20BIT				(1<<4)
	#define SUNXI_IISFAT0_SR_24BIT				(2<<4)
	#define SUNXI_IISFAT0_WSS_16BCLK			(0<<2)
	#define SUNXI_IISFAT0_WSS_20BCLK			(1<<2)
	#define SUNXI_IISFAT0_WSS_24BCLK			(2<<2)
	#define SUNXI_IISFAT0_WSS_32BCLK			(3<<2)
	#define SUNXI_IISFAT0_FMT_I2S				(0<<0)
	#define SUNXI_IISFAT0_FMT_LFT				(1<<0)
	#define SUNXI_IISFAT0_FMT_RGT				(2<<0)
	#define SUNXI_IISFAT0_FMT_RVD				(3<<0)

#define SUNXI_IISFAT1		(0x08)
	#define SUNXI_IISFAT1_SYNCLEN_16BCLK		(0<<12)
	#define SUNXI_IISFAT1_SYNCLEN_32BCLK		(1<<12)
	#define SUNXI_IISFAT1_SYNCLEN_64BCLK		(2<<12)
	#define SUNXI_IISFAT1_SYNCLEN_128BCLK		(3<<12)
	#define SUNXI_IISFAT1_SYNCLEN_256BCLK		(4<<12)
	#define SUNXI_IISFAT1_SYNCOUTEN			(1<<11)
	#define SUNXI_IISFAT1_OUTMUTE 				(1<<10)
	#define SUNXI_IISFAT1_MLS		 			(1<<9)
	#define SUNXI_IISFAT1_SEXT		 			(1<<8)
	#define SUNXI_IISFAT1_SI_1ST				(0<<6)
	#define SUNXI_IISFAT1_SI_2ND			 	(1<<6)
	#define SUNXI_IISFAT1_SI_3RD			 	(2<<6)
	#define SUNXI_IISFAT1_SI_4TH			 	(3<<6)
	#define SUNXI_IISFAT1_SW			 		(1<<5)
	#define SUNXI_IISFAT1_SSYNC	 			(1<<4)
	#define SUNXI_IISFAT1_RXPDM_16PCM			(0<<2)
	#define SUNXI_IISFAT1_RXPDM_8PCM			(1<<2)
	#define SUNXI_IISFAT1_RXPDM_8ULAW			(2<<2)
	#define SUNXI_IISFAT1_RXPDM_8ALAW  		(3<<2)
	#define SUNXI_IISFAT1_TXPDM_16PCM			(0<<0)
	#define SUNXI_IISFAT1_TXPDM_8PCM			(1<<0)
	#define SUNXI_IISFAT1_TXPDM_8ULAW			(2<<0)
	#define SUNXI_IISFAT1_TXPDM_8ALAW  		(3<<0)

#define SUNXI_IISTXFIFO 	(0x0C)

#define SUNXI_IISRXFIFO 	(0x10)

#define SUNXI_IISFCTL  	(0x14)
	#define SUNXI_IISFCTL_FIFOSRC			(1<<31)
	#define SUNXI_IISFCTL_FTX				(1<<25)
	#define SUNXI_IISFCTL_FRX				(1<<24)
	#define SUNXI_IISFCTL_TXTL(v)			((v)<<12)
	#define SUNXI_IISFCTL_RXTL(v)  		((v)<<4)
	#define SUNXI_IISFCTL_TXIM_MOD0		(0<<2)
	#define SUNXI_IISFCTL_TXIM_MOD1		(1<<2)
	#define SUNXI_IISFCTL_RXOM_MOD0		(0<<0)
	#define SUNXI_IISFCTL_RXOM_MOD1		(1<<0)
	#define SUNXI_IISFCTL_RXOM_MOD2		(2<<0)
	#define SUNXI_IISFCTL_RXOM_MOD3		(3<<0)

#define SUNXI_IISFSTA   	(0x18)
	#define SUNXI_IISFSTA_TXE				(1<<28)
	#define SUNXI_IISFSTA_TXECNT(v)		((v)<<16)
	#define SUNXI_IISFSTA_RXA				(1<<8)
	#define SUNXI_IISFSTA_RXACNT(v)		((v)<<0)

#define SUNXI_IISINT    	(0x1C)
	#define SUNXI_IISINT_TXDRQEN				(1<<7)
	#define SUNXI_IISINT_TXUIEN				(1<<6)
	#define SUNXI_IISINT_TXOIEN				(1<<5)
	#define SUNXI_IISINT_TXEIEN				(1<<4)
	#define SUNXI_IISINT_RXDRQEN				(1<<2)
	#define SUNXI_IISINT_RXOIEN				(1<<1)
	#define SUNXI_IISINT_RXAIEN				(1<<0)

#define SUNXI_IISISTA   	(0x20)
	#define SUNXI_IISISTA_TXUISTA			(1<<6)
	#define SUNXI_IISISTA_TXOISTA			(1<<5)
	#define SUNXI_IISISTA_TXEISTA			(1<<4)
	#define SUNXI_IISISTA_RXOISTA			(1<<1)
	#define SUNXI_IISISTA_RXAISTA			(1<<0)

#define SUNXI_IISCLKD   	(0x24)
	#define SUNXI_IISCLKD_MCLKOEN			(1<<7)
	#define SUNXI_IISCLKD_BCLKDIV_2		(0<<4)
	#define SUNXI_IISCLKD_BCLKDIV_4		(1<<4)
	#define SUNXI_IISCLKD_BCLKDIV_6		(2<<4)
	#define SUNXI_IISCLKD_BCLKDIV_8		(3<<4)
	#define SUNXI_IISCLKD_BCLKDIV_12		(4<<4)
	#define SUNXI_IISCLKD_BCLKDIV_16		(5<<4)
	#define SUNXI_IISCLKD_BCLKDIV_32		(6<<4)
	#define SUNXI_IISCLKD_BCLKDIV_64		(7<<4)
	#define SUNXI_IISCLKD_MCLKDIV_1		(0<<0)
	#define SUNXI_IISCLKD_MCLKDIV_2		(1<<0)
	#define SUNXI_IISCLKD_MCLKDIV_4		(2<<0)
	#define SUNXI_IISCLKD_MCLKDIV_6		(3<<0)
	#define SUNXI_IISCLKD_MCLKDIV_8		(4<<0)
	#define SUNXI_IISCLKD_MCLKDIV_12		(5<<0)
	#define SUNXI_IISCLKD_MCLKDIV_16		(6<<0)
	#define SUNXI_IISCLKD_MCLKDIV_24		(7<<0)
	#define SUNXI_IISCLKD_MCLKDIV_32		(8<<0)
	#define SUNXI_IISCLKD_MCLKDIV_48		(9<<0)
	#define SUNXI_IISCLKD_MCLKDIV_64		(10<<0)

#define SUNXI_IISTXCNT  	(0x28)

#define SUNXI_IISRXCNT  	(0x2C)

#define SUNXI_TXCHSEL		(0x30)
	#define SUNXI_TXCHSEL_CHNUM(v)			(((v)-1)<<0)

#define SUNXI_TXCHMAP		(0x34)
	#define SUNXI_TXCHMAP_CH7(v)			(((v)-1)<<28)
	#define SUNXI_TXCHMAP_CH6(v)			(((v)-1)<<24)
	#define SUNXI_TXCHMAP_CH5(v)			(((v)-1)<<20)
	#define SUNXI_TXCHMAP_CH4(v)			(((v)-1)<<16)
	#define SUNXI_TXCHMAP_CH3(v)			(((v)-1)<<12)
	#define SUNXI_TXCHMAP_CH2(v)			(((v)-1)<<8)
	#define SUNXI_TXCHMAP_CH1(v)			(((v)-1)<<4)
	#define SUNXI_TXCHMAP_CH0(v)			(((v)-1)<<0)

#define SUNXI_RXCHSEL		(0x38)
	#define SUNXI_RXCHSEL_CHNUM(v)			(((v)-1)<<0)

#define SUNXI_RXCHMAP		(0x3C)
	#define SUNXI_RXCHMAP_CH3(v)			(((v)-1)<<12)
	#define SUNXI_RXCHMAP_CH2(v)			(((v)-1)<<8)
	#define SUNXI_RXCHMAP_CH1(v)			(((v)-1)<<4)
	#define SUNXI_RXCHMAP_CH0(v)			(((v)-1)<<0)


/* DMA REGISTER */
#define SUNXI_DMABASE	(0x01C02000)

#define SUNXI_DMAIRQEN						(0x0)
	#define SUNXI_DMAIRQEN_NDMA_FULLEN(v)				(1<<((v)*2+1))
	#define SUNXI_DMAIRQEN_NDMA_HALFEN(v)				(1<<((v)*2))

#define SUNXI_DMAIRQPENDING	 		(0x4)
	#define SUNXI_DMAIRQPENGDING_NDMA_FULLPEND(v)		(1<<((v)*2+1))
	#define SUNXI_DMAIRQPENGDING_NDMA_HALFPEND(v)		(1<<((v)*2))

#define SUNXI_NDMACFG(v)				((v)*0x20+0x100)
	#define SUNXI_NDMACFG_DMALOAD					(1<<31)
	#define SUNXI_NDMACFG_BUSY						(1<<30)
	#define SUNXI_NDMACFG_CONTINUOUS				(1<<29)
	#define SUNXI_NDMACFG_WAIT(v)					(((v)-1)<<26)   //wait clock = 2^n  example: 8 clocks = 2^3
	#define SUNXI_NDMACFG_DSTDATAWIDTH_8BIT		(0<<24)
	#define SUNXI_NDMACFG_DSTDATAWIDTH_16BIT		(1<<24)
	#define SUNXI_NDMACFG_DSTDATAWIDTH_32BIT		(2<<24)
	#define SUNXI_NDMACFG_DSTDATAWIDTH_RVD			(3<<24)
	#define SUNXI_NDMACFG_DSTBURST4				(1<<23)
	#define SUNXI_NDMACFG_DSTADDRTYPE_INC			(0<<21)
	#define SUNXI_NDMACFG_DSTADDRTYPE_CON 			(1<<21)
	#define SUNXI_NDMACFG_DSTTYPE_IRTX				(0x0<<16)
	#define SUNXI_NDMACFG_DSTTYPE_SPDIFTX			(0x1<<16)
	#define SUNXI_NDMACFG_DSTTYPE_IISTX			(0x2<<16)
	#define SUNXI_NDMACFG_DSTTYPE_AC97TX			(0x3<<16)
	#define SUNXI_NDMACFG_DSTTYPE_SPI0TX 			(0x4<<16)
	#define SUNXI_NDMACFG_DSTTYPE_SPI1TX			(0x5<<16)
	#define SUNXI_NDMACFG_DSTTYPE_SPI2TX			(0x6<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART0TX			(0x8<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART1TX			(0x9<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART2TX			(0xA<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART3TX			(0xB<<16)
	#define SUNXI_NDMACFG_DSTTYPE_AUDIODA			(0xC<<16)
	#define SUNXI_NDMACFG_DSTTYPE_NFC				(0xF<<16)
	#define SUNXI_NDMACFG_DSTTYPE_SRAM				(0x10<<16)
	#define SUNXI_NDMACFG_DSTTYPE_DRAM				(0x11<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART4TX			(0x12<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART5TX          (0x13<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART6TX			(0x14<<16)
	#define SUNXI_NDMACFG_DSTTYPE_UART7TX			(0x15<<16)
	#define SUNXI_NDMACFG_SRCDATAWIDTH_8BIT		(0<<8)
	#define SUNXI_NDMACFG_SRCDATAWIDTH_16BIT		(1<<8)
	#define SUNXI_NDMACFG_SRCDATAWIDTH_32BIT		(2<<8)
	#define SUNXI_NDMACFG_SRCDATAWIDTH_RVD			(3<<8)
	#define SUNXI_NDMACFG_SRCBURST4				(1<<7)
	#define SUNXI_NDMACFG_SRCADDRTYPE_INC			(0<<5)
	#define SUNXI_NDMACFG_SRCADDRTYPE_CON 			(1<<5)
	#define SUNXI_NDMACFG_SRCTYPE_IRRX				(0x0<<0)
	#define SUNXI_NDMACFG_SRCTYPE_SPDIFRX			(0x1<<0)
	#define SUNXI_NDMACFG_SRCTYPE_IISRX			(0x2<<0)
	#define SUNXI_NDMACFG_SRCTYPE_AC97RX			(0x3<<0)
	#define SUNXI_NDMACFG_SRCTYPE_SPI0RX 			(0x4<<0)
	#define SUNXI_NDMACFG_SRCTYPE_SPI1RX			(0x5<<0)
	#define SUNXI_NDMACFG_SRCTYPE_SPI2RX			(0x6<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART0RX			(0x8<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART1RX			(0x9<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART2RX			(0xA<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART3RX			(0xB<<0)
	#define SUNXI_NDMACFG_SRCTYPE_AUDIOAD			(0xC<<0)
	#define SUNXI_NDMACFG_SRCTYPE_TPAD				(0xD<<0)
	#define SUNXI_NDMACFG_SRCTYPE_NFC				(0xF<<0)
	#define SUNXI_NDMACFG_SRCTYPE_SRAM				(0x10<<0)
	#define SUNXI_NDMACFG_SRCTYPE_DRAM				(0x11<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART4RX			(0x12<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART5RX			(0x13<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART6RX			(0x14<<0)
	#define SUNXI_NDMACFG_SRCTYPE_UART7RX			(0x15<<0)

#define SUNXI_NDMASRCADDR(v)				((v)*0x20 + 0x100 + 4)

#define SUNXI_NDMADSTADDR(v)				((v)*0x20 + 0x100 + 8)

#define SUNXI_NDMACNT(v)				((v)*0x20 + 0x100 + 0xC)


/* CCM REGISTER */
#define SUNXI_CCMBASE    (0x01C20000)

#define SUNXI_CCM_AUDIO_HOSC_PLL_REG   (0x08)
	#define SUNXI_CCM_AUDIO_HOSC_PLL_REG_AUDIOEN		(1<<31)
	#define SUNXI_CCM_AUDIO_HOSC_PLL_REG_FRE225792MHZ	(0<<27)
	#define SUNXI_CCM_AUDIO_HOSC_PLL_REG_FRE24576MHZ	(1<<27)

#define SUNXI_CCM_APB_GATE_REG    		 (0x68)
	#define SUNXI_CCM_APB_GATE_REG_IISGATE				(1<<3)

#define SUNXI_CCM_AUDIO_CLK_REG				(0xb8)
	#define SUNXI_CCM_AUDIO_CLK_REG_IISSPECIALGATE		(1<<31)
	#define SUNXI_CCM_AUDIO_CLK_REG_DIV(v)					((v)<<16)
/*------------------------------------------------------------*/

/*------------------------------------------------------------*/
/* Clock dividers */
#define SUNXI_DIV_MCLK	0
#define SUNXI_DIV_BCLK	1

#define SUNXI_IISCLKD_MCLK_MASK   0x0f
#define SUNXI_IISCLKD_MCLK_OFFS   0
#define SUNXI_IISCLKD_BCLK_MASK   0x070
#define SUNXI_IISCLKD_BCLK_OFFS   4
#define SUNXI_IISCLKD_MCLKEN_OFFS 7

unsigned int sunxi_i2s_get_clockrate(void);
extern struct sunxi_i2s_info sunxi_i2s;
//extern struct snd_soc_dai sunxi_iis_dai;

extern void sunxi_snd_txctrl_i2s(struct snd_pcm_substream *substream, int on);
extern void sunxi_snd_rxctrl_i2s(int on);

struct sunxi_i2s_info {
	void __iomem   *regs;    /* IIS BASE */
	void __iomem   *ccmregs;  //CCM BASE
	void __iomem   *ioregs;   //IO BASE

	u32 slave;					//0: master, 1: slave
	u32 mono;					//0: stereo, 1: mono
	u32 samp_fs;				//audio sample rate (unit in kHz)
	u32 samp_res;			//16 bits, 20 bits , 24 bits, 32 bits)
	u32 samp_format;		//audio sample format (0: standard I2S, 1: left-justified, 2: right-justified, 3: pcm)
	u32 ws_size;				//16 BCLK, 20 BCLK, 24 BCLK, 32 BCLK)
	u32 mclk_rate;			//mclk frequency divide by fs (128fs, 192fs, 256fs, 384fs, 512fs, 768fs)
	u32 lrc_pol;				//LRC clock polarity (0: normal ,1: inverted)
	u32 bclk_pol;			//BCLK polarity (0: normal, 1: inverted)
	u32 pcm_txtype;		//PCM transmitter type (0: 16-bits linear mode, 1: 8-bits linear mode, 2: u-law, 3: A-law)
	u32 pcm_rxtype;		//PCM receiver type  (0: 16-bits linear mode, 1: 8-bits linear mode, 2: u-law, 3: A-law)
	u32 pcm_sw;				//PCM slot width (8: 8 bits, 16: 16 bits)
	u32 pcm_sync_period;//PCM sync period (16/32/64/128/256)
	u32 pcm_sync_type;	//PCM sync symbol size (0: short sync, 1: long sync)
	u32 pcm_start_slot;//PCM start slot index (1--4)
	u32 pcm_lsb_first;	//0: MSB first, 1: LSB first
	u32 pcm_ch_num;		//PCM channel number (1: one channel, 2: two channel)

};

extern struct sunxi_i2s_info sunxi_i2s;
#endif
