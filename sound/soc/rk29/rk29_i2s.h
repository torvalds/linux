/*
 * rockchip-iis.h - ALSA IIS interface for the Rockchip rk28 SoC
 *
 * Driver for rockchip iis audio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/version.h>

#ifndef _ROCKCHIP_IIS_H
#define _ROCKCHIP_IIS_H

//I2S_TXCR

#define PCM_2DATA               (0<<18)
#define PCM_4DATA               (1<<18)
#define PCM_6DATA               (2<<18)
#define PCM_8DATA               (3<<18)

#define CHANNEL_1_EN            (0<<15)
#define CHANNEL_2_EN            (1<<15)
#define CHANNEL_3_EN            (2<<15)
#define CHANNLE_4_EN            (3<<15)
#define TX_MODE_MASTER          (0<<13)
#define TX_MODE_SLAVE           (1<<13)
#define RESET_TX                (1<<17)
#define RESET_RX                (1<<16)
#define I2S_DMA_REQ1_DISABLE    (1<<6)
#define I2S_DMA_REQ1_ENABLE     (0)
#define I2S_DMA_REQ2_DISABLE    (1<<5)
#define I2S_DMA_REQ2_ENABLE     (0)
#define I2S_DMA_REQ1_TX_ENABLE  (0)
#define I2S_DMA_REQ1_RX_ENABLE  (1<<4)
#define I2S_DMA_REQ2_TX_ENABLE  (0)
#define I2S_DMA_REQ2_RX_ENABLE  (1<<3)
#define TX_START                (1<<1)
#define RX_START                (1)



//I2S_TXCTL I2S_RXCTL
#define CLEAR_RXFIFO            (1<<24)
#define TRAN_DEVICES0           (0)
#define TRAN_DEVICES1           (1<<18)
#define TRAN_DEVICES2           (2<<18)
#define TRAN_DEVICES3           (3<<18)
#define OVERSAMPLING_RATE_32FS  (0)
#define OVERSAMPLING_RATE_64FS  (1<<16)
#define OVERSAMPLING_RATE_128FS (2<<16)
#define SCK_RATE2               (0x02<<8)
#define SCK_RATE4               (0x04<<8)
#define SCK_RATE8               (0x08<<8)
#define SAMPLE_DATA_8bit        (0)
#define SAMPLE_DATA_16bit       (1<<4)
#define SAMPLE_DATA_MASK        (3<<4)
#define MONO_MODE               (1<<3)
#define STEREO_MODE             (0)
#define I2S_MODE                (0)
#define LEFT_JUSTIFIED          (1<<1)
#define RIGHT_JUSTIFIED         (2<<1)
#define IISMOD_SDF_MASK         (3<<1)
#define MASTER_MODE             (1)
#define SLAVE_MODE              (0)

//I2S_FIFOSTS
#define TX_HALF_FULL            (1<<18)
#define RX_HALF_FULL            (1<<16)

/* Clock dividers */
#define ROCKCHIP_DIV_MCLK	0
#define ROCKCHIP_DIV_BCLK	1
#define ROCKCHIP_DIV_PRESCALER	2


/* I2S_TXCR */
#define I2S_RSTL_SCLK(c)        ((c&0x3F)<<26)
#define I2S_RSTR_SCLK(c)        ((c&0x3F)<<20)

#define I2S_PCM_2DATA           (0<<18)
#define I2S_PCM_4DATA           (1<<18)
#define I2S_PCM_6DATA           (2<<18)
#define I2S_PCM_8DATA           (3<<18)
#define I2S_PCM_DATA_MASK       (3<<18)

#define I2S_CSR_CH2             (0<<15)
#define I2S_CSR_CH4             (1<<15)
#define I2S_CRS_CH6             (2<<15)
#define I2S_CRS_CH8             (3<<15)
#define I2S_CRS_CH_MASK         (3<<15)

#define I2S_HWT_16BIT           (0<<14)
#define I2S_HWT_32BIT           (1<<14)

#ifdef CONFIG_ARCH_RK29
	#define I2S_MASTER_MODE         (0<<13)
	#define I2S_SLAVE_MODE          (1<<13)
	#define I2S_MODE_MASK           (1<<13)
#endif

#define I2S_JUSTIFIED_RIGHT     (0<<12)
#define I2S_JUSTIFIED_LEFT      (1<<12)

#define I2S_FIRST_BIT_MSB       (0<<11)
#define I2S_FIRST_BIT_LSB       (1<<11)

#define I2S_BUS_MODE_NOR        (0<<9)
#define I2S_BUS_MODE_LSJM       (1<<9)
#define I2S_BUS_MODE_RSJM       (2<<9)
#define I2S_BUS_MODE_MASK       (3<<9)

#define I2S_PCM_NO_DELAY        (0<<7)
#define I2S_PCM_DELAY_1MODE     (1<<7)
#define I2S_PCM_DELAY_2MODE     (2<<7)
#define I2S_PCM_DELAY_3MODE     (3<<7)
#define I2S_PCM_DELAY_MASK      (3<<7)

#define I2S_TX_LRCK_OUT_BT_DISABLE      (0<<6)
#define I2S_TX_LRCK_OUT_BT_ENABLE       (1<<6)

#define I2S_TX_LRCK_OUT_I2S             (0<<5)
#define I2S_TX_LRCK_OUT_PCM             (1<<5)

#define I2S_DATA_WIDTH(w)               ((w&0x1F)<<0)

/* */


/* I2S_TXCKR */
#ifdef CONFIG_ARCH_RK29
	#define I2S_TSP_POSEDGE         (0<<25)
	#define I2S_TSP_NEGEDGE         (1<<25)
	#define I2S_TLP_NORMAL          (0<<24)
	#define I2S_TLP_OPPSITE         (1<<24)

	#define I2S_MCLK_DIV(x)         ((0xFF&x)<<16)
	#define I2S_MCLK_DIV_MASK       ((0xFF)<<16)

	#define I2S_TSD_FIXED           (0<<12)
	#define I2S_TSD_CHANGED         (1<<12)

	#define I2S_TX_LRCK_NO_DELAY    (0<<10)
	#define I2S_TX_LRCK_DELAY_ONE   (1<<10)
	#define I2S_TX_LRCK_DELAY_TWO   (2<<10)
	#define I2S_TX_LRCK_DELAY_THREE (3<<10)
	#define I2S_TX_LRCK_DELAY_MASK  (3<<10)

	#define I2S_TX_SCLK_DIV(x)      (x&0x3FF)
	#define I2S_TX_SCLK_DIV_MASK    (0x3FF);
#else
//I2S_CKR
	#define I2S_MASTER_MODE         (0<<27)
	#define I2S_SLAVE_MODE          (1<<27)
	#define I2S_MODE_MASK           (1<<27)

	#define I2S_BCLK_POSEDGE        (0<<26)//sclk polarity   invert??
	#define I2S_BCLK_NEGEDGE        (1<<26)

	#define I2S_RX_LRCK_POSEDGE     (0<<25)//LRCK polarity   invert??
	#define I2S_RX_LRCK_NEGEDGE     (1<<25)

	#define I2S_TX_LRCK_POSEDGE     (0<<24)
	#define I2S_TX_LRCK_NEGEDGE     (1<<24)	
	
	#define I2S_MCLK_DIV(x)         ((0xFF&x)<<16)
	#define I2S_MCLK_DIV_MASK       ((0xFF)<<16)
	
	#define I2S_RX_SCLK_DIV(x)      ((x&0xFF)<<8)
	#define I2S_RX_SCLK_DIV_MASK    ((0xFF)<<8)
	
	#define I2S_TX_SCLK_DIV(x)      (x&0xFF)
	#define I2S_TX_SCLK_DIV_MASK    (0xFF)
#endif

/* I2S_DMACR */
#define I2S_RECE_DMA_DISABLE    (0<<24)
#define I2S_RECE_DMA_ENABLE     (1<<24)
#define I2S_DMARDL(x)           ((x&0x1f)<<16)

#define I2S_TRAN_DMA_DISABLE    (0<<8)
#define I2S_TRAN_DMA_ENABLE     (1<<8)
#define I2S_DMATDL(x)           ((x&0x1f)<<0)

/* I2S_INTCR */
#define I2S_RXOV_INT_DISABLE    (0<<17)
#define I2S_RXOV_INT_ENABLE     (1<<17)
#define I2S_RXFU_INT_DISABLE    (0<<16)
#define I2S_RXFU_INT_ENABLE     (1<<16)

#define I2S_TXUND_INT_DISABLE   (0<<1)
#define I2S_TXUND_INT_ENABLE    (1<<1)
#define I2S_TXEMP_INT_DISABLE   (0<<0)
#define I2S_TXEMP_INT_ENABLE    (1<<0)

/* I2S_XFER */
#define I2S_RX_TRAN_STOP        (0<<1)
#define I2S_RX_TRAN_START       (1<<1)
#define I2S_TX_TRAN_STOP        (0<<0)
#define I2S_TX_TRAN_START       (1<<0)

//I2S_CLR
#define I2S_RX_CLEAR	(1<<1)
#define I2S_TX_CLEAR	1


#ifdef CONFIG_ARCH_RK29
#define I2S_TXR_BUFF            0x20
#define I2S_RXR_BUFF            0x24
//I2S Registers
typedef volatile struct tagIIS_STRUCT
{
    unsigned int I2S_TXCR;
    unsigned int I2S_RXCR;
    unsigned int I2S_TXCKR;
    unsigned int I2S_RXCKR;
    unsigned int I2S_FIFOLR;
    unsigned int I2S_DMACR;
    unsigned int I2S_INTCR;
    unsigned int I2S_INTSR;
    unsigned int I2S_TXDR;
    unsigned int I2S_RXDR;
    unsigned int I2S_XFER;
    unsigned int I2S_TXRST;
    unsigned int I2S_RXRST;
}I2S_REG,*pI2S_REG;
#else
#define I2S_TXR_BUFF            0x24
#define I2S_RXR_BUFF            0x28
typedef volatile struct tagIIS_STRUCT
{
    unsigned int I2S_TXCR;//0xF  0
    unsigned int I2S_RXCR;//0xF 4
    unsigned int I2S_CKR;//0x3F 8
    unsigned int I2S_FIFOLR;//c 
    unsigned int I2S_DMACR;//0x001F0110 10
    unsigned int I2S_INTCR;//0x01F00000 14
    unsigned int I2S_INTSR;//0x00 18
	unsigned int I2S_XFER;//0x00000003 1c
	unsigned int I2S_CLR;//20
    unsigned int I2S_TXDR;//24
    unsigned int I2S_RXDR;
}I2S_REG,*pI2S_REG;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
extern struct snd_soc_dai_driver rk29_i2s_dai[];
#else
extern struct snd_soc_dai rk29_i2s_dai[];
#endif

#ifdef CONFIG_SND_SOC_RT5631
extern struct delayed_work rt5631_delay_cap; //bard 7-16
#endif

#endif /* _ROCKCHIP_IIS_H */

