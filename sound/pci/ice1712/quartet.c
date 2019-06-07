// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Infrasonic Quartet
 *
 *	Copyright (c) 2009 Pavel Hofman <pavel.hofman@ivitera.com>
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/tlv.h>
#include <sound/info.h>

#include "ice1712.h"
#include "envy24ht.h"
#include <sound/ak4113.h>
#include "quartet.h"

struct qtet_spec {
	struct ak4113 *ak4113;
	unsigned int scr;	/* system control register */
	unsigned int mcr;	/* monitoring control register */
	unsigned int cpld;	/* cpld register */
};

struct qtet_kcontrol_private {
	unsigned int bit;
	void (*set_register)(struct snd_ice1712 *ice, unsigned int val);
	unsigned int (*get_register)(struct snd_ice1712 *ice);
	const char * const texts[2];
};

enum {
	IN12_SEL = 0,
	IN34_SEL,
	AIN34_SEL,
	COAX_OUT,
	IN12_MON12,
	IN12_MON34,
	IN34_MON12,
	IN34_MON34,
	OUT12_MON34,
	OUT34_MON12,
};

static const char * const ext_clock_names[3] = {"IEC958 In", "Word Clock 1xFS",
	"Word Clock 256xFS"};

/* chip address on I2C bus */
#define AK4113_ADDR		0x26	/* S/PDIF receiver */

/* chip address on SPI bus */
#define AK4620_ADDR		0x02	/* ADC/DAC */


/*
 * GPIO pins
 */

/* GPIO0 - O - DATA0, def. 0 */
#define GPIO_D0			(1<<0)
/* GPIO1 - I/O - DATA1, Jack Detect Input0 (0:present, 1:missing), def. 1 */
#define GPIO_D1_JACKDTC0	(1<<1)
/* GPIO2 - I/O - DATA2, Jack Detect Input1 (0:present, 1:missing), def. 1 */
#define GPIO_D2_JACKDTC1	(1<<2)
/* GPIO3 - I/O - DATA3, def. 1 */
#define GPIO_D3			(1<<3)
/* GPIO4 - I/O - DATA4, SPI CDTO, def. 1 */
#define GPIO_D4_SPI_CDTO	(1<<4)
/* GPIO5 - I/O - DATA5, SPI CCLK, def. 1 */
#define GPIO_D5_SPI_CCLK	(1<<5)
/* GPIO6 - I/O - DATA6, Cable Detect Input (0:detected, 1:not detected */
#define GPIO_D6_CD		(1<<6)
/* GPIO7 - I/O - DATA7, Device Detect Input (0:detected, 1:not detected */
#define GPIO_D7_DD		(1<<7)
/* GPIO8 - O - CPLD Chip Select, def. 1 */
#define GPIO_CPLD_CSN		(1<<8)
/* GPIO9 - O - CPLD register read/write (0:write, 1:read), def. 0 */
#define GPIO_CPLD_RW		(1<<9)
/* GPIO10 - O - SPI Chip Select for CODEC#0, def. 1 */
#define GPIO_SPI_CSN0		(1<<10)
/* GPIO11 - O - SPI Chip Select for CODEC#1, def. 1 */
#define GPIO_SPI_CSN1		(1<<11)
/* GPIO12 - O - Ex. Register Output Enable (0:enable, 1:disable), def. 1,
 * init 0 */
#define GPIO_EX_GPIOE		(1<<12)
/* GPIO13 - O - Ex. Register0 Chip Select for System Control Register,
 * def. 1 */
#define GPIO_SCR		(1<<13)
/* GPIO14 - O - Ex. Register1 Chip Select for Monitor Control Register,
 * def. 1 */
#define GPIO_MCR		(1<<14)

#define GPIO_SPI_ALL		(GPIO_D4_SPI_CDTO | GPIO_D5_SPI_CCLK |\
		GPIO_SPI_CSN0 | GPIO_SPI_CSN1)

#define GPIO_DATA_MASK		(GPIO_D0 | GPIO_D1_JACKDTC0 | \
		GPIO_D2_JACKDTC1 | GPIO_D3 | \
		GPIO_D4_SPI_CDTO | GPIO_D5_SPI_CCLK | \
		GPIO_D6_CD | GPIO_D7_DD)

/* System Control Register GPIO_SCR data bits */
/* Mic/Line select relay (0:line, 1:mic) */
#define SCR_RELAY		GPIO_D0
/* Phantom power drive control (0:5V, 1:48V) */
#define SCR_PHP_V		GPIO_D1_JACKDTC0
/* H/W mute control (0:Normal, 1:Mute) */
#define SCR_MUTE		GPIO_D2_JACKDTC1
/* Phantom power control (0:Phantom on, 1:off) */
#define SCR_PHP			GPIO_D3
/* Analog input 1/2 Source Select */
#define SCR_AIN12_SEL0		GPIO_D4_SPI_CDTO
#define SCR_AIN12_SEL1		GPIO_D5_SPI_CCLK
/* Analog input 3/4 Source Select (0:line, 1:hi-z) */
#define SCR_AIN34_SEL		GPIO_D6_CD
/* Codec Power Down (0:power down, 1:normal) */
#define SCR_CODEC_PDN		GPIO_D7_DD

#define SCR_AIN12_LINE		(0)
#define SCR_AIN12_MIC		(SCR_AIN12_SEL0)
#define SCR_AIN12_LOWCUT	(SCR_AIN12_SEL1 | SCR_AIN12_SEL0)

/* Monitor Control Register GPIO_MCR data bits */
/* Input 1/2 to Monitor 1/2 (0:off, 1:on) */
#define MCR_IN12_MON12		GPIO_D0
/* Input 1/2 to Monitor 3/4 (0:off, 1:on) */
#define MCR_IN12_MON34		GPIO_D1_JACKDTC0
/* Input 3/4 to Monitor 1/2 (0:off, 1:on) */
#define MCR_IN34_MON12		GPIO_D2_JACKDTC1
/* Input 3/4 to Monitor 3/4 (0:off, 1:on) */
#define MCR_IN34_MON34		GPIO_D3
/* Output to Monitor 1/2 (0:off, 1:on) */
#define MCR_OUT34_MON12		GPIO_D4_SPI_CDTO
/* Output to Monitor 3/4 (0:off, 1:on) */
#define MCR_OUT12_MON34		GPIO_D5_SPI_CCLK

/* CPLD Register DATA bits */
/* Clock Rate Select */
#define CPLD_CKS0		GPIO_D0
#define CPLD_CKS1		GPIO_D1_JACKDTC0
#define CPLD_CKS2		GPIO_D2_JACKDTC1
/* Sync Source Select (0:Internal, 1:External) */
#define CPLD_SYNC_SEL		GPIO_D3
/* Word Clock FS Select (0:FS, 1:256FS) */
#define CPLD_WORD_SEL		GPIO_D4_SPI_CDTO
/* Coaxial Output Source (IS-Link) (0:SPDIF, 1:I2S) */
#define CPLD_COAX_OUT		GPIO_D5_SPI_CCLK
/* Input 1/2 Source Select (0:Analog12, 1:An34) */
#define CPLD_IN12_SEL		GPIO_D6_CD
/* Input 3/4 Source Select (0:Analog34, 1:Digital In) */
#define CPLD_IN34_SEL		GPIO_D7_DD

/* internal clock (CPLD_SYNC_SEL = 0) options */
#define CPLD_CKS_44100HZ	(0)
#define CPLD_CKS_48000HZ	(CPLD_CKS0)
#define CPLD_CKS_88200HZ	(CPLD_CKS1)
#define CPLD_CKS_96000HZ	(CPLD_CKS1 | CPLD_CKS0)
#define CPLD_CKS_176400HZ	(CPLD_CKS2)
#define CPLD_CKS_192000HZ	(CPLD_CKS2 | CPLD_CKS0)

#define CPLD_CKS_MASK		(CPLD_CKS0 | CPLD_CKS1 | CPLD_CKS2)

/* external clock (CPLD_SYNC_SEL = 1) options */
/* external clock - SPDIF */
#define CPLD_EXT_SPDIF	(0 | CPLD_SYNC_SEL)
/* external clock - WordClock 1xfs */
#define CPLD_EXT_WORDCLOCK_1FS	(CPLD_CKS1 | CPLD_SYNC_SEL)
/* external clock - WordClock 256xfs */
#define CPLD_EXT_WORDCLOCK_256FS	(CPLD_CKS1 | CPLD_WORD_SEL |\
		CPLD_SYNC_SEL)

#define EXT_SPDIF_TYPE			0
#define EXT_WORDCLOCK_1FS_TYPE		1
#define EXT_WORDCLOCK_256FS_TYPE	2

#define AK4620_DFS0		(1<<0)
#define AK4620_DFS1		(1<<1)
#define AK4620_CKS0		(1<<2)
#define AK4620_CKS1		(1<<3)
/* Clock and Format Control register */
#define AK4620_DFS_REG		0x02

/* Deem and Volume Control register */
#define AK4620_DEEMVOL_REG	0x03
#define AK4620_SMUTE		(1<<7)

/*
 * Conversion from int value to its binary form. Used for debugging.
 * The output buffer must be allocated prior to calling the function.
 */
static char *get_binary(char *buffer, int value)
{
	int i, j, pos;
	pos = 0;
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < 8; ++j) {
			if (value & (1 << (31-(i*8 + j))))
				buffer[pos] = '1';
			else
				buffer[pos] = '0';
			pos++;
		}
		if (i < 3) {
			buffer[pos] = ' ';
			pos++;
		}
	}
	buffer[pos] = '\0';
	return buffer;
}

/*
 * Initial setup of the conversion array GPIO <-> rate
 */
static const unsigned int qtet_rates[] = {
	44100, 48000, 88200,
	96000, 176400, 192000,
};

static const unsigned int cks_vals[] = {
	CPLD_CKS_44100HZ, CPLD_CKS_48000HZ, CPLD_CKS_88200HZ,
	CPLD_CKS_96000HZ, CPLD_CKS_176400HZ, CPLD_CKS_192000HZ,
};

static const struct snd_pcm_hw_constraint_list qtet_rates_info = {
	.count = ARRAY_SIZE(qtet_rates),
	.list = qtet_rates,
	.mask = 0,
};

static void qtet_ak4113_write(void *private_data, unsigned char reg,
		unsigned char val)
{
	snd_vt1724_write_i2c((struct snd_ice1712 *)private_data, AK4113_ADDR,
			reg, val);
}

static unsigned char qtet_ak4113_read(void *private_data, unsigned char reg)
{
	return snd_vt1724_read_i2c((struct snd_ice1712 *)private_data,
			AK4113_ADDR, reg);
}


/*
 * AK4620 section
 */

/*
 * Write data to addr register of ak4620
 */
static void qtet_akm_write(struct snd_akm4xxx *ak, int chip,
		unsigned char addr, unsigned char data)
{
	unsigned int tmp, orig_dir;
	int idx;
	unsigned int addrdata;
	struct snd_ice1712 *ice = ak->private_data[0];

	if (snd_BUG_ON(chip < 0 || chip >= 4))
		return;
	/*dev_dbg(ice->card->dev, "Writing to AK4620: chip=%d, addr=0x%x,
	  data=0x%x\n", chip, addr, data);*/
	orig_dir = ice->gpio.get_dir(ice);
	ice->gpio.set_dir(ice, orig_dir | GPIO_SPI_ALL);
	/* set mask - only SPI bits */
	ice->gpio.set_mask(ice, ~GPIO_SPI_ALL);

	tmp = ice->gpio.get_data(ice);
	/* high all */
	tmp |= GPIO_SPI_ALL;
	ice->gpio.set_data(ice, tmp);
	udelay(100);
	/* drop chip select */
	if (chip)
		/* CODEC 1 */
		tmp &= ~GPIO_SPI_CSN1;
	else
		tmp &= ~GPIO_SPI_CSN0;
	ice->gpio.set_data(ice, tmp);
	udelay(100);

	/* build I2C address + data byte */
	addrdata = (AK4620_ADDR << 6) | 0x20 | (addr & 0x1f);
	addrdata = (addrdata << 8) | data;
	for (idx = 15; idx >= 0; idx--) {
		/* drop clock */
		tmp &= ~GPIO_D5_SPI_CCLK;
		ice->gpio.set_data(ice, tmp);
		udelay(100);
		/* set data */
		if (addrdata & (1 << idx))
			tmp |= GPIO_D4_SPI_CDTO;
		else
			tmp &= ~GPIO_D4_SPI_CDTO;
		ice->gpio.set_data(ice, tmp);
		udelay(100);
		/* raise clock */
		tmp |= GPIO_D5_SPI_CCLK;
		ice->gpio.set_data(ice, tmp);
		udelay(100);
	}
	/* all back to 1 */
	tmp |= GPIO_SPI_ALL;
	ice->gpio.set_data(ice, tmp);
	udelay(100);

	/* return all gpios to non-writable */
	ice->gpio.set_mask(ice, 0xffffff);
	/* restore GPIOs direction */
	ice->gpio.set_dir(ice, orig_dir);
}

static void qtet_akm_set_regs(struct snd_akm4xxx *ak, unsigned char addr,
		unsigned char mask, unsigned char value)
{
	unsigned char tmp;
	int chip;
	for (chip = 0; chip < ak->num_chips; chip++) {
		tmp = snd_akm4xxx_get(ak, chip, addr);
		/* clear the bits */
		tmp &= ~mask;
		/* set the new bits */
		tmp |= value;
		snd_akm4xxx_write(ak, chip, addr, tmp);
	}
}

/*
 * change the rate of AK4620
 */
static void qtet_akm_set_rate_val(struct snd_akm4xxx *ak, unsigned int rate)
{
	unsigned char ak4620_dfs;

	if (rate == 0)  /* no hint - S/PDIF input is master or the new spdif
			   input rate undetected, simply return */
		return;

	/* adjust DFS on codecs - see datasheet */
	if (rate > 108000)
		ak4620_dfs = AK4620_DFS1 | AK4620_CKS1;
	else if (rate > 54000)
		ak4620_dfs = AK4620_DFS0 | AK4620_CKS0;
	else
		ak4620_dfs = 0;

	/* set new value */
	qtet_akm_set_regs(ak, AK4620_DFS_REG, AK4620_DFS0 | AK4620_DFS1 |
			AK4620_CKS0 | AK4620_CKS1, ak4620_dfs);
}

#define AK_CONTROL(xname, xch)	{ .name = xname, .num_channels = xch }

#define PCM_12_PLAYBACK_VOLUME	"PCM 1/2 Playback Volume"
#define PCM_34_PLAYBACK_VOLUME	"PCM 3/4 Playback Volume"
#define PCM_12_CAPTURE_VOLUME	"PCM 1/2 Capture Volume"
#define PCM_34_CAPTURE_VOLUME	"PCM 3/4 Capture Volume"

static const struct snd_akm4xxx_dac_channel qtet_dac[] = {
	AK_CONTROL(PCM_12_PLAYBACK_VOLUME, 2),
	AK_CONTROL(PCM_34_PLAYBACK_VOLUME, 2),
};

static const struct snd_akm4xxx_adc_channel qtet_adc[] = {
	AK_CONTROL(PCM_12_CAPTURE_VOLUME, 2),
	AK_CONTROL(PCM_34_CAPTURE_VOLUME, 2),
};

static const struct snd_akm4xxx akm_qtet_dac = {
	.type = SND_AK4620,
	.num_dacs = 4,	/* DAC1 - Output 12
	*/
	.num_adcs = 4,	/* ADC1 - Input 12
	*/
	.ops = {
		.write = qtet_akm_write,
		.set_rate_val = qtet_akm_set_rate_val,
	},
	.dac_info = qtet_dac,
	.adc_info = qtet_adc,
};

/* Communication routines with the CPLD */


/* Writes data to external register reg, both reg and data are
 * GPIO representations */
static void reg_write(struct snd_ice1712 *ice, unsigned int reg,
		unsigned int data)
{
	unsigned int tmp;

	mutex_lock(&ice->gpio_mutex);
	/* set direction of used GPIOs*/
	/* all outputs */
	tmp = 0x00ffff;
	ice->gpio.set_dir(ice, tmp);
	/* mask - writable bits */
	ice->gpio.set_mask(ice, ~(tmp));
	/* write the data */
	tmp = ice->gpio.get_data(ice);
	tmp &= ~GPIO_DATA_MASK;
	tmp |= data;
	ice->gpio.set_data(ice, tmp);
	udelay(100);
	/* drop output enable */
	tmp &=  ~GPIO_EX_GPIOE;
	ice->gpio.set_data(ice, tmp);
	udelay(100);
	/* drop the register gpio */
	tmp &= ~reg;
	ice->gpio.set_data(ice, tmp);
	udelay(100);
	/* raise the register GPIO */
	tmp |= reg;
	ice->gpio.set_data(ice, tmp);
	udelay(100);

	/* raise all data gpios */
	tmp |= GPIO_DATA_MASK;
	ice->gpio.set_data(ice, tmp);
	/* mask - immutable bits */
	ice->gpio.set_mask(ice, 0xffffff);
	/* outputs only 8-15 */
	ice->gpio.set_dir(ice, 0x00ff00);
	mutex_unlock(&ice->gpio_mutex);
}

static unsigned int get_scr(struct snd_ice1712 *ice)
{
	struct qtet_spec *spec = ice->spec;
	return spec->scr;
}

static unsigned int get_mcr(struct snd_ice1712 *ice)
{
	struct qtet_spec *spec = ice->spec;
	return spec->mcr;
}

static unsigned int get_cpld(struct snd_ice1712 *ice)
{
	struct qtet_spec *spec = ice->spec;
	return spec->cpld;
}

static void set_scr(struct snd_ice1712 *ice, unsigned int val)
{
	struct qtet_spec *spec = ice->spec;
	reg_write(ice, GPIO_SCR, val);
	spec->scr = val;
}

static void set_mcr(struct snd_ice1712 *ice, unsigned int val)
{
	struct qtet_spec *spec = ice->spec;
	reg_write(ice, GPIO_MCR, val);
	spec->mcr = val;
}

static void set_cpld(struct snd_ice1712 *ice, unsigned int val)
{
	struct qtet_spec *spec = ice->spec;
	reg_write(ice, GPIO_CPLD_CSN, val);
	spec->cpld = val;
}

static void proc_regs_read(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	char bin_buffer[36];

	snd_iprintf(buffer, "SCR:	%s\n", get_binary(bin_buffer,
				get_scr(ice)));
	snd_iprintf(buffer, "MCR:	%s\n", get_binary(bin_buffer,
				get_mcr(ice)));
	snd_iprintf(buffer, "CPLD:	%s\n", get_binary(bin_buffer,
				get_cpld(ice)));
}

static void proc_init(struct snd_ice1712 *ice)
{
	snd_card_ro_proc_new(ice->card, "quartet", ice, proc_regs_read);
}

static int qtet_mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	val = get_scr(ice) & SCR_MUTE;
	ucontrol->value.integer.value[0] = (val) ? 0 : 1;
	return 0;
}

static int qtet_mute_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int old, new, smute;
	old = get_scr(ice) & SCR_MUTE;
	if (ucontrol->value.integer.value[0]) {
		/* unmute */
		new = 0;
		/* un-smuting DAC */
		smute = 0;
	} else {
		/* mute */
		new = SCR_MUTE;
		/* smuting DAC */
		smute = AK4620_SMUTE;
	}
	if (old != new) {
		struct snd_akm4xxx *ak = ice->akm;
		set_scr(ice, (get_scr(ice) & ~SCR_MUTE) | new);
		/* set smute */
		qtet_akm_set_regs(ak, AK4620_DEEMVOL_REG, AK4620_SMUTE, smute);
		return 1;
	}
	/* no change */
	return 0;
}

static int qtet_ain12_enum_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[3] =
		{"Line In 1/2", "Mic", "Mic + Low-cut"};
	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static int qtet_ain12_sw_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val, result;
	val = get_scr(ice) & (SCR_AIN12_SEL1 | SCR_AIN12_SEL0);
	switch (val) {
	case SCR_AIN12_LINE:
		result = 0;
		break;
	case SCR_AIN12_MIC:
		result = 1;
		break;
	case SCR_AIN12_LOWCUT:
		result = 2;
		break;
	default:
		/* BUG - no other combinations allowed */
		snd_BUG();
		result = 0;
	}
	ucontrol->value.integer.value[0] = result;
	return 0;
}

static int qtet_ain12_sw_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int old, new, tmp, masked_old;
	old = new = get_scr(ice);
	masked_old = old & (SCR_AIN12_SEL1 | SCR_AIN12_SEL0);
	tmp = ucontrol->value.integer.value[0];
	if (tmp == 2)
		tmp = 3;	/* binary 10 is not supported */
	tmp <<= 4;	/* shifting to SCR_AIN12_SEL0 */
	if (tmp != masked_old) {
		/* change requested */
		switch (tmp) {
		case SCR_AIN12_LINE:
			new = old & ~(SCR_AIN12_SEL1 | SCR_AIN12_SEL0);
			set_scr(ice, new);
			/* turn off relay */
			new &= ~SCR_RELAY;
			set_scr(ice, new);
			break;
		case SCR_AIN12_MIC:
			/* turn on relay */
			new = old | SCR_RELAY;
			set_scr(ice, new);
			new = (new & ~SCR_AIN12_SEL1) | SCR_AIN12_SEL0;
			set_scr(ice, new);
			break;
		case SCR_AIN12_LOWCUT:
			/* turn on relay */
			new = old | SCR_RELAY;
			set_scr(ice, new);
			new |= SCR_AIN12_SEL1 | SCR_AIN12_SEL0;
			set_scr(ice, new);
			break;
		default:
			snd_BUG();
		}
		return 1;
	}
	/* no change */
	return 0;
}

static int qtet_php_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	/* if phantom voltage =48V, phantom on */
	val = get_scr(ice) & SCR_PHP_V;
	ucontrol->value.integer.value[0] = val ? 1 : 0;
	return 0;
}

static int qtet_php_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int old, new;
	old = new = get_scr(ice);
	if (ucontrol->value.integer.value[0] /* phantom on requested */
			&& (~old & SCR_PHP_V)) /* 0 = voltage 5V */ {
		/* is off, turn on */
		/* turn voltage on first, = 1 */
		new = old | SCR_PHP_V;
		set_scr(ice, new);
		/* turn phantom on, = 0 */
		new &= ~SCR_PHP;
		set_scr(ice, new);
	} else if (!ucontrol->value.integer.value[0] && (old & SCR_PHP_V)) {
		/* phantom off requested and 1 = voltage 48V */
		/* is on, turn off */
		/* turn voltage off first, = 0 */
		new = old & ~SCR_PHP_V;
		set_scr(ice, new);
		/* turn phantom off, = 1 */
		new |= SCR_PHP;
		set_scr(ice, new);
	}
	if (old != new)
		return 1;
	/* no change */
	return 0;
}

#define PRIV_SW(xid, xbit, xreg)	[xid] = {.bit = xbit,\
	.set_register = set_##xreg,\
	.get_register = get_##xreg, }


#define PRIV_ENUM2(xid, xbit, xreg, xtext1, xtext2)	[xid] = {.bit = xbit,\
	.set_register = set_##xreg,\
	.get_register = get_##xreg,\
	.texts = {xtext1, xtext2} }

static struct qtet_kcontrol_private qtet_privates[] = {
	PRIV_ENUM2(IN12_SEL, CPLD_IN12_SEL, cpld, "An In 1/2", "An In 3/4"),
	PRIV_ENUM2(IN34_SEL, CPLD_IN34_SEL, cpld, "An In 3/4", "IEC958 In"),
	PRIV_ENUM2(AIN34_SEL, SCR_AIN34_SEL, scr, "Line In 3/4", "Hi-Z"),
	PRIV_ENUM2(COAX_OUT, CPLD_COAX_OUT, cpld, "IEC958", "I2S"),
	PRIV_SW(IN12_MON12, MCR_IN12_MON12, mcr),
	PRIV_SW(IN12_MON34, MCR_IN12_MON34, mcr),
	PRIV_SW(IN34_MON12, MCR_IN34_MON12, mcr),
	PRIV_SW(IN34_MON34, MCR_IN34_MON34, mcr),
	PRIV_SW(OUT12_MON34, MCR_OUT12_MON34, mcr),
	PRIV_SW(OUT34_MON12, MCR_OUT34_MON12, mcr),
};

static int qtet_enum_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	struct qtet_kcontrol_private private =
		qtet_privates[kcontrol->private_value];
	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(private.texts),
				 private.texts);
}

static int qtet_sw_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct qtet_kcontrol_private private =
		qtet_privates[kcontrol->private_value];
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] =
		(private.get_register(ice) & private.bit) ? 1 : 0;
	return 0;
}

static int qtet_sw_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct qtet_kcontrol_private private =
		qtet_privates[kcontrol->private_value];
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int old, new;
	old = private.get_register(ice);
	if (ucontrol->value.integer.value[0])
		new = old | private.bit;
	else
		new = old & ~private.bit;
	if (old != new) {
		private.set_register(ice, new);
		return 1;
	}
	/* no change */
	return 0;
}

#define qtet_sw_info	snd_ctl_boolean_mono_info

#define QTET_CONTROL(xname, xtype, xpriv)	\
	{.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname,\
	.info = qtet_##xtype##_info,\
	.get = qtet_sw_get,\
	.put = qtet_sw_put,\
	.private_value = xpriv }

static struct snd_kcontrol_new qtet_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = qtet_sw_info,
		.get = qtet_mute_get,
		.put = qtet_mute_put,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Phantom Power",
		.info = qtet_sw_info,
		.get = qtet_php_get,
		.put = qtet_php_put,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog In 1/2 Capture Switch",
		.info = qtet_ain12_enum_info,
		.get = qtet_ain12_sw_get,
		.put = qtet_ain12_sw_put,
		.private_value = 0
	},
	QTET_CONTROL("Analog In 3/4 Capture Switch", enum, AIN34_SEL),
	QTET_CONTROL("PCM In 1/2 Capture Switch", enum, IN12_SEL),
	QTET_CONTROL("PCM In 3/4 Capture Switch", enum, IN34_SEL),
	QTET_CONTROL("Coax Output Source", enum, COAX_OUT),
	QTET_CONTROL("Analog In 1/2 to Monitor 1/2", sw, IN12_MON12),
	QTET_CONTROL("Analog In 1/2 to Monitor 3/4", sw, IN12_MON34),
	QTET_CONTROL("Analog In 3/4 to Monitor 1/2", sw, IN34_MON12),
	QTET_CONTROL("Analog In 3/4 to Monitor 3/4", sw, IN34_MON34),
	QTET_CONTROL("Output 1/2 to Monitor 3/4", sw, OUT12_MON34),
	QTET_CONTROL("Output 3/4 to Monitor 1/2", sw, OUT34_MON12),
};

static char *slave_vols[] = {
	PCM_12_PLAYBACK_VOLUME,
	PCM_34_PLAYBACK_VOLUME,
	NULL
};

static
DECLARE_TLV_DB_SCALE(qtet_master_db_scale, -6350, 50, 1);

static struct snd_kcontrol *ctl_find(struct snd_card *card,
				     const char *name)
{
	struct snd_ctl_elem_id sid = {0};

	strlcpy(sid.name, name, sizeof(sid.name));
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_find_id(card, &sid);
}

static void add_slaves(struct snd_card *card,
		       struct snd_kcontrol *master, char * const *list)
{
	for (; *list; list++) {
		struct snd_kcontrol *slave = ctl_find(card, *list);
		if (slave)
			snd_ctl_add_slave(master, slave);
	}
}

static int qtet_add_controls(struct snd_ice1712 *ice)
{
	struct qtet_spec *spec = ice->spec;
	int err, i;
	struct snd_kcontrol *vmaster;
	err = snd_ice1712_akm4xxx_build_controls(ice);
	if (err < 0)
		return err;
	for (i = 0; i < ARRAY_SIZE(qtet_controls); i++) {
		err = snd_ctl_add(ice->card,
				snd_ctl_new1(&qtet_controls[i], ice));
		if (err < 0)
			return err;
	}

	/* Create virtual master control */
	vmaster = snd_ctl_make_virtual_master("Master Playback Volume",
			qtet_master_db_scale);
	if (!vmaster)
		return -ENOMEM;
	add_slaves(ice->card, vmaster, slave_vols);
	err = snd_ctl_add(ice->card, vmaster);
	if (err < 0)
		return err;
	/* only capture SPDIF over AK4113 */
	return snd_ak4113_build(spec->ak4113,
			ice->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream);
}

static inline int qtet_is_spdif_master(struct snd_ice1712 *ice)
{
	/* CPLD_SYNC_SEL: 0 = internal, 1 = external (i.e. spdif master) */
	return (get_cpld(ice) & CPLD_SYNC_SEL) ? 1 : 0;
}

static unsigned int qtet_get_rate(struct snd_ice1712 *ice)
{
	int i;
	unsigned char result;

	result =  get_cpld(ice) & CPLD_CKS_MASK;
	for (i = 0; i < ARRAY_SIZE(cks_vals); i++)
		if (cks_vals[i] == result)
			return qtet_rates[i];
	return 0;
}

static int get_cks_val(int rate)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(qtet_rates); i++)
		if (qtet_rates[i] == rate)
			return cks_vals[i];
	return 0;
}

/* setting new rate */
static void qtet_set_rate(struct snd_ice1712 *ice, unsigned int rate)
{
	unsigned int new;
	unsigned char val;
	/* switching ice1724 to external clock - supplied by ext. circuits */
	val = inb(ICEMT1724(ice, RATE));
	outb(val | VT1724_SPDIF_MASTER, ICEMT1724(ice, RATE));

	new =  (get_cpld(ice) & ~CPLD_CKS_MASK) | get_cks_val(rate);
	/* switch to internal clock, drop CPLD_SYNC_SEL */
	new &= ~CPLD_SYNC_SEL;
	/* dev_dbg(ice->card->dev, "QT - set_rate: old %x, new %x\n",
	   get_cpld(ice), new); */
	set_cpld(ice, new);
}

static inline unsigned char qtet_set_mclk(struct snd_ice1712 *ice,
		unsigned int rate)
{
	/* no change in master clock */
	return 0;
}

/* setting clock to external - SPDIF */
static int qtet_set_spdif_clock(struct snd_ice1712 *ice, int type)
{
	unsigned int old, new;

	old = new = get_cpld(ice);
	new &= ~(CPLD_CKS_MASK | CPLD_WORD_SEL);
	switch (type) {
	case EXT_SPDIF_TYPE:
		new |= CPLD_EXT_SPDIF;
		break;
	case EXT_WORDCLOCK_1FS_TYPE:
		new |= CPLD_EXT_WORDCLOCK_1FS;
		break;
	case EXT_WORDCLOCK_256FS_TYPE:
		new |= CPLD_EXT_WORDCLOCK_256FS;
		break;
	default:
		snd_BUG();
	}
	if (old != new) {
		set_cpld(ice, new);
		/* changed */
		return 1;
	}
	return 0;
}

static int qtet_get_spdif_master_type(struct snd_ice1712 *ice)
{
	unsigned int val;
	int result;
	val = get_cpld(ice);
	/* checking only rate/clock-related bits */
	val &= (CPLD_CKS_MASK | CPLD_WORD_SEL | CPLD_SYNC_SEL);
	if (!(val & CPLD_SYNC_SEL)) {
		/* switched to internal clock, is not any external type */
		result = -1;
	} else {
		switch (val) {
		case (CPLD_EXT_SPDIF):
			result = EXT_SPDIF_TYPE;
			break;
		case (CPLD_EXT_WORDCLOCK_1FS):
			result = EXT_WORDCLOCK_1FS_TYPE;
			break;
		case (CPLD_EXT_WORDCLOCK_256FS):
			result = EXT_WORDCLOCK_256FS_TYPE;
			break;
		default:
			/* undefined combination of external clock setup */
			snd_BUG();
			result = 0;
		}
	}
	return result;
}

/* Called when ak4113 detects change in the input SPDIF stream */
static void qtet_ak4113_change(struct ak4113 *ak4113, unsigned char c0,
		unsigned char c1)
{
	struct snd_ice1712 *ice = ak4113->change_callback_private;
	int rate;
	if ((qtet_get_spdif_master_type(ice) == EXT_SPDIF_TYPE) &&
			c1) {
		/* only for SPDIF master mode, rate was changed */
		rate = snd_ak4113_external_rate(ak4113);
		/* dev_dbg(ice->card->dev, "ak4113 - input rate changed to %d\n",
		   rate); */
		qtet_akm_set_rate_val(ice->akm, rate);
	}
}

/*
 * If clock slaved to SPDIF-IN, setting runtime rate
 * to the detected external rate
 */
static void qtet_spdif_in_open(struct snd_ice1712 *ice,
		struct snd_pcm_substream *substream)
{
	struct qtet_spec *spec = ice->spec;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int rate;

	if (qtet_get_spdif_master_type(ice) != EXT_SPDIF_TYPE)
		/* not external SPDIF, no rate limitation */
		return;
	/* only external SPDIF can detect incoming sample rate */
	rate = snd_ak4113_external_rate(spec->ak4113);
	if (rate >= runtime->hw.rate_min && rate <= runtime->hw.rate_max) {
		runtime->hw.rate_min = rate;
		runtime->hw.rate_max = rate;
	}
}

/*
 * initialize the chip
 */
static int qtet_init(struct snd_ice1712 *ice)
{
	static const unsigned char ak4113_init_vals[] = {
		/* AK4113_REG_PWRDN */	AK4113_RST | AK4113_PWN |
			AK4113_OCKS0 | AK4113_OCKS1,
		/* AK4113_REQ_FORMAT */	AK4113_DIF_I24I2S | AK4113_VTX |
			AK4113_DEM_OFF | AK4113_DEAU,
		/* AK4113_REG_IO0 */	AK4113_OPS2 | AK4113_TXE |
			AK4113_XTL_24_576M,
		/* AK4113_REG_IO1 */	AK4113_EFH_1024LRCLK | AK4113_IPS(0),
		/* AK4113_REG_INT0_MASK */	0,
		/* AK4113_REG_INT1_MASK */	0,
		/* AK4113_REG_DATDTS */		0,
	};
	int err;
	struct qtet_spec *spec;
	struct snd_akm4xxx *ak;
	unsigned char val;

	/* switching ice1724 to external clock - supplied by ext. circuits */
	val = inb(ICEMT1724(ice, RATE));
	outb(val | VT1724_SPDIF_MASTER, ICEMT1724(ice, RATE));

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	/* qtet is clocked by Xilinx array */
	ice->hw_rates = &qtet_rates_info;
	ice->is_spdif_master = qtet_is_spdif_master;
	ice->get_rate = qtet_get_rate;
	ice->set_rate = qtet_set_rate;
	ice->set_mclk = qtet_set_mclk;
	ice->set_spdif_clock = qtet_set_spdif_clock;
	ice->get_spdif_master_type = qtet_get_spdif_master_type;
	ice->ext_clock_names = ext_clock_names;
	ice->ext_clock_count = ARRAY_SIZE(ext_clock_names);
	/* since Qtet can detect correct SPDIF-in rate, all streams can be
	 * limited to this specific rate */
	ice->spdif.ops.open = ice->pro_open = qtet_spdif_in_open;
	ice->spec = spec;

	/* Mute Off */
	/* SCR Initialize*/
	/* keep codec power down first */
	set_scr(ice, SCR_PHP);
	udelay(1);
	/* codec power up */
	set_scr(ice, SCR_PHP | SCR_CODEC_PDN);

	/* MCR Initialize */
	set_mcr(ice, 0);

	/* CPLD Initialize */
	set_cpld(ice, 0);


	ice->num_total_dacs = 2;
	ice->num_total_adcs = 2;

	ice->akm = kcalloc(2, sizeof(struct snd_akm4xxx), GFP_KERNEL);
	ak = ice->akm;
	if (!ak)
		return -ENOMEM;
	/* only one codec with two chips */
	ice->akm_codecs = 1;
	err = snd_ice1712_akm4xxx_init(ak, &akm_qtet_dac, NULL, ice);
	if (err < 0)
		return err;
	err = snd_ak4113_create(ice->card,
			qtet_ak4113_read,
			qtet_ak4113_write,
			ak4113_init_vals,
			ice, &spec->ak4113);
	if (err < 0)
		return err;
	/* callback for codecs rate setting */
	spec->ak4113->change_callback = qtet_ak4113_change;
	spec->ak4113->change_callback_private = ice;
	/* AK41143 in Quartet can detect external rate correctly
	 * (i.e. check_flags = 0) */
	spec->ak4113->check_flags = 0;

	proc_init(ice);

	qtet_set_rate(ice, 44100);
	return 0;
}

static unsigned char qtet_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x28,	/* clock 256(24MHz), mpu401, 1xADC,
					   1xDACs, SPDIF in */
	[ICE_EEP2_ACLINK]      = 0x80,	/* I2S */
	[ICE_EEP2_I2S]         = 0x78,	/* 96k, 24bit, 192k */
	[ICE_EEP2_SPDIF]       = 0xc3,	/* out-en, out-int, in, out-ext */
	[ICE_EEP2_GPIO_DIR]    = 0x00,	/* 0-7 inputs, switched to output
					   only during output operations */
	[ICE_EEP2_GPIO_DIR1]   = 0xff,  /* 8-15 outputs */
	[ICE_EEP2_GPIO_DIR2]   = 0x00,
	[ICE_EEP2_GPIO_MASK]   = 0xff,	/* changed only for OUT operations */
	[ICE_EEP2_GPIO_MASK1]  = 0x00,
	[ICE_EEP2_GPIO_MASK2]  = 0xff,

	[ICE_EEP2_GPIO_STATE]  = 0x00, /* inputs */
	[ICE_EEP2_GPIO_STATE1] = 0x7d, /* all 1, but GPIO_CPLD_RW
					  and GPIO15 always zero */
	[ICE_EEP2_GPIO_STATE2] = 0x00, /* inputs */
};

/* entry point */
struct snd_ice1712_card_info snd_vt1724_qtet_cards[] = {
	{
		.subvendor = VT1724_SUBDEVICE_QTET,
		.name = "Infrasonic Quartet",
		.model = "quartet",
		.chip_init = qtet_init,
		.build_controls = qtet_add_controls,
		.eeprom_size = sizeof(qtet_eeprom),
		.eeprom_data = qtet_eeprom,
	},
	{ } /* terminator */
};
