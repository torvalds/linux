#ifndef __SOUND_VT1724_H
#define __SOUND_VT1724_H

/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24)
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */      

#include <sound/control.h>
#include <sound/ac97_codec.h>
#include <sound/rawmidi.h>
#include <sound/i2c.h>
#include <sound/pcm.h>

#include "ice1712.h"

enum {
	ICE_EEP2_SYSCONF = 0,	/* 06 */
	ICE_EEP2_ACLINK,	/* 07 */
	ICE_EEP2_I2S,		/* 08 */
	ICE_EEP2_SPDIF,		/* 09 */
	ICE_EEP2_GPIO_DIR,	/* 0a */
	ICE_EEP2_GPIO_DIR1,	/* 0b */
	ICE_EEP2_GPIO_DIR2,	/* 0c */
	ICE_EEP2_GPIO_MASK,	/* 0d */
	ICE_EEP2_GPIO_MASK1,	/* 0e */
	ICE_EEP2_GPIO_MASK2,	/* 0f */
	ICE_EEP2_GPIO_STATE,	/* 10 */
	ICE_EEP2_GPIO_STATE1,	/* 11 */
	ICE_EEP2_GPIO_STATE2	/* 12 */
};
	
/*
 *  Direct registers
 */

#define ICEREG1724(ice, x) ((ice)->port + VT1724_REG_##x)

#define VT1724_REG_CONTROL		0x00	/* byte */
#define   VT1724_RESET			0x80	/* reset whole chip */
#define VT1724_REG_IRQMASK		0x01	/* byte */
#define   VT1724_IRQ_MPU_RX		0x80
#define   VT1724_IRQ_MPU_TX		0x20
#define   VT1724_IRQ_MTPCM		0x10
#define VT1724_REG_IRQSTAT		0x02	/* byte */
/* look to VT1724_IRQ_* */
#define VT1724_REG_SYS_CFG		0x04	/* byte - system configuration PCI60 on Envy24*/
#define   VT1724_CFG_CLOCK	0xc0
#define     VT1724_CFG_CLOCK512	0x00	/* 22.5692Mhz, 44.1kHz*512 */
#define     VT1724_CFG_CLOCK384  0x40	/* 16.9344Mhz, 44.1kHz*384 */
#define   VT1724_CFG_MPU401	0x20		/* MPU401 UARTs */
#define   VT1724_CFG_ADC_MASK	0x0c	/* one, two or one and S/PDIF, stereo ADCs */
#define   VT1724_CFG_DAC_MASK	0x03	/* one, two, three, four stereo DACs */

#define VT1724_REG_AC97_CFG		0x05	/* byte */
#define   VT1724_CFG_PRO_I2S	0x80	/* multitrack converter: I2S or AC'97 */
#define   VT1724_CFG_AC97_PACKED	0x01	/* split or packed mode - AC'97 */

#define VT1724_REG_I2S_FEATURES		0x06	/* byte */
#define   VT1724_CFG_I2S_VOLUME	0x80	/* volume/mute capability */
#define   VT1724_CFG_I2S_96KHZ	0x40	/* supports 96kHz sampling */
#define   VT1724_CFG_I2S_RESMASK	0x30	/* resolution mask, 16,18,20,24-bit */
#define   VT1724_CFG_I2S_192KHZ	0x08	/* supports 192kHz sampling */
#define   VT1724_CFG_I2S_OTHER	0x07	/* other I2S IDs */

#define VT1724_REG_SPDIF_CFG		0x07	/* byte */
#define   VT1724_CFG_SPDIF_OUT_EN	0x80	/*Internal S/PDIF output is enabled*/
#define   VT1724_CFG_SPDIF_OUT_INT	0x40	/*Internal S/PDIF output is implemented*/
#define   VT1724_CFG_I2S_CHIPID	0x3c	/* I2S chip ID */
#define   VT1724_CFG_SPDIF_IN	0x02	/* S/PDIF input is present */
#define   VT1724_CFG_SPDIF_OUT	0x01	/* External S/PDIF output is present */

/*there is no consumer AC97 codec with the VT1724*/
//#define VT1724_REG_AC97_INDEX		0x08	/* byte */
//#define VT1724_REG_AC97_CMD		0x09	/* byte */

#define VT1724_REG_MPU_TXFIFO		0x0a	/*byte ro. number of bytes in TX fifo*/
#define VT1724_REG_MPU_RXFIFO		0x0b	/*byte ro. number of bytes in RX fifo*/

//are these 2 the wrong way around? they don't seem to be used yet anyway
#define VT1724_REG_MPU_CTRL		0x0c	/* byte */
#define VT1724_REG_MPU_DATA		0x0d	/* byte */

#define VT1724_REG_MPU_FIFO_WM	0x0e	/*byte set the high/low watermarks for RX/TX fifos*/
#define   VT1724_MPU_RX_FIFO	0x20	//1=rx fifo watermark 0=tx fifo watermark
#define   VT1724_MPU_FIFO_MASK	0x1f	

#define VT1724_REG_I2C_DEV_ADDR	0x10	/* byte */
#define   VT1724_I2C_WRITE		0x01	/* write direction */
#define VT1724_REG_I2C_BYTE_ADDR	0x11	/* byte */
#define VT1724_REG_I2C_DATA		0x12	/* byte */
#define VT1724_REG_I2C_CTRL		0x13	/* byte */
#define   VT1724_I2C_EEPROM		0x80	/* 1 = EEPROM exists */
#define   VT1724_I2C_BUSY		0x01	/* busy bit */

#define VT1724_REG_GPIO_DATA	0x14	/* word */
#define VT1724_REG_GPIO_WRITE_MASK	0x16 /* word */
#define VT1724_REG_GPIO_DIRECTION	0x18 /* dword? (3 bytes) 0=input 1=output. 
						bit3 - during reset used for Eeprom power-on strapping
						if TESTEN# pin active, bit 2 always input*/
#define VT1724_REG_POWERDOWN	0x1c
#define VT1724_REG_GPIO_DATA_22	0x1e /* byte direction for GPIO 16:22 */
#define VT1724_REG_GPIO_WRITE_MASK_22	0x1f /* byte write mask for GPIO 16:22 */


/* 
 *  Professional multi-track direct control registers
 */

#define ICEMT1724(ice, x) ((ice)->profi_port + VT1724_MT_##x)

#define VT1724_MT_IRQ			0x00	/* byte - interrupt mask */
#define   VT1724_MULTI_PDMA4	0x80	/* SPDIF Out / PDMA4 */
#define	  VT1724_MULTI_PDMA3	0x40	/* PDMA3 */
#define   VT1724_MULTI_PDMA2	0x20	/* PDMA2 */
#define   VT1724_MULTI_PDMA1	0x10	/* PDMA1 */
#define   VT1724_MULTI_FIFO_ERR 0x08	/* DMA FIFO underrun/overrun. */
#define   VT1724_MULTI_RDMA1	0x04	/* RDMA1 (S/PDIF input) */
#define   VT1724_MULTI_RDMA0	0x02	/* RMDA0 */
#define   VT1724_MULTI_PDMA0	0x01	/* MC Interleave/PDMA0 */

#define VT1724_MT_RATE			0x01	/* byte - sampling rate select */
#define   VT1724_SPDIF_MASTER		0x10	/* S/PDIF input is master clock */
#define VT1724_MT_I2S_FORMAT		0x02	/* byte - I2S data format */
#define   VT1724_MT_I2S_MCLK_128X	0x08
#define   VT1724_MT_I2S_FORMAT_MASK	0x03
#define   VT1724_MT_I2S_FORMAT_I2S	0x00
#define VT1724_MT_DMA_INT_MASK		0x03	/* byte -DMA Interrupt Mask */
/* lool to VT1724_MULTI_* */
#define VT1724_MT_AC97_INDEX		0x04	/* byte - AC'97 index */
#define VT1724_MT_AC97_CMD		0x05	/* byte - AC'97 command & status */
#define   VT1724_AC97_COLD	0x80	/* cold reset */
#define   VT1724_AC97_WARM	0x40	/* warm reset */
#define   VT1724_AC97_WRITE	0x20	/* W: write, R: write in progress */
#define   VT1724_AC97_READ	0x10	/* W: read, R: read in progress */
#define   VT1724_AC97_READY	0x08	/* codec ready status bit */
#define   VT1724_AC97_ID_MASK	0x03	/* codec id mask */
#define VT1724_MT_AC97_DATA		0x06	/* word - AC'97 data */
#define VT1724_MT_PLAYBACK_ADDR		0x10	/* dword - playback address */
#define VT1724_MT_PLAYBACK_SIZE		0x14	/* dword - playback size */
#define VT1724_MT_DMA_CONTROL		0x18	/* byte - control */
#define   VT1724_PDMA4_START	0x80	/* SPDIF out / PDMA4 start */
#define   VT1724_PDMA3_START	0x40	/* PDMA3 start */
#define   VT1724_PDMA2_START	0x20	/* PDMA2 start */
#define   VT1724_PDMA1_START	0x10	/* PDMA1 start */
#define   VT1724_RDMA1_START	0x04	/* RDMA1 start */
#define   VT1724_RDMA0_START	0x02	/* RMDA0 start */
#define   VT1724_PDMA0_START	0x01	/* MC Interleave / PDMA0 start */
#define VT1724_MT_BURST			0x19	/* Interleaved playback DMA Active streams / PCI burst size */
#define VT1724_MT_DMA_FIFO_ERR		0x1a	/*Global playback and record DMA FIFO Underrun/Overrun */
#define   VT1724_PDMA4_UNDERRUN		0x80
#define   VT1724_PDMA2_UNDERRUN		0x40
#define   VT1724_PDMA3_UNDERRUN		0x20
#define   VT1724_PDMA1_UNDERRUN		0x10
#define   VT1724_RDMA1_UNDERRUN		0x04
#define   VT1724_RDMA0_UNDERRUN		0x02
#define   VT1724_PDMA0_UNDERRUN		0x01
#define VT1724_MT_DMA_PAUSE		0x1b	/*Global playback and record DMA FIFO pause/resume */
#define	  VT1724_PDMA4_PAUSE	0x80
#define	  VT1724_PDMA3_PAUSE	0x40
#define	  VT1724_PDMA2_PAUSE	0x20
#define	  VT1724_PDMA1_PAUSE	0x10
#define	  VT1724_RDMA1_PAUSE	0x04
#define	  VT1724_RDMA0_PAUSE	0x02
#define	  VT1724_PDMA0_PAUSE	0x01
#define VT1724_MT_PLAYBACK_COUNT	0x1c	/* word - playback count */
#define VT1724_MT_CAPTURE_ADDR		0x20	/* dword - capture address */
#define VT1724_MT_CAPTURE_SIZE		0x24	/* word - capture size */
#define VT1724_MT_CAPTURE_COUNT		0x26	/* word - capture count */

#define VT1724_MT_ROUTE_PLAYBACK	0x2c	/* word */

#define VT1724_MT_RDMA1_ADDR		0x30	/* dword - RDMA1 capture address */
#define VT1724_MT_RDMA1_SIZE		0x34	/* word - RDMA1 capture size */
#define VT1724_MT_RDMA1_COUNT		0x36	/* word - RDMA1 capture count */

#define VT1724_MT_SPDIF_CTRL		0x3c	/* word */
#define VT1724_MT_MONITOR_PEAKINDEX	0x3e	/* byte */
#define VT1724_MT_MONITOR_PEAKDATA	0x3f	/* byte */

/* concurrent stereo channels */
#define VT1724_MT_PDMA4_ADDR		0x40	/* dword */
#define VT1724_MT_PDMA4_SIZE		0x44	/* word */
#define VT1724_MT_PDMA4_COUNT		0x46	/* word */
#define VT1724_MT_PDMA3_ADDR		0x50	/* dword */
#define VT1724_MT_PDMA3_SIZE		0x54	/* word */
#define VT1724_MT_PDMA3_COUNT		0x56	/* word */
#define VT1724_MT_PDMA2_ADDR		0x60	/* dword */
#define VT1724_MT_PDMA2_SIZE		0x64	/* word */
#define VT1724_MT_PDMA2_COUNT		0x66	/* word */
#define VT1724_MT_PDMA1_ADDR		0x70	/* dword */
#define VT1724_MT_PDMA1_SIZE		0x74	/* word */
#define VT1724_MT_PDMA1_COUNT		0x76	/* word */


unsigned char snd_vt1724_read_i2c(struct snd_ice1712 *ice, unsigned char dev, unsigned char addr);
void snd_vt1724_write_i2c(struct snd_ice1712 *ice, unsigned char dev, unsigned char addr, unsigned char data);

#endif /* __SOUND_VT1724_H */
