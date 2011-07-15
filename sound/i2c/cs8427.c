/*
 *  Routines for control of the CS8427 via i2c bus
 *  IEC958 (S/PDIF) receiver & transmitter by Cirrus Logic
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/bitrev.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/cs8427.h>
#include <sound/asoundef.h>

static void snd_cs8427_reset(struct snd_i2c_device *cs8427);

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("IEC958 (S/PDIF) receiver & transmitter by Cirrus Logic");
MODULE_LICENSE("GPL");

#define CS8427_ADDR			(0x20>>1) /* fixed address */

struct cs8427_stream {
	struct snd_pcm_substream *substream;
	char hw_status[24];		/* hardware status */
	char def_status[24];		/* default status */
	char pcm_status[24];		/* PCM private status */
	char hw_udata[32];
	struct snd_kcontrol *pcm_ctl;
};

struct cs8427 {
	unsigned char regmap[0x14];	/* map of first 1 + 13 registers */
	unsigned int rate;
	unsigned int reset_timeout;
	struct cs8427_stream playback;
	struct cs8427_stream capture;
};

int snd_cs8427_reg_write(struct snd_i2c_device *device, unsigned char reg,
			 unsigned char val)
{
	int err;
	unsigned char buf[2];

	buf[0] = reg & 0x7f;
	buf[1] = val;
	if ((err = snd_i2c_sendbytes(device, buf, 2)) != 2) {
		snd_printk(KERN_ERR "unable to send bytes 0x%02x:0x%02x "
			   "to CS8427 (%i)\n", buf[0], buf[1], err);
		return err < 0 ? err : -EIO;
	}
	return 0;
}

EXPORT_SYMBOL(snd_cs8427_reg_write);

static int snd_cs8427_reg_read(struct snd_i2c_device *device, unsigned char reg)
{
	int err;
	unsigned char buf;

	if ((err = snd_i2c_sendbytes(device, &reg, 1)) != 1) {
		snd_printk(KERN_ERR "unable to send register 0x%x byte "
			   "to CS8427\n", reg);
		return err < 0 ? err : -EIO;
	}
	if ((err = snd_i2c_readbytes(device, &buf, 1)) != 1) {
		snd_printk(KERN_ERR "unable to read register 0x%x byte "
			   "from CS8427\n", reg);
		return err < 0 ? err : -EIO;
	}
	return buf;
}

static int snd_cs8427_select_corudata(struct snd_i2c_device *device, int udata)
{
	struct cs8427 *chip = device->private_data;
	int err;

	udata = udata ? CS8427_BSEL : 0;
	if (udata != (chip->regmap[CS8427_REG_CSDATABUF] & udata)) {
		chip->regmap[CS8427_REG_CSDATABUF] &= ~CS8427_BSEL;
		chip->regmap[CS8427_REG_CSDATABUF] |= udata;
		err = snd_cs8427_reg_write(device, CS8427_REG_CSDATABUF,
					   chip->regmap[CS8427_REG_CSDATABUF]);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_cs8427_send_corudata(struct snd_i2c_device *device,
				    int udata,
				    unsigned char *ndata,
				    int count)
{
	struct cs8427 *chip = device->private_data;
	char *hw_data = udata ?
		chip->playback.hw_udata : chip->playback.hw_status;
	char data[32];
	int err, idx;

	if (!memcmp(hw_data, ndata, count))
		return 0;
	if ((err = snd_cs8427_select_corudata(device, udata)) < 0)
		return err;
	memcpy(hw_data, ndata, count);
	if (udata) {
		memset(data, 0, sizeof(data));
		if (memcmp(hw_data, data, count) == 0) {
			chip->regmap[CS8427_REG_UDATABUF] &= ~CS8427_UBMMASK;
			chip->regmap[CS8427_REG_UDATABUF] |= CS8427_UBMZEROS |
				CS8427_EFTUI;
			err = snd_cs8427_reg_write(device, CS8427_REG_UDATABUF,
						   chip->regmap[CS8427_REG_UDATABUF]);
			return err < 0 ? err : 0;
		}
	}
	data[0] = CS8427_REG_AUTOINC | CS8427_REG_CORU_DATABUF;
	for (idx = 0; idx < count; idx++)
		data[idx + 1] = bitrev8(ndata[idx]);
	if (snd_i2c_sendbytes(device, data, count + 1) != count + 1)
		return -EIO;
	return 1;
}

static void snd_cs8427_free(struct snd_i2c_device *device)
{
	kfree(device->private_data);
}

int snd_cs8427_create(struct snd_i2c_bus *bus,
		      unsigned char addr,
		      unsigned int reset_timeout,
		      struct snd_i2c_device **r_cs8427)
{
	static unsigned char initvals1[] = {
	  CS8427_REG_CONTROL1 | CS8427_REG_AUTOINC,
	  /* CS8427_REG_CONTROL1: RMCK to OMCK, valid PCM audio, disable mutes,
	     TCBL=output */
	  CS8427_SWCLK | CS8427_TCBLDIR,
	  /* CS8427_REG_CONTROL2: hold last valid audio sample, RMCK=256*Fs,
	     normal stereo operation */
	  0x00,
	  /* CS8427_REG_DATAFLOW: output drivers normal operation, Tx<=serial,
	     Rx=>serial */
	  CS8427_TXDSERIAL | CS8427_SPDAES3RECEIVER,
	  /* CS8427_REG_CLOCKSOURCE: Run off, CMCK=256*Fs,
	     output time base = OMCK, input time base = recovered input clock,
	     recovered input clock source is ILRCK changed to AES3INPUT
	     (workaround, see snd_cs8427_reset) */
	  CS8427_RXDILRCK,
	  /* CS8427_REG_SERIALINPUT: Serial audio input port data format = I2S,
	     24-bit, 64*Fsi */
	  CS8427_SIDEL | CS8427_SILRPOL,
	  /* CS8427_REG_SERIALOUTPUT: Serial audio output port data format
	     = I2S, 24-bit, 64*Fsi */
	  CS8427_SODEL | CS8427_SOLRPOL,
	};
	static unsigned char initvals2[] = {
	  CS8427_REG_RECVERRMASK | CS8427_REG_AUTOINC,
	  /* CS8427_REG_RECVERRMASK: unmask the input PLL clock, V, confidence,
	     biphase, parity status bits */
	  /* CS8427_UNLOCK | CS8427_V | CS8427_CONF | CS8427_BIP | CS8427_PAR,*/
	  0xff, /* set everything */
	  /* CS8427_REG_CSDATABUF:
	     Registers 32-55 window to CS buffer
	     Inhibit D->E transfers from overwriting first 5 bytes of CS data.
	     Inhibit D->E transfers (all) of CS data.
	     Allow E->F transfer of CS data.
	     One byte mode; both A/B channels get same written CB data.
	     A channel info is output to chip's EMPH* pin. */
	  CS8427_CBMR | CS8427_DETCI,
	  /* CS8427_REG_UDATABUF:
	     Use internal buffer to transmit User (U) data.
	     Chip's U pin is an output.
	     Transmit all O's for user data.
	     Inhibit D->E transfers.
	     Inhibit E->F transfers. */
	  CS8427_UD | CS8427_EFTUI | CS8427_DETUI,
	};
	int err;
	struct cs8427 *chip;
	struct snd_i2c_device *device;
	unsigned char buf[24];

	if ((err = snd_i2c_device_create(bus, "CS8427",
					 CS8427_ADDR | (addr & 7),
					 &device)) < 0)
		return err;
	chip = device->private_data = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
	      	snd_i2c_device_free(device);
		return -ENOMEM;
	}
	device->private_free = snd_cs8427_free;
	
	snd_i2c_lock(bus);
	err = snd_cs8427_reg_read(device, CS8427_REG_ID_AND_VER);
	if (err != CS8427_VER8427A) {
		/* give second chance */
		snd_printk(KERN_WARNING "invalid CS8427 signature 0x%x: "
			   "let me try again...\n", err);
		err = snd_cs8427_reg_read(device, CS8427_REG_ID_AND_VER);
	}
	if (err != CS8427_VER8427A) {
		snd_i2c_unlock(bus);
		snd_printk(KERN_ERR "unable to find CS8427 signature "
			   "(expected 0x%x, read 0x%x),\n",
			   CS8427_VER8427A, err);
		snd_printk(KERN_ERR "   initialization is not completed\n");
		return -EFAULT;
	}
	/* turn off run bit while making changes to configuration */
	err = snd_cs8427_reg_write(device, CS8427_REG_CLOCKSOURCE, 0x00);
	if (err < 0)
		goto __fail;
	/* send initial values */
	memcpy(chip->regmap + (initvals1[0] & 0x7f), initvals1 + 1, 6);
	if ((err = snd_i2c_sendbytes(device, initvals1, 7)) != 7) {
		err = err < 0 ? err : -EIO;
		goto __fail;
	}
	/* Turn off CS8427 interrupt stuff that is not used in hardware */
	memset(buf, 0, 7);
	/* from address 9 to 15 */
	buf[0] = 9;	/* register */
	if ((err = snd_i2c_sendbytes(device, buf, 7)) != 7)
		goto __fail;
	/* send transfer initialization sequence */
	memcpy(chip->regmap + (initvals2[0] & 0x7f), initvals2 + 1, 3);
	if ((err = snd_i2c_sendbytes(device, initvals2, 4)) != 4) {
		err = err < 0 ? err : -EIO;
		goto __fail;
	}
	/* write default channel status bytes */
	put_unaligned_le32(SNDRV_PCM_DEFAULT_CON_SPDIF, buf);
	memset(buf + 4, 0, 24 - 4);
	if (snd_cs8427_send_corudata(device, 0, buf, 24) < 0)
		goto __fail;
	memcpy(chip->playback.def_status, buf, 24);
	memcpy(chip->playback.pcm_status, buf, 24);
	snd_i2c_unlock(bus);

	/* turn on run bit and rock'n'roll */
	if (reset_timeout < 1)
		reset_timeout = 1;
	chip->reset_timeout = reset_timeout;
	snd_cs8427_reset(device);

#if 0	// it's nice for read tests
	{
	char buf[128];
	int xx;
	buf[0] = 0x81;
	snd_i2c_sendbytes(device, buf, 1);
	snd_i2c_readbytes(device, buf, 127);
	for (xx = 0; xx < 127; xx++)
		printk(KERN_DEBUG "reg[0x%x] = 0x%x\n", xx+1, buf[xx]);
	}
#endif
	
	if (r_cs8427)
		*r_cs8427 = device;
	return 0;

      __fail:
      	snd_i2c_unlock(bus);
      	snd_i2c_device_free(device);
      	return err < 0 ? err : -EIO;
}

EXPORT_SYMBOL(snd_cs8427_create);

/*
 * Reset the chip using run bit, also lock PLL using ILRCK and
 * put back AES3INPUT. This workaround is described in latest
 * CS8427 datasheet, otherwise TXDSERIAL will not work.
 */
static void snd_cs8427_reset(struct snd_i2c_device *cs8427)
{
	struct cs8427 *chip;
	unsigned long end_time;
	int data, aes3input = 0;

	if (snd_BUG_ON(!cs8427))
		return;
	chip = cs8427->private_data;
	snd_i2c_lock(cs8427->bus);
	if ((chip->regmap[CS8427_REG_CLOCKSOURCE] & CS8427_RXDAES3INPUT) ==
	    CS8427_RXDAES3INPUT)  /* AES3 bit is set */
		aes3input = 1;
	chip->regmap[CS8427_REG_CLOCKSOURCE] &= ~(CS8427_RUN | CS8427_RXDMASK);
	snd_cs8427_reg_write(cs8427, CS8427_REG_CLOCKSOURCE,
			     chip->regmap[CS8427_REG_CLOCKSOURCE]);
	udelay(200);
	chip->regmap[CS8427_REG_CLOCKSOURCE] |= CS8427_RUN | CS8427_RXDILRCK;
	snd_cs8427_reg_write(cs8427, CS8427_REG_CLOCKSOURCE,
			     chip->regmap[CS8427_REG_CLOCKSOURCE]);
	udelay(200);
	snd_i2c_unlock(cs8427->bus);
	end_time = jiffies + chip->reset_timeout;
	while (time_after_eq(end_time, jiffies)) {
		snd_i2c_lock(cs8427->bus);
		data = snd_cs8427_reg_read(cs8427, CS8427_REG_RECVERRORS);
		snd_i2c_unlock(cs8427->bus);
		if (!(data & CS8427_UNLOCK))
			break;
		schedule_timeout_uninterruptible(1);
	}
	snd_i2c_lock(cs8427->bus);
	chip->regmap[CS8427_REG_CLOCKSOURCE] &= ~CS8427_RXDMASK;
	if (aes3input)
		chip->regmap[CS8427_REG_CLOCKSOURCE] |= CS8427_RXDAES3INPUT;
	snd_cs8427_reg_write(cs8427, CS8427_REG_CLOCKSOURCE,
			     chip->regmap[CS8427_REG_CLOCKSOURCE]);
	snd_i2c_unlock(cs8427->bus);
}

static int snd_cs8427_in_status_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_cs8427_in_status_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_i2c_device *device = snd_kcontrol_chip(kcontrol);
	int data;

	snd_i2c_lock(device->bus);
	data = snd_cs8427_reg_read(device, kcontrol->private_value);
	snd_i2c_unlock(device->bus);
	if (data < 0)
		return data;
	ucontrol->value.integer.value[0] = data;
	return 0;
}

static int snd_cs8427_qsubcode_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = 10;
	return 0;
}

static int snd_cs8427_qsubcode_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_i2c_device *device = snd_kcontrol_chip(kcontrol);
	unsigned char reg = CS8427_REG_QSUBCODE;
	int err;

	snd_i2c_lock(device->bus);
	if ((err = snd_i2c_sendbytes(device, &reg, 1)) != 1) {
		snd_printk(KERN_ERR "unable to send register 0x%x byte "
			   "to CS8427\n", reg);
		snd_i2c_unlock(device->bus);
		return err < 0 ? err : -EIO;
	}
	err = snd_i2c_readbytes(device, ucontrol->value.bytes.data, 10);
	if (err != 10) {
		snd_printk(KERN_ERR "unable to read Q-subcode bytes "
			   "from CS8427\n");
		snd_i2c_unlock(device->bus);
		return err < 0 ? err : -EIO;
	}
	snd_i2c_unlock(device->bus);
	return 0;
}

static int snd_cs8427_spdif_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_cs8427_spdif_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_i2c_device *device = snd_kcontrol_chip(kcontrol);
	struct cs8427 *chip = device->private_data;
	
	snd_i2c_lock(device->bus);
	memcpy(ucontrol->value.iec958.status, chip->playback.def_status, 24);
	snd_i2c_unlock(device->bus);
	return 0;
}

static int snd_cs8427_spdif_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_i2c_device *device = snd_kcontrol_chip(kcontrol);
	struct cs8427 *chip = device->private_data;
	unsigned char *status = kcontrol->private_value ?
		chip->playback.pcm_status : chip->playback.def_status;
	struct snd_pcm_runtime *runtime = chip->playback.substream ?
		chip->playback.substream->runtime : NULL;
	int err, change;

	snd_i2c_lock(device->bus);
	change = memcmp(ucontrol->value.iec958.status, status, 24) != 0;
	memcpy(status, ucontrol->value.iec958.status, 24);
	if (change && (kcontrol->private_value ?
		       runtime != NULL : runtime == NULL)) {
		err = snd_cs8427_send_corudata(device, 0, status, 24);
		if (err < 0)
			change = err;
	}
	snd_i2c_unlock(device->bus);
	return change;
}

static int snd_cs8427_spdif_mask_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_cs8427_spdif_mask_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	memset(ucontrol->value.iec958.status, 0xff, 24);
	return 0;
}

static struct snd_kcontrol_new snd_cs8427_iec958_controls[] = {
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.info =		snd_cs8427_in_status_info,
	.name =		"IEC958 CS8427 Input Status",
	.access =	(SNDRV_CTL_ELEM_ACCESS_READ |
			 SNDRV_CTL_ELEM_ACCESS_VOLATILE),
	.get =		snd_cs8427_in_status_get,
	.private_value = 15,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.info =		snd_cs8427_in_status_info,
	.name =		"IEC958 CS8427 Error Status",
	.access =	(SNDRV_CTL_ELEM_ACCESS_READ |
			 SNDRV_CTL_ELEM_ACCESS_VOLATILE),
	.get =		snd_cs8427_in_status_get,
	.private_value = 16,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.info =		snd_cs8427_spdif_mask_info,
	.get =		snd_cs8427_spdif_mask_get,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_cs8427_spdif_info,
	.get =		snd_cs8427_spdif_get,
	.put =		snd_cs8427_spdif_put,
	.private_value = 0
},
{
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_INACTIVE),
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_cs8427_spdif_info,
	.get =		snd_cs8427_spdif_get,
	.put =		snd_cs8427_spdif_put,
	.private_value = 1
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.info =		snd_cs8427_qsubcode_info,
	.name =		"IEC958 Q-subcode Capture Default",
	.access =	(SNDRV_CTL_ELEM_ACCESS_READ |
			 SNDRV_CTL_ELEM_ACCESS_VOLATILE),
	.get =		snd_cs8427_qsubcode_get
}};

int snd_cs8427_iec958_build(struct snd_i2c_device *cs8427,
			    struct snd_pcm_substream *play_substream,
			    struct snd_pcm_substream *cap_substream)
{
	struct cs8427 *chip = cs8427->private_data;
	struct snd_kcontrol *kctl;
	unsigned int idx;
	int err;

	if (snd_BUG_ON(!play_substream || !cap_substream))
		return -EINVAL;
	for (idx = 0; idx < ARRAY_SIZE(snd_cs8427_iec958_controls); idx++) {
		kctl = snd_ctl_new1(&snd_cs8427_iec958_controls[idx], cs8427);
		if (kctl == NULL)
			return -ENOMEM;
		kctl->id.device = play_substream->pcm->device;
		kctl->id.subdevice = play_substream->number;
		err = snd_ctl_add(cs8427->bus->card, kctl);
		if (err < 0)
			return err;
		if (! strcmp(kctl->id.name,
			     SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM)))
			chip->playback.pcm_ctl = kctl;
	}

	chip->playback.substream = play_substream;
	chip->capture.substream = cap_substream;
	if (snd_BUG_ON(!chip->playback.pcm_ctl))
		return -EIO;
	return 0;
}

EXPORT_SYMBOL(snd_cs8427_iec958_build);

int snd_cs8427_iec958_active(struct snd_i2c_device *cs8427, int active)
{
	struct cs8427 *chip;

	if (snd_BUG_ON(!cs8427))
		return -ENXIO;
	chip = cs8427->private_data;
	if (active)
		memcpy(chip->playback.pcm_status,
		       chip->playback.def_status, 24);
	chip->playback.pcm_ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(cs8427->bus->card,
		       SNDRV_CTL_EVENT_MASK_VALUE | SNDRV_CTL_EVENT_MASK_INFO,
		       &chip->playback.pcm_ctl->id);
	return 0;
}

EXPORT_SYMBOL(snd_cs8427_iec958_active);

int snd_cs8427_iec958_pcm(struct snd_i2c_device *cs8427, unsigned int rate)
{
	struct cs8427 *chip;
	char *status;
	int err, reset;

	if (snd_BUG_ON(!cs8427))
		return -ENXIO;
	chip = cs8427->private_data;
	status = chip->playback.pcm_status;
	snd_i2c_lock(cs8427->bus);
	if (status[0] & IEC958_AES0_PROFESSIONAL) {
		status[0] &= ~IEC958_AES0_PRO_FS;
		switch (rate) {
		case 32000: status[0] |= IEC958_AES0_PRO_FS_32000; break;
		case 44100: status[0] |= IEC958_AES0_PRO_FS_44100; break;
		case 48000: status[0] |= IEC958_AES0_PRO_FS_48000; break;
		default: status[0] |= IEC958_AES0_PRO_FS_NOTID; break;
		}
	} else {
		status[3] &= ~IEC958_AES3_CON_FS;
		switch (rate) {
		case 32000: status[3] |= IEC958_AES3_CON_FS_32000; break;
		case 44100: status[3] |= IEC958_AES3_CON_FS_44100; break;
		case 48000: status[3] |= IEC958_AES3_CON_FS_48000; break;
		}
	}
	err = snd_cs8427_send_corudata(cs8427, 0, status, 24);
	if (err > 0)
		snd_ctl_notify(cs8427->bus->card,
			       SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->playback.pcm_ctl->id);
	reset = chip->rate != rate;
	chip->rate = rate;
	snd_i2c_unlock(cs8427->bus);
	if (reset)
		snd_cs8427_reset(cs8427);
	return err < 0 ? err : 0;
}

EXPORT_SYMBOL(snd_cs8427_iec958_pcm);

static int __init alsa_cs8427_module_init(void)
{
	return 0;
}

static void __exit alsa_cs8427_module_exit(void)
{
}

module_init(alsa_cs8427_module_init)
module_exit(alsa_cs8427_module_exit)
