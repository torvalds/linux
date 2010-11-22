/*
 * rockchip-iis.h - ALSA IIS interface for the Rockchip rk28 SoC
 *
 * Driver for rockchip iis audio
 *  Copyright (C) 2009 lhh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ROCKCHIP_IIS_H
#define _ROCKCHIP_IIS_H

//I2S_OPR
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

#define I2S_TXR_BUFF            0x04
#define I2S_RXR_BUFF            0x08

/* Clock dividers */
#define ROCKCHIP_DIV_MCLK	0
#define ROCKCHIP_DIV_BCLK	1
#define ROCKCHIP_DIV_PRESCALER	2

//I2S Registers
typedef volatile struct tagIIS_STRUCT
{
    unsigned int I2S_OPR;
    unsigned int I2S_TXR;
    unsigned int I2S_RXR;
    unsigned int I2S_TXCTL;
    unsigned int I2S_RXCTL;
    unsigned int I2S_FIFOSTS;
    unsigned int I2S_IER;
    unsigned int I2S_ISR;
}I2S_REG,*pI2S_REG;

extern struct snd_soc_dai rk29_i2s_dai[];
//extern void rockchip_add_device_i2s(void);
#endif /* _ROCKCHIP_IIS_H */

