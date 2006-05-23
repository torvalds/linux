/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
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

/*
  NOTES:
  - spdif nonaudio consumer mode does not work (at least with my
    Sony STR-DB830)
*/

/*
 * Changes:
 *
 *  2002.09.09	Takashi Iwai <tiwai@suse.de>
 *	split the code to several files.  each low-level routine
 *	is stored in the local file and called from registration
 *	function from card_info struct.
 *
 *  2002.11.26	James Stafford <jstafford@ampltd.com>
 *	Added support for VT1724 (Envy24HT)
 *	I have left out support for 176.4 and 192 KHz for the moment. 
 *  I also haven't done anything with the internal S/PDIF transmitter or the MPU-401
 *
 *  2003.02.20  Taksahi Iwai <tiwai@suse.de>
 *	Split vt1724 part to an independent driver.
 *	The GPIO is accessed through the callback functions now.
 *
 * 2004.03.31 Doug McLain <nostar@comcast.net>
 *    Added support for Event Electronics EZ8 card to hoontech.c.
 */


#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/cs8427.h>
#include <sound/info.h>
#include <sound/mpu401.h>
#include <sound/initval.h>

#include <sound/asoundef.h>

#include "ice1712.h"

/* lowlevel routines */
#include "delta.h"
#include "ews.h"
#include "hoontech.h"

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("ICEnsemble ICE1712 (Envy24)");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{"
	       HOONTECH_DEVICE_DESC
	       DELTA_DEVICE_DESC
	       EWS_DEVICE_DESC
	       "{ICEnsemble,Generic ICE1712},"
	       "{ICEnsemble,Generic Envy24}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */
static char *model[SNDRV_CARDS];
static int omni[SNDRV_CARDS];				/* Delta44 & 66 Omni I/O support */
static int cs8427_timeout[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 500}; /* CS8427 S/PDIF transciever reset timeout value in msec */
static int dxr_enable[SNDRV_CARDS];			/* DXR enable for DMX6FIRE */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ICE1712 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ICE1712 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ICE1712 soundcard.");
module_param_array(omni, bool, NULL, 0444);
MODULE_PARM_DESC(omni, "Enable Midiman M-Audio Delta Omni I/O support.");
module_param_array(cs8427_timeout, int, NULL, 0444);
MODULE_PARM_DESC(cs8427_timeout, "Define reset timeout for cs8427 chip in msec resolution.");
module_param_array(model, charp, NULL, 0444);
MODULE_PARM_DESC(model, "Use the given board model.");
module_param_array(dxr_enable, int, NULL, 0444);
MODULE_PARM_DESC(dxr_enable, "Enable DXR support for Terratec DMX6FIRE.");


static struct pci_device_id snd_ice1712_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_ICE, PCI_DEVICE_ID_ICE_1712, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },   /* ICE1712 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_ice1712_ids);

static int snd_ice1712_build_pro_mixer(struct snd_ice1712 *ice);
static int snd_ice1712_build_controls(struct snd_ice1712 *ice);

static int PRO_RATE_LOCKED;
static int PRO_RATE_RESET = 1;
static unsigned int PRO_RATE_DEFAULT = 44100;

/*
 *  Basic I/O
 */
 
/* check whether the clock mode is spdif-in */
static inline int is_spdif_master(struct snd_ice1712 *ice)
{
	return (inb(ICEMT(ice, RATE)) & ICE1712_SPDIF_MASTER) ? 1 : 0;
}

static inline int is_pro_rate_locked(struct snd_ice1712 *ice)
{
	return is_spdif_master(ice) || PRO_RATE_LOCKED;
}

static inline void snd_ice1712_ds_write(struct snd_ice1712 * ice, u8 channel, u8 addr, u32 data)
{
	outb((channel << 4) | addr, ICEDS(ice, INDEX));
	outl(data, ICEDS(ice, DATA));
}

static inline u32 snd_ice1712_ds_read(struct snd_ice1712 * ice, u8 channel, u8 addr)
{
	outb((channel << 4) | addr, ICEDS(ice, INDEX));
	return inl(ICEDS(ice, DATA));
}

static void snd_ice1712_ac97_write(struct snd_ac97 *ac97,
				   unsigned short reg,
				   unsigned short val)
{
	struct snd_ice1712 *ice = ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEREG(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEREG(ice, AC97_INDEX));
	outw(val, ICEREG(ice, AC97_DATA));
	old_cmd &= ~(ICE1712_AC97_PBK_VSR | ICE1712_AC97_CAP_VSR);
	outb(old_cmd | ICE1712_AC97_WRITE, ICEREG(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEREG(ice, AC97_CMD)) & ICE1712_AC97_WRITE) == 0)
			break;
}

static unsigned short snd_ice1712_ac97_read(struct snd_ac97 *ac97,
					    unsigned short reg)
{
	struct snd_ice1712 *ice = ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEREG(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEREG(ice, AC97_INDEX));
	outb(old_cmd | ICE1712_AC97_READ, ICEREG(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEREG(ice, AC97_CMD)) & ICE1712_AC97_READ) == 0)
			break;
	if (tm >= 0x10000)		/* timeout */
		return ~0;
	return inw(ICEREG(ice, AC97_DATA));
}

/*
 * pro ac97 section
 */

static void snd_ice1712_pro_ac97_write(struct snd_ac97 *ac97,
				       unsigned short reg,
				       unsigned short val)
{
	struct snd_ice1712 *ice = ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEMT(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEMT(ice, AC97_INDEX));
	outw(val, ICEMT(ice, AC97_DATA));
	old_cmd &= ~(ICE1712_AC97_PBK_VSR | ICE1712_AC97_CAP_VSR);
	outb(old_cmd | ICE1712_AC97_WRITE, ICEMT(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEMT(ice, AC97_CMD)) & ICE1712_AC97_WRITE) == 0)
			break;
}


static unsigned short snd_ice1712_pro_ac97_read(struct snd_ac97 *ac97,
						unsigned short reg)
{
	struct snd_ice1712 *ice = ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEMT(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEMT(ice, AC97_INDEX));
	outb(old_cmd | ICE1712_AC97_READ, ICEMT(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEMT(ice, AC97_CMD)) & ICE1712_AC97_READ) == 0)
			break;
	if (tm >= 0x10000)		/* timeout */
		return ~0;
	return inw(ICEMT(ice, AC97_DATA));
}

/*
 * consumer ac97 digital mix
 */
static int snd_ice1712_digmix_route_ac97_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_digmix_route_ac97_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = inb(ICEMT(ice, MONITOR_ROUTECTRL)) & ICE1712_ROUTE_AC97 ? 1 : 0;
	return 0;
}

static int snd_ice1712_digmix_route_ac97_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val, nval;
	
	spin_lock_irq(&ice->reg_lock);
	val = inb(ICEMT(ice, MONITOR_ROUTECTRL));
	nval = val & ~ICE1712_ROUTE_AC97;
	if (ucontrol->value.integer.value[0]) nval |= ICE1712_ROUTE_AC97;
	outb(nval, ICEMT(ice, MONITOR_ROUTECTRL));
	spin_unlock_irq(&ice->reg_lock);
	return val != nval;
}

static struct snd_kcontrol_new snd_ice1712_mixer_digmix_route_ac97 __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Mixer To AC97",
	.info = snd_ice1712_digmix_route_ac97_info,
	.get = snd_ice1712_digmix_route_ac97_get,
	.put = snd_ice1712_digmix_route_ac97_put,
};


/*
 * gpio operations
 */
static void snd_ice1712_set_gpio_dir(struct snd_ice1712 *ice, unsigned int data)
{
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, data);
	inb(ICEREG(ice, DATA)); /* dummy read for pci-posting */
}

static void snd_ice1712_set_gpio_mask(struct snd_ice1712 *ice, unsigned int data)
{
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, data);
	inb(ICEREG(ice, DATA)); /* dummy read for pci-posting */
}

static unsigned int snd_ice1712_get_gpio_data(struct snd_ice1712 *ice)
{
	return snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
}

static void snd_ice1712_set_gpio_data(struct snd_ice1712 *ice, unsigned int val)
{
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, val);
	inb(ICEREG(ice, DATA)); /* dummy read for pci-posting */
}

/*
 *
 * CS8427 interface
 *
 */

/*
 * change the input clock selection
 * spdif_clock = 1 - IEC958 input, 0 - Envy24
 */
static int snd_ice1712_cs8427_set_input_clock(struct snd_ice1712 *ice, int spdif_clock)
{
	unsigned char reg[2] = { 0x80 | 4, 0 };   /* CS8427 auto increment | register number 4 + data */
	unsigned char val, nval;
	int res = 0;
	
	snd_i2c_lock(ice->i2c);
	if (snd_i2c_sendbytes(ice->cs8427, reg, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	if (snd_i2c_readbytes(ice->cs8427, &val, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	nval = val & 0xf0;
	if (spdif_clock)
		nval |= 0x01;
	else
		nval |= 0x04;
	if (val != nval) {
		reg[1] = nval;
		if (snd_i2c_sendbytes(ice->cs8427, reg, 2) != 2) {
			res = -EIO;
		} else {
			res++;
		}
	}
	snd_i2c_unlock(ice->i2c);
	return res;
}

/*
 * spdif callbacks
 */
static void open_cs8427(struct snd_ice1712 *ice, struct snd_pcm_substream *substream)
{
	snd_cs8427_iec958_active(ice->cs8427, 1);
}

static void close_cs8427(struct snd_ice1712 *ice, struct snd_pcm_substream *substream)
{
	snd_cs8427_iec958_active(ice->cs8427, 0);
}

static void setup_cs8427(struct snd_ice1712 *ice, int rate)
{
	snd_cs8427_iec958_pcm(ice->cs8427, rate);
}

/*
 * create and initialize callbacks for cs8427 interface
 */
int __devinit snd_ice1712_init_cs8427(struct snd_ice1712 *ice, int addr)
{
	int err;

	if ((err = snd_cs8427_create(ice->i2c, addr,
				     (ice->cs8427_timeout * HZ) / 1000,
				     &ice->cs8427)) < 0) {
		snd_printk(KERN_ERR "CS8427 initialization failed\n");
		return err;
	}
	ice->spdif.ops.open = open_cs8427;
	ice->spdif.ops.close = close_cs8427;
	ice->spdif.ops.setup_rate = setup_cs8427;
	return 0;
}

static void snd_ice1712_set_input_clock_source(struct snd_ice1712 *ice, int spdif_is_master)
{
        /* change CS8427 clock source too */
        if (ice->cs8427)
                snd_ice1712_cs8427_set_input_clock(ice, spdif_is_master);
	/* notify ak4524 chip as well */
	if (spdif_is_master) {
		unsigned int i;
		for (i = 0; i < ice->akm_codecs; i++) {
			if (ice->akm[i].ops.set_rate_val)
				ice->akm[i].ops.set_rate_val(&ice->akm[i], 0);
		}
	}
}

/*
 *  Interrupt handler
 */

static irqreturn_t snd_ice1712_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct snd_ice1712 *ice = dev_id;
	unsigned char status;
	int handled = 0;

	while (1) {
		status = inb(ICEREG(ice, IRQSTAT));
		if (status == 0)
			break;
		handled = 1;
		if (status & ICE1712_IRQ_MPU1) {
			if (ice->rmidi[0])
				snd_mpu401_uart_interrupt(irq, ice->rmidi[0]->private_data, regs);
			outb(ICE1712_IRQ_MPU1, ICEREG(ice, IRQSTAT));
			status &= ~ICE1712_IRQ_MPU1;
		}
		if (status & ICE1712_IRQ_TIMER)
			outb(ICE1712_IRQ_TIMER, ICEREG(ice, IRQSTAT));
		if (status & ICE1712_IRQ_MPU2) {
			if (ice->rmidi[1])
				snd_mpu401_uart_interrupt(irq, ice->rmidi[1]->private_data, regs);
			outb(ICE1712_IRQ_MPU2, ICEREG(ice, IRQSTAT));
			status &= ~ICE1712_IRQ_MPU2;
		}
		if (status & ICE1712_IRQ_PROPCM) {
			unsigned char mtstat = inb(ICEMT(ice, IRQ));
			if (mtstat & ICE1712_MULTI_PBKSTATUS) {
				if (ice->playback_pro_substream)
					snd_pcm_period_elapsed(ice->playback_pro_substream);
				outb(ICE1712_MULTI_PBKSTATUS, ICEMT(ice, IRQ));
			}
			if (mtstat & ICE1712_MULTI_CAPSTATUS) {
				if (ice->capture_pro_substream)
					snd_pcm_period_elapsed(ice->capture_pro_substream);
				outb(ICE1712_MULTI_CAPSTATUS, ICEMT(ice, IRQ));
			}
		}
		if (status & ICE1712_IRQ_FM)
			outb(ICE1712_IRQ_FM, ICEREG(ice, IRQSTAT));
		if (status & ICE1712_IRQ_PBKDS) {
			u32 idx;
			u16 pbkstatus;
			struct snd_pcm_substream *substream;
			pbkstatus = inw(ICEDS(ice, INTSTAT));
			//printk("pbkstatus = 0x%x\n", pbkstatus);
			for (idx = 0; idx < 6; idx++) {
				if ((pbkstatus & (3 << (idx * 2))) == 0)
					continue;
				if ((substream = ice->playback_con_substream_ds[idx]) != NULL)
					snd_pcm_period_elapsed(substream);
				outw(3 << (idx * 2), ICEDS(ice, INTSTAT));
			}
			outb(ICE1712_IRQ_PBKDS, ICEREG(ice, IRQSTAT));
		}
		if (status & ICE1712_IRQ_CONCAP) {
			if (ice->capture_con_substream)
				snd_pcm_period_elapsed(ice->capture_con_substream);
			outb(ICE1712_IRQ_CONCAP, ICEREG(ice, IRQSTAT));
		}
		if (status & ICE1712_IRQ_CONPBK) {
			if (ice->playback_con_substream)
				snd_pcm_period_elapsed(ice->playback_con_substream);
			outb(ICE1712_IRQ_CONPBK, ICEREG(ice, IRQSTAT));
		}
	}
	return IRQ_RETVAL(handled);
}


/*
 *  PCM part - misc
 */

static int snd_ice1712_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_ice1712_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/*
 *  PCM part - consumer I/O
 */

static int snd_ice1712_playback_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	int result = 0;
	u32 tmp;
	
	spin_lock(&ice->reg_lock);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_PBK_CTRL);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		tmp |= 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		tmp &= ~1;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH) {
		tmp |= 2;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_RELEASE) {
		tmp &= ~2;
	} else {
		result = -EINVAL;
	}
	snd_ice1712_write(ice, ICE1712_IREG_PBK_CTRL, tmp);
	spin_unlock(&ice->reg_lock);
	return result;
}

static int snd_ice1712_playback_ds_trigger(struct snd_pcm_substream *substream,
					   int cmd)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	int result = 0;
	u32 tmp;
	
	spin_lock(&ice->reg_lock);
	tmp = snd_ice1712_ds_read(ice, substream->number * 2, ICE1712_DSC_CONTROL);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		tmp |= 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		tmp &= ~1;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH) {
		tmp |= 2;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_RELEASE) {
		tmp &= ~2;
	} else {
		result = -EINVAL;
	}
	snd_ice1712_ds_write(ice, substream->number * 2, ICE1712_DSC_CONTROL, tmp);
	spin_unlock(&ice->reg_lock);
	return result;
}

static int snd_ice1712_capture_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	int result = 0;
	u8 tmp;
	
	spin_lock(&ice->reg_lock);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_CAP_CTRL);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		tmp |= 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		tmp &= ~1;
	} else {
		result = -EINVAL;
	}
	snd_ice1712_write(ice, ICE1712_IREG_CAP_CTRL, tmp);
	spin_unlock(&ice->reg_lock);
	return result;
}

static int snd_ice1712_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u32 period_size, buf_size, rate, tmp;

	period_size = (snd_pcm_lib_period_bytes(substream) >> 2) - 1;
	buf_size = snd_pcm_lib_buffer_bytes(substream) - 1;
	tmp = 0x0000;
	if (snd_pcm_format_width(runtime->format) == 16)
		tmp |= 0x10;
	if (runtime->channels == 2)
		tmp |= 0x08;
	rate = (runtime->rate * 8192) / 375;
	if (rate > 0x000fffff)
		rate = 0x000fffff;
	spin_lock_irq(&ice->reg_lock);
	outb(0, ice->ddma_port + 15);
	outb(ICE1712_DMA_MODE_WRITE | ICE1712_DMA_AUTOINIT, ice->ddma_port + 0x0b);
	outl(runtime->dma_addr, ice->ddma_port + 0);
	outw(buf_size, ice->ddma_port + 4);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RATE_LO, rate & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RATE_MID, (rate >> 8) & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RATE_HI, (rate >> 16) & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_CTRL, tmp);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_COUNT_LO, period_size & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_COUNT_HI, period_size >> 8);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_LEFT, 0);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RIGHT, 0);
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int snd_ice1712_playback_ds_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u32 period_size, buf_size, rate, tmp, chn;

	period_size = snd_pcm_lib_period_bytes(substream) - 1;
	buf_size = snd_pcm_lib_buffer_bytes(substream) - 1;
	tmp = 0x0064;
	if (snd_pcm_format_width(runtime->format) == 16)
		tmp &= ~0x04;
	if (runtime->channels == 2)
		tmp |= 0x08;
	rate = (runtime->rate * 8192) / 375;
	if (rate > 0x000fffff)
		rate = 0x000fffff;
	ice->playback_con_active_buf[substream->number] = 0;
	ice->playback_con_virt_addr[substream->number] = runtime->dma_addr;
	chn = substream->number * 2;
	spin_lock_irq(&ice->reg_lock);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_ADDR0, runtime->dma_addr);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_COUNT0, period_size);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_ADDR1, runtime->dma_addr + (runtime->periods > 1 ? period_size + 1 : 0));
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_COUNT1, period_size);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_RATE, rate);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_VOLUME, 0);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_CONTROL, tmp);
	if (runtime->channels == 2) {
		snd_ice1712_ds_write(ice, chn + 1, ICE1712_DSC_RATE, rate);
		snd_ice1712_ds_write(ice, chn + 1, ICE1712_DSC_VOLUME, 0);
	}
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int snd_ice1712_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u32 period_size, buf_size;
	u8 tmp;

	period_size = (snd_pcm_lib_period_bytes(substream) >> 2) - 1;
	buf_size = snd_pcm_lib_buffer_bytes(substream) - 1;
	tmp = 0x06;
	if (snd_pcm_format_width(runtime->format) == 16)
		tmp &= ~0x04;
	if (runtime->channels == 2)
		tmp &= ~0x02;
	spin_lock_irq(&ice->reg_lock);
	outl(ice->capture_con_virt_addr = runtime->dma_addr, ICEREG(ice, CONCAP_ADDR));
	outw(buf_size, ICEREG(ice, CONCAP_COUNT));
	snd_ice1712_write(ice, ICE1712_IREG_CAP_COUNT_HI, period_size >> 8);
	snd_ice1712_write(ice, ICE1712_IREG_CAP_COUNT_LO, period_size & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_CAP_CTRL, tmp);
	spin_unlock_irq(&ice->reg_lock);
	snd_ac97_set_rate(ice->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	return 0;
}

static snd_pcm_uframes_t snd_ice1712_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	size_t ptr;

	if (!(snd_ice1712_read(ice, ICE1712_IREG_PBK_CTRL) & 1))
		return 0;
	ptr = runtime->buffer_size - inw(ice->ddma_port + 4);
	if (ptr == runtime->buffer_size)
		ptr = 0;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_ice1712_playback_ds_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	u8 addr;
	size_t ptr;

	if (!(snd_ice1712_ds_read(ice, substream->number * 2, ICE1712_DSC_CONTROL) & 1))
		return 0;
	if (ice->playback_con_active_buf[substream->number])
		addr = ICE1712_DSC_ADDR1;
	else
		addr = ICE1712_DSC_ADDR0;
	ptr = snd_ice1712_ds_read(ice, substream->number * 2, addr) -
		ice->playback_con_virt_addr[substream->number];
	if (ptr == substream->runtime->buffer_size)
		ptr = 0;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_ice1712_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(snd_ice1712_read(ice, ICE1712_IREG_CAP_CTRL) & 1))
		return 0;
	ptr = inl(ICEREG(ice, CONCAP_ADDR)) - ice->capture_con_virt_addr;
	if (ptr == substream->runtime->buffer_size)
		ptr = 0;
	return bytes_to_frames(substream->runtime, ptr);
}

static struct snd_pcm_hardware snd_ice1712_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(64*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(64*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_ice1712_playback_ds =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_ice1712_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(64*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(64*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static int snd_ice1712_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	ice->playback_con_substream = substream;
	runtime->hw = snd_ice1712_playback;
	return 0;
}

static int snd_ice1712_playback_ds_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	u32 tmp;

	ice->playback_con_substream_ds[substream->number] = substream;
	runtime->hw = snd_ice1712_playback_ds;
	spin_lock_irq(&ice->reg_lock); 
	tmp = inw(ICEDS(ice, INTMASK)) & ~(1 << (substream->number * 2));
	outw(tmp, ICEDS(ice, INTMASK));
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int snd_ice1712_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	ice->capture_con_substream = substream;
	runtime->hw = snd_ice1712_capture;
	runtime->hw.rates = ice->ac97->rates[AC97_RATES_ADC];
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	return 0;
}

static int snd_ice1712_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	ice->playback_con_substream = NULL;
	return 0;
}

static int snd_ice1712_playback_ds_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	u32 tmp;

	spin_lock_irq(&ice->reg_lock); 
	tmp = inw(ICEDS(ice, INTMASK)) | (3 << (substream->number * 2));
	outw(tmp, ICEDS(ice, INTMASK));
	spin_unlock_irq(&ice->reg_lock);
	ice->playback_con_substream_ds[substream->number] = NULL;
	return 0;
}

static int snd_ice1712_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	ice->capture_con_substream = NULL;
	return 0;
}

static struct snd_pcm_ops snd_ice1712_playback_ops = {
	.open =		snd_ice1712_playback_open,
	.close =	snd_ice1712_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_ice1712_hw_params,
	.hw_free =	snd_ice1712_hw_free,
	.prepare =	snd_ice1712_playback_prepare,
	.trigger =	snd_ice1712_playback_trigger,
	.pointer =	snd_ice1712_playback_pointer,
};

static struct snd_pcm_ops snd_ice1712_playback_ds_ops = {
	.open =		snd_ice1712_playback_ds_open,
	.close =	snd_ice1712_playback_ds_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_ice1712_hw_params,
	.hw_free =	snd_ice1712_hw_free,
	.prepare =	snd_ice1712_playback_ds_prepare,
	.trigger =	snd_ice1712_playback_ds_trigger,
	.pointer =	snd_ice1712_playback_ds_pointer,
};

static struct snd_pcm_ops snd_ice1712_capture_ops = {
	.open =		snd_ice1712_capture_open,
	.close =	snd_ice1712_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_ice1712_hw_params,
	.hw_free =	snd_ice1712_hw_free,
	.prepare =	snd_ice1712_capture_prepare,
	.trigger =	snd_ice1712_capture_trigger,
	.pointer =	snd_ice1712_capture_pointer,
};

static int __devinit snd_ice1712_pcm(struct snd_ice1712 * ice, int device, struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(ice->card, "ICE1712 consumer", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ice1712_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ice1712_capture_ops);

	pcm->private_data = ice;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1712 consumer");
	ice->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(ice->pci), 64*1024, 64*1024);

	if (rpcm)
		*rpcm = pcm;

	printk(KERN_WARNING "Consumer PCM code does not work well at the moment --jk\n");

	return 0;
}

static int __devinit snd_ice1712_pcm_ds(struct snd_ice1712 * ice, int device, struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(ice->card, "ICE1712 consumer (DS)", device, 6, 0, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ice1712_playback_ds_ops);

	pcm->private_data = ice;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1712 consumer (DS)");
	ice->pcm_ds = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(ice->pci), 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;

	return 0;
}

/*
 *  PCM code - professional part (multitrack)
 */

static unsigned int rates[] = { 8000, 9600, 11025, 12000, 16000, 22050, 24000,
				32000, 44100, 48000, 64000, 88200, 96000 };

static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

static int snd_ice1712_pro_trigger(struct snd_pcm_substream *substream,
				   int cmd)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	{
		unsigned int what;
		unsigned int old;
		if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
			return -EINVAL;
		what = ICE1712_PLAYBACK_PAUSE;
		snd_pcm_trigger_done(substream, substream);
		spin_lock(&ice->reg_lock);
		old = inl(ICEMT(ice, PLAYBACK_CONTROL));
		if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH)
			old |= what;
		else
			old &= ~what;
		outl(old, ICEMT(ice, PLAYBACK_CONTROL));
		spin_unlock(&ice->reg_lock);
		break;
	}
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		unsigned int what = 0;
		unsigned int old;
		struct list_head *pos;
		struct snd_pcm_substream *s;

		snd_pcm_group_for_each(pos, substream) {
			s = snd_pcm_group_substream_entry(pos);
			if (s == ice->playback_pro_substream) {
				what |= ICE1712_PLAYBACK_START;
				snd_pcm_trigger_done(s, substream);
			} else if (s == ice->capture_pro_substream) {
				what |= ICE1712_CAPTURE_START_SHADOW;
				snd_pcm_trigger_done(s, substream);
			}
		}
		spin_lock(&ice->reg_lock);
		old = inl(ICEMT(ice, PLAYBACK_CONTROL));
		if (cmd == SNDRV_PCM_TRIGGER_START)
			old |= what;
		else
			old &= ~what;
		outl(old, ICEMT(ice, PLAYBACK_CONTROL));
		spin_unlock(&ice->reg_lock);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 */
static void snd_ice1712_set_pro_rate(struct snd_ice1712 *ice, unsigned int rate, int force)
{
	unsigned long flags;
	unsigned char val, old;
	unsigned int i;

	switch (rate) {
	case 8000: val = 6; break;
	case 9600: val = 3; break;
	case 11025: val = 10; break;
	case 12000: val = 2; break;
	case 16000: val = 5; break;
	case 22050: val = 9; break;
	case 24000: val = 1; break;
	case 32000: val = 4; break;
	case 44100: val = 8; break;
	case 48000: val = 0; break;
	case 64000: val = 15; break;
	case 88200: val = 11; break;
	case 96000: val = 7; break;
	default:
		snd_BUG();
		val = 0;
		rate = 48000;
		break;
	}

	spin_lock_irqsave(&ice->reg_lock, flags);
	if (inb(ICEMT(ice, PLAYBACK_CONTROL)) & (ICE1712_CAPTURE_START_SHADOW|
						 ICE1712_PLAYBACK_PAUSE|
						 ICE1712_PLAYBACK_START)) {
	      __out:
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		return;
	}
	if (!force && is_pro_rate_locked(ice))
		goto __out;

        old = inb(ICEMT(ice, RATE));
	if (!force && old == val)
		goto __out;
	outb(val, ICEMT(ice, RATE));
	spin_unlock_irqrestore(&ice->reg_lock, flags);

	if (ice->gpio.set_pro_rate)
		ice->gpio.set_pro_rate(ice, rate);
	for (i = 0; i < ice->akm_codecs; i++) {
		if (ice->akm[i].ops.set_rate_val)
			ice->akm[i].ops.set_rate_val(&ice->akm[i], rate);
	}
	if (ice->spdif.ops.setup_rate)
		ice->spdif.ops.setup_rate(ice, rate);
}

static int snd_ice1712_playback_pro_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	ice->playback_pro_size = snd_pcm_lib_buffer_bytes(substream);
	spin_lock_irq(&ice->reg_lock);
	outl(substream->runtime->dma_addr, ICEMT(ice, PLAYBACK_ADDR));
	outw((ice->playback_pro_size >> 2) - 1, ICEMT(ice, PLAYBACK_SIZE));
	outw((snd_pcm_lib_period_bytes(substream) >> 2) - 1, ICEMT(ice, PLAYBACK_COUNT));
	spin_unlock_irq(&ice->reg_lock);

	return 0;
}

static int snd_ice1712_playback_pro_hw_params(struct snd_pcm_substream *substream,
					      struct snd_pcm_hw_params *hw_params)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	snd_ice1712_set_pro_rate(ice, params_rate(hw_params), 0);
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_ice1712_capture_pro_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	ice->capture_pro_size = snd_pcm_lib_buffer_bytes(substream);
	spin_lock_irq(&ice->reg_lock);
	outl(substream->runtime->dma_addr, ICEMT(ice, CAPTURE_ADDR));
	outw((ice->capture_pro_size >> 2) - 1, ICEMT(ice, CAPTURE_SIZE));
	outw((snd_pcm_lib_period_bytes(substream) >> 2) - 1, ICEMT(ice, CAPTURE_COUNT));
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int snd_ice1712_capture_pro_hw_params(struct snd_pcm_substream *substream,
					     struct snd_pcm_hw_params *hw_params)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	snd_ice1712_set_pro_rate(ice, params_rate(hw_params), 0);
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static snd_pcm_uframes_t snd_ice1712_playback_pro_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(inl(ICEMT(ice, PLAYBACK_CONTROL)) & ICE1712_PLAYBACK_START))
		return 0;
	ptr = ice->playback_pro_size - (inw(ICEMT(ice, PLAYBACK_SIZE)) << 2);
	if (ptr == substream->runtime->buffer_size)
		ptr = 0;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_ice1712_capture_pro_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(inl(ICEMT(ice, PLAYBACK_CONTROL)) & ICE1712_CAPTURE_START_SHADOW))
		return 0;
	ptr = ice->capture_pro_size - (inw(ICEMT(ice, CAPTURE_SIZE)) << 2);
	if (ptr == substream->runtime->buffer_size)
		ptr = 0;
	return bytes_to_frames(substream->runtime, ptr);
}

static struct snd_pcm_hardware snd_ice1712_playback_pro =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_96000,
	.rate_min =		4000,
	.rate_max =		96000,
	.channels_min =		10,
	.channels_max =		10,
	.buffer_bytes_max =	(256*1024),
	.period_bytes_min =	10 * 4 * 2,
	.period_bytes_max =	131040,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_ice1712_capture_pro =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_96000,
	.rate_min =		4000,
	.rate_max =		96000,
	.channels_min =		12,
	.channels_max =		12,
	.buffer_bytes_max =	(256*1024),
	.period_bytes_min =	12 * 4 * 2,
	.period_bytes_max =	131040,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static int snd_ice1712_playback_pro_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	ice->playback_pro_substream = substream;
	runtime->hw = snd_ice1712_playback_pro;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);

	if (ice->spdif.ops.open)
		ice->spdif.ops.open(ice, substream);

	return 0;
}

static int snd_ice1712_capture_pro_open(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	ice->capture_pro_substream = substream;
	runtime->hw = snd_ice1712_capture_pro;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
	return 0;
}

static int snd_ice1712_playback_pro_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	if (PRO_RATE_RESET)
		snd_ice1712_set_pro_rate(ice, PRO_RATE_DEFAULT, 0);
	ice->playback_pro_substream = NULL;
	if (ice->spdif.ops.close)
		ice->spdif.ops.close(ice, substream);

	return 0;
}

static int snd_ice1712_capture_pro_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	if (PRO_RATE_RESET)
		snd_ice1712_set_pro_rate(ice, PRO_RATE_DEFAULT, 0);
	ice->capture_pro_substream = NULL;
	return 0;
}

static struct snd_pcm_ops snd_ice1712_playback_pro_ops = {
	.open =		snd_ice1712_playback_pro_open,
	.close =	snd_ice1712_playback_pro_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_ice1712_playback_pro_hw_params,
	.hw_free =	snd_ice1712_hw_free,
	.prepare =	snd_ice1712_playback_pro_prepare,
	.trigger =	snd_ice1712_pro_trigger,
	.pointer =	snd_ice1712_playback_pro_pointer,
};

static struct snd_pcm_ops snd_ice1712_capture_pro_ops = {
	.open =		snd_ice1712_capture_pro_open,
	.close =	snd_ice1712_capture_pro_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_ice1712_capture_pro_hw_params,
	.hw_free =	snd_ice1712_hw_free,
	.prepare =	snd_ice1712_capture_pro_prepare,
	.trigger =	snd_ice1712_pro_trigger,
	.pointer =	snd_ice1712_capture_pro_pointer,
};

static int __devinit snd_ice1712_pcm_profi(struct snd_ice1712 * ice, int device, struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(ice->card, "ICE1712 multi", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ice1712_playback_pro_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ice1712_capture_pro_ops);

	pcm->private_data = ice;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1712 multi");

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(ice->pci), 256*1024, 256*1024);

	ice->pcm_pro = pcm;
	if (rpcm)
		*rpcm = pcm;
	
	if (ice->cs8427) {
		/* assign channels to iec958 */
		err = snd_cs8427_iec958_build(ice->cs8427,
					      pcm->streams[0].substream,
					      pcm->streams[1].substream);
		if (err < 0)
			return err;
	}

	if ((err = snd_ice1712_build_pro_mixer(ice)) < 0)
		return err;
	return 0;
}

/*
 *  Mixer section
 */

static void snd_ice1712_update_volume(struct snd_ice1712 *ice, int index)
{
	unsigned int vol = ice->pro_volumes[index];
	unsigned short val = 0;

	val |= (vol & 0x8000) == 0 ? (96 - (vol & 0x7f)) : 0x7f;
	val |= ((vol & 0x80000000) == 0 ? (96 - ((vol >> 16) & 0x7f)) : 0x7f) << 8;
	outb(index, ICEMT(ice, MONITOR_INDEX));
	outw(val, ICEMT(ice, MONITOR_VOLUME));
}

static int snd_ice1712_pro_mixer_switch_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_pro_mixer_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int index = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + kcontrol->private_value;
	
	spin_lock_irq(&ice->reg_lock);
	ucontrol->value.integer.value[0] = !((ice->pro_volumes[index] >> 15) & 1);
	ucontrol->value.integer.value[1] = !((ice->pro_volumes[index] >> 31) & 1);
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int snd_ice1712_pro_mixer_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int index = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + kcontrol->private_value;
	unsigned int nval, change;

	nval = (ucontrol->value.integer.value[0] ? 0 : 0x00008000) |
	       (ucontrol->value.integer.value[1] ? 0 : 0x80000000);
	spin_lock_irq(&ice->reg_lock);
	nval |= ice->pro_volumes[index] & ~0x80008000;
	change = nval != ice->pro_volumes[index];
	ice->pro_volumes[index] = nval;
	snd_ice1712_update_volume(ice, index);
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static int snd_ice1712_pro_mixer_volume_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 96;
	return 0;
}

static int snd_ice1712_pro_mixer_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int index = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + kcontrol->private_value;
	
	spin_lock_irq(&ice->reg_lock);
	ucontrol->value.integer.value[0] = (ice->pro_volumes[index] >> 0) & 127;
	ucontrol->value.integer.value[1] = (ice->pro_volumes[index] >> 16) & 127;
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int snd_ice1712_pro_mixer_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int index = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + kcontrol->private_value;
	unsigned int nval, change;

	nval = (ucontrol->value.integer.value[0] & 127) |
	       ((ucontrol->value.integer.value[1] & 127) << 16);
	spin_lock_irq(&ice->reg_lock);
	nval |= ice->pro_volumes[index] & ~0x007f007f;
	change = nval != ice->pro_volumes[index];
	ice->pro_volumes[index] = nval;
	snd_ice1712_update_volume(ice, index);
	spin_unlock_irq(&ice->reg_lock);
	return change;
}


static struct snd_kcontrol_new snd_ice1712_multi_playback_ctrls[] __devinitdata = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Multi Playback Switch",
		.info = snd_ice1712_pro_mixer_switch_info,
		.get = snd_ice1712_pro_mixer_switch_get,
		.put = snd_ice1712_pro_mixer_switch_put,
		.private_value = 0,
		.count = 10,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Multi Playback Volume",
		.info = snd_ice1712_pro_mixer_volume_info,
		.get = snd_ice1712_pro_mixer_volume_get,
		.put = snd_ice1712_pro_mixer_volume_put,
		.private_value = 0,
		.count = 10,
	},
};

static struct snd_kcontrol_new snd_ice1712_multi_capture_analog_switch __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "H/W Multi Capture Switch",
	.info = snd_ice1712_pro_mixer_switch_info,
	.get = snd_ice1712_pro_mixer_switch_get,
	.put = snd_ice1712_pro_mixer_switch_put,
	.private_value = 10,
};

static struct snd_kcontrol_new snd_ice1712_multi_capture_spdif_switch __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("Multi ",CAPTURE,SWITCH),
	.info = snd_ice1712_pro_mixer_switch_info,
	.get = snd_ice1712_pro_mixer_switch_get,
	.put = snd_ice1712_pro_mixer_switch_put,
	.private_value = 18,
	.count = 2,
};

static struct snd_kcontrol_new snd_ice1712_multi_capture_analog_volume __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "H/W Multi Capture Volume",
	.info = snd_ice1712_pro_mixer_volume_info,
	.get = snd_ice1712_pro_mixer_volume_get,
	.put = snd_ice1712_pro_mixer_volume_put,
	.private_value = 10,
};

static struct snd_kcontrol_new snd_ice1712_multi_capture_spdif_volume __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("Multi ",CAPTURE,VOLUME),
	.info = snd_ice1712_pro_mixer_volume_info,
	.get = snd_ice1712_pro_mixer_volume_get,
	.put = snd_ice1712_pro_mixer_volume_put,
	.private_value = 18,
	.count = 2,
};

static int __devinit snd_ice1712_build_pro_mixer(struct snd_ice1712 *ice)
{
	struct snd_card *card = ice->card;
	unsigned int idx;
	int err;

	/* multi-channel mixer */
	for (idx = 0; idx < ARRAY_SIZE(snd_ice1712_multi_playback_ctrls); idx++) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_ice1712_multi_playback_ctrls[idx], ice));
		if (err < 0)
			return err;
	}
	
	if (ice->num_total_adcs > 0) {
		struct snd_kcontrol_new tmp = snd_ice1712_multi_capture_analog_switch;
		tmp.count = ice->num_total_adcs;
		err = snd_ctl_add(card, snd_ctl_new1(&tmp, ice));
		if (err < 0)
			return err;
	}

	err = snd_ctl_add(card, snd_ctl_new1(&snd_ice1712_multi_capture_spdif_switch, ice));
	if (err < 0)
		return err;

	if (ice->num_total_adcs > 0) {
		struct snd_kcontrol_new tmp = snd_ice1712_multi_capture_analog_volume;
		tmp.count = ice->num_total_adcs;
		err = snd_ctl_add(card, snd_ctl_new1(&tmp, ice));
		if (err < 0)
			return err;
	}

	err = snd_ctl_add(card, snd_ctl_new1(&snd_ice1712_multi_capture_spdif_volume, ice));
	if (err < 0)
		return err;

	/* initialize volumes */
	for (idx = 0; idx < 10; idx++) {
		ice->pro_volumes[idx] = 0x80008000;	/* mute */
		snd_ice1712_update_volume(ice, idx);
	}
	for (idx = 10; idx < 10 + ice->num_total_adcs; idx++) {
		ice->pro_volumes[idx] = 0x80008000;	/* mute */
		snd_ice1712_update_volume(ice, idx);
	}
	for (idx = 18; idx < 20; idx++) {
		ice->pro_volumes[idx] = 0x80008000;	/* mute */
		snd_ice1712_update_volume(ice, idx);
	}
	return 0;
}

static void snd_ice1712_mixer_free_ac97(struct snd_ac97 *ac97)
{
	struct snd_ice1712 *ice = ac97->private_data;
	ice->ac97 = NULL;
}

static int __devinit snd_ice1712_ac97_mixer(struct snd_ice1712 * ice)
{
	int err, bus_num = 0;
	struct snd_ac97_template ac97;
	struct snd_ac97_bus *pbus;
	static struct snd_ac97_bus_ops con_ops = {
		.write = snd_ice1712_ac97_write,
		.read = snd_ice1712_ac97_read,
	};
	static struct snd_ac97_bus_ops pro_ops = {
		.write = snd_ice1712_pro_ac97_write,
		.read = snd_ice1712_pro_ac97_read,
	};

	if (ice_has_con_ac97(ice)) {
		if ((err = snd_ac97_bus(ice->card, bus_num++, &con_ops, NULL, &pbus)) < 0)
			return err;
		memset(&ac97, 0, sizeof(ac97));
		ac97.private_data = ice;
		ac97.private_free = snd_ice1712_mixer_free_ac97;
		if ((err = snd_ac97_mixer(pbus, &ac97, &ice->ac97)) < 0)
			printk(KERN_WARNING "ice1712: cannot initialize ac97 for consumer, skipped\n");
		else {
			if ((err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_mixer_digmix_route_ac97, ice))) < 0)
				return err;
			return 0;
		}
	}

	if (! (ice->eeprom.data[ICE_EEP1_ACLINK] & ICE1712_CFG_PRO_I2S)) {
		if ((err = snd_ac97_bus(ice->card, bus_num, &pro_ops, NULL, &pbus)) < 0)
			return err;
		memset(&ac97, 0, sizeof(ac97));
		ac97.private_data = ice;
		ac97.private_free = snd_ice1712_mixer_free_ac97;
		if ((err = snd_ac97_mixer(pbus, &ac97, &ice->ac97)) < 0)
			printk(KERN_WARNING "ice1712: cannot initialize pro ac97, skipped\n");
		else
			return 0;
	}
	/* I2S mixer only */
	strcat(ice->card->mixername, "ICE1712 - multitrack");
	return 0;
}

/*
 *
 */

static inline unsigned int eeprom_double(struct snd_ice1712 *ice, int idx)
{
	return (unsigned int)ice->eeprom.data[idx] | ((unsigned int)ice->eeprom.data[idx + 1] << 8);
}

static void snd_ice1712_proc_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	unsigned int idx;

	snd_iprintf(buffer, "%s\n\n", ice->card->longname);
	snd_iprintf(buffer, "EEPROM:\n");

	snd_iprintf(buffer, "  Subvendor        : 0x%x\n", ice->eeprom.subvendor);
	snd_iprintf(buffer, "  Size             : %i bytes\n", ice->eeprom.size);
	snd_iprintf(buffer, "  Version          : %i\n", ice->eeprom.version);
	snd_iprintf(buffer, "  Codec            : 0x%x\n", ice->eeprom.data[ICE_EEP1_CODEC]);
	snd_iprintf(buffer, "  ACLink           : 0x%x\n", ice->eeprom.data[ICE_EEP1_ACLINK]);
	snd_iprintf(buffer, "  I2S ID           : 0x%x\n", ice->eeprom.data[ICE_EEP1_I2SID]);
	snd_iprintf(buffer, "  S/PDIF           : 0x%x\n", ice->eeprom.data[ICE_EEP1_SPDIF]);
	snd_iprintf(buffer, "  GPIO mask        : 0x%x\n", ice->eeprom.gpiomask);
	snd_iprintf(buffer, "  GPIO state       : 0x%x\n", ice->eeprom.gpiostate);
	snd_iprintf(buffer, "  GPIO direction   : 0x%x\n", ice->eeprom.gpiodir);
	snd_iprintf(buffer, "  AC'97 main       : 0x%x\n", eeprom_double(ice, ICE_EEP1_AC97_MAIN_LO));
	snd_iprintf(buffer, "  AC'97 pcm        : 0x%x\n", eeprom_double(ice, ICE_EEP1_AC97_PCM_LO));
	snd_iprintf(buffer, "  AC'97 record     : 0x%x\n", eeprom_double(ice, ICE_EEP1_AC97_REC_LO));
	snd_iprintf(buffer, "  AC'97 record src : 0x%x\n", ice->eeprom.data[ICE_EEP1_AC97_RECSRC]);
	for (idx = 0; idx < 4; idx++)
		snd_iprintf(buffer, "  DAC ID #%i        : 0x%x\n", idx, ice->eeprom.data[ICE_EEP1_DAC_ID + idx]);
	for (idx = 0; idx < 4; idx++)
		snd_iprintf(buffer, "  ADC ID #%i        : 0x%x\n", idx, ice->eeprom.data[ICE_EEP1_ADC_ID + idx]);
	for (idx = 0x1c; idx < ice->eeprom.size; idx++)
		snd_iprintf(buffer, "  Extra #%02i        : 0x%x\n", idx, ice->eeprom.data[idx]);

	snd_iprintf(buffer, "\nRegisters:\n");
	snd_iprintf(buffer, "  PSDOUT03         : 0x%04x\n", (unsigned)inw(ICEMT(ice, ROUTE_PSDOUT03)));
	snd_iprintf(buffer, "  CAPTURE          : 0x%08x\n", inl(ICEMT(ice, ROUTE_CAPTURE)));
	snd_iprintf(buffer, "  SPDOUT           : 0x%04x\n", (unsigned)inw(ICEMT(ice, ROUTE_SPDOUT)));
	snd_iprintf(buffer, "  RATE             : 0x%02x\n", (unsigned)inb(ICEMT(ice, RATE)));
	snd_iprintf(buffer, "  GPIO_DATA        : 0x%02x\n", (unsigned)snd_ice1712_get_gpio_data(ice));
        snd_iprintf(buffer, "  GPIO_WRITE_MASK  : 0x%02x\n", (unsigned)snd_ice1712_read(ice, ICE1712_IREG_GPIO_WRITE_MASK));
	snd_iprintf(buffer, "  GPIO_DIRECTION   : 0x%02x\n", (unsigned)snd_ice1712_read(ice, ICE1712_IREG_GPIO_DIRECTION));
}

static void __devinit snd_ice1712_proc_init(struct snd_ice1712 * ice)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(ice->card, "ice1712", &entry))
		snd_info_set_text_ops(entry, ice, snd_ice1712_proc_read);
}

/*
 *
 */

static int snd_ice1712_eeprom_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(struct snd_ice1712_eeprom);
	return 0;
}

static int snd_ice1712_eeprom_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	
	memcpy(ucontrol->value.bytes.data, &ice->eeprom, sizeof(ice->eeprom));
	return 0;
}

static struct snd_kcontrol_new snd_ice1712_eeprom __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.name = "ICE1712 EEPROM",
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_ice1712_eeprom_info,
	.get = snd_ice1712_eeprom_get
};

/*
 */
static int snd_ice1712_spdif_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ice1712_spdif_default_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	if (ice->spdif.ops.default_get)
		ice->spdif.ops.default_get(ice, ucontrol); 
	return 0;
}

static int snd_ice1712_spdif_default_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	if (ice->spdif.ops.default_put)
		return ice->spdif.ops.default_put(ice, ucontrol);
	return 0;
}

static struct snd_kcontrol_new snd_ice1712_spdif_default __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_ice1712_spdif_info,
	.get =		snd_ice1712_spdif_default_get,
	.put =		snd_ice1712_spdif_default_put
};

static int snd_ice1712_spdif_maskc_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	if (ice->spdif.ops.default_get) {
		ucontrol->value.iec958.status[0] = IEC958_AES0_NONAUDIO |
						     IEC958_AES0_PROFESSIONAL |
						     IEC958_AES0_CON_NOT_COPYRIGHT |
						     IEC958_AES0_CON_EMPHASIS;
		ucontrol->value.iec958.status[1] = IEC958_AES1_CON_ORIGINAL |
						     IEC958_AES1_CON_CATEGORY;
		ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS;
	} else {
		ucontrol->value.iec958.status[0] = 0xff;
		ucontrol->value.iec958.status[1] = 0xff;
		ucontrol->value.iec958.status[2] = 0xff;
		ucontrol->value.iec958.status[3] = 0xff;
		ucontrol->value.iec958.status[4] = 0xff;
	}
	return 0;
}

static int snd_ice1712_spdif_maskp_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	if (ice->spdif.ops.default_get) {
		ucontrol->value.iec958.status[0] = IEC958_AES0_NONAUDIO |
						     IEC958_AES0_PROFESSIONAL |
						     IEC958_AES0_PRO_FS |
						     IEC958_AES0_PRO_EMPHASIS;
		ucontrol->value.iec958.status[1] = IEC958_AES1_PRO_MODE;
	} else {
		ucontrol->value.iec958.status[0] = 0xff;
		ucontrol->value.iec958.status[1] = 0xff;
		ucontrol->value.iec958.status[2] = 0xff;
		ucontrol->value.iec958.status[3] = 0xff;
		ucontrol->value.iec958.status[4] = 0xff;
	}
	return 0;
}

static struct snd_kcontrol_new snd_ice1712_spdif_maskc __devinitdata =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	.info =		snd_ice1712_spdif_info,
	.get =		snd_ice1712_spdif_maskc_get,
};

static struct snd_kcontrol_new snd_ice1712_spdif_maskp __devinitdata =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
	.info =		snd_ice1712_spdif_info,
	.get =		snd_ice1712_spdif_maskp_get,
};

static int snd_ice1712_spdif_stream_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	if (ice->spdif.ops.stream_get)
		ice->spdif.ops.stream_get(ice, ucontrol);
	return 0;
}

static int snd_ice1712_spdif_stream_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	if (ice->spdif.ops.stream_put)
		return ice->spdif.ops.stream_put(ice, ucontrol);
	return 0;
}

static struct snd_kcontrol_new snd_ice1712_spdif_stream __devinitdata =
{
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_INACTIVE),
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_ice1712_spdif_info,
	.get =		snd_ice1712_spdif_stream_get,
	.put =		snd_ice1712_spdif_stream_put
};

int snd_ice1712_gpio_info(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

int snd_ice1712_gpio_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value & (1<<24)) ? 1 : 0;
	
	snd_ice1712_save_gpio_status(ice);
	ucontrol->value.integer.value[0] =
		(snd_ice1712_gpio_read(ice) & mask ? 1 : 0) ^ invert;
	snd_ice1712_restore_gpio_status(ice);
	return 0;
}

int snd_ice1712_gpio_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value & (1<<24)) ? mask : 0;
	unsigned int val, nval;

	if (kcontrol->private_value & (1 << 31))
		return -EPERM;
	nval = (ucontrol->value.integer.value[0] ? mask : 0) ^ invert;
	snd_ice1712_save_gpio_status(ice);
	val = snd_ice1712_gpio_read(ice);
	nval |= val & ~mask;
	if (val != nval)
		snd_ice1712_gpio_write(ice, nval);
	snd_ice1712_restore_gpio_status(ice);
	return val != nval;
}

/*
 *  rate
 */
static int snd_ice1712_pro_internal_clock_info(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"8000",		/* 0: 6 */
		"9600",		/* 1: 3 */
		"11025",	/* 2: 10 */
		"12000",	/* 3: 2 */
		"16000",	/* 4: 5 */
		"22050",	/* 5: 9 */
		"24000",	/* 6: 1 */
		"32000",	/* 7: 4 */
		"44100",	/* 8: 8 */
		"48000",	/* 9: 0 */
		"64000",	/* 10: 15 */
		"88200",	/* 11: 11 */
		"96000",	/* 12: 7 */
		"IEC958 Input",	/* 13: -- */
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 14;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_pro_internal_clock_get(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	static unsigned char xlate[16] = {
		9, 6, 3, 1, 7, 4, 0, 12, 8, 5, 2, 11, 255, 255, 255, 10
	};
	unsigned char val;
	
	spin_lock_irq(&ice->reg_lock);
	if (is_spdif_master(ice)) {
		ucontrol->value.enumerated.item[0] = 13;
	} else {
		val = xlate[inb(ICEMT(ice, RATE)) & 15];
		if (val == 255) {
			snd_BUG();
			val = 0;
		}
		ucontrol->value.enumerated.item[0] = val;
	}
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int snd_ice1712_pro_internal_clock_put(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	static unsigned int xrate[13] = {
		8000, 9600, 11025, 12000, 1600, 22050, 24000,
		32000, 44100, 48000, 64000, 88200, 96000
	};
	unsigned char oval;
	int change = 0;

	spin_lock_irq(&ice->reg_lock);
	oval = inb(ICEMT(ice, RATE));
	if (ucontrol->value.enumerated.item[0] == 13) {
		outb(oval | ICE1712_SPDIF_MASTER, ICEMT(ice, RATE));
	} else {
		PRO_RATE_DEFAULT = xrate[ucontrol->value.integer.value[0] % 13];
		spin_unlock_irq(&ice->reg_lock);
		snd_ice1712_set_pro_rate(ice, PRO_RATE_DEFAULT, 1);
		spin_lock_irq(&ice->reg_lock);
	}
	change = inb(ICEMT(ice, RATE)) != oval;
	spin_unlock_irq(&ice->reg_lock);

	if ((oval & ICE1712_SPDIF_MASTER) !=
	    (inb(ICEMT(ice, RATE)) & ICE1712_SPDIF_MASTER))
	        snd_ice1712_set_input_clock_source(ice, is_spdif_master(ice));

	return change;
}

static struct snd_kcontrol_new snd_ice1712_pro_internal_clock __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Internal Clock",
	.info = snd_ice1712_pro_internal_clock_info,
	.get = snd_ice1712_pro_internal_clock_get,
	.put = snd_ice1712_pro_internal_clock_put
};

static int snd_ice1712_pro_internal_clock_default_info(struct snd_kcontrol *kcontrol,
						       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"8000",		/* 0: 6 */
		"9600",		/* 1: 3 */
		"11025",	/* 2: 10 */
		"12000",	/* 3: 2 */
		"16000",	/* 4: 5 */
		"22050",	/* 5: 9 */
		"24000",	/* 6: 1 */
		"32000",	/* 7: 4 */
		"44100",	/* 8: 8 */
		"48000",	/* 9: 0 */
		"64000",	/* 10: 15 */
		"88200",	/* 11: 11 */
		"96000",	/* 12: 7 */
		// "IEC958 Input",	/* 13: -- */
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 13;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_pro_internal_clock_default_get(struct snd_kcontrol *kcontrol,
						      struct snd_ctl_elem_value *ucontrol)
{
	int val;
	static unsigned int xrate[13] = {
		8000, 9600, 11025, 12000, 1600, 22050, 24000,
		32000, 44100, 48000, 64000, 88200, 96000
	};

	for (val = 0; val < 13; val++) {
		if (xrate[val] == PRO_RATE_DEFAULT)
			break;
	}

	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int snd_ice1712_pro_internal_clock_default_put(struct snd_kcontrol *kcontrol,
						      struct snd_ctl_elem_value *ucontrol)
{
	static unsigned int xrate[13] = {
		8000, 9600, 11025, 12000, 1600, 22050, 24000,
		32000, 44100, 48000, 64000, 88200, 96000
	};
	unsigned char oval;
	int change = 0;

	oval = PRO_RATE_DEFAULT;
	PRO_RATE_DEFAULT = xrate[ucontrol->value.integer.value[0] % 13];
	change = PRO_RATE_DEFAULT != oval;

	return change;
}

static struct snd_kcontrol_new snd_ice1712_pro_internal_clock_default __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Internal Clock Default",
	.info = snd_ice1712_pro_internal_clock_default_info,
	.get = snd_ice1712_pro_internal_clock_default_get,
	.put = snd_ice1712_pro_internal_clock_default_put
};

static int snd_ice1712_pro_rate_locking_info(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_pro_rate_locking_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = PRO_RATE_LOCKED;
	return 0;
}

static int snd_ice1712_pro_rate_locking_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int change = 0, nval;

	nval = ucontrol->value.integer.value[0] ? 1 : 0;
	spin_lock_irq(&ice->reg_lock);
	change = PRO_RATE_LOCKED != nval;
	PRO_RATE_LOCKED = nval;
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_ice1712_pro_rate_locking __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Rate Locking",
	.info = snd_ice1712_pro_rate_locking_info,
	.get = snd_ice1712_pro_rate_locking_get,
	.put = snd_ice1712_pro_rate_locking_put
};

static int snd_ice1712_pro_rate_reset_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_pro_rate_reset_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = PRO_RATE_RESET;
	return 0;
}

static int snd_ice1712_pro_rate_reset_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int change = 0, nval;

	nval = ucontrol->value.integer.value[0] ? 1 : 0;
	spin_lock_irq(&ice->reg_lock);
	change = PRO_RATE_RESET != nval;
	PRO_RATE_RESET = nval;
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_ice1712_pro_rate_reset __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Rate Reset",
	.info = snd_ice1712_pro_rate_reset_info,
	.get = snd_ice1712_pro_rate_reset_get,
	.put = snd_ice1712_pro_rate_reset_put
};

/*
 * routing
 */
static int snd_ice1712_pro_route_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"PCM Out", /* 0 */
		"H/W In 0", "H/W In 1", "H/W In 2", "H/W In 3", /* 1-4 */
		"H/W In 4", "H/W In 5", "H/W In 6", "H/W In 7", /* 5-8 */
		"IEC958 In L", "IEC958 In R", /* 9-10 */
		"Digital Mixer", /* 11 - optional */
	};
	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items =
		snd_ctl_get_ioffidx(kcontrol, &uinfo->id) < 2 ? 12 : 11;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_pro_route_analog_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int val, cval;

	spin_lock_irq(&ice->reg_lock);
	val = inw(ICEMT(ice, ROUTE_PSDOUT03));
	cval = inl(ICEMT(ice, ROUTE_CAPTURE));
	spin_unlock_irq(&ice->reg_lock);

	val >>= ((idx % 2) * 8) + ((idx / 2) * 2);
	val &= 3;
	cval >>= ((idx / 2) * 8) + ((idx % 2) * 4);
	if (val == 1 && idx < 2)
		ucontrol->value.enumerated.item[0] = 11;
	else if (val == 2)
		ucontrol->value.enumerated.item[0] = (cval & 7) + 1;
	else if (val == 3)
		ucontrol->value.enumerated.item[0] = ((cval >> 3) & 1) + 9;
	else
		ucontrol->value.enumerated.item[0] = 0;
	return 0;
}

static int snd_ice1712_pro_route_analog_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int change, shift;
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int val, old_val, nval;
	
	/* update PSDOUT */
	if (ucontrol->value.enumerated.item[0] >= 11)
		nval = idx < 2 ? 1 : 0; /* dig mixer (or pcm) */
	else if (ucontrol->value.enumerated.item[0] >= 9)
		nval = 3; /* spdif in */
	else if (ucontrol->value.enumerated.item[0] >= 1)
		nval = 2; /* analog in */
	else
		nval = 0; /* pcm */
	shift = ((idx % 2) * 8) + ((idx / 2) * 2);
	spin_lock_irq(&ice->reg_lock);
	val = old_val = inw(ICEMT(ice, ROUTE_PSDOUT03));
	val &= ~(0x03 << shift);
	val |= nval << shift;
	change = val != old_val;
	if (change)
		outw(val, ICEMT(ice, ROUTE_PSDOUT03));
	spin_unlock_irq(&ice->reg_lock);
	if (nval < 2) /* dig mixer of pcm */
		return change;

	/* update CAPTURE */
	spin_lock_irq(&ice->reg_lock);
	val = old_val = inl(ICEMT(ice, ROUTE_CAPTURE));
	shift = ((idx / 2) * 8) + ((idx % 2) * 4);
	if (nval == 2) { /* analog in */
		nval = ucontrol->value.enumerated.item[0] - 1;
		val &= ~(0x07 << shift);
		val |= nval << shift;
	} else { /* spdif in */
		nval = (ucontrol->value.enumerated.item[0] - 9) << 3;
		val &= ~(0x08 << shift);
		val |= nval << shift;
	}
	if (val != old_val) {
		change = 1;
		outl(val, ICEMT(ice, ROUTE_CAPTURE));
	}
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static int snd_ice1712_pro_route_spdif_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int val, cval;
	val = inw(ICEMT(ice, ROUTE_SPDOUT));
	cval = (val >> (idx * 4 + 8)) & 0x0f;
	val = (val >> (idx * 2)) & 0x03;
	if (val == 1)
		ucontrol->value.enumerated.item[0] = 11;
	else if (val == 2)
		ucontrol->value.enumerated.item[0] = (cval & 7) + 1;
	else if (val == 3)
		ucontrol->value.enumerated.item[0] = ((cval >> 3) & 1) + 9;
	else
		ucontrol->value.enumerated.item[0] = 0;
	return 0;
}

static int snd_ice1712_pro_route_spdif_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int change, shift;
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int val, old_val, nval;
	
	/* update SPDOUT */
	spin_lock_irq(&ice->reg_lock);
	val = old_val = inw(ICEMT(ice, ROUTE_SPDOUT));
	if (ucontrol->value.enumerated.item[0] >= 11)
		nval = 1;
	else if (ucontrol->value.enumerated.item[0] >= 9)
		nval = 3;
	else if (ucontrol->value.enumerated.item[0] >= 1)
		nval = 2;
	else
		nval = 0;
	shift = idx * 2;
	val &= ~(0x03 << shift);
	val |= nval << shift;
	shift = idx * 4 + 8;
	if (nval == 2) {
		nval = ucontrol->value.enumerated.item[0] - 1;
		val &= ~(0x07 << shift);
		val |= nval << shift;
	} else if (nval == 3) {
		nval = (ucontrol->value.enumerated.item[0] - 9) << 3;
		val &= ~(0x08 << shift);
		val |= nval << shift;
	}
	change = val != old_val;
	if (change)
		outw(val, ICEMT(ice, ROUTE_SPDOUT));
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_ice1712_mixer_pro_analog_route __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "H/W Playback Route",
	.info = snd_ice1712_pro_route_info,
	.get = snd_ice1712_pro_route_analog_get,
	.put = snd_ice1712_pro_route_analog_put,
};

static struct snd_kcontrol_new snd_ice1712_mixer_pro_spdif_route __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Route",
	.info = snd_ice1712_pro_route_info,
	.get = snd_ice1712_pro_route_spdif_get,
	.put = snd_ice1712_pro_route_spdif_put,
	.count = 2,
};


static int snd_ice1712_pro_volume_rate_info(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_ice1712_pro_volume_rate_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = inb(ICEMT(ice, MONITOR_RATE));
	return 0;
}

static int snd_ice1712_pro_volume_rate_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int change;

	spin_lock_irq(&ice->reg_lock);
	change = inb(ICEMT(ice, MONITOR_RATE)) != ucontrol->value.integer.value[0];
	outb(ucontrol->value.integer.value[0], ICEMT(ice, MONITOR_RATE));
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_ice1712_mixer_pro_volume_rate __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Volume Rate",
	.info = snd_ice1712_pro_volume_rate_info,
	.get = snd_ice1712_pro_volume_rate_get,
	.put = snd_ice1712_pro_volume_rate_put
};

static int snd_ice1712_pro_peak_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 22;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_ice1712_pro_peak_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx;
	
	spin_lock_irq(&ice->reg_lock);
	for (idx = 0; idx < 22; idx++) {
		outb(idx, ICEMT(ice, MONITOR_PEAKINDEX));
		ucontrol->value.integer.value[idx] = inb(ICEMT(ice, MONITOR_PEAKDATA));
	}
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static struct snd_kcontrol_new snd_ice1712_mixer_pro_peak __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Peak",
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = snd_ice1712_pro_peak_info,
	.get = snd_ice1712_pro_peak_get
};

/*
 *
 */

/*
 * list of available boards
 */
static struct snd_ice1712_card_info *card_tables[] __devinitdata = {
	snd_ice1712_hoontech_cards,
	snd_ice1712_delta_cards,
	snd_ice1712_ews_cards,
	NULL,
};

static unsigned char __devinit snd_ice1712_read_i2c(struct snd_ice1712 *ice,
						 unsigned char dev,
						 unsigned char addr)
{
	long t = 0x10000;

	outb(addr, ICEREG(ice, I2C_BYTE_ADDR));
	outb(dev & ~ICE1712_I2C_WRITE, ICEREG(ice, I2C_DEV_ADDR));
	while (t-- > 0 && (inb(ICEREG(ice, I2C_CTRL)) & ICE1712_I2C_BUSY)) ;
	return inb(ICEREG(ice, I2C_DATA));
}

static int __devinit snd_ice1712_read_eeprom(struct snd_ice1712 *ice,
					     const char *modelname)
{
	int dev = 0xa0;		/* EEPROM device address */
	unsigned int i, size;
	struct snd_ice1712_card_info **tbl, *c;

	if (! modelname || ! *modelname) {
		ice->eeprom.subvendor = 0;
		if ((inb(ICEREG(ice, I2C_CTRL)) & ICE1712_I2C_EEPROM) != 0)
			ice->eeprom.subvendor = (snd_ice1712_read_i2c(ice, dev, 0x00) << 0) |
				(snd_ice1712_read_i2c(ice, dev, 0x01) << 8) | 
				(snd_ice1712_read_i2c(ice, dev, 0x02) << 16) | 
				(snd_ice1712_read_i2c(ice, dev, 0x03) << 24);
		if (ice->eeprom.subvendor == 0 ||
		    ice->eeprom.subvendor == (unsigned int)-1) {
			/* invalid subvendor from EEPROM, try the PCI subststem ID instead */
			u16 vendor, device;
			pci_read_config_word(ice->pci, PCI_SUBSYSTEM_VENDOR_ID, &vendor);
			pci_read_config_word(ice->pci, PCI_SUBSYSTEM_ID, &device);
			ice->eeprom.subvendor = ((unsigned int)swab16(vendor) << 16) | swab16(device);
			if (ice->eeprom.subvendor == 0 || ice->eeprom.subvendor == (unsigned int)-1) {
				printk(KERN_ERR "ice1712: No valid ID is found\n");
				return -ENXIO;
			}
		}
	}
	for (tbl = card_tables; *tbl; tbl++) {
		for (c = *tbl; c->subvendor; c++) {
			if (modelname && c->model && ! strcmp(modelname, c->model)) {
				printk(KERN_INFO "ice1712: Using board model %s\n", c->name);
				ice->eeprom.subvendor = c->subvendor;
			} else if (c->subvendor != ice->eeprom.subvendor)
				continue;
			if (! c->eeprom_size || ! c->eeprom_data)
				goto found;
			/* if the EEPROM is given by the driver, use it */
			snd_printdd("using the defined eeprom..\n");
			ice->eeprom.version = 1;
			ice->eeprom.size = c->eeprom_size + 6;
			memcpy(ice->eeprom.data, c->eeprom_data, c->eeprom_size);
			goto read_skipped;
		}
	}
	printk(KERN_WARNING "ice1712: No matching model found for ID 0x%x\n",
	       ice->eeprom.subvendor);

 found:
	ice->eeprom.size = snd_ice1712_read_i2c(ice, dev, 0x04);
	if (ice->eeprom.size < 6)
		ice->eeprom.size = 32; /* FIXME: any cards without the correct size? */
	else if (ice->eeprom.size > 32) {
		snd_printk(KERN_ERR "invalid EEPROM (size = %i)\n", ice->eeprom.size);
		return -EIO;
	}
	ice->eeprom.version = snd_ice1712_read_i2c(ice, dev, 0x05);
	if (ice->eeprom.version != 1) {
		snd_printk(KERN_ERR "invalid EEPROM version %i\n",
			   ice->eeprom.version);
		/* return -EIO; */
	}
	size = ice->eeprom.size - 6;
	for (i = 0; i < size; i++)
		ice->eeprom.data[i] = snd_ice1712_read_i2c(ice, dev, i + 6);

 read_skipped:
	ice->eeprom.gpiomask = ice->eeprom.data[ICE_EEP1_GPIO_MASK];
	ice->eeprom.gpiostate = ice->eeprom.data[ICE_EEP1_GPIO_STATE];
	ice->eeprom.gpiodir = ice->eeprom.data[ICE_EEP1_GPIO_DIR];

	return 0;
}



static int __devinit snd_ice1712_chip_init(struct snd_ice1712 *ice)
{
	outb(ICE1712_RESET | ICE1712_NATIVE, ICEREG(ice, CONTROL));
	udelay(200);
	outb(ICE1712_NATIVE, ICEREG(ice, CONTROL));
	udelay(200);
	if (ice->eeprom.subvendor == ICE1712_SUBDEVICE_DMX6FIRE &&
	    !ice->dxr_enable)
		/*  Set eeprom value to limit active ADCs and DACs to 6;
		 *  Also disable AC97 as no hardware in standard 6fire card/box
		 *  Note: DXR extensions are not currently supported
		 */
		ice->eeprom.data[ICE_EEP1_CODEC] = 0x3a;
	pci_write_config_byte(ice->pci, 0x60, ice->eeprom.data[ICE_EEP1_CODEC]);
	pci_write_config_byte(ice->pci, 0x61, ice->eeprom.data[ICE_EEP1_ACLINK]);
	pci_write_config_byte(ice->pci, 0x62, ice->eeprom.data[ICE_EEP1_I2SID]);
	pci_write_config_byte(ice->pci, 0x63, ice->eeprom.data[ICE_EEP1_SPDIF]);
	if (ice->eeprom.subvendor != ICE1712_SUBDEVICE_STDSP24) {
		ice->gpio.write_mask = ice->eeprom.gpiomask;
		ice->gpio.direction = ice->eeprom.gpiodir;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK,
				  ice->eeprom.gpiomask);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION,
				  ice->eeprom.gpiodir);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA,
				  ice->eeprom.gpiostate);
	} else {
		ice->gpio.write_mask = 0xc0;
		ice->gpio.direction = 0xff;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, 0xc0);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, 0xff);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA,
				  ICE1712_STDSP24_CLOCK_BIT);
	}
	snd_ice1712_write(ice, ICE1712_IREG_PRO_POWERDOWN, 0);
	if (!(ice->eeprom.data[ICE_EEP1_CODEC] & ICE1712_CFG_NO_CON_AC97)) {
		outb(ICE1712_AC97_WARM, ICEREG(ice, AC97_CMD));
		udelay(100);
		outb(0, ICEREG(ice, AC97_CMD));
		udelay(200);
		snd_ice1712_write(ice, ICE1712_IREG_CONSUMER_POWERDOWN, 0);
	}
	snd_ice1712_set_pro_rate(ice, 48000, 1);

	return 0;
}

int __devinit snd_ice1712_spdif_build_controls(struct snd_ice1712 *ice)
{
	int err;
	struct snd_kcontrol *kctl;

	snd_assert(ice->pcm_pro != NULL, return -EIO);
	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_default, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm_pro->device;
	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_maskc, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm_pro->device;
	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_maskp, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm_pro->device;
	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_stream, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm_pro->device;
	ice->spdif.stream_ctl = kctl;
	return 0;
}


static int __devinit snd_ice1712_build_controls(struct snd_ice1712 *ice)
{
	int err;

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_eeprom, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_pro_internal_clock, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_pro_internal_clock_default, ice));
	if (err < 0)
		return err;

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_pro_rate_locking, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_pro_rate_reset, ice));
	if (err < 0)
		return err;

	if (ice->num_total_dacs > 0) {
		struct snd_kcontrol_new tmp = snd_ice1712_mixer_pro_analog_route;
		tmp.count = ice->num_total_dacs;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&tmp, ice));
		if (err < 0)
			return err;
	}

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_mixer_pro_spdif_route, ice));
	if (err < 0)
		return err;

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_mixer_pro_volume_rate, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_mixer_pro_peak, ice));
	if (err < 0)
		return err;

	return 0;
}

static int snd_ice1712_free(struct snd_ice1712 *ice)
{
	if (! ice->port)
		goto __hw_end;
	/* mask all interrupts */
	outb(0xc0, ICEMT(ice, IRQ));
	outb(0xff, ICEREG(ice, IRQMASK));
	/* --- */
      __hw_end:
	if (ice->irq >= 0) {
		synchronize_irq(ice->irq);
		free_irq(ice->irq, ice);
	}
	if (ice->port)
		pci_release_regions(ice->pci);
	snd_ice1712_akm4xxx_free(ice);
	pci_disable_device(ice->pci);
	kfree(ice);
	return 0;
}

static int snd_ice1712_dev_free(struct snd_device *device)
{
	struct snd_ice1712 *ice = device->device_data;
	return snd_ice1712_free(ice);
}

static int __devinit snd_ice1712_create(struct snd_card *card,
					struct pci_dev *pci,
					const char *modelname,
					int omni,
					int cs8427_timeout,
					int dxr_enable,
					struct snd_ice1712 ** r_ice1712)
{
	struct snd_ice1712 *ice;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_ice1712_dev_free,
	};

	*r_ice1712 = NULL;

        /* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	/* check, if we can restrict PCI DMA transfers to 28 bits */
	if (pci_set_dma_mask(pci, DMA_28BIT_MASK) < 0 ||
	    pci_set_consistent_dma_mask(pci, DMA_28BIT_MASK) < 0) {
		snd_printk(KERN_ERR "architecture does not support 28bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	ice = kzalloc(sizeof(*ice), GFP_KERNEL);
	if (ice == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	ice->omni = omni ? 1 : 0;
	if (cs8427_timeout < 1)
		cs8427_timeout = 1;
	else if (cs8427_timeout > 1000)
		cs8427_timeout = 1000;
	ice->cs8427_timeout = cs8427_timeout;
	ice->dxr_enable = dxr_enable;
	spin_lock_init(&ice->reg_lock);
	mutex_init(&ice->gpio_mutex);
	mutex_init(&ice->i2c_mutex);
	mutex_init(&ice->open_mutex);
	ice->gpio.set_mask = snd_ice1712_set_gpio_mask;
	ice->gpio.set_dir = snd_ice1712_set_gpio_dir;
	ice->gpio.set_data = snd_ice1712_set_gpio_data;
	ice->gpio.get_data = snd_ice1712_get_gpio_data;

	ice->spdif.cs8403_bits =
		ice->spdif.cs8403_stream_bits = (0x01 |	/* consumer format */
						 0x10 |	/* no emphasis */
						 0x20);	/* PCM encoder/decoder */
	ice->card = card;
	ice->pci = pci;
	ice->irq = -1;
	pci_set_master(pci);
	pci_write_config_word(ice->pci, 0x40, 0x807f);
	pci_write_config_word(ice->pci, 0x42, 0x0006);
	snd_ice1712_proc_init(ice);
	synchronize_irq(pci->irq);

	if ((err = pci_request_regions(pci, "ICE1712")) < 0) {
		kfree(ice);
		pci_disable_device(pci);
		return err;
	}
	ice->port = pci_resource_start(pci, 0);
	ice->ddma_port = pci_resource_start(pci, 1);
	ice->dmapath_port = pci_resource_start(pci, 2);
	ice->profi_port = pci_resource_start(pci, 3);

	if (request_irq(pci->irq, snd_ice1712_interrupt, SA_INTERRUPT|SA_SHIRQ,
			"ICE1712", ice)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_ice1712_free(ice);
		return -EIO;
	}
	
	ice->irq = pci->irq;

	if (snd_ice1712_read_eeprom(ice, modelname) < 0) {
		snd_ice1712_free(ice);
		return -EIO;
	}
	if (snd_ice1712_chip_init(ice) < 0) {
		snd_ice1712_free(ice);
		return -EIO;
	}

	/* unmask used interrupts */
	outb(((ice->eeprom.data[ICE_EEP1_CODEC] & ICE1712_CFG_2xMPU401) == 0 ?
	      ICE1712_IRQ_MPU2 : 0) |
	     ((ice->eeprom.data[ICE_EEP1_CODEC] & ICE1712_CFG_NO_CON_AC97) ?
	      ICE1712_IRQ_PBKDS | ICE1712_IRQ_CONCAP | ICE1712_IRQ_CONPBK : 0),
	     ICEREG(ice, IRQMASK));
	outb(0x00, ICEMT(ice, IRQ));

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, ice, &ops)) < 0) {
		snd_ice1712_free(ice);
 		return err;
	}

	snd_card_set_dev(card, &pci->dev);

	*r_ice1712 = ice;
	return 0;
}


/*
 *
 * Registration
 *
 */

static struct snd_ice1712_card_info no_matched __devinitdata;

static int __devinit snd_ice1712_probe(struct pci_dev *pci,
				       const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_ice1712 *ice;
	int pcm_dev = 0, err;
	struct snd_ice1712_card_info **tbl, *c;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "ICE1712");
	strcpy(card->shortname, "ICEnsemble ICE1712");
	
	if ((err = snd_ice1712_create(card, pci, model[dev], omni[dev],
				      cs8427_timeout[dev], dxr_enable[dev],
				      &ice)) < 0) {
		snd_card_free(card);
		return err;
	}

	for (tbl = card_tables; *tbl; tbl++) {
		for (c = *tbl; c->subvendor; c++) {
			if (c->subvendor == ice->eeprom.subvendor) {
				strcpy(card->shortname, c->name);
				if (c->driver) /* specific driver? */
					strcpy(card->driver, c->driver);
				if (c->chip_init) {
					if ((err = c->chip_init(ice)) < 0) {
						snd_card_free(card);
						return err;
					}
				}
				goto __found;
			}
		}
	}
	c = &no_matched;
 __found:

	if ((err = snd_ice1712_pcm_profi(ice, pcm_dev++, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	
	if (ice_has_con_ac97(ice))
		if ((err = snd_ice1712_pcm(ice, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}

	if ((err = snd_ice1712_ac97_mixer(ice)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_ice1712_build_controls(ice)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (c->build_controls) {
		if ((err = c->build_controls(ice)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if (ice_has_con_ac97(ice))
		if ((err = snd_ice1712_pcm_ds(ice, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}

	if (! c->no_mpu401) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_ICE1712,
					       ICEREG(ice, MPU1_CTRL),
					       MPU401_INFO_INTEGRATED,
					       ice->irq, 0,
					       &ice->rmidi[0])) < 0) {
			snd_card_free(card);
			return err;
		}
		if (c->mpu401_1_name)
			/*  Prefered name available in card_info */
			snprintf(ice->rmidi[0]->name,
				 sizeof(ice->rmidi[0]->name),
				 "%s %d", c->mpu401_1_name, card->number);

		if (ice->eeprom.data[ICE_EEP1_CODEC] & ICE1712_CFG_2xMPU401) {
			/*  2nd port used  */
			if ((err = snd_mpu401_uart_new(card, 1, MPU401_HW_ICE1712,
						       ICEREG(ice, MPU2_CTRL),
						       MPU401_INFO_INTEGRATED,
						       ice->irq, 0,
						       &ice->rmidi[1])) < 0) {
				snd_card_free(card);
				return err;
			}
			if (c->mpu401_2_name)
				/*  Prefered name available in card_info */
				snprintf(ice->rmidi[1]->name,
					 sizeof(ice->rmidi[1]->name),
					 "%s %d", c->mpu401_2_name,
					 card->number);
		}
	}

	snd_ice1712_set_input_clock_source(ice, 0);

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, ice->port, ice->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_ice1712_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "ICE1712",
	.id_table = snd_ice1712_ids,
	.probe = snd_ice1712_probe,
	.remove = __devexit_p(snd_ice1712_remove),
};

static int __init alsa_card_ice1712_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_ice1712_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_ice1712_init)
module_exit(alsa_card_ice1712_exit)
