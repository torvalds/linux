/*	$OpenBSD: envyreg.h,v 1.20 2022/01/09 05:42:45 jsg Exp $	*/
/*
 * Copyright (c) 2007 Alexandre Ratchov <alex@caoua.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef SYS_DEV_PCI_ENVYREG_H
#define SYS_DEV_PCI_ENVYREG_H

/*
 * BARs at PCI config space
 */
#define ENVY_CTL_BAR		0x10
#define ENVY_MT_BAR(isht)	((isht) ? 0x14 : 0x1c)
#define ENVY_CONF		0x60

/*
 * CCS "control" register
 */
#define ENVY_CTL		0x00
#define   ENVY_CTL_RESET	0x80
#define   ENVY_CTL_NATIVE	0x01
#define ENVY_CCS_INTMASK	0x01
#define   ENVY_CCS_INT_MT	0x10
#define   ENVY_CCS_INT_MIDI0	0x80
#define   ENVY_CCS_INT_MIDI1	0x20	/* Envy24 only */
#define ENVY_CCS_INTSTAT	0x02
#define ENVY_CCS_CONF		0x04	/* Envy24HT only */
#define ENVY_CCS_ACLINK		0x05	/* Envy24HT only */
#define ENVY_CCS_I2S		0x06	/* Envy24HT only */
#define ENVY_CCS_SPDIF		0x07	/* Envy24HT only */
#define ENVY_CCS_MIDIDATA0	0x0c
#define ENVY_CCS_MIDISTAT0	0x0d
#define ENVY_CCS_MIDIDATA1	0x1c	/* Envy24 only */
#define ENVY_CCS_MIDISTAT1	0x1d	/* Envy24 only */
#define ENVY_CCS_MIDIWAT	0x0e	/* Envy24HT only */
#define   ENVY_CCS_MIDIWAT_RX	0x20
#define ENVY_CCS_MIDIDATA1	0x1c
#define ENVY_CCS_MIDISTAT1	0x1d
#define ENVY_CCS_GPIODATA0	0x14	/* Envy24HT only */
#define ENVY_CCS_GPIODATA1	0x15	/* Envy24HT only */
#define ENVY_CCS_GPIODATA2	0x1e	/* Envy24HT only */
#define ENVY_CCS_GPIOMASK0	0x16	/* Envy24HT only */
#define ENVY_CCS_GPIOMASK1	0x17	/* Envy24HT only */
#define ENVY_CCS_GPIOMASK2	0x1f	/* Envy24HT only */
#define ENVY_CCS_GPIODIR0	0x18	/* Envy24HT only */
#define ENVY_CCS_GPIODIR1	0x19	/* Envy24HT only */
#define ENVY_CCS_GPIODIR2	0x1a	/* Envy24HT only */

/*
 * CCS registers to access indirect registers (CCI)
 */
#define ENVY_CCI_INDEX	0x3
#define ENVY_CCI_DATA	0x4

/*
 * CCS registers to access iic bus
 */
#define ENVY_I2C_DEV		0x10
#define   ENVY_I2C_DEV_SHIFT	0x01
#define   ENVY_I2C_DEV_WRITE	0x01
#define   ENVY_I2C_DEV_EEPROM	0x50
#define ENVY_I2C_ADDR		0x11
#define ENVY_I2C_DATA		0x12
#define ENVY_I2C_CTL		0x13
#define   ENVY_I2C_CTL_BUSY	0x1

/*
 * CCI registers to access GPIO pins
 */
#define ENVY_CCI_GPIODATA	0x20
#define ENVY_CCI_GPIOMASK	0x21
#define ENVY_CCI_GPIODIR	0x22

/*
 * EEPROM bytes signification
 */
#define ENVY_EEPROM_CONF	6
#define   ENVY_CONF_MIDI	0x20
#define ENVY_EEPROM_ACLINK	7
#define ENVY_EEPROM_I2S		8
#define ENVY_EEPROM_SPDIF	9
#define ENVY_EEPROM_GPIOMASK(s)	((s)->isht ? 13 : 10)
#define ENVY_EEPROM_GPIOST(s)	((s)->isht ? 16 : 11)
#define ENVY_EEPROM_GPIODIR(s)	((s)->isht ? 10 : 12)

/*
 * MIDI status
 */
#define ENVY_MIDISTAT_IEMPTY(s)	((s)->isht ? 0x8 : 0x80)
#define ENVY_MIDISTAT_OBUSY(s)	((s)->isht ? 0x4 : 0x40)
#define ENVY_MIDISTAT_RESET	0xff
#define ENVY_MIDISTAT_UART	0x3f

/*
 * MT registers for play/record params
 */
#define ENVY_MT_INTR		0
#define   ENVY_MT_INTR_PACK	0x01
#define   ENVY_MT_INTR_RACK	0x02
#define   ENVY_MT_INTR_ERR	0x08	/* fifo error on HT, else reads 0 */
#define   ENVY_MT_INTR_ALL	0x0b	/* all of above */
#define   ENVY_MT_INTR_PMASK	0x40	/* !HT only */
#define   ENVY_MT_INTR_RMASK	0x80	/* !HT only */
#define ENVY_MT_RATE		1
#define   ENVY_MT_RATEMASK	0x0f
#define ENVY_MT_FMT		2
#define   ENVY_MT_FMT_128X	0x08	/* HT only */
#define ENVY_MT_IMASK		3	/* HT only */
#define   ENVY_MT_IMASK_PDMA0	0x1
#define   ENVY_MT_IMASK_RDMA0	0x2
#define   ENVY_MT_IMASK_ERR	0x8
#define ENVY_MT_AC97_IDX	4
#define ENVY_MT_AC97_CMD	5
#define   ENVY_MT_AC97_READY	0x08
#define   ENVY_MT_AC97_CMD_MASK	0x30
#define   ENVY_MT_AC97_CMD_RD	0x10
#define   ENVY_MT_AC97_CMD_WR	0x20
#define   ENVY_MT_AC97_CMD_RST	0x80
#define ENVY_MT_AC97_DATA	6
#define ENVY_MT_PADDR		0x10
#define ENVY_MT_PBUFSZ		0x14
#define ENVY_MT_PBLKSZ(s)	((s)->isht ? 0x1c : 0x16)
#define ENVY_MT_CTL		0x18
#define   ENVY_MT_CTL_PSTART	0x01
#define   ENVY_MT_CTL_RSTART(s)	((s)->isht ? 0x02 : 0x04)
#define ENVY_MT_NSTREAM		0x19	/* HT only: 4 - active DACs */
#define ENVY_MT_ERR		0x1a	/* HT only: fifo error */
#define ENVY_MT_RADDR		0x20
#define ENVY_MT_RBUFSZ		0x24
#define ENVY_MT_RBLKSZ		0x26

/*
 * MT registers for monitor gains
 */
#define ENVY_MT_MONDATA		0x38
#define   ENVY_MT_MONVAL_BITS	7
#define   ENVY_MT_MONVAL_MASK	((1 << ENVY_MT_MONVAL_BITS) - 1)
#define ENVY_MT_MONIDX		0x3a

/*
 * MT registers to access the digital mixer
 */
#define ENVY_MT_OUTSRC		0x30
#define   ENVY_MT_OUTSRC_DMA	0x00
#define   ENVY_MT_OUTSRC_MON	0x01
#define   ENVY_MT_OUTSRC_LINE	0x02
#define   ENVY_MT_OUTSRC_SPD	0x03
#define   ENVY_MT_OUTSRC_MASK	0x03
#define ENVY_MT_SPDROUTE	0x32
#define   ENVY_MT_SPDSRC_DMA	0x00
#define   ENVY_MT_SPDSRC_MON	0x01
#define   ENVY_MT_SPDSRC_LINE	0x02
#define   ENVY_MT_SPDSRC_SPD	0x03
#define   ENVY_MT_SPDSRC_BITS	0x02
#define   ENVY_MT_SPDSRC_MASK	((1 << ENVY_MT_SPDSRC_BITS) - 1)
#define   ENVY_MT_SPDSEL_BITS	0x4
#define   ENVY_MT_SPDSEL_MASK	((1 << ENVY_MT_SPDSEL_BITS) - 1)
#define ENVY_MT_INSEL		0x34
#define   ENVY_MT_INSEL_BITS	0x4
#define   ENVY_MT_INSEL_MASK	((1 << ENVY_MT_INSEL_BITS) - 1)

/*
 * HT routing control
 */
#define ENVY_MT_HTSRC		0x2c
#define   ENVY_MT_HTSRC_DMA	0x00
#define   ENVY_MT_HTSRC_LINE	0x02
#define   ENVY_MT_HTSRC_SPD	0x04
#define   ENVY_MT_HTSRC_MASK	0x07

/*
 * AK4524 control registers
 */
#define AK4524_PWR		0x00
#define   AK4524_PWR_DA		0x01
#define   AK4524_PWR_AD		0x02
#define   AK4524_PWR_VREF	0x04
#define AK4524_RST		0x01
#define   AK4524_RST_DA		0x01
#define   AK4524_RST_AD		0x02
#define AK4524_FMT		0x02
#define   AK4524_FMT_NORM	0
#define   AK4524_FMT_DBL	0x01
#define   AK4524_FMT_QUA	0x02
#define   AK4524_FMT_QAUDFILT	0x04
#define   AK4524_FMT_256	0
#define   AK4524_FMT_512	0x04
#define   AK4524_FMT_1024	0x08
#define   AK4524_FMT_384	0x10
#define   AK4524_FMT_768	0x14
#define   AK4524_FMT_LSB16	0
#define   AK4524_FMT_LSB20	0x20
#define   AK4524_FMT_MSB24	0x40
#define   AK4524_FMT_IIS24	0x60
#define   AK4524_FMT_LSB24	0x80
#define AK4524_DEEMVOL		0x03
#define   AK4524_DEEM_44K1	0x00
#define   AK4524_DEEM_OFF	0x01
#define   AK4524_DEEM_48K	0x02
#define   AK4524_DEEM_32K	0x03
#define   AK4524_MUTE		0x80
#define AK4524_ADC_GAIN0	0x04
#define AK4524_ADC_GAIN1	0x05
#define AK4524_DAC_GAIN0	0x06
#define AK4524_DAC_GAIN1	0x07

/*
 * AK4358 control registers
 */
#define AK4358_ATT(chan)	((chan) <= 5 ? 0x4 + (chan) : 0xb - 6 + (chan))
#define   AK4358_ATT_EN		0x80
#define AK4358_SPEED		2
#define   AK4358_SPEED_RSTN	0x01	/* 0 = reset, 1 = normal op */
#define   AK4358_SPEED_PW1	0x02	/* power-down dac1 */
#define   AK4358_SPEED_PW2	0x04	/* power-down dac2 */
#define   AK4358_SPEED_PW3	0x08	/* power-down dac3 */
#define   AK4358_SPEED_DFS0	0x10	/* rate multiplier (1x, 2x, 4x) */
#define   AK4358_SPEED_DFS1	0x20
#define   AK4358_SPEED_PW4	0x40	/* power-down dac4 */
#define   AK4358_SPEED_DEFAULT	0x4f	/* default register value */

/*
 * AK5365 control registers
 */
#define AK5365_RST		0x00
#define   AK5365_RST_NORM	0x01
#define AK5365_SRC		0x01
#define   AK5365_SRC_MASK	0x07
#define AK5365_CTRL		0x02
#define   AK5365_CTRL_MUTE	0x01
#define   AK5365_CTRL_I2S	0x08
#define AK5365_ATT(chan)	(0x4 + (chan))

/*
 * default formats
 */
#define ENVY_RCHANS		12
#define ENVY_PCHANS		10
#define ENVY_RFRAME_SIZE	(4 * ENVY_RCHANS)
#define ENVY_PFRAME_SIZE	(4 * ENVY_PCHANS)

#endif /* !defined(SYS_DEV_PCI_ENVYREG_H) */
