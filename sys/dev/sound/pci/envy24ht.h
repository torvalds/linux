/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Konstantin Dimitrov <kosio.dimitrov@gmail.com>
 * Copyright (c) 2001 Katsurajima Naoto <raven@katsurajima.seya.yokohama.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THEPOSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


/* -------------------------------------------------------------------- */

/* PCI device ID */
#define PCIV_ENVY24 0x1412
#define PCID_ENVY24HT 0x1724

#define PCIR_CCS		0x10 /* Controller I/O Base Address */
#define ENVY24HT_PCIR_MT   	0x14 /* Multi-Track I/O Base Address */

/* Controller Registers */

#define ENVY24HT_CCS_CTL      0x00 /* Control/Status Register */
#define ENVY24HT_CCS_CTL_RESET   0x80 /* Entire Chip soft reset */

#define ENVY24HT_CCS_IMASK    0x01 /* Interrupt Mask Register */
#define ENVY24HT_CCS_IMASK_PMT   0x10 /* Professional Multi-track */

#define ENVY24HT_CCS_I2CDEV   0x10 /* I2C Port Device Address Register */ 
#define ENVY24HT_CCS_I2CDEV_ADDR 0xfe /* I2C device address */
#define ENVY24HT_CCS_I2CDEV_ROM  0xa0 /* reserved for the external I2C E2PROM */
#define ENVY24HT_CCS_I2CDEV_WR   0x01 /* write */
#define ENVY24HT_CCS_I2CDEV_RD   0x00 /* read */
  
#define ENVY24HT_CCS_I2CADDR  0x11 /* I2C Port Byte Address Register */
#define ENVY24HT_CCS_I2CDATA  0x12 /* I2C Port Read/Write Data Register */

#define ENVY24HT_CCS_I2CSTAT  0x13 /* I2C Port Control and Status Register */
#define ENVY24HT_CCS_I2CSTAT_ROM 0x80 /* external E2PROM exists */
#define ENVY24HT_CCS_I2CSTAT_BSY 0x01 /* I2C port read/write status busy */

#define ENVY24HT_CCS_SCFG  0x04 /* System Configuration Register */
#define ENVY24HT_CCSM_SCFG_XIN2      0xc0 /* XIN2 Clock Source Configuration */
                                 	  /* 00: 24.576MHz(96kHz*256) */
                                 	  /* 01: 49.152MHz(192kHz*256) */
                                 	  /* 1x: Reserved */
#define ENVY24HT_CCSM_SCFG_MPU       0x20 /* 0(not implemented)/1(1) MPU-401 UART */
#define ENVY24HT_CCSM_SCFG_ADC       0x0c /* 1-2 stereo ADC connected, S/PDIF receiver connected */
#define ENVY24HT_CCSM_SCFG_DAC       0x03 /* 1-4 stereo DAC connected */

#define ENVY24HT_CCS_ACL   0x05 /* AC-Link Configuration Register */
#define ENVY24HT_CCSM_ACL_MTC        0x80 /* Multi-track converter type: 0:AC'97 1:I2S */
#define ENVY24HT_CCSM_ACL_OMODE      0x02 /* AC 97 codec SDATA_OUT 0:split 1:packed */

#define ENVY24HT_CCS_I2S   0x06 /* I2S Converters Features Register */
#define ENVY24HT_CCSM_I2S_VOL        0x80 /* I2S codec Volume and mute */
#define ENVY24HT_CCSM_I2S_96KHZ      0x40 /* I2S converter 96kHz sampling rate support */
#define ENVY24HT_CCSM_I2S_192KHZ     0x08 /* I2S converter 192kHz sampling rate support */
#define ENVY24HT_CCSM_I2S_RES        0x30 /* Converter resolution */
#define ENVY24HT_CCSM_I2S_16BIT      0x00 /* 16bit */
#define ENVY24HT_CCSM_I2S_18BIT      0x10 /* 18bit */
#define ENVY24HT_CCSM_I2S_20BIT      0x20 /* 20bit */
#define ENVY24HT_CCSM_I2S_24BIT      0x30 /* 24bit */
#define ENVY24HT_CCSM_I2S_ID         0x07 /* Other I2S IDs */

#define ENVY24HT_CCS_SPDIF 0x07 /* S/PDIF Configuration Register */
#define ENVY24HT_CCSM_SPDIF_INT_EN   0x80 /* Enable integrated S/PDIF transmitter */
#define ENVY24HT_CCSM_SPDIF_INT_OUT  0x40 /* Internal S/PDIF Out implemented */
#define ENVY24HT_CCSM_SPDIF_ID       0x3c /* S/PDIF chip ID */
#define ENVY24HT_CCSM_SPDIF_IN       0x02 /* S/PDIF Stereo In is present */
#define ENVY24HT_CCSM_SPDIF_OUT      0x01 /* External S/PDIF Out implemented */

/* Professional Multi-Track Control Registers */
 
#define ENVY24HT_MT_INT_STAT    0x00 /* DMA Interrupt Mask and Status Register */ 
#define ENVY24HT_MT_INT_RSTAT   0x02 /* Multi-track record interrupt status */
#define ENVY24HT_MT_INT_PSTAT   0x01 /* Multi-track playback interrupt status */
#define ENVY24HT_MT_INT_MASK	0x03
#define ENVY24HT_MT_INT_RMASK   0x02 /* Multi-track record interrupt mask */
#define ENVY24HT_MT_INT_PMASK   0x01 /* Multi-track playback interrupt mask */

#define ENVY24HT_MT_RATE     0x01 /* Sampling Rate Select Register */ 
#define ENVY24HT_MT_RATE_SPDIF  0x10 /* S/PDIF input clock as the master */
#define ENVY24HT_MT_RATE_48000  0x00
#define ENVY24HT_MT_RATE_24000  0x01
#define ENVY24HT_MT_RATE_12000  0x02
#define ENVY24HT_MT_RATE_9600   0x03
#define ENVY24HT_MT_RATE_32000  0x04
#define ENVY24HT_MT_RATE_16000  0x05
#define ENVY24HT_MT_RATE_8000   0x06
#define ENVY24HT_MT_RATE_96000  0x07
#define ENVY24HT_MT_RATE_192000 0x0e
#define ENVY24HT_MT_RATE_64000  0x0f
#define ENVY24HT_MT_RATE_44100  0x08
#define ENVY24HT_MT_RATE_22050  0x09
#define ENVY24HT_MT_RATE_11025  0x0a
#define ENVY24HT_MT_RATE_88200  0x0b
#define ENVY24HT_MT_RATE_176400 0x0c
#define ENVY24HT_MT_RATE_MASK   0x0f

#define ENVY24HT_MT_I2S      0x02 /* I2S Data Format Register */
#define ENVY24HT_MT_I2S_MLR128  0x08 /* MCLK/LRCLK ratio 128x (or 256x) */

#define ENVY24HT_MT_PADDR    0x10 /* Playback DMA Current/Base Address Register */
#define ENVY24HT_MT_PCNT     0x14 /* Playback DMA Current/Base Count Register */
#define ENVY24HT_MT_PTERM    0x1C /* Playback Current/Base Terminal Count Register */

#define ENVY24HT_MT_PCTL     0x18 /* Global Playback and Record DMA Start/Stop Register */
#define ENVY24HT_MT_PCTL_RSTART 0x02 /* 1: Record start; 0: Record stop */
#define ENVY24HT_MT_PCTL_PSTART 0x01 /* 1: Playback start; 0: Playback stop */

#define ENVY24HT_MT_RADDR    0x20 /* Record DMA Current/Base Address Register */
#define ENVY24HT_MT_RCNT     0x24 /* Record DMA Current/Base Count Register */
#define ENVY24HT_MT_RTERM    0x26 /* Record Current/Base Terminal Count Register */

/*
  These map values are refferd from ALSA sound driver.
*/
/* ENVY24 configuration E2PROM map */
#define ENVY24HT_E2PROM_SUBVENDOR  0x02
#define ENVY24HT_E2PROM_SUBDEVICE  0x00
#define ENVY24HT_E2PROM_SIZE       0x04
#define ENVY24HT_E2PROM_VERSION    0x05
#define ENVY24HT_E2PROM_SCFG       0x06
#define ENVY24HT_E2PROM_ACL        0x07
#define ENVY24HT_E2PROM_I2S        0x08
#define ENVY24HT_E2PROM_SPDIF      0x09
#define ENVY24HT_E2PROM_GPIOMASK   0x0d
#define ENVY24HT_E2PROM_GPIOSTATE  0x10
#define ENVY24HT_E2PROM_GPIODIR    0x0a

/* ENVY24 mixer channel defines */
/*
  ENVY24 mixer has original line matrix. So, general mixer command is not
  able to use for this. If system has consumer AC'97 output, AC'97 line is
  used as master mixer, and it is able to control.
*/
#define ENVY24HT_CHAN_NUM  11 /* Play * 5 + Record * 5 + Mix * 1 */

#define ENVY24HT_CHAN_PLAY_DAC1  0
#define ENVY24HT_CHAN_PLAY_DAC2  1
#define ENVY24HT_CHAN_PLAY_DAC3  2
#define ENVY24HT_CHAN_PLAY_DAC4  3
#define ENVY24HT_CHAN_PLAY_SPDIF 4
#define ENVY24HT_CHAN_REC_ADC1   5
#define ENVY24HT_CHAN_REC_ADC2   6
#define ENVY24HT_CHAN_REC_ADC3   7
#define ENVY24HT_CHAN_REC_ADC4   8
#define ENVY24HT_CHAN_REC_SPDIF  9
#define ENVY24HT_CHAN_REC_MIX   10

#define ENVY24HT_MIX_MASK     0x3fd
#define ENVY24HT_MIX_REC_MASK 0x3e0

/* volume value constants */
#define ENVY24HT_VOL_MAX    0 /* 0db(negate) */
#define ENVY24HT_VOL_MIN   96 /* -144db(negate) */
#define ENVY24HT_VOL_MUTE 127 /* mute */

#define BUS_SPACE_MAXSIZE_ENVY24 0x3fffc /* 64k x 4byte(1dword) */

#define ENVY24HT_CCS_GPIO_HDATA 0x1E
#define ENVY24HT_CCS_GPIO_LDATA 0x14
#define ENVY24HT_CCS_GPIO_LMASK 0x16
#define ENVY24HT_CCS_GPIO_HMASK 0x1F
#define ENVY24HT_CCS_GPIO_CTLDIR 0x18

