/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for M-Audio Delta 1010, 1010E, 44, 66, 66E, Dio2496,
 *			    Audiophile, Digigram VX442
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
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
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/cs8427.h>
#include <sound/asoundef.h>

#include "ice1712.h"
#include "delta.h"

#define SND_CS8403
#include <sound/cs8403.h>


/*
 * CS8427 via SPI mode (for Audiophile), emulated I2C
 */

/* send 8 bits */
static void ap_cs8427_write_byte(struct snd_ice1712 *ice, unsigned char data, unsigned char tmp)
{
	int idx;

	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~(ICE1712_DELTA_AP_DOUT|ICE1712_DELTA_AP_CCLK);
		if (data & (1 << idx))
			tmp |= ICE1712_DELTA_AP_DOUT;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
		tmp |= ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
	}
}

/* read 8 bits */
static unsigned char ap_cs8427_read_byte(struct snd_ice1712 *ice, unsigned char tmp)
{
	unsigned char data = 0;
	int idx;
	
	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
		if (snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & ICE1712_DELTA_AP_DIN)
			data |= 1 << idx;
		tmp |= ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
	}
	return data;
}

/* assert chip select */
static unsigned char ap_cs8427_codec_select(struct snd_ice1712 *ice)
{
	unsigned char tmp;
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010E:
	case ICE1712_SUBDEVICE_DELTA1010LT:
		tmp &= ~ICE1712_DELTA_1010LT_CS;
		tmp |= ICE1712_DELTA_1010LT_CCLK | ICE1712_DELTA_1010LT_CS_CS8427;
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_DELTA410:
		tmp |= ICE1712_DELTA_AP_CCLK | ICE1712_DELTA_AP_CS_CODEC;
		tmp &= ~ICE1712_DELTA_AP_CS_DIGITAL;
		break;
	case ICE1712_SUBDEVICE_VX442:
		tmp |= ICE1712_VX442_CCLK | ICE1712_VX442_CODEC_CHIP_A | ICE1712_VX442_CODEC_CHIP_B;
		tmp &= ~ICE1712_VX442_CS_DIGITAL;
		break;
	}
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	udelay(5);
	return tmp;
}

/* deassert chip select */
static void ap_cs8427_codec_deassert(struct snd_ice1712 *ice, unsigned char tmp)
{
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010E:
	case ICE1712_SUBDEVICE_DELTA1010LT:
		tmp &= ~ICE1712_DELTA_1010LT_CS;
		tmp |= ICE1712_DELTA_1010LT_CS_NONE;
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_DELTA410:
		tmp |= ICE1712_DELTA_AP_CS_DIGITAL;
		break;
	case ICE1712_SUBDEVICE_VX442:
		tmp |= ICE1712_VX442_CS_DIGITAL;
		break;
	}
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
}

/* sequential write */
static int ap_cs8427_sendbytes(struct snd_i2c_device *device, unsigned char *bytes, int count)
{
	struct snd_ice1712 *ice = device->bus->private_data;
	int res = count;
	unsigned char tmp;

	mutex_lock(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, (device->addr << 1) | 0, tmp); /* address + write mode */
	while (count-- > 0)
		ap_cs8427_write_byte(ice, *bytes++, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	mutex_unlock(&ice->gpio_mutex);
	return res;
}

/* sequential read */
static int ap_cs8427_readbytes(struct snd_i2c_device *device, unsigned char *bytes, int count)
{
	struct snd_ice1712 *ice = device->bus->private_data;
	int res = count;
	unsigned char tmp;
	
	mutex_lock(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, (device->addr << 1) | 1, tmp); /* address + read mode */
	while (count-- > 0)
		*bytes++ = ap_cs8427_read_byte(ice, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	mutex_unlock(&ice->gpio_mutex);
	return res;
}

static int ap_cs8427_probeaddr(struct snd_i2c_bus *bus, unsigned short addr)
{
	if (addr == 0x10)
		return 1;
	return -ENOENT;
}

static struct snd_i2c_ops ap_cs8427_i2c_ops = {
	.sendbytes = ap_cs8427_sendbytes,
	.readbytes = ap_cs8427_readbytes,
	.probeaddr = ap_cs8427_probeaddr,
};

/*
 */

static void snd_ice1712_delta_cs8403_spdif_write(struct snd_ice1712 *ice, unsigned char bits)
{
	unsigned char tmp, mask1, mask2;
	int idx;
	/* send byte to transmitter */
	mask1 = ICE1712_DELTA_SPDIF_OUT_STAT_CLOCK;
	mask2 = ICE1712_DELTA_SPDIF_OUT_STAT_DATA;
	mutex_lock(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~(mask1 | mask2);
		if (bits & (1 << idx))
			tmp |= mask2;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(100);
		tmp |= mask1;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(100);
	}
	tmp &= ~mask1;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	mutex_unlock(&ice->gpio_mutex);
}


static void delta_spdif_default_get(struct snd_ice1712 *ice, struct snd_ctl_elem_value *ucontrol)
{
	snd_cs8403_decode_spdif_bits(&ucontrol->value.iec958, ice->spdif.cs8403_bits);
}

static int delta_spdif_default_put(struct snd_ice1712 *ice, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val;
	int change;

	val = snd_cs8403_encode_spdif_bits(&ucontrol->value.iec958);
	spin_lock_irq(&ice->reg_lock);
	change = ice->spdif.cs8403_bits != val;
	ice->spdif.cs8403_bits = val;
	if (change && ice->playback_pro_substream == NULL) {
		spin_unlock_irq(&ice->reg_lock);
		snd_ice1712_delta_cs8403_spdif_write(ice, val);
	} else {
		spin_unlock_irq(&ice->reg_lock);
	}
	return change;
}

static void delta_spdif_stream_get(struct snd_ice1712 *ice, struct snd_ctl_elem_value *ucontrol)
{
	snd_cs8403_decode_spdif_bits(&ucontrol->value.iec958, ice->spdif.cs8403_stream_bits);
}

static int delta_spdif_stream_put(struct snd_ice1712 *ice, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val;
	int change;

	val = snd_cs8403_encode_spdif_bits(&ucontrol->value.iec958);
	spin_lock_irq(&ice->reg_lock);
	change = ice->spdif.cs8403_stream_bits != val;
	ice->spdif.cs8403_stream_bits = val;
	if (change && ice->playback_pro_substream != NULL) {
		spin_unlock_irq(&ice->reg_lock);
		snd_ice1712_delta_cs8403_spdif_write(ice, val);
	} else {
		spin_unlock_irq(&ice->reg_lock);
	}
	return change;
}


/*
 * AK4524 on Delta 44 and 66 to choose the chip mask
 */
static void delta_ak4524_lock(struct snd_akm4xxx *ak, int chip)
{
        struct snd_ak4xxx_private *priv = (void *)ak->private_value[0];
        struct snd_ice1712 *ice = ak->private_data[0];

	snd_ice1712_save_gpio_status(ice);
	priv->cs_mask =
	priv->cs_addr = chip == 0 ? ICE1712_DELTA_CODEC_CHIP_A :
				    ICE1712_DELTA_CODEC_CHIP_B;
}

/*
 * AK4524 on Delta1010LT to choose the chip address
 */
static void delta1010lt_ak4524_lock(struct snd_akm4xxx *ak, int chip)
{
        struct snd_ak4xxx_private *priv = (void *)ak->private_value[0];
        struct snd_ice1712 *ice = ak->private_data[0];

	snd_ice1712_save_gpio_status(ice);
	priv->cs_mask = ICE1712_DELTA_1010LT_CS;
	priv->cs_addr = chip << 4;
}

/*
 * AK4528 on VX442 to choose the chip mask
 */
static void vx442_ak4524_lock(struct snd_akm4xxx *ak, int chip)
{
        struct snd_ak4xxx_private *priv = (void *)ak->private_value[0];
        struct snd_ice1712 *ice = ak->private_data[0];

	snd_ice1712_save_gpio_status(ice);
	priv->cs_mask =
	priv->cs_addr = chip == 0 ? ICE1712_VX442_CODEC_CHIP_A :
				    ICE1712_VX442_CODEC_CHIP_B;
}

/*
 * change the DFS bit according rate for Delta1010
 */
static void delta_1010_set_rate_val(struct snd_ice1712 *ice, unsigned int rate)
{
	unsigned char tmp, tmp2;

	if (rate == 0)	/* no hint - S/PDIF input is master, simply return */
		return;

	mutex_lock(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	tmp2 = tmp & ~ICE1712_DELTA_DFS;
	if (rate > 48000)
		tmp2 |= ICE1712_DELTA_DFS;
	if (tmp != tmp2)
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp2);
	mutex_unlock(&ice->gpio_mutex);
}

/*
 * change the rate of AK4524 on Delta 44/66, AP, 1010LT
 */
static void delta_ak4524_set_rate_val(struct snd_akm4xxx *ak, unsigned int rate)
{
	unsigned char tmp, tmp2;
	struct snd_ice1712 *ice = ak->private_data[0];

	if (rate == 0)	/* no hint - S/PDIF input is master, simply return */
		return;

	/* check before reset ak4524 to avoid unnecessary clicks */
	mutex_lock(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	mutex_unlock(&ice->gpio_mutex);
	tmp2 = tmp & ~ICE1712_DELTA_DFS; 
	if (rate > 48000)
		tmp2 |= ICE1712_DELTA_DFS;
	if (tmp == tmp2)
		return;

	/* do it again */
	snd_akm4xxx_reset(ak, 1);
	mutex_lock(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & ~ICE1712_DELTA_DFS;
	if (rate > 48000)
		tmp |= ICE1712_DELTA_DFS;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	mutex_unlock(&ice->gpio_mutex);
	snd_akm4xxx_reset(ak, 0);
}

/*
 * change the rate of AK4524 on VX442
 */
static void vx442_ak4524_set_rate_val(struct snd_akm4xxx *ak, unsigned int rate)
{
	unsigned char val;

	val = (rate > 48000) ? 0x65 : 0x60;
	if (snd_akm4xxx_get(ak, 0, 0x02) != val ||
	    snd_akm4xxx_get(ak, 1, 0x02) != val) {
		snd_akm4xxx_reset(ak, 1);
		snd_akm4xxx_write(ak, 0, 0x02, val);
		snd_akm4xxx_write(ak, 1, 0x02, val);
		snd_akm4xxx_reset(ak, 0);
	}
}


/*
 * SPDIF ops for Delta 1010, Dio, 66
 */

/* open callback */
static void delta_open_spdif(struct snd_ice1712 *ice, struct snd_pcm_substream *substream)
{
	ice->spdif.cs8403_stream_bits = ice->spdif.cs8403_bits;
}

/* set up */
static void delta_setup_spdif(struct snd_ice1712 *ice, int rate)
{
	unsigned long flags;
	unsigned int tmp;
	int change;

	spin_lock_irqsave(&ice->reg_lock, flags);
	tmp = ice->spdif.cs8403_stream_bits;
	if (tmp & 0x01)		/* consumer */
		tmp &= (tmp & 0x01) ? ~0x06 : ~0x18;
	switch (rate) {
	case 32000: tmp |= (tmp & 0x01) ? 0x04 : 0x00; break;
	case 44100: tmp |= (tmp & 0x01) ? 0x00 : 0x10; break;
	case 48000: tmp |= (tmp & 0x01) ? 0x02 : 0x08; break;
	default: tmp |= (tmp & 0x01) ? 0x00 : 0x18; break;
	}
	change = ice->spdif.cs8403_stream_bits != tmp;
	ice->spdif.cs8403_stream_bits = tmp;
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	if (change)
		snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE, &ice->spdif.stream_ctl->id);
	snd_ice1712_delta_cs8403_spdif_write(ice, tmp);
}

#define snd_ice1712_delta1010lt_wordclock_status_info \
	snd_ctl_boolean_mono_info

static int snd_ice1712_delta1010lt_wordclock_status_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	char reg = 0x10; /* CS8427 receiver error register */
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	if (snd_i2c_sendbytes(ice->cs8427, &reg, 1) != 1)
		snd_printk(KERN_ERR "unable to send register 0x%x byte to CS8427\n", reg);
	snd_i2c_readbytes(ice->cs8427, &reg, 1);
	ucontrol->value.integer.value[0] = (reg & CS8427_UNLOCK) ? 1 : 0;
	return 0;
}

static struct snd_kcontrol_new snd_ice1712_delta1010lt_wordclock_status __devinitdata =
{
	.access =	(SNDRV_CTL_ELEM_ACCESS_READ),
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Word Clock Status",
	.info =		snd_ice1712_delta1010lt_wordclock_status_info,
	.get =		snd_ice1712_delta1010lt_wordclock_status_get,
};

/*
 * initialize the chips on M-Audio cards
 */

static struct snd_akm4xxx akm_audiophile __devinitdata = {
	.type = SND_AK4528,
	.num_adcs = 2,
	.num_dacs = 2,
	.ops = {
		.set_rate_val = delta_ak4524_set_rate_val
	}
};

static struct snd_ak4xxx_private akm_audiophile_priv __devinitdata = {
	.caddr = 2,
	.cif = 0,
	.data_mask = ICE1712_DELTA_AP_DOUT,
	.clk_mask = ICE1712_DELTA_AP_CCLK,
	.cs_mask = ICE1712_DELTA_AP_CS_CODEC,
	.cs_addr = ICE1712_DELTA_AP_CS_CODEC,
	.cs_none = 0,
	.add_flags = ICE1712_DELTA_AP_CS_DIGITAL,
	.mask_flags = 0,
};

static struct snd_akm4xxx akm_delta410 __devinitdata = {
	.type = SND_AK4529,
	.num_adcs = 2,
	.num_dacs = 8,
	.ops = {
		.set_rate_val = delta_ak4524_set_rate_val
	}
};

static struct snd_ak4xxx_private akm_delta410_priv __devinitdata = {
	.caddr = 0,
	.cif = 0,
	.data_mask = ICE1712_DELTA_AP_DOUT,
	.clk_mask = ICE1712_DELTA_AP_CCLK,
	.cs_mask = ICE1712_DELTA_AP_CS_CODEC,
	.cs_addr = ICE1712_DELTA_AP_CS_CODEC,
	.cs_none = 0,
	.add_flags = ICE1712_DELTA_AP_CS_DIGITAL,
	.mask_flags = 0,
};

static struct snd_akm4xxx akm_delta1010lt __devinitdata = {
	.type = SND_AK4524,
	.num_adcs = 8,
	.num_dacs = 8,
	.ops = {
		.lock = delta1010lt_ak4524_lock,
		.set_rate_val = delta_ak4524_set_rate_val
	}
};

static struct snd_ak4xxx_private akm_delta1010lt_priv __devinitdata = {
	.caddr = 2,
	.cif = 0, /* the default level of the CIF pin from AK4524 */
	.data_mask = ICE1712_DELTA_1010LT_DOUT,
	.clk_mask = ICE1712_DELTA_1010LT_CCLK,
	.cs_mask = 0,
	.cs_addr = 0, /* set later */
	.cs_none = ICE1712_DELTA_1010LT_CS_NONE,
	.add_flags = 0,
	.mask_flags = 0,
};

static struct snd_akm4xxx akm_delta44 __devinitdata = {
	.type = SND_AK4524,
	.num_adcs = 4,
	.num_dacs = 4,
	.ops = {
		.lock = delta_ak4524_lock,
		.set_rate_val = delta_ak4524_set_rate_val
	}
};

static struct snd_ak4xxx_private akm_delta44_priv __devinitdata = {
	.caddr = 2,
	.cif = 0, /* the default level of the CIF pin from AK4524 */
	.data_mask = ICE1712_DELTA_CODEC_SERIAL_DATA,
	.clk_mask = ICE1712_DELTA_CODEC_SERIAL_CLOCK,
	.cs_mask = 0,
	.cs_addr = 0, /* set later */
	.cs_none = 0,
	.add_flags = 0,
	.mask_flags = 0,
};

static struct snd_akm4xxx akm_vx442 __devinitdata = {
	.type = SND_AK4524,
	.num_adcs = 4,
	.num_dacs = 4,
	.ops = {
		.lock = vx442_ak4524_lock,
		.set_rate_val = vx442_ak4524_set_rate_val
	}
};

static struct snd_ak4xxx_private akm_vx442_priv __devinitdata = {
	.caddr = 2,
	.cif = 0,
	.data_mask = ICE1712_VX442_DOUT,
	.clk_mask = ICE1712_VX442_CCLK,
	.cs_mask = 0,
	.cs_addr = 0, /* set later */
	.cs_none = 0,
	.add_flags = 0,
	.mask_flags = 0,
};

static int __devinit snd_ice1712_delta_init(struct snd_ice1712 *ice)
{
	int err;
	struct snd_akm4xxx *ak;

	if (ice->eeprom.subvendor == ICE1712_SUBDEVICE_DELTA1010 &&
	    ice->eeprom.gpiodir == 0x7b)
		ice->eeprom.subvendor = ICE1712_SUBDEVICE_DELTA1010E;

	if (ice->eeprom.subvendor == ICE1712_SUBDEVICE_DELTA66 &&
	    ice->eeprom.gpiodir == 0xfb)
	    	ice->eeprom.subvendor = ICE1712_SUBDEVICE_DELTA66E;

	/* determine I2C, DACs and ADCs */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		ice->num_total_dacs = 2;
		ice->num_total_adcs = 2;
		break;
	case ICE1712_SUBDEVICE_DELTA410:
		ice->num_total_dacs = 8;
		ice->num_total_adcs = 2;
		break;
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_DELTA66:
		ice->num_total_dacs = ice->omni ? 8 : 4;
		ice->num_total_adcs = ice->omni ? 8 : 4;
		break;
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTA1010E:
	case ICE1712_SUBDEVICE_DELTA1010LT:
	case ICE1712_SUBDEVICE_MEDIASTATION:
	case ICE1712_SUBDEVICE_EDIROLDA2496:
		ice->num_total_dacs = 8;
		ice->num_total_adcs = 8;
		break;
	case ICE1712_SUBDEVICE_DELTADIO2496:
		ice->num_total_dacs = 4;	/* two AK4324 codecs */
		break;
	case ICE1712_SUBDEVICE_VX442:
	case ICE1712_SUBDEVICE_DELTA66E:	/* omni not suported yet */
		ice->num_total_dacs = 4;
		ice->num_total_adcs = 4;
		break;
	}

	/* initialize spdif */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_DELTA410:
	case ICE1712_SUBDEVICE_DELTA1010E:
	case ICE1712_SUBDEVICE_DELTA1010LT:
	case ICE1712_SUBDEVICE_VX442:
	case ICE1712_SUBDEVICE_DELTA66E:
		if ((err = snd_i2c_bus_create(ice->card, "ICE1712 GPIO 1", NULL, &ice->i2c)) < 0) {
			snd_printk(KERN_ERR "unable to create I2C bus\n");
			return err;
		}
		ice->i2c->private_data = ice;
		ice->i2c->ops = &ap_cs8427_i2c_ops;
		if ((err = snd_ice1712_init_cs8427(ice, CS8427_BASE_ADDR)) < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_MEDIASTATION:
		ice->gpio.set_pro_rate = delta_1010_set_rate_val;
		break;
	case ICE1712_SUBDEVICE_DELTADIO2496:
		ice->gpio.set_pro_rate = delta_1010_set_rate_val;
		/* fall thru */
	case ICE1712_SUBDEVICE_DELTA66:
		ice->spdif.ops.open = delta_open_spdif;
		ice->spdif.ops.setup_rate = delta_setup_spdif;
		ice->spdif.ops.default_get = delta_spdif_default_get;
		ice->spdif.ops.default_put = delta_spdif_default_put;
		ice->spdif.ops.stream_get = delta_spdif_stream_get;
		ice->spdif.ops.stream_put = delta_spdif_stream_put;
		/* Set spdif defaults */
		snd_ice1712_delta_cs8403_spdif_write(ice, ice->spdif.cs8403_bits);
		break;
	}

	/* no analog? */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTA1010E:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_MEDIASTATION:
		return 0;
	}

	/* second stage of initialization, analog parts and others */
	ak = ice->akm = kmalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	if (! ak)
		return -ENOMEM;
	ice->akm_codecs = 1;

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		err = snd_ice1712_akm4xxx_init(ak, &akm_audiophile, &akm_audiophile_priv, ice);
		break;
	case ICE1712_SUBDEVICE_DELTA410:
		err = snd_ice1712_akm4xxx_init(ak, &akm_delta410, &akm_delta410_priv, ice);
		break;
	case ICE1712_SUBDEVICE_DELTA1010LT:
	case ICE1712_SUBDEVICE_EDIROLDA2496:
		err = snd_ice1712_akm4xxx_init(ak, &akm_delta1010lt, &akm_delta1010lt_priv, ice);
		break;
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_DELTA44:
		err = snd_ice1712_akm4xxx_init(ak, &akm_delta44, &akm_delta44_priv, ice);
		break;
	case ICE1712_SUBDEVICE_VX442:
	case ICE1712_SUBDEVICE_DELTA66E:
		err = snd_ice1712_akm4xxx_init(ak, &akm_vx442, &akm_vx442_priv, ice);
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}

	return err;
}


/*
 * additional controls for M-Audio cards
 */

static struct snd_kcontrol_new snd_ice1712_delta1010_wordclock_select __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_MIXER, "Word Clock Sync", 0, ICE1712_DELTA_WORD_CLOCK_SELECT, 1, 0);
static struct snd_kcontrol_new snd_ice1712_delta1010lt_wordclock_select __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_MIXER, "Word Clock Sync", 0, ICE1712_DELTA_1010LT_WORDCLOCK, 0, 0);
static struct snd_kcontrol_new snd_ice1712_delta1010_wordclock_status __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_MIXER, "Word Clock Status", 0, ICE1712_DELTA_WORD_CLOCK_STATUS, 1, SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE);
static struct snd_kcontrol_new snd_ice1712_deltadio2496_spdif_in_select __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_MIXER, "IEC958 Input Optical", 0, ICE1712_DELTA_SPDIF_INPUT_SELECT, 0, 0);
static struct snd_kcontrol_new snd_ice1712_delta_spdif_in_status __devinitdata =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_MIXER, "Delta IEC958 Input Status", 0, ICE1712_DELTA_SPDIF_IN_STAT, 1, SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE);


static int __devinit snd_ice1712_delta_add_controls(struct snd_ice1712 *ice)
{
	int err;

	/* 1010 and dio specific controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_MEDIASTATION:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010_wordclock_select, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010_wordclock_status, ice));
		if (err < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_DELTADIO2496:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_deltadio2496_spdif_in_select, ice));
		if (err < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_DELTA1010E:
	case ICE1712_SUBDEVICE_DELTA1010LT:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010lt_wordclock_select, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010lt_wordclock_status, ice));
		if (err < 0)
			return err;
		break;
	}

	/* normal spdif controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_MEDIASTATION:
		err = snd_ice1712_spdif_build_controls(ice);
		if (err < 0)
			return err;
		break;
	}

	/* spdif status in */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_MEDIASTATION:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta_spdif_in_status, ice));
		if (err < 0)
			return err;
		break;
	}

	/* ak4524 controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010LT:
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_DELTA410:
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_VX442:
	case ICE1712_SUBDEVICE_DELTA66E:
	case ICE1712_SUBDEVICE_EDIROLDA2496:
		err = snd_ice1712_akm4xxx_build_controls(ice);
		if (err < 0)
			return err;
		break;
	}

	return 0;
}


/* entry point */
struct snd_ice1712_card_info snd_ice1712_delta_cards[] __devinitdata = {
	{
		.subvendor = ICE1712_SUBDEVICE_DELTA1010,
		.name = "M Audio Delta 1010",
		.model = "delta1010",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_DELTADIO2496,
		.name = "M Audio Delta DiO 2496",
		.model = "dio2496",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
		.no_mpu401 = 1,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_DELTA66,
		.name = "M Audio Delta 66",
		.model = "delta66",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
		.no_mpu401 = 1,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_DELTA44,
		.name = "M Audio Delta 44",
		.model = "delta44",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
		.no_mpu401 = 1,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_AUDIOPHILE,
		.name = "M Audio Audiophile 24/96",
		.model = "audiophile",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_DELTA410,
		.name = "M Audio Delta 410",
		.model = "delta410",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_DELTA1010LT,
		.name = "M Audio Delta 1010LT",
		.model = "delta1010lt",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_VX442,
		.name = "Digigram VX442",
		.model = "vx442",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
		.no_mpu401 = 1,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_MEDIASTATION,
		.name = "Lionstracs Mediastation",
		.model = "mediastation",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_EDIROLDA2496,
		.name = "Edirol DA2496",
		.model = "da2496",
		.chip_init = snd_ice1712_delta_init,
		.build_controls = snd_ice1712_delta_add_controls,
	},
	{ } /* terminator */
};
