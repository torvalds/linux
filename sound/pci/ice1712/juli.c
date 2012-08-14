/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for ESI Juli@ cards
 *
 *	Copyright (c) 2004 Jaroslav Kysela <perex@perex.cz>
 *	              2008 Pavel Hofman <dustin@seznam.cz>
 *
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

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/tlv.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "juli.h"

struct juli_spec {
	struct ak4114 *ak4114;
	unsigned int analog:1;
};

/*
 * chip addresses on I2C bus
 */
#define AK4114_ADDR		0x20		/* S/PDIF receiver */
#define AK4358_ADDR		0x22		/* DAC */

/*
 * Juli does not use the standard ICE1724 clock scheme. Juli's ice1724 chip is
 * supplied by external clock provided by Xilinx array and MK73-1 PLL frequency
 * multiplier. Actual frequency is set by ice1724 GPIOs hooked to the Xilinx.
 *
 * The clock circuitry is supplied by the two ice1724 crystals. This
 * arrangement allows to generate independent clock signal for AK4114's input
 * rate detection circuit. As a result, Juli, unlike most other
 * ice1724+ak4114-based cards, detects spdif input rate correctly.
 * This fact is applied in the driver, allowing to modify PCM stream rate
 * parameter according to the actual input rate.
 *
 * Juli uses the remaining three stereo-channels of its DAC to optionally
 * monitor analog input, digital input, and digital output. The corresponding
 * I2S signals are routed by Xilinx, controlled by GPIOs.
 *
 * The master mute is implemented using output muting transistors (GPIO) in
 * combination with smuting the DAC.
 *
 * The card itself has no HW master volume control, implemented using the
 * vmaster control.
 *
 * TODO:
 * researching and fixing the input monitors
 */

/*
 * GPIO pins
 */
#define GPIO_FREQ_MASK		(3<<0)
#define GPIO_FREQ_32KHZ		(0<<0)
#define GPIO_FREQ_44KHZ		(1<<0)
#define GPIO_FREQ_48KHZ		(2<<0)
#define GPIO_MULTI_MASK		(3<<2)
#define GPIO_MULTI_4X		(0<<2)
#define GPIO_MULTI_2X		(1<<2)
#define GPIO_MULTI_1X		(2<<2)		/* also external */
#define GPIO_MULTI_HALF		(3<<2)
#define GPIO_INTERNAL_CLOCK	(1<<4)		/* 0 = external, 1 = internal */
#define GPIO_CLOCK_MASK		(1<<4)
#define GPIO_ANALOG_PRESENT	(1<<5)		/* RO only: 0 = present */
#define GPIO_RXMCLK_SEL		(1<<7)		/* must be 0 */
#define GPIO_AK5385A_CKS0	(1<<8)
#define GPIO_AK5385A_DFS1	(1<<9)
#define GPIO_AK5385A_DFS0	(1<<10)
#define GPIO_DIGOUT_MONITOR	(1<<11)		/* 1 = active */
#define GPIO_DIGIN_MONITOR	(1<<12)		/* 1 = active */
#define GPIO_ANAIN_MONITOR	(1<<13)		/* 1 = active */
#define GPIO_AK5385A_CKS1	(1<<14)		/* must be 0 */
#define GPIO_MUTE_CONTROL	(1<<15)		/* output mute, 1 = muted */

#define GPIO_RATE_MASK		(GPIO_FREQ_MASK | GPIO_MULTI_MASK | \
		GPIO_CLOCK_MASK)
#define GPIO_AK5385A_MASK	(GPIO_AK5385A_CKS0 | GPIO_AK5385A_DFS0 | \
		GPIO_AK5385A_DFS1 | GPIO_AK5385A_CKS1)

#define JULI_PCM_RATE	(SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
		SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000 | \
		SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | \
		SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

#define GPIO_RATE_16000		(GPIO_FREQ_32KHZ | GPIO_MULTI_HALF | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_22050		(GPIO_FREQ_44KHZ | GPIO_MULTI_HALF | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_24000		(GPIO_FREQ_48KHZ | GPIO_MULTI_HALF | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_32000		(GPIO_FREQ_32KHZ | GPIO_MULTI_1X | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_44100		(GPIO_FREQ_44KHZ | GPIO_MULTI_1X | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_48000		(GPIO_FREQ_48KHZ | GPIO_MULTI_1X | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_64000		(GPIO_FREQ_32KHZ | GPIO_MULTI_2X | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_88200		(GPIO_FREQ_44KHZ | GPIO_MULTI_2X | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_96000		(GPIO_FREQ_48KHZ | GPIO_MULTI_2X | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_176400	(GPIO_FREQ_44KHZ | GPIO_MULTI_4X | \
		GPIO_INTERNAL_CLOCK)
#define GPIO_RATE_192000	(GPIO_FREQ_48KHZ | GPIO_MULTI_4X | \
		GPIO_INTERNAL_CLOCK)

/*
 * Initial setup of the conversion array GPIO <-> rate
 */
static unsigned int juli_rates[] = {
	16000, 22050, 24000, 32000,
	44100, 48000, 64000, 88200,
	96000, 176400, 192000,
};

static unsigned int gpio_vals[] = {
	GPIO_RATE_16000, GPIO_RATE_22050, GPIO_RATE_24000, GPIO_RATE_32000,
	GPIO_RATE_44100, GPIO_RATE_48000, GPIO_RATE_64000, GPIO_RATE_88200,
	GPIO_RATE_96000, GPIO_RATE_176400, GPIO_RATE_192000,
};

static struct snd_pcm_hw_constraint_list juli_rates_info = {
	.count = ARRAY_SIZE(juli_rates),
	.list = juli_rates,
	.mask = 0,
};

static int get_gpio_val(int rate)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(juli_rates); i++)
		if (juli_rates[i] == rate)
			return gpio_vals[i];
	return 0;
}

static void juli_ak4114_write(void *private_data, unsigned char reg,
				unsigned char val)
{
	snd_vt1724_write_i2c((struct snd_ice1712 *)private_data, AK4114_ADDR,
				reg, val);
}

static unsigned char juli_ak4114_read(void *private_data, unsigned char reg)
{
	return snd_vt1724_read_i2c((struct snd_ice1712 *)private_data,
					AK4114_ADDR, reg);
}

/*
 * If SPDIF capture and slaved to SPDIF-IN, setting runtime rate
 * to the external rate
 */
static void juli_spdif_in_open(struct snd_ice1712 *ice,
				struct snd_pcm_substream *substream)
{
	struct juli_spec *spec = ice->spec;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int rate;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ||
			!ice->is_spdif_master(ice))
		return;
	rate = snd_ak4114_external_rate(spec->ak4114);
	if (rate >= runtime->hw.rate_min && rate <= runtime->hw.rate_max) {
		runtime->hw.rate_min = rate;
		runtime->hw.rate_max = rate;
	}
}

/*
 * AK4358 section
 */

static void juli_akm_lock(struct snd_akm4xxx *ak, int chip)
{
}

static void juli_akm_unlock(struct snd_akm4xxx *ak, int chip)
{
}

static void juli_akm_write(struct snd_akm4xxx *ak, int chip,
			   unsigned char addr, unsigned char data)
{
	struct snd_ice1712 *ice = ak->private_data[0];
	 
	if (snd_BUG_ON(chip))
		return;
	snd_vt1724_write_i2c(ice, AK4358_ADDR, addr, data);
}

/*
 * change the rate of envy24HT, AK4358, AK5385
 */
static void juli_akm_set_rate_val(struct snd_akm4xxx *ak, unsigned int rate)
{
	unsigned char old, tmp, ak4358_dfs;
	unsigned int ak5385_pins, old_gpio, new_gpio;
	struct snd_ice1712 *ice = ak->private_data[0];
	struct juli_spec *spec = ice->spec;

	if (rate == 0)  /* no hint - S/PDIF input is master or the new spdif
			   input rate undetected, simply return */
		return;

	/* adjust DFS on codecs */
	if (rate > 96000)  {
		ak4358_dfs = 2;
		ak5385_pins = GPIO_AK5385A_DFS1 | GPIO_AK5385A_CKS0;
	} else if (rate > 48000) {
		ak4358_dfs = 1;
		ak5385_pins = GPIO_AK5385A_DFS0;
	} else {
		ak4358_dfs = 0;
		ak5385_pins = 0;
	}
	/* AK5385 first, since it requires cold reset affecting both codecs */
	old_gpio = ice->gpio.get_data(ice);
	new_gpio =  (old_gpio & ~GPIO_AK5385A_MASK) | ak5385_pins;
	/* printk(KERN_DEBUG "JULI - ak5385 set_rate_val: new gpio 0x%x\n",
		new_gpio); */
	ice->gpio.set_data(ice, new_gpio);

	/* cold reset */
	old = inb(ICEMT1724(ice, AC97_CMD));
	outb(old | VT1724_AC97_COLD, ICEMT1724(ice, AC97_CMD));
	udelay(1);
	outb(old & ~VT1724_AC97_COLD, ICEMT1724(ice, AC97_CMD));

	/* AK4358 */
	/* set new value, reset DFS */
	tmp = snd_akm4xxx_get(ak, 0, 2);
	snd_akm4xxx_reset(ak, 1);
	tmp = snd_akm4xxx_get(ak, 0, 2);
	tmp &= ~(0x03 << 4);
	tmp |= ak4358_dfs << 4;
	snd_akm4xxx_set(ak, 0, 2, tmp);
	snd_akm4xxx_reset(ak, 0);

	/* reinit ak4114 */
	snd_ak4114_reinit(spec->ak4114);
}

#define AK_DAC(xname, xch)	{ .name = xname, .num_channels = xch }
#define PCM_VOLUME		"PCM Playback Volume"
#define MONITOR_AN_IN_VOLUME	"Monitor Analog In Volume"
#define MONITOR_DIG_IN_VOLUME	"Monitor Digital In Volume"
#define MONITOR_DIG_OUT_VOLUME	"Monitor Digital Out Volume"

static const struct snd_akm4xxx_dac_channel juli_dac[] = {
	AK_DAC(PCM_VOLUME, 2),
	AK_DAC(MONITOR_AN_IN_VOLUME, 2),
	AK_DAC(MONITOR_DIG_OUT_VOLUME, 2),
	AK_DAC(MONITOR_DIG_IN_VOLUME, 2),
};


static struct snd_akm4xxx akm_juli_dac __devinitdata = {
	.type = SND_AK4358,
	.num_dacs = 8,	/* DAC1 - analog out
			   DAC2 - analog in monitor
			   DAC3 - digital out monitor
			   DAC4 - digital in monitor
			 */
	.ops = {
		.lock = juli_akm_lock,
		.unlock = juli_akm_unlock,
		.write = juli_akm_write,
		.set_rate_val = juli_akm_set_rate_val
	},
	.dac_info = juli_dac,
};

#define juli_mute_info		snd_ctl_boolean_mono_info

static int juli_mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	val = ice->gpio.get_data(ice) & (unsigned int) kcontrol->private_value;
	if (kcontrol->private_value == GPIO_MUTE_CONTROL)
		/* val 0 = signal on */
		ucontrol->value.integer.value[0] = (val) ? 0 : 1;
	else
		/* val 1 = signal on */
		ucontrol->value.integer.value[0] = (val) ? 1 : 0;
	return 0;
}

static int juli_mute_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int old_gpio, new_gpio;
	old_gpio = ice->gpio.get_data(ice);
	if (ucontrol->value.integer.value[0]) {
		/* unmute */
		if (kcontrol->private_value == GPIO_MUTE_CONTROL) {
			/* 0 = signal on */
			new_gpio = old_gpio & ~GPIO_MUTE_CONTROL;
			/* un-smuting DAC */
			snd_akm4xxx_write(ice->akm, 0, 0x01, 0x01);
		} else
			/* 1 = signal on */
			new_gpio =  old_gpio |
				(unsigned int) kcontrol->private_value;
	} else {
		/* mute */
		if (kcontrol->private_value == GPIO_MUTE_CONTROL) {
			/* 1 = signal off */
			new_gpio = old_gpio | GPIO_MUTE_CONTROL;
			/* smuting DAC */
			snd_akm4xxx_write(ice->akm, 0, 0x01, 0x03);
		} else
			/* 0 = signal off */
			new_gpio =  old_gpio &
				~((unsigned int) kcontrol->private_value);
	}
	/* printk(KERN_DEBUG
		"JULI - mute/unmute: control_value: 0x%x, old_gpio: 0x%x, "
		"new_gpio 0x%x\n",
		(unsigned int)ucontrol->value.integer.value[0], old_gpio,
		new_gpio); */
	if (old_gpio != new_gpio) {
		ice->gpio.set_data(ice, new_gpio);
		return 1;
	}
	/* no change */
	return 0;
}

static struct snd_kcontrol_new juli_mute_controls[] __devinitdata = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = juli_mute_info,
		.get = juli_mute_get,
		.put = juli_mute_put,
		.private_value = GPIO_MUTE_CONTROL,
	},
	/* Although the following functionality respects the succint NDA'd
	 * documentation from the card manufacturer, and the same way of
	 * operation is coded in OSS Juli driver, only Digital Out monitor
	 * seems to work. Surprisingly, Analog input monitor outputs Digital
	 * output data. The two are independent, as enabling both doubles
	 * volume of the monitor sound.
	 *
	 * Checking traces on the board suggests the functionality described
	 * by the manufacturer is correct - I2S from ADC and AK4114
	 * go to ICE as well as to Xilinx, I2S inputs of DAC2,3,4 (the monitor
	 * inputs) are fed from Xilinx.
	 *
	 * I even checked traces on board and coded a support in driver for
	 * an alternative possibility - the unused I2S ICE output channels
	 * switched to HW-IN/SPDIF-IN and providing the monitoring signal to
	 * the DAC - to no avail. The I2S outputs seem to be unconnected.
	 *
	 * The windows driver supports the monitoring correctly.
	 */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Monitor Analog In Switch",
		.info = juli_mute_info,
		.get = juli_mute_get,
		.put = juli_mute_put,
		.private_value = GPIO_ANAIN_MONITOR,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Monitor Digital Out Switch",
		.info = juli_mute_info,
		.get = juli_mute_get,
		.put = juli_mute_put,
		.private_value = GPIO_DIGOUT_MONITOR,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Monitor Digital In Switch",
		.info = juli_mute_info,
		.get = juli_mute_get,
		.put = juli_mute_put,
		.private_value = GPIO_DIGIN_MONITOR,
	},
};

static char *slave_vols[] __devinitdata = {
	PCM_VOLUME,
	MONITOR_AN_IN_VOLUME,
	MONITOR_DIG_IN_VOLUME,
	MONITOR_DIG_OUT_VOLUME,
	NULL
};

static __devinitdata
DECLARE_TLV_DB_SCALE(juli_master_db_scale, -6350, 50, 1);

static struct snd_kcontrol __devinit *ctl_find(struct snd_card *card,
		const char *name)
{
	struct snd_ctl_elem_id sid;
	memset(&sid, 0, sizeof(sid));
	/* FIXME: strcpy is bad. */
	strcpy(sid.name, name);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_find_id(card, &sid);
}

static void __devinit add_slaves(struct snd_card *card,
				 struct snd_kcontrol *master, char **list)
{
	for (; *list; list++) {
		struct snd_kcontrol *slave = ctl_find(card, *list);
		/* printk(KERN_DEBUG "add_slaves - %s\n", *list); */
		if (slave) {
			/* printk(KERN_DEBUG "slave %s found\n", *list); */
			snd_ctl_add_slave(master, slave);
		}
	}
}

static int __devinit juli_add_controls(struct snd_ice1712 *ice)
{
	struct juli_spec *spec = ice->spec;
	int err;
	unsigned int i;
	struct snd_kcontrol *vmaster;

	err = snd_ice1712_akm4xxx_build_controls(ice);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(juli_mute_controls); i++) {
		err = snd_ctl_add(ice->card,
				snd_ctl_new1(&juli_mute_controls[i], ice));
		if (err < 0)
			return err;
	}
	/* Create virtual master control */
	vmaster = snd_ctl_make_virtual_master("Master Playback Volume",
					      juli_master_db_scale);
	if (!vmaster)
		return -ENOMEM;
	add_slaves(ice->card, vmaster, slave_vols);
	err = snd_ctl_add(ice->card, vmaster);
	if (err < 0)
		return err;

	/* only capture SPDIF over AK4114 */
	err = snd_ak4114_build(spec->ak4114, NULL,
			ice->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream);
	if (err < 0)
		return err;
	return 0;
}

/*
 * suspend/resume
 * */

#ifdef CONFIG_PM_SLEEP
static int juli_resume(struct snd_ice1712 *ice)
{
	struct snd_akm4xxx *ak = ice->akm;
	struct juli_spec *spec = ice->spec;
	/* akm4358 un-reset, un-mute */
	snd_akm4xxx_reset(ak, 0);
	/* reinit ak4114 */
	snd_ak4114_reinit(spec->ak4114);
	return 0;
}

static int juli_suspend(struct snd_ice1712 *ice)
{
	struct snd_akm4xxx *ak = ice->akm;
	/* akm4358 reset and soft-mute */
	snd_akm4xxx_reset(ak, 1);
	return 0;
}
#endif

/*
 * initialize the chip
 */

static inline int juli_is_spdif_master(struct snd_ice1712 *ice)
{
	return (ice->gpio.get_data(ice) & GPIO_INTERNAL_CLOCK) ? 0 : 1;
}

static unsigned int juli_get_rate(struct snd_ice1712 *ice)
{
	int i;
	unsigned char result;

	result =  ice->gpio.get_data(ice) & GPIO_RATE_MASK;
	for (i = 0; i < ARRAY_SIZE(gpio_vals); i++)
		if (gpio_vals[i] == result)
			return juli_rates[i];
	return 0;
}

/* setting new rate */
static void juli_set_rate(struct snd_ice1712 *ice, unsigned int rate)
{
	unsigned int old, new;
	unsigned char val;

	old = ice->gpio.get_data(ice);
	new =  (old & ~GPIO_RATE_MASK) | get_gpio_val(rate);
	/* printk(KERN_DEBUG "JULI - set_rate: old %x, new %x\n",
			old & GPIO_RATE_MASK,
			new & GPIO_RATE_MASK); */

	ice->gpio.set_data(ice, new);
	/* switching to external clock - supplied by external circuits */
	val = inb(ICEMT1724(ice, RATE));
	outb(val | VT1724_SPDIF_MASTER, ICEMT1724(ice, RATE));
}

static inline unsigned char juli_set_mclk(struct snd_ice1712 *ice,
					  unsigned int rate)
{
	/* no change in master clock */
	return 0;
}

/* setting clock to external - SPDIF */
static int juli_set_spdif_clock(struct snd_ice1712 *ice, int type)
{
	unsigned int old;
	old = ice->gpio.get_data(ice);
	/* external clock (= 0), multiply 1x, 48kHz */
	ice->gpio.set_data(ice, (old & ~GPIO_RATE_MASK) | GPIO_MULTI_1X |
			GPIO_FREQ_48KHZ);
	return 0;
}

/* Called when ak4114 detects change in the input SPDIF stream */
static void juli_ak4114_change(struct ak4114 *ak4114, unsigned char c0,
			       unsigned char c1)
{
	struct snd_ice1712 *ice = ak4114->change_callback_private;
	int rate;
	if (ice->is_spdif_master(ice) && c1) {
		/* only for SPDIF master mode, rate was changed */
		rate = snd_ak4114_external_rate(ak4114);
		/* printk(KERN_DEBUG "ak4114 - input rate changed to %d\n",
				rate); */
		juli_akm_set_rate_val(ice->akm, rate);
	}
}

static int __devinit juli_init(struct snd_ice1712 *ice)
{
	static const unsigned char ak4114_init_vals[] = {
		/* AK4117_REG_PWRDN */	AK4114_RST | AK4114_PWN |
					AK4114_OCKS0 | AK4114_OCKS1,
		/* AK4114_REQ_FORMAT */	AK4114_DIF_I24I2S,
		/* AK4114_REG_IO0 */	AK4114_TX1E,
		/* AK4114_REG_IO1 */	AK4114_EFH_1024 | AK4114_DIT |
					AK4114_IPS(1),
		/* AK4114_REG_INT0_MASK */ 0,
		/* AK4114_REG_INT1_MASK */ 0
	};
	static const unsigned char ak4114_init_txcsb[] = {
		0x41, 0x02, 0x2c, 0x00, 0x00
	};
	int err;
	struct juli_spec *spec;
	struct snd_akm4xxx *ak;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;

	err = snd_ak4114_create(ice->card,
				juli_ak4114_read,
				juli_ak4114_write,
				ak4114_init_vals, ak4114_init_txcsb,
				ice, &spec->ak4114);
	if (err < 0)
		return err;
	/* callback for codecs rate setting */
	spec->ak4114->change_callback = juli_ak4114_change;
	spec->ak4114->change_callback_private = ice;
	/* AK4114 in Juli can detect external rate correctly */
	spec->ak4114->check_flags = 0;

#if 0
/*
 * it seems that the analog doughter board detection does not work reliably, so
 * force the analog flag; it should be very rare (if ever) to come at Juli@
 * used without the analog daughter board
 */
	spec->analog = (ice->gpio.get_data(ice) & GPIO_ANALOG_PRESENT) ? 0 : 1;
#else
	spec->analog = 1;
#endif

	if (spec->analog) {
		printk(KERN_INFO "juli@: analog I/O detected\n");
		ice->num_total_dacs = 2;
		ice->num_total_adcs = 2;

		ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
		ak = ice->akm;
		if (!ak)
			return -ENOMEM;
		ice->akm_codecs = 1;
		err = snd_ice1712_akm4xxx_init(ak, &akm_juli_dac, NULL, ice);
		if (err < 0)
			return err;
	}

	/* juli is clocked by Xilinx array */
	ice->hw_rates = &juli_rates_info;
	ice->is_spdif_master = juli_is_spdif_master;
	ice->get_rate = juli_get_rate;
	ice->set_rate = juli_set_rate;
	ice->set_mclk = juli_set_mclk;
	ice->set_spdif_clock = juli_set_spdif_clock;

	ice->spdif.ops.open = juli_spdif_in_open;

#ifdef CONFIG_PM_SLEEP
	ice->pm_resume = juli_resume;
	ice->pm_suspend = juli_suspend;
	ice->pm_suspend_enabled = 1;
#endif

	return 0;
}


/*
 * Juli@ boards don't provide the EEPROM data except for the vendor IDs.
 * hence the driver needs to sets up it properly.
 */

static unsigned char juli_eeprom[] __devinitdata = {
	[ICE_EEP2_SYSCONF]     = 0x2b,	/* clock 512, mpu401, 1xADC, 1xDACs,
					   SPDIF in */
	[ICE_EEP2_ACLINK]      = 0x80,	/* I2S */
	[ICE_EEP2_I2S]         = 0xf8,	/* vol, 96k, 24bit, 192k */
	[ICE_EEP2_SPDIF]       = 0xc3,	/* out-en, out-int, spdif-in */
	[ICE_EEP2_GPIO_DIR]    = 0x9f,	/* 5, 6:inputs; 7, 4-0 outputs*/
	[ICE_EEP2_GPIO_DIR1]   = 0xff,
	[ICE_EEP2_GPIO_DIR2]   = 0x7f,
	[ICE_EEP2_GPIO_MASK]   = 0x60,	/* 5, 6: locked; 7, 4-0 writable */
	[ICE_EEP2_GPIO_MASK1]  = 0x00,  /* 0-7 writable */
	[ICE_EEP2_GPIO_MASK2]  = 0x7f,
	[ICE_EEP2_GPIO_STATE]  = GPIO_FREQ_48KHZ | GPIO_MULTI_1X |
	       GPIO_INTERNAL_CLOCK,	/* internal clock, multiple 1x, 48kHz*/
	[ICE_EEP2_GPIO_STATE1] = 0x00,	/* unmuted */
	[ICE_EEP2_GPIO_STATE2] = 0x00,
};

/* entry point */
struct snd_ice1712_card_info snd_vt1724_juli_cards[] __devinitdata = {
	{
		.subvendor = VT1724_SUBDEVICE_JULI,
		.name = "ESI Juli@",
		.model = "juli",
		.chip_init = juli_init,
		.build_controls = juli_add_controls,
		.eeprom_size = sizeof(juli_eeprom),
		.eeprom_data = juli_eeprom,
	},
	{ } /* terminator */
};
