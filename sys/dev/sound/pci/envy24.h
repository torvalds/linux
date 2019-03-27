/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#define PCID_ENVY24 0x1712

/* PCI Registers */

#define PCIR_CCS   0x10 /* Controller I/O Base Address */
#define PCIR_DDMA  0x14 /* DDMA I/O Base Address */
#define PCIR_DS    0x18 /* DMA Path Registers I/O Base Address */
#define PCIR_MT    0x1c /* Professional Multi-Track I/O Base Address */

#define PCIR_LAC   0x40 /* Legacy Audio Control */
#define PCIM_LAC_DISABLE    0x8000 /* Legacy Audio Hardware disabled */
#define PCIM_LAC_SBDMA0     0x0000 /* SB DMA Channel Select: 0 */
#define PCIM_LAC_SBDMA1     0x0040 /* SB DMA Channel Select: 1 */
#define PCIM_LAC_SBDMA3     0x00c0 /* SB DMA Channel Select: 3 */
#define PCIM_LAC_IOADDR10   0x0020 /* I/O Address Alias Control */ 
#define PCIM_LAC_MPU401     0x0008 /* MPU-401 I/O enable */
#define PCIM_LAC_GAME       0x0004 /* Game Port enable (200h) */
#define PCIM_LAC_FM         0x0002 /* FM I/O enable (AdLib 388h base) */
#define PCIM_LAC_SB         0x0001 /* SB I/O enable */

#define PCIR_LCC   0x42 /* Legacy Configuration Control */
#define PCIM_LCC_VINT       0xff00 /* Interrupt vector to be snooped */
#define PCIM_LCC_SVIDRW     0x0080 /* SVID read/write enable */
#define PCIM_LCC_SNPSB      0x0040 /* snoop SB 22C/24Ch I/O write cycle */
#define PCIM_LCC_SNPPIC     0x0020 /* snoop PIC I/O R/W cycle */
#define PCIM_LCC_SNPPCI     0x0010 /* snoop PCI bus interrupt acknowledge cycle */
#define PCIM_LCC_SBBASE     0x0008 /* SB base 240h(1)/220h(0) */
#define PCIM_LCC_MPUBASE    0x0006 /* MPU-401 base 300h-330h */
#define PCIM_LCC_LDMA       0x0001 /* Legacy DMA enable */

#define PCIR_SCFG  0x60 /* System Configuration Register */
#define PCIM_SCFG_XIN2      0xc0 /* XIN2 Clock Source Configuration */
                                 /* 00: 22.5792MHz(44.1kHz*512) */
                                 /* 01: 16.9344MHz(44.1kHz*384) */
                                 /* 10: from external clock synthesizer chip */
#define PCIM_SCFG_MPU       0x20 /* 1(0)/2(1) MPU-401 UART(s) */
#define PCIM_SCFG_AC97      0x10 /* 0: AC'97 codec exist */
                                 /* 1: AC'97 codec not exist */
#define PCIM_SCFG_ADC       0x0c /* 1-4 stereo ADC connected */
#define PCIM_SCFG_DAC       0x03 /* 1-4 stereo DAC connected */

#define PCIR_ACL   0x61 /* AC-Link Configuration Register */
#define PCIM_ACL_MTC        0x80 /* Multi-track converter type: 0:AC'97 1:I2S */
#define PCIM_ACL_OMODE      0x02 /* AC 97 codec SDATA_OUT 0:split 1:packed */
#define PCIM_ACL_IMODE      0x01 /* AC 97 codec SDATA_IN 0:split 1:packed */

#define PCIR_I2S   0x62 /* I2S Converters Features Register */
#define PCIM_I2S_VOL        0x80 /* I2S codec Volume and mute */
#define PCIM_I2S_96KHZ      0x40 /* I2S converter 96kHz sampling rate support */
#define PCIM_I2S_RES        0x30 /* Converter resolution */
#define PCIM_I2S_16BIT      0x00 /* 16bit */
#define PCIM_I2S_18BIT      0x10 /* 18bit */
#define PCIM_I2S_20BIT      0x20 /* 20bit */
#define PCIM_I2S_24BIT      0x30 /* 24bit */
#define PCIM_I2S_ID         0x0f /* Other I2S IDs */

#define PCIR_SPDIF 0x63 /* S/PDIF Configuration Register */
#define PCIM_SPDIF_ID       0xfc /* S/PDIF chip ID */
#define PCIM_SPDIF_IN       0x02 /* S/PDIF Stereo In is present */
#define PCIM_SPDIF_OUT      0x01 /* S/PDIF Stereo Out is present */

#define PCIR_POWER_STAT     0x84 /* Power Management Control and Status */

/* Controller Registers */

#define ENVY24_CCS_CTL      0x00 /* Control/Status Register */
#define ENVY24_CCS_CTL_RESET   0x80 /* Entire Chip soft reset */
#define ENVY24_CCS_CTL_DMAINT  0x40 /* DS DMA Channel-C interrupt */
#define ENVY24_CCS_CTL_DOSVOL  0x10 /* set the DOS WT volume control */
#define ENVY24_CCS_CTL_EDGE    0x08 /* SERR# edge (only one PCI clock width) */
#define ENVY24_CCS_CTL_SBINT   0x02 /* SERR# assertion for SB interrupt */
#define ENVY24_CCS_CTL_NATIVE  0x01 /* Mode select: 0:SB mode 1:native mode */

#define ENVY24_CCS_IMASK    0x01 /* Interrupt Mask Register */
#define ENVY24_CCS_IMASK_PMIDI 0x80 /* Primary MIDI */
#define ENVY24_CCS_IMASK_TIMER 0x40 /* Timer */
#define ENVY24_CCS_IMASK_SMIDI 0x20 /* Secondary MIDI */
#define ENVY24_CCS_IMASK_PMT   0x10 /* Professional Multi-track */
#define ENVY24_CCS_IMASK_FM    0x08 /* FM/MIDI trapping */
#define ENVY24_CCS_IMASK_PDMA  0x04 /* Playback DS DMA */
#define ENVY24_CCS_IMASK_RDMA  0x02 /* Consumer record DMA */
#define ENVY24_CCS_IMASK_SB    0x01 /* Consumer/SB mode playback */

#define ENVY24_CCS_ISTAT    0x02 /* Interrupt Status Register */
#define ENVY24_CCS_ISTAT_PMIDI 0x80 /* Primary MIDI */
#define ENVY24_CCS_ISTAT_TIMER 0x40 /* Timer */
#define ENVY24_CCS_ISTAT_SMIDI 0x20 /* Secondary MIDI */
#define ENVY24_CCS_ISTAT_PMT   0x10 /* Professional Multi-track */
#define ENVY24_CCS_ISTAT_FM    0x08 /* FM/MIDI trapping */
#define ENVY24_CCS_ISTAT_PDMA  0x04 /* Playback DS DMA */
#define ENVY24_CCS_ISTAT_RDMA  0x02 /* Consumer record DMA */
#define ENVY24_CCS_ISTAT_SB    0x01 /* Consumer/SB mode playback */

#define ENVY24_CCS_INDEX    0x03 /* Envy24 Index Register */
#define ENVY24_CCS_DATA     0x04 /* Envy24 Data Register */

#define ENVY24_CCS_NMI1     0x05 /* NMI Status Register 1 */
#define ENVY24_CCS_NMI1_PCI    0x80 /* PCI I/O read/write cycle */
#define ENVY24_CCS_NMI1_SB     0x40 /* SB 22C/24C write */
#define ENVY24_CCS_NMI1_SBDMA  0x10 /* SB interrupt (SB DMA/SB F2 command) */
#define ENVY24_CCS_NMI1_DSDMA  0x08 /* DS channel C DMA interrupt */
#define ENVY24_CCS_NMI1_MIDI   0x04 /* MIDI 330h or [PCI_10]h+Ch write */
#define ENVY24_CCS_NMI1_FM     0x01 /* FM data register write */

#define ENVY24_CCS_NMIDAT   0x06 /* NMI Data Register */
#define ENVY24_CCS_NMIIDX   0x07 /* NMI Index Register */
#define ENVY24_CCS_AC97IDX  0x08 /* Consumer AC'97 Index Register */

#define ENVY24_CCS_AC97CMD  0x09 /* Consumer AC'97 Command/Status Register */
#define ENVY24_CCS_AC97CMD_COLD    0x80 /* Cold reset */
#define ENVY24_CCS_AC97CMD_WARM    0x40 /* Warm reset */
#define ENVY24_CCS_AC97CMD_WRCODEC 0x20 /* Write to AC'97 codec registers */
#define ENVY24_CCS_AC97CMD_RDCODEC 0x10 /* Read from AC'97 codec registers */
#define ENVY24_CCS_AC97CMD_READY   0x08 /* AC'97 codec ready status bit */
#define ENVY24_CCS_AC97CMD_PVSR    0x02 /* VSR for Playback */
#define ENVY24_CCS_AC97CMD_RVSR    0x01 /* VSR for Record */

#define ENVY24_CCS_AC97DAT  0x0a /* Consumer AC'97 Data Port Register */
#define ENVY24_CCS_PMIDIDAT 0x0c /* Primary MIDI UART Data Register */
#define ENVY24_CCS_PMIDICMD 0x0d /* Primary MIDI UART Command/Status Register */

#define ENVY24_CCS_NMI2     0x0e /* NMI Status Register 2 */
#define ENVY24_CCS_NMI2_FMBANK 0x30 /* FM bank indicator */
#define ENVY24_CCS_NMI2_FM0    0x10 /* FM bank 0 (388h/220h/228h) */
#define ENVY24_CCS_NMI2_FM1    0x20 /* FM bank 1 (38ah/222h) */
#define ENVY24_CCS_NMI2_PICIO  0x0f /* PIC I/O cycle */
#define ENVY24_CCS_NMI2_PIC20W 0x01 /* 20h write */
#define ENVY24_CCS_NMI2_PICA0W 0x02 /* a0h write */
#define ENVY24_CCS_NMI2_PIC21W 0x05 /* 21h write */
#define ENVY24_CCS_NMI2_PICA1W 0x06 /* a1h write */
#define ENVY24_CCS_NMI2_PIC20R 0x09 /* 20h read */
#define ENVY24_CCS_NMI2_PICA0R 0x0a /* a0h read */
#define ENVY24_CCS_NMI2_PIC21R 0x0d /* 21h read */
#define ENVY24_CCS_NMI2_PICA1R 0x0e /* a1h read */

#define ENVY24_CCS_JOY      0x0f /* Game port register */

#define ENVY24_CCS_I2CDEV   0x10 /* I2C Port Device Address Register */
#define ENVY24_CCS_I2CDEV_ADDR 0xfe /* I2C device address */
#define ENVY24_CCS_I2CDEV_ROM  0xa0 /* reserved for the external I2C E2PROM */
#define ENVY24_CCS_I2CDEV_WR   0x01 /* write */
#define ENVY24_CCS_I2CDEV_RD   0x00 /* read */

#define ENVY24_CCS_I2CADDR  0x11 /* I2C Port Byte Address Register */
#define ENVY24_CCS_I2CDATA  0x12 /* I2C Port Read/Write Data Register */

#define ENVY24_CCS_I2CSTAT  0x13 /* I2C Port Control and Status Register */
#define ENVY24_CCS_I2CSTAT_ROM 0x80 /* external E2PROM exists */
#define ENVY24_CCS_I2CSTAT_BSY 0x01 /* I2C port read/write status busy */

#define ENVY24_CCS_CDMABASE 0x14 /* Consumer Record DMA Current/Base Address Register */
#define ENVY24_CCS_CDMACNT  0x18 /* Consumer Record DMA Current/Base Count Register */
#define ENVY24_CCS_SERR     0x1b /* PCI Configuration SERR# Shadow Register */
#define ENVY24_CCS_SMIDIDAT 0x1c /* Secondary MIDI UART Data Register */
#define ENVY24_CCS_SMIDICMD 0x1d /* Secondary MIDI UART Command/Status Register */

#define ENVY24_CCS_TIMER    0x1e /* Timer Register */
#define ENVY24_CCS_TIMER_EN    0x8000 /* Timer count enable */
#define ENVY24_CCS_TIMER_MASK  0x7fff /* Timer counter mask */

/* Controller Indexed Registers */

#define ENVY24_CCI_PTCHIGH  0x00 /* Playback Terminal Count Register (High Byte) */
#define ENVY24_CCI_PTCLOW   0x01 /* Playback Terminal Count Register (Low Byte) */

#define ENVY24_CCI_PCTL     0x02 /* Playback Control Register */
#define ENVY24_CCI_PCTL_TURBO  0x80 /* 4x up sampling in the host by software */
#define ENVY24_CCI_PCTL_U8     0x10 /* 8 bits unsigned */
#define ENVY24_CCI_PCTL_S16    0x00 /* 16 bits signed */
#define ENVY24_CCI_PCTL_STEREO 0x08 /* stereo */
#define ENVY24_CCI_PCTL_MONO   0x00 /* mono */
#define ENVY24_CCI_PCTL_FLUSH  0x04 /* FIFO flush (sticky bit. Requires toggling) */
#define ENVY24_CCI_PCTL_PAUSE  0x02 /* Pause */
#define ENVY24_CCI_PCTL_ENABLE 0x01 /* Playback enable */

#define ENVY24_CCI_PLVOL    0x03 /* Playback Left Volume/Pan Register */
#define ENVY24_CCI_PRVOL    0x04 /* Playback Right Volume/Pan Register */
#define ENVY24_CCI_VOL_MASK    0x3f /* Volume value mask */

#define ENVY24_CCI_SOFTVOL  0x05 /* Soft Volume/Mute Control Register */
#define ENVY24_CCI_PSRLOW   0x06 /* Playback Sampling Rate Register (Low Byte) */
#define ENVY24_CCI_PSRMID   0x07 /* Playback Sampling Rate Register (Middle Byte) */
#define ENVY24_CCI_PSRHIGH  0x08 /* Playback Sampling Rate Register (High Byte) */
#define ENVY24_CCI_RTCHIGH  0x10 /* Record Terminal Count Register (High Byte) */
#define ENVY24_CCI_RTCLOW   0x11 /* Record Terminal Count Register (Low Byte) */

#define ENVY24_CCI_RCTL     0x12 /* Record Control Register */
#define ENVY24_CCI_RCTL_DRTN   0x80 /* Digital return enable */
#define ENVY24_CCI_RCTL_U8     0x04 /* 8 bits unsigned */
#define ENVY24_CCI_RCTL_S16    0x00 /* 16 bits signed */
#define ENVY24_CCI_RCTL_STEREO 0x00 /* stereo */
#define ENVY24_CCI_RCTL_MONO   0x02 /* mono */
#define ENVY24_CCI_RCTL_ENABLE 0x01 /* Record enable */

#define ENVY24_CCI_GPIODAT  0x20 /* GPIO Data Register */
#define ENVY24_CCI_GPIOMASK 0x21 /* GPIO Write Mask Register */

#define ENVY24_CCI_GPIOCTL  0x22 /* GPIO Direction Control Register */
#define ENVY24_CCI_GPIO_OUT    1 /* output */
#define ENVY24_CCI_GPIO_IN     0 /* input */

#define ENVY24_CCI_CPDWN   0x30 /* Consumer Section Power Down Register */
#define ENVY24_CCI_CPDWN_XTAL  0x80 /* Crystal clock generation power down for XTAL_1 */
#define ENVY24_CCI_CPDWN_GAME  0x40 /* Game port analog power down */
#define ENVY24_CCI_CPDWN_I2C   0x10 /* I2C port clock */
#define ENVY24_CCI_CPDWN_MIDI  0x08 /* MIDI clock */
#define ENVY24_CCI_CPDWN_AC97  0x04 /* AC'97 clock */
#define ENVY24_CCI_CPDWN_DS    0x02 /* DS Block clock */
#define ENVY24_CCI_CPDWN_PCI   0x01 /* PCI clock for SB, DMA controller */

#define ENVY24_CCI_MTPDWN  0x31 /* Multi-Track Section Power Down Register */
#define ENVY24_CCI_MTPDWN_XTAL 0x80 /* Crystal clock generation power down for XTAL_2 */
#define ENVY24_CCI_MTPDWN_SPDIF 0x04 /* S/PDIF clock */
#define ENVY24_CCI_MTPDWN_MIX  0x02 /* Professional digital mixer clock */
#define ENVY24_CCI_MTPDWN_I2S  0x01 /* Multi-track I2S serial interface clock */

/* DDMA Registers */

#define ENVY24_DDMA_ADDR0  0x00 /* DMA Base and Current Address bit 0-7 */
#define ENVY24_DDMA_ADDR8  0x01 /* DMA Base and Current Address bit 8-15 */
#define ENVY24_DDMA_ADDR16 0x02 /* DMA Base and Current Address bit 16-23 */
#define ENVY24_DDMA_ADDR24 0x03 /* DMA Base and Current Address bit 24-31 */
#define ENVY24_DDMA_CNT0   0x04 /* DMA Base and Current Count 0-7 */
#define ENVY24_DDMA_CNT8   0x05 /* DMA Base and Current Count 8-15 */
#define ENVY24_DDMA_CNT16  0x06 /* (not supported) */
#define ENVY24_DDMA_CMD    0x08 /* Status and Command */
#define ENVY24_DDMA_MODE   0x0b /* Mode */
#define ENVY24_DDMA_RESET  0x0c /* Master reset */
#define ENVY24_DDMA_CHAN   0x0f /* Channel Mask */

/* Consumer Section DMA Channel Registers */

#define ENVY24_CS_INTMASK  0x00 /* DirectSound DMA Interrupt Mask Register */
#define ENVY24_CS_INTSTAT  0x02 /* DirectSound DMA Interrupt Status Register */
#define ENVY24_CS_CHDAT    0x04 /* Channel Data register */

#define ENVY24_CS_CHIDX    0x08 /* Channel Index Register */
#define ENVY24_CS_CHIDX_NUM   0xf0 /* Channel number */
#define ENVY24_CS_CHIDX_ADDR0 0x00 /* Buffer_0 DMA base address */
#define ENVY24_CS_CHIDX_CNT0  0x01 /* Buffer_0 DMA base count */
#define ENVY24_CS_CHIDX_ADDR1 0x02 /* Buffer_1 DMA base address */
#define ENVY24_CS_CHIDX_CNT1  0x03 /* Buffer_1 DMA base count */
#define ENVY24_CS_CHIDX_CTL   0x04 /* Channel Control and Status register */
#define ENVY24_CS_CHIDX_RATE  0x05 /* Channel Sampling Rate */
#define ENVY24_CS_CHIDX_VOL   0x06 /* Channel left and right volume/pan control */
/* Channel Control and Status Register at Index 4h */
#define ENVY24_CS_CTL_BUF     0x80 /* indicating that the current active buffer */
#define ENVY24_CS_CTL_AUTO1   0x40 /* Buffer_1 auto init. enable */
#define ENVY24_CS_CTL_AUTO0   0x20 /* Buffer_0 auto init. enable */
#define ENVY24_CS_CTL_FLUSH   0x10 /* Flush FIFO */
#define ENVY24_CS_CTL_STEREO  0x08 /* stereo(or mono) */
#define ENVY24_CS_CTL_U8      0x04 /* 8-bit unsigned(or 16-bit signed) */
#define ENVY24_CS_CTL_PAUSE   0x02 /* DMA request 1:pause */
#define ENVY24_CS_CTL_START   0x01 /* DMA request 1: start, 0:stop */
/* Consumer mode Left/Right Volume Register at Index 06h */
#define ENVY24_CS_VOL_RIGHT   0x3f00
#define ENVY24_CS_VOL_LEFT    0x003f

/* Professional Multi-Track Control Registers */

#define ENVY24_MT_INT      0x00 /* DMA Interrupt Mask and Status Register */
#define ENVY24_MT_INT_RMASK   0x80 /* Multi-track record interrupt mask */
#define ENVY24_MT_INT_PMASK   0x40 /* Multi-track playback interrupt mask */
#define ENVY24_MT_INT_RSTAT   0x02 /* Multi-track record interrupt status */
#define ENVY24_MT_INT_PSTAT   0x01 /* Multi-track playback interrupt status */

#define ENVY24_MT_RATE     0x01 /* Sampling Rate Select Register */
#define ENVY24_MT_RATE_SPDIF  0x10 /* S/PDIF input clock as the master */
#define ENVY24_MT_RATE_48000  0x00
#define ENVY24_MT_RATE_24000  0x01
#define ENVY24_MT_RATE_12000  0x02
#define ENVY24_MT_RATE_9600   0x03
#define ENVY24_MT_RATE_32000  0x04
#define ENVY24_MT_RATE_16000  0x05
#define ENVY24_MT_RATE_8000   0x06
#define ENVY24_MT_RATE_96000  0x07
#define ENVY24_MT_RATE_64000  0x0f
#define ENVY24_MT_RATE_44100  0x08
#define ENVY24_MT_RATE_22050  0x09
#define ENVY24_MT_RATE_11025  0x0a
#define ENVY24_MT_RATE_88200  0x0b
#define ENVY24_MT_RATE_MASK   0x0f

#define ENVY24_MT_I2S      0x02 /* I2S Data Format Register */
#define ENVY24_MT_I2S_MLR128  0x08 /* MCLK/LRCLK ratio 128x(or 256x) */
#define ENVY24_MT_I2S_SLR48   0x04 /* SCLK/LRCLK ratio 48bpf(or 64bpf) */
#define ENVY24_MT_I2S_FORM    0x00 /* I2S data format */

#define ENVY24_MT_AC97IDX  0x04 /* Index Register for AC'97 Codecs */

#define ENVY24_MT_AC97CMD  0x05 /* Command and Status Register for AC'97 Codecs */
#define ENVY24_MT_AC97CMD_CLD 0x80 /* Cold reset */
#define ENVY24_MT_AC97CMD_WRM 0x40 /* Warm reset */
#define ENVY24_MT_AC97CMD_WR  0x20 /* write to AC'97 codec register */
#define ENVY24_MT_AC97CMD_RD  0x10 /* read AC'97 CODEC register */
#define ENVY24_MT_AC97CMD_RDY 0x08 /* AC'97 codec ready status bit */
#define ENVY24_MT_AC97CMD_ID  0x03 /* ID(0-3) for external AC 97 registers */

#define ENVY24_MT_AC97DLO  0x06 /* AC'97 codec register data low byte */
#define ENVY24_MT_AC97DHI  0x07 /* AC'97 codec register data high byte */
#define ENVY24_MT_PADDR    0x10 /* Playback DMA Current/Base Address Register */
#define ENVY24_MT_PCNT     0x14 /* Playback DMA Current/Base Count Register */
#define ENVY24_MT_PTERM    0x16 /* Playback Current/Base Terminal Count Register */
#define ENVY24_MT_PCTL     0x18 /* Playback and Record Control Register */
#define ENVY24_MT_PCTL_RSTART 0x04 /* 1: Record start; 0: Record stop */
#define ENVY24_MT_PCTL_PAUSE  0x02 /* 1: Pause; 0: Resume */
#define ENVY24_MT_PCTL_PSTART 0x01 /* 1: Playback start; 0: Playback stop */

#define ENVY24_MT_RADDR    0x20 /* Record DMA Current/Base Address Register */
#define ENVY24_MT_RCNT     0x24 /* Record DMA Current/Base Count Register */
#define ENVY24_MT_RTERM    0x26 /* Record Current/Base Terminal Count Register */
#define ENVY24_MT_RCTL     0x28 /* Record Control Register */
#define ENVY24_MT_RCTL_RSTART 0x01 /* 1: Record start; 0: Record stop */

#define ENVY24_MT_PSDOUT   0x30 /* Routing Control Register for Data to PSDOUT[0:3] */
#define ENVY24_MT_SPDOUT   0x32 /* Routing Control Register for SPDOUT */
#define ENVY24_MT_RECORD   0x34 /* Captured (Recorded) data Routing Selection Register */

#define BUS_SPACE_MAXADDR_ENVY24 0x0fffffff /* Address space beyond 256MB is not supported */
#define BUS_SPACE_MAXSIZE_ENVY24 0x3fffc /* 64k x 4byte(1dword) */

#define ENVY24_MT_VOLUME   0x38 /* Left/Right Volume Control Data Register */
#define ENVY24_MT_VOLUME_L    0x007f /* Left Volume Mask */
#define ENVY24_MT_VOLUME_R    0x7f00 /* Right Volume Mask */

#define ENVY24_MT_VOLIDX   0x3a /* Volume Control Stream Index Register */
#define ENVY24_MT_VOLRATE  0x3b /* Volume Control Rate Register */
#define ENVY24_MT_MONAC97  0x3c /* Digital Mixer Monitor Routing Control Register */
#define ENVY24_MT_PEAKIDX  0x3e /* Peak Meter Index Register */
#define ENVY24_MT_PEAKDAT  0x3f /* Peak Meter Data Register */

/* -------------------------------------------------------------------- */

/* ENVY24 mixer channel defines */
/*
  ENVY24 mixer has original line matrix. So, general mixer command is not
  able to use for this. If system has consumer AC'97 output, AC'97 line is
  used as master mixer, and it is able to control.
*/
#define ENVY24_CHAN_NUM  11 /* Play * 5 + Record * 5 + Mix * 1 */

#define ENVY24_CHAN_PLAY_DAC1  0
#define ENVY24_CHAN_PLAY_DAC2  1
#define ENVY24_CHAN_PLAY_DAC3  2
#define ENVY24_CHAN_PLAY_DAC4  3
#define ENVY24_CHAN_PLAY_SPDIF 4
#define ENVY24_CHAN_REC_ADC1   5
#define ENVY24_CHAN_REC_ADC2   6
#define ENVY24_CHAN_REC_ADC3   7
#define ENVY24_CHAN_REC_ADC4   8
#define ENVY24_CHAN_REC_SPDIF  9
#define ENVY24_CHAN_REC_MIX   10

#define ENVY24_MIX_MASK     0x3ff
#define ENVY24_MIX_REC_MASK 0x3e0

/* volume value constants */
#define ENVY24_VOL_MAX    0 /* 0db(negate) */
#define ENVY24_VOL_MIN   96 /* -144db(negate) */
#define ENVY24_VOL_MUTE 127 /* mute */

/* -------------------------------------------------------------------- */

/* ENVY24 routing control defines */
/*
  ENVY24 has input->output data routing matrix switch. But original ENVY24
  matrix control is so complex. So, in this driver, matrix control is
  defined 4 parameters.

  1: output DAC channels (include S/PDIF output)
  2: output data classes
     a. direct output from DMA
     b. MIXER output which mixed the DMA outputs and input channels
        (NOTICE: this class is able to set only DAC-1 and S/PDIF output)
     c. direct input from ADC
     d. direct input from S/PDIF
  3: input ADC channel selection(when 2:c. is selected)
  4: left/right reverse

  These parameters matrix is bit reduced from original ENVY24 matrix
  pattern(ex. route different ADC input to one DAC). But almost case
  this is enough to use.
*/
#define ENVY24_ROUTE_DAC_1       0
#define ENVY24_ROUTE_DAC_2       1
#define ENVY24_ROUTE_DAC_3       2
#define ENVY24_ROUTE_DAC_4       3
#define ENVY24_ROUTE_DAC_SPDIF   4

#define ENVY24_ROUTE_CLASS_DMA   0
#define ENVY24_ROUTE_CLASS_MIX   1
#define ENVY24_ROUTE_CLASS_ADC   2
#define ENVY24_ROUTE_CLASS_SPDIF 3

#define ENVY24_ROUTE_ADC_1       0
#define ENVY24_ROUTE_ADC_2       1
#define ENVY24_ROUTE_ADC_3       2
#define ENVY24_ROUTE_ADC_4       3

#define ENVY24_ROUTE_NORMAL      0
#define ENVY24_ROUTE_REVERSE     1
#define ENVY24_ROUTE_LEFT        0
#define ENVY24_ROUTE_RIGHT       1

/* -------------------------------------------------------------------- */

/*
  These map values are refferd from ALSA sound driver.
*/
/* ENVY24 configuration E2PROM map */
#define ENVY24_E2PROM_SUBVENDOR  0x00
#define ENVY24_E2PROM_SUBDEVICE  0x02
#define ENVY24_E2PROM_SIZE       0x04
#define ENVY24_E2PROM_VERSION    0x05
#define ENVY24_E2PROM_SCFG       0x06
#define ENVY24_E2PROM_ACL        0x07
#define ENVY24_E2PROM_I2S        0x08
#define ENVY24_E2PROM_SPDIF      0x09
#define ENVY24_E2PROM_GPIOMASK   0x0a
#define ENVY24_E2PROM_GPIOSTATE  0x0b
#define ENVY24_E2PROM_GPIODIR    0x0c
#define ENVY24_E2PROM_AC97MAIN   0x0d
#define ENVY24_E2PROM_AC97PCM    0x0f
#define ENVY24_E2PROM_AC97REC    0x11
#define ENVY24_E2PROM_AC97RECSRC 0x13
#define ENVY24_E2PROM_DACID      0x14
#define ENVY24_E2PROM_ADCID      0x18
#define ENVY24_E2PROM_EXTRA      0x1c

/* GPIO connect map of M-Audio Delta series */
#define ENVY24_GPIO_CS84X4_PRO    0x01
#define ENVY24_GPIO_CS8414_STATUS 0x02
#define ENVY24_GPIO_CS84X4_CLK    0x04
#define ENVY24_GPIO_CS84X4_DATA   0x08
#define ENVY24_GPIO_AK4524_CDTI   0x10 /* this value is duplicated to input select */
#define ENVY24_GPIO_AK4524_CCLK   0x20
#define ENVY24_GPIO_AK4524_CS0    0x40
#define ENVY24_GPIO_AK4524_CS1    0x80

/* M-Audio Delta series S/PDIF(CS84[01]4) control pin values */
#define ENVY24_CS8404_PRO_RATE    0x18
#define ENVY24_CS8404_PRO_RATE32  0x00
#define ENVY24_CS8404_PRO_RATE441 0x10
#define ENVY24_CS8404_PRO_RATE48  0x08

/* M-Audio Delta series parameter */
#define ENVY24_DELTA_AK4524_CIF 0

#define I2C_DELAY 1000

/* PCA9554 registers */
#define PCA9554_I2CDEV          0x40    /* I2C device address */
#define PCA9554_IN              0x00    /* input port */
#define PCA9554_OUT             0x01    /* output port */
#define PCA9554_INVERT          0x02    /* polarity invert */
#define PCA9554_DIR             0x03    /* port directions */

/* PCF8574 registers */
#define PCF8574_I2CDEV_DAC      0x48
#define PCF8574_SENSE_MASK      0x40

/* end of file */
