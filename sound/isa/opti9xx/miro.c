/*
 *   ALSA soundcard driver for Miro miroSOUND PCM1 pro
 *                                  miroSOUND PCM12
 *                                  miroSOUND PCM20 Radio
 *
 *   Copyright (C) 2004-2005 Martin Langer <martin-langer@gmx.de>
 *
 *   Based on OSS ACI and ALSA OPTi9xx drivers
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
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/moduleparam.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/opl4.h>
#include <sound/control.h>
#include <sound/info.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>
#include "miro.h"

MODULE_AUTHOR("Martin Langer <martin-langer@gmx.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Miro miroSOUND PCM1 pro, PCM12, PCM20 Radio");
MODULE_SUPPORTED_DEVICE("{{Miro,miroSOUND PCM1 pro}, "
			"{Miro,miroSOUND PCM12}, "
			"{Miro,miroSOUND PCM20 Radio}}");

static int index = SNDRV_DEFAULT_IDX1;		/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;		/* ID for this card */
static long port = SNDRV_DEFAULT_PORT1; 	/* 0x530,0xe80,0xf40,0x604 */
static long mpu_port = SNDRV_DEFAULT_PORT1;	/* 0x300,0x310,0x320,0x330 */
static long fm_port = SNDRV_DEFAULT_PORT1;	/* 0x388 */
static int irq = SNDRV_DEFAULT_IRQ1;		/* 5,7,9,10,11 */
static int mpu_irq = SNDRV_DEFAULT_IRQ1;	/* 5,7,9,10 */
static int dma1 = SNDRV_DEFAULT_DMA1;		/* 0,1,3 */
static int dma2 = SNDRV_DEFAULT_DMA1;		/* 0,1,3 */
static int wss;
static int ide;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for miro soundcard.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for miro soundcard.");
module_param(port, long, 0444);
MODULE_PARM_DESC(port, "WSS port # for miro driver.");
module_param(mpu_port, long, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for miro driver.");
module_param(fm_port, long, 0444);
MODULE_PARM_DESC(fm_port, "FM Port # for miro driver.");
module_param(irq, int, 0444);
MODULE_PARM_DESC(irq, "WSS irq # for miro driver.");
module_param(mpu_irq, int, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 irq # for miro driver.");
module_param(dma1, int, 0444);
MODULE_PARM_DESC(dma1, "1st dma # for miro driver.");
module_param(dma2, int, 0444);
MODULE_PARM_DESC(dma2, "2nd dma # for miro driver.");
module_param(wss, int, 0444);
MODULE_PARM_DESC(wss, "wss mode");
module_param(ide, int, 0444);
MODULE_PARM_DESC(ide, "enable ide port");

#define OPTi9XX_HW_DETECT	0
#define OPTi9XX_HW_82C928	1
#define OPTi9XX_HW_82C929	2
#define OPTi9XX_HW_82C924	3
#define OPTi9XX_HW_82C925	4
#define OPTi9XX_HW_82C930	5
#define OPTi9XX_HW_82C931	6
#define OPTi9XX_HW_82C933	7
#define OPTi9XX_HW_LAST		OPTi9XX_HW_82C933

#define OPTi9XX_MC_REG(n)	n


struct snd_miro {
	unsigned short hardware;
	unsigned char password;
	char name[7];

	struct resource *res_mc_base;
	struct resource *res_aci_port;

	unsigned long mc_base;
	unsigned long mc_base_size;
	unsigned long pwd_reg;

	spinlock_t lock;
	struct snd_card *card;
	struct snd_pcm *pcm;

	long wss_base;
	int irq;
	int dma1;
	int dma2;

	long fm_port;

	long mpu_port;
	int mpu_irq;

	unsigned long aci_port;
	int aci_vendor;
	int aci_product;
	int aci_version;
	int aci_amp;
	int aci_preamp;
	int aci_solomode;

	struct mutex aci_mutex;
};

static void snd_miro_proc_init(struct snd_miro * miro);

static char * snd_opti9xx_names[] = {
	"unkown",
	"82C928", "82C929",
	"82C924", "82C925",
	"82C930", "82C931", "82C933"
};

/* 
 *  ACI control
 */

static int aci_busy_wait(struct snd_miro * miro)
{
	long timeout;
	unsigned char byte;

	for (timeout = 1; timeout <= ACI_MINTIME+30; timeout++) {
		if (((byte=inb(miro->aci_port + ACI_REG_BUSY)) & 1) == 0) {
			if (timeout >= ACI_MINTIME)
				snd_printd("aci ready in round %ld.\n",
					   timeout-ACI_MINTIME);
			return byte;
		}
		if (timeout >= ACI_MINTIME) {
			long out=10*HZ;
			switch (timeout-ACI_MINTIME) {
			case 0 ... 9:
				out /= 10;
			case 10 ... 19:
				out /= 10;
			case 20 ... 30:
				out /= 10;
			default:
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(out);
				break;
			}
		}
	}
	snd_printk(KERN_ERR "aci_busy_wait() time out\n");
	return -EBUSY;
}

static inline int aci_write(struct snd_miro * miro, unsigned char byte)
{
	if (aci_busy_wait(miro) >= 0) {
		outb(byte, miro->aci_port + ACI_REG_COMMAND);
		return 0;
	} else {
		snd_printk(KERN_ERR "aci busy, aci_write(0x%x) stopped.\n", byte);
		return -EBUSY;
	}
}

static inline int aci_read(struct snd_miro * miro)
{
	unsigned char byte;

	if (aci_busy_wait(miro) >= 0) {
		byte=inb(miro->aci_port + ACI_REG_STATUS);
		return byte;
	} else {
		snd_printk(KERN_ERR "aci busy, aci_read() stopped.\n");
		return -EBUSY;
	}
}

static int aci_cmd(struct snd_miro * miro, int write1, int write2, int write3)
{
	int write[] = {write1, write2, write3};
	int value, i;

	if (mutex_lock_interruptible(&miro->aci_mutex))
		return -EINTR;

	for (i=0; i<3; i++) {
		if (write[i]< 0 || write[i] > 255)
			break;
		else {
			value = aci_write(miro, write[i]);
			if (value < 0)
				goto out;
		}
	}

	value = aci_read(miro);

out:	mutex_unlock(&miro->aci_mutex);
	return value;
}

static int aci_getvalue(struct snd_miro * miro, unsigned char index)
{
	return aci_cmd(miro, ACI_STATUS, index, -1);
}

static int aci_setvalue(struct snd_miro * miro, unsigned char index, int value)
{
	return aci_cmd(miro, index, value, -1);
}

/*
 *  MIXER part
 */

#define snd_miro_info_capture	snd_ctl_boolean_mono_info

static int snd_miro_get_capture(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	int value;

	if ((value = aci_getvalue(miro, ACI_S_GENERAL)) < 0) {
		snd_printk(KERN_ERR "snd_miro_get_capture() failed: %d\n", value);
		return value;
	}

	ucontrol->value.integer.value[0] = value & 0x20;

	return 0;
}

static int snd_miro_put_capture(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	int change, value, error;

	value = !(ucontrol->value.integer.value[0]);

	if ((error = aci_setvalue(miro, ACI_SET_SOLOMODE, value)) < 0) {
		snd_printk(KERN_ERR "snd_miro_put_capture() failed: %d\n", error);
		return error;
	}

	change = (value != miro->aci_solomode);
	miro->aci_solomode = value;
	
	return change;
}

static int snd_miro_info_preamp(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 3;

	return 0;
}

static int snd_miro_get_preamp(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	int value;

	if (miro->aci_version <= 176) {

		/* 
		   OSS says it's not readable with versions < 176.
		   But it doesn't work on my card,
		   which is a PCM12 with aci_version = 176.
		*/

		ucontrol->value.integer.value[0] = miro->aci_preamp;
		return 0;
	}

	if ((value = aci_getvalue(miro, ACI_GET_PREAMP)) < 0) {
		snd_printk(KERN_ERR "snd_miro_get_preamp() failed: %d\n", value);
		return value;
	}
	
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int snd_miro_put_preamp(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	int error, value, change;

	value = ucontrol->value.integer.value[0];

	if ((error = aci_setvalue(miro, ACI_SET_PREAMP, value)) < 0) {
		snd_printk(KERN_ERR "snd_miro_put_preamp() failed: %d\n", error);
		return error;
	}

	change = (value != miro->aci_preamp);
	miro->aci_preamp = value;

	return change;
}

#define snd_miro_info_amp	snd_ctl_boolean_mono_info

static int snd_miro_get_amp(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = miro->aci_amp;

	return 0;
}

static int snd_miro_put_amp(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	int error, value, change;

	value = ucontrol->value.integer.value[0];

	if ((error = aci_setvalue(miro, ACI_SET_POWERAMP, value)) < 0) {
		snd_printk(KERN_ERR "snd_miro_put_amp() to %d failed: %d\n", value, error);
		return error;
	}

	change = (value != miro->aci_amp);
	miro->aci_amp = value;

	return change;
}

#define MIRO_DOUBLE(ctl_name, ctl_index, get_right_reg, set_right_reg) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .name = ctl_name, \
  .index = ctl_index, \
  .info = snd_miro_info_double, \
  .get = snd_miro_get_double, \
  .put = snd_miro_put_double, \
  .private_value = get_right_reg | (set_right_reg << 8) \
}

static int snd_miro_info_double(struct snd_kcontrol *kcontrol, 
				struct snd_ctl_elem_info *uinfo)
{
	int reg = kcontrol->private_value & 0xff;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;

	if ((reg >= ACI_GET_EQ1) && (reg <= ACI_GET_EQ7)) {

		/* equalizer elements */

		uinfo->value.integer.min = - 0x7f;
		uinfo->value.integer.max = 0x7f;
	} else {

		/* non-equalizer elements */

		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 0x20;
	}

	return 0;
}

static int snd_miro_get_double(struct snd_kcontrol *kcontrol, 
			       struct snd_ctl_elem_value *uinfo)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	int left_val, right_val;

	int right_reg = kcontrol->private_value & 0xff;
	int left_reg = right_reg + 1;

	if ((right_val = aci_getvalue(miro, right_reg)) < 0) {
		snd_printk(KERN_ERR "aci_getvalue(%d) failed: %d\n", right_reg, right_val);
		return right_val;
	}

	if ((left_val = aci_getvalue(miro, left_reg)) < 0) {
		snd_printk(KERN_ERR "aci_getvalue(%d) failed: %d\n", left_reg, left_val);
		return left_val;
	}

	if ((right_reg >= ACI_GET_EQ1) && (right_reg <= ACI_GET_EQ7)) {

		/* equalizer elements */

		if (left_val < 0x80) {
			uinfo->value.integer.value[0] = left_val;
		} else {
			uinfo->value.integer.value[0] = 0x80 - left_val;
		}

		if (right_val < 0x80) {
			uinfo->value.integer.value[1] = right_val;
		} else {
			uinfo->value.integer.value[1] = 0x80 - right_val;
		}

	} else {

		/* non-equalizer elements */

		uinfo->value.integer.value[0] = 0x20 - left_val;
		uinfo->value.integer.value[1] = 0x20 - right_val;
	}

	return 0;
}

static int snd_miro_put_double(struct snd_kcontrol *kcontrol, 
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_miro *miro = snd_kcontrol_chip(kcontrol);
	int left, right, left_old, right_old;
	int setreg_left, setreg_right, getreg_left, getreg_right;
	int change, error;

	left = ucontrol->value.integer.value[0];
	right = ucontrol->value.integer.value[1];

	setreg_right = (kcontrol->private_value >> 8) & 0xff;
	if (setreg_right == ACI_SET_MASTER) {
		setreg_left = setreg_right + 1;
	} else {
		setreg_left = setreg_right + 8;
	}

	getreg_right = kcontrol->private_value & 0xff;
	getreg_left = getreg_right + 1;

	if ((left_old = aci_getvalue(miro, getreg_left)) < 0) {
		snd_printk(KERN_ERR "aci_getvalue(%d) failed: %d\n", getreg_left, left_old);
		return left_old;
	}

	if ((right_old = aci_getvalue(miro, getreg_right)) < 0) {
		snd_printk(KERN_ERR "aci_getvalue(%d) failed: %d\n", getreg_right, right_old);
		return right_old;
	}

	if ((getreg_right >= ACI_GET_EQ1) && (getreg_right <= ACI_GET_EQ7)) {

		/* equalizer elements */

		if (left_old > 0x80) 
			left_old = 0x80 - left_old;
		if (right_old > 0x80) 
			right_old = 0x80 - right_old;

		if (left >= 0) {
			if ((error = aci_setvalue(miro, setreg_left, left)) < 0) {
				snd_printk(KERN_ERR "aci_setvalue(%d) failed: %d\n",
					   left, error);
				return error;
			}
		} else {
			if ((error = aci_setvalue(miro, setreg_left, 0x80 - left)) < 0) {
				snd_printk(KERN_ERR "aci_setvalue(%d) failed: %d\n",
					   0x80 - left, error);
				return error;
			}
		}

		if (right >= 0) {
			if ((error = aci_setvalue(miro, setreg_right, right)) < 0) {
				snd_printk(KERN_ERR "aci_setvalue(%d) failed: %d\n",
					   right, error);
				return error;
			}
		} else {
			if ((error = aci_setvalue(miro, setreg_right, 0x80 - right)) < 0) {
				snd_printk(KERN_ERR "aci_setvalue(%d) failed: %d\n",
					   0x80 - right, error);
				return error;
			}
		}

	} else {

		/* non-equalizer elements */

		left_old = 0x20 - left_old;
		right_old = 0x20 - right_old;

		if ((error = aci_setvalue(miro, setreg_left, 0x20 - left)) < 0) {
			snd_printk(KERN_ERR "aci_setvalue(%d) failed: %d\n",
				   0x20 - left, error);
			return error;
		}
		if ((error = aci_setvalue(miro, setreg_right, 0x20 - right)) < 0) {
			snd_printk(KERN_ERR "aci_setvalue(%d) failed: %d\n",
				   0x20 - right, error);
			return error;
		}
	}

	change = (left != left_old) || (right != right_old);

	return change;
}

static struct snd_kcontrol_new snd_miro_controls[] __devinitdata = {
MIRO_DOUBLE("Master Playback Volume", 0, ACI_GET_MASTER, ACI_SET_MASTER),
MIRO_DOUBLE("Mic Playback Volume", 1, ACI_GET_MIC, ACI_SET_MIC),
MIRO_DOUBLE("Line Playback Volume", 1, ACI_GET_LINE, ACI_SET_LINE),
MIRO_DOUBLE("CD Playback Volume", 0, ACI_GET_CD, ACI_SET_CD),
MIRO_DOUBLE("Synth Playback Volume", 0, ACI_GET_SYNTH, ACI_SET_SYNTH),
MIRO_DOUBLE("PCM Playback Volume", 1, ACI_GET_PCM, ACI_SET_PCM),
MIRO_DOUBLE("Aux Playback Volume", 2, ACI_GET_LINE2, ACI_SET_LINE2),
};

/* Equalizer with seven bands (only PCM20) 
   from -12dB up to +12dB on each band */
static struct snd_kcontrol_new snd_miro_eq_controls[] __devinitdata = {
MIRO_DOUBLE("Tone Control - 28 Hz", 0, ACI_GET_EQ1, ACI_SET_EQ1),
MIRO_DOUBLE("Tone Control - 160 Hz", 0, ACI_GET_EQ2, ACI_SET_EQ2),
MIRO_DOUBLE("Tone Control - 400 Hz", 0, ACI_GET_EQ3, ACI_SET_EQ3),
MIRO_DOUBLE("Tone Control - 1 kHz", 0, ACI_GET_EQ4, ACI_SET_EQ4),
MIRO_DOUBLE("Tone Control - 2.5 kHz", 0, ACI_GET_EQ5, ACI_SET_EQ5),
MIRO_DOUBLE("Tone Control - 6.3 kHz", 0, ACI_GET_EQ6, ACI_SET_EQ6),
MIRO_DOUBLE("Tone Control - 16 kHz", 0, ACI_GET_EQ7, ACI_SET_EQ7),
};

static struct snd_kcontrol_new snd_miro_radio_control[] __devinitdata = {
MIRO_DOUBLE("Radio Playback Volume", 0, ACI_GET_LINE1, ACI_SET_LINE1),
};

static struct snd_kcontrol_new snd_miro_line_control[] __devinitdata = {
MIRO_DOUBLE("Line Playback Volume", 2, ACI_GET_LINE1, ACI_SET_LINE1),
};

static struct snd_kcontrol_new snd_miro_preamp_control[] __devinitdata = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Mic Boost",
	.index = 1,
	.info = snd_miro_info_preamp,
	.get = snd_miro_get_preamp,
	.put = snd_miro_put_preamp,
}};

static struct snd_kcontrol_new snd_miro_amp_control[] __devinitdata = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Line Boost",
	.index = 0,
	.info = snd_miro_info_amp,
	.get = snd_miro_get_amp,
	.put = snd_miro_put_amp,
}};

static struct snd_kcontrol_new snd_miro_capture_control[] __devinitdata = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Capture Switch",
	.index = 0,
	.info = snd_miro_info_capture,
	.get = snd_miro_get_capture,
	.put = snd_miro_put_capture,
}};

static unsigned char aci_init_values[][2] __devinitdata = {
	{ ACI_SET_MUTE, 0x00 },
	{ ACI_SET_POWERAMP, 0x00 },
	{ ACI_SET_PREAMP, 0x00 },
	{ ACI_SET_SOLOMODE, 0x00 },
	{ ACI_SET_MIC + 0, 0x20 },
	{ ACI_SET_MIC + 8, 0x20 },
	{ ACI_SET_LINE + 0, 0x20 },
	{ ACI_SET_LINE + 8, 0x20 },
	{ ACI_SET_CD + 0, 0x20 },
	{ ACI_SET_CD + 8, 0x20 },
	{ ACI_SET_PCM + 0, 0x20 },
	{ ACI_SET_PCM + 8, 0x20 },
	{ ACI_SET_LINE1 + 0, 0x20 },
	{ ACI_SET_LINE1 + 8, 0x20 },
	{ ACI_SET_LINE2 + 0, 0x20 },
	{ ACI_SET_LINE2 + 8, 0x20 },
	{ ACI_SET_SYNTH + 0, 0x20 },
	{ ACI_SET_SYNTH + 8, 0x20 },
	{ ACI_SET_MASTER + 0, 0x20 },
	{ ACI_SET_MASTER + 1, 0x20 },
};

static int __devinit snd_set_aci_init_values(struct snd_miro *miro)
{
	int idx, error;

	/* enable WSS on PCM1 */

	if ((miro->aci_product == 'A') && wss) {
		if ((error = aci_setvalue(miro, ACI_SET_WSS, wss)) < 0) {
			snd_printk(KERN_ERR "enabling WSS mode failed\n");
			return error;
		}
	}

	/* enable IDE port */

	if (ide) {
		if ((error = aci_setvalue(miro, ACI_SET_IDE, ide)) < 0) {
			snd_printk(KERN_ERR "enabling IDE port failed\n");
			return error;
		}
	}

	/* set common aci values */

	for (idx = 0; idx < ARRAY_SIZE(aci_init_values); idx++)
                if ((error = aci_setvalue(miro, aci_init_values[idx][0], 
					  aci_init_values[idx][1])) < 0) {
			snd_printk(KERN_ERR "aci_setvalue(%d) failed: %d\n", 
				   aci_init_values[idx][0], error);
                        return error;
                }

	miro->aci_amp = 0;
	miro->aci_preamp = 0;
	miro->aci_solomode = 1;

	return 0;
}

static int snd_miro_mixer(struct snd_miro *miro)
{
	struct snd_card *card;
	unsigned int idx;
	int err;

	snd_assert(miro != NULL && miro->card != NULL, return -EINVAL);

	card = miro->card;

	switch (miro->hardware) {
	case OPTi9XX_HW_82C924:
		strcpy(card->mixername, "ACI & OPTi924");
		break;
	case OPTi9XX_HW_82C929:
		strcpy(card->mixername, "ACI & OPTi929");
		break;
	default:
		snd_BUG();
		break;
	}

	for (idx = 0; idx < ARRAY_SIZE(snd_miro_controls); idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_miro_controls[idx], miro))) < 0)
			return err;
	}

	if ((miro->aci_product == 'A') || (miro->aci_product == 'B')) {
		/* PCM1/PCM12 with power-amp and Line 2 */
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_miro_line_control[0], miro))) < 0)
			return err;
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_miro_amp_control[0], miro))) < 0)
			return err;
	}

	if ((miro->aci_product == 'B') || (miro->aci_product == 'C')) {
		/* PCM12/PCM20 with mic-preamp */
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_miro_preamp_control[0], miro))) < 0)
			return err;
		if (miro->aci_version >= 176)
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_miro_capture_control[0], miro))) < 0)
				return err;
	}

	if (miro->aci_product == 'C') {
		/* PCM20 with radio and 7 band equalizer */
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_miro_radio_control[0], miro))) < 0)
			return err;
		for (idx = 0; idx < ARRAY_SIZE(snd_miro_eq_controls); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_miro_eq_controls[idx], miro))) < 0)
				return err;
		}
	}

	return 0;
}

static long snd_legacy_find_free_ioport(long *port_table, long size)
{
	while (*port_table != -1) {
		struct resource *res;
		if ((res = request_region(*port_table, size, 
					  "ALSA test")) != NULL) {
			release_and_free_resource(res);
			return *port_table;
		}
		port_table++;
	}
	return -1;
}

static int __devinit snd_miro_init(struct snd_miro *chip,
				   unsigned short hardware)
{
	static int opti9xx_mc_size[] = {7, 7, 10, 10, 2, 2, 2};

	chip->hardware = hardware;
	strcpy(chip->name, snd_opti9xx_names[hardware]);

	chip->mc_base_size = opti9xx_mc_size[hardware];  

	spin_lock_init(&chip->lock);

	chip->wss_base = -1;
	chip->irq = -1;
	chip->dma1 = -1;
	chip->dma2 = -1;
	chip->fm_port = -1;
	chip->mpu_port = -1;
	chip->mpu_irq = -1;

	switch (hardware) {
	case OPTi9XX_HW_82C929:
		chip->mc_base = 0xf8c;
		chip->password = 0xe3;
		chip->pwd_reg = 3;
		break;

	case OPTi9XX_HW_82C924:
		chip->mc_base = 0xf8c;
		chip->password = 0xe5;
		chip->pwd_reg = 3;
		break;

	default:
		snd_printk(KERN_ERR "sorry, no support for %d\n", hardware);
		return -ENODEV;
	}

	return 0;
}

static unsigned char snd_miro_read(struct snd_miro *chip,
				   unsigned char reg)
{
	unsigned long flags;
	unsigned char retval = 0xff;

	spin_lock_irqsave(&chip->lock, flags);
	outb(chip->password, chip->mc_base + chip->pwd_reg);

	switch (chip->hardware) {
	case OPTi9XX_HW_82C924:
		if (reg > 7) {
			outb(reg, chip->mc_base + 8);
			outb(chip->password, chip->mc_base + chip->pwd_reg);
			retval = inb(chip->mc_base + 9);
			break;
		}

	case OPTi9XX_HW_82C929:
		retval = inb(chip->mc_base + reg);
		break;

	default:
		snd_printk(KERN_ERR "sorry, no support for %d\n", chip->hardware);
	}

	spin_unlock_irqrestore(&chip->lock, flags);
	return retval;
}

static void snd_miro_write(struct snd_miro *chip, unsigned char reg,
			   unsigned char value)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	outb(chip->password, chip->mc_base + chip->pwd_reg);

	switch (chip->hardware) {
	case OPTi9XX_HW_82C924:
		if (reg > 7) {
			outb(reg, chip->mc_base + 8);
			outb(chip->password, chip->mc_base + chip->pwd_reg);
			outb(value, chip->mc_base + 9);
			break;
		}

	case OPTi9XX_HW_82C929:
		outb(value, chip->mc_base + reg);
		break;

	default:
		snd_printk(KERN_ERR "sorry, no support for %d\n", chip->hardware);
	}

	spin_unlock_irqrestore(&chip->lock, flags);
}


#define snd_miro_write_mask(chip, reg, value, mask)	\
	snd_miro_write(chip, reg,			\
		(snd_miro_read(chip, reg) & ~(mask)) | ((value) & (mask)))

/*
 *  Proc Interface
 */

static void snd_miro_proc_read(struct snd_info_entry * entry, 
			       struct snd_info_buffer *buffer)
{
	struct snd_miro *miro = (struct snd_miro *) entry->private_data;
	char* model = "unknown";

	/* miroSOUND PCM1 pro, early PCM12 */

	if ((miro->hardware == OPTi9XX_HW_82C929) &&
	    (miro->aci_vendor == 'm') && 
	    (miro->aci_product == 'A')) {
		switch(miro->aci_version) {
		case 3:
			model = "miroSOUND PCM1 pro";
			break;
		default:
			model = "miroSOUND PCM1 pro / (early) PCM12";
			break;
		}
	}

	/* miroSOUND PCM12, PCM12 (Rev. E), PCM12 pnp */

	if ((miro->hardware == OPTi9XX_HW_82C924) &&
	    (miro->aci_vendor == 'm') && 
	    (miro->aci_product == 'B')) {
		switch(miro->aci_version) {
		case 4:
			model = "miroSOUND PCM12";
			break;
		case 176:
			model = "miroSOUND PCM12 (Rev. E)";
			break;
		default:
			model = "miroSOUND PCM12 / PCM12 pnp";
			break;
		}
	}

	/* miroSOUND PCM20 radio */

	if ((miro->hardware == OPTi9XX_HW_82C924) &&
	    (miro->aci_vendor == 'm') && 
	    (miro->aci_product == 'C')) {
		switch(miro->aci_version) {
		case 7:
			model = "miroSOUND PCM20 radio (Rev. E)";
			break;
		default:
			model = "miroSOUND PCM20 radio";
			break;
		}
	}

	snd_iprintf(buffer, "\nGeneral information:\n");
	snd_iprintf(buffer, "  model   : %s\n", model);
	snd_iprintf(buffer, "  opti    : %s\n", miro->name);
	snd_iprintf(buffer, "  codec   : %s\n", miro->pcm->name);
	snd_iprintf(buffer, "  port    : 0x%lx\n", miro->wss_base);
	snd_iprintf(buffer, "  irq     : %d\n", miro->irq);
	snd_iprintf(buffer, "  dma     : %d,%d\n\n", miro->dma1, miro->dma2);

	snd_iprintf(buffer, "MPU-401:\n");
	snd_iprintf(buffer, "  port    : 0x%lx\n", miro->mpu_port);
	snd_iprintf(buffer, "  irq     : %d\n\n", miro->mpu_irq);

	snd_iprintf(buffer, "ACI information:\n");
	snd_iprintf(buffer, "  vendor  : ");
	switch(miro->aci_vendor) {
	case 'm':
		snd_iprintf(buffer, "Miro\n");
		break;
	default:
		snd_iprintf(buffer, "unknown (0x%x)\n", miro->aci_vendor);
		break;
	}

	snd_iprintf(buffer, "  product : ");
	switch(miro->aci_product) {
	case 'A':
		snd_iprintf(buffer, "miroSOUND PCM1 pro / (early) PCM12\n");
		break;
	case 'B':
		snd_iprintf(buffer, "miroSOUND PCM12\n");
		break;
	case 'C':
		snd_iprintf(buffer, "miroSOUND PCM20 radio\n");
		break;
	default:
		snd_iprintf(buffer, "unknown (0x%x)\n", miro->aci_product);
		break;
	}

	snd_iprintf(buffer, "  firmware: %d (0x%x)\n",
		    miro->aci_version, miro->aci_version);
	snd_iprintf(buffer, "  port    : 0x%lx-0x%lx\n", 
		    miro->aci_port, miro->aci_port+2);
	snd_iprintf(buffer, "  wss     : 0x%x\n", wss);
	snd_iprintf(buffer, "  ide     : 0x%x\n", ide);
	snd_iprintf(buffer, "  solomode: 0x%x\n", miro->aci_solomode);
	snd_iprintf(buffer, "  amp     : 0x%x\n", miro->aci_amp);
	snd_iprintf(buffer, "  preamp  : 0x%x\n", miro->aci_preamp);
}

static void __devinit snd_miro_proc_init(struct snd_miro * miro)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(miro->card, "miro", &entry))
		snd_info_set_text_ops(entry, miro, snd_miro_proc_read);
}

/*
 *  Init
 */

static int __devinit snd_miro_configure(struct snd_miro *chip)
{
	unsigned char wss_base_bits;
	unsigned char irq_bits;
	unsigned char dma_bits;
	unsigned char mpu_port_bits = 0;
	unsigned char mpu_irq_bits;
	unsigned long flags;

	switch (chip->hardware) {
	case OPTi9XX_HW_82C924:
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(6), 0x02, 0x02);
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(1), 0x80, 0x80);
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(2), 0x20, 0x20); /* OPL4 */
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(3), 0xf0, 0xff);
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(5), 0x02, 0x02);
		break;
	case OPTi9XX_HW_82C929:
		/* untested init commands for OPTi929 */
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(1), 0x80, 0x80);
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(2), 0x20, 0x20); /* OPL4 */
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(4), 0x00, 0x0c);
		snd_miro_write_mask(chip, OPTi9XX_MC_REG(5), 0x02, 0x02);
		break;
	default:
		snd_printk(KERN_ERR "chip %d not supported\n", chip->hardware);
		return -EINVAL;
	}

	switch (chip->wss_base) {
	case 0x530:
		wss_base_bits = 0x00;
		break;
	case 0x604:
		wss_base_bits = 0x03;
		break;
	case 0xe80:
		wss_base_bits = 0x01;
		break;
	case 0xf40:
		wss_base_bits = 0x02;
		break;
	default:
		snd_printk(KERN_ERR "WSS port 0x%lx not valid\n", chip->wss_base);
		goto __skip_base;
	}
	snd_miro_write_mask(chip, OPTi9XX_MC_REG(1), wss_base_bits << 4, 0x30);

__skip_base:
	switch (chip->irq) {
	case 5:
		irq_bits = 0x05;
		break;
	case 7:
		irq_bits = 0x01;
		break;
	case 9:
		irq_bits = 0x02;
		break;
	case 10:
		irq_bits = 0x03;
		break;
	case 11:
		irq_bits = 0x04;
		break;
	default:
		snd_printk(KERN_ERR "WSS irq # %d not valid\n", chip->irq);
		goto __skip_resources;
	}

	switch (chip->dma1) {
	case 0:
		dma_bits = 0x01;
		break;
	case 1:
		dma_bits = 0x02;
		break;
	case 3:
		dma_bits = 0x03;
		break;
	default:
		snd_printk(KERN_ERR "WSS dma1 # %d not valid\n", chip->dma1);
		goto __skip_resources;
	}

	if (chip->dma1 == chip->dma2) {
		snd_printk(KERN_ERR "don't want to share dmas\n");
		return -EBUSY;
	}

	switch (chip->dma2) {
	case 0:
	case 1:
		break;
	default:
		snd_printk(KERN_ERR "WSS dma2 # %d not valid\n", chip->dma2);
		goto __skip_resources;
	}
	dma_bits |= 0x04;

	spin_lock_irqsave(&chip->lock, flags);
	outb(irq_bits << 3 | dma_bits, chip->wss_base);
	spin_unlock_irqrestore(&chip->lock, flags);

__skip_resources:
	if (chip->hardware > OPTi9XX_HW_82C928) {
		switch (chip->mpu_port) {
		case 0:
		case -1:
			break;
		case 0x300:
			mpu_port_bits = 0x03;
			break;
		case 0x310:
			mpu_port_bits = 0x02;
			break;
		case 0x320:
			mpu_port_bits = 0x01;
			break;
		case 0x330:
			mpu_port_bits = 0x00;
			break;
		default:
			snd_printk(KERN_ERR "MPU-401 port 0x%lx not valid\n",
				   chip->mpu_port);
			goto __skip_mpu;
		}

		switch (chip->mpu_irq) {
		case 5:
			mpu_irq_bits = 0x02;
			break;
		case 7:
			mpu_irq_bits = 0x03;
			break;
		case 9:
			mpu_irq_bits = 0x00;
			break;
		case 10:
			mpu_irq_bits = 0x01;
			break;
		default:
			snd_printk(KERN_ERR "MPU-401 irq # %d not valid\n",
				   chip->mpu_irq);
			goto __skip_mpu;
		}

		snd_miro_write_mask(chip, OPTi9XX_MC_REG(6),
			(chip->mpu_port <= 0) ? 0x00 :
				0x80 | mpu_port_bits << 5 | mpu_irq_bits << 3,
			0xf8);
	}
__skip_mpu:

	return 0;
}

static int __devinit snd_card_miro_detect(struct snd_card *card,
					  struct snd_miro *chip)
{
	int i, err;
	unsigned char value;

	for (i = OPTi9XX_HW_82C929; i <= OPTi9XX_HW_82C924; i++) {

		if ((err = snd_miro_init(chip, i)) < 0)
			return err;

		if ((chip->res_mc_base = request_region(chip->mc_base, chip->mc_base_size, "OPTi9xx MC")) == NULL)
			continue;

		value = snd_miro_read(chip, OPTi9XX_MC_REG(1));
		if ((value != 0xff) && (value != inb(chip->mc_base + 1)))
			if (value == snd_miro_read(chip, OPTi9XX_MC_REG(1)))
				return 1;

		release_and_free_resource(chip->res_mc_base);
		chip->res_mc_base = NULL;

	}

	return -ENODEV;
}

static int __devinit snd_card_miro_aci_detect(struct snd_card *card,
					      struct snd_miro * miro)
{
	unsigned char regval;
	int i;

	mutex_init(&miro->aci_mutex);

	/* get ACI port from OPTi9xx MC 4 */

	miro->mc_base = 0xf8c;
	regval=inb(miro->mc_base + 4);
	miro->aci_port = (regval & 0x10) ? 0x344: 0x354;

	if ((miro->res_aci_port = request_region(miro->aci_port, 3, "miro aci")) == NULL) {
		snd_printk(KERN_ERR "aci i/o area 0x%lx-0x%lx already used.\n", 
			   miro->aci_port, miro->aci_port+2);
		return -ENOMEM;
	}

        /* force ACI into a known state */
	for (i = 0; i < 3; i++)
		if (aci_cmd(miro, ACI_ERROR_OP, -1, -1) < 0) {
			snd_printk(KERN_ERR "can't force aci into known state.\n");
			return -ENXIO;
		}

	if ((miro->aci_vendor=aci_cmd(miro, ACI_READ_IDCODE, -1, -1)) < 0 ||
	    (miro->aci_product=aci_cmd(miro, ACI_READ_IDCODE, -1, -1)) < 0) {
		snd_printk(KERN_ERR "can't read aci id on 0x%lx.\n", miro->aci_port);
		return -ENXIO;
	}

	if ((miro->aci_version=aci_cmd(miro, ACI_READ_VERSION, -1, -1)) < 0) {
		snd_printk(KERN_ERR "can't read aci version on 0x%lx.\n", 
			   miro->aci_port);
		return -ENXIO;
	}

	if (aci_cmd(miro, ACI_INIT, -1, -1) < 0 ||
	    aci_cmd(miro, ACI_ERROR_OP, ACI_ERROR_OP, ACI_ERROR_OP) < 0 ||
	    aci_cmd(miro, ACI_ERROR_OP, ACI_ERROR_OP, ACI_ERROR_OP) < 0) {
		snd_printk(KERN_ERR "can't initialize aci.\n"); 
		return -ENXIO;
	}

	return 0;
}

static void snd_card_miro_free(struct snd_card *card)
{
	struct snd_miro *miro = card->private_data;
        
	release_and_free_resource(miro->res_aci_port);
	release_and_free_resource(miro->res_mc_base);
}

static int __devinit snd_miro_match(struct device *devptr, unsigned int n)
{
	return 1;
}

static int __devinit snd_miro_probe(struct device *devptr, unsigned int n)
{
	static long possible_ports[] = {0x530, 0xe80, 0xf40, 0x604, -1};
	static long possible_mpu_ports[] = {0x330, 0x300, 0x310, 0x320, -1};
	static int possible_irqs[] = {11, 9, 10, 7, -1};
	static int possible_mpu_irqs[] = {10, 5, 9, 7, -1};
	static int possible_dma1s[] = {3, 1, 0, -1};
	static int possible_dma2s[][2] = {{1,-1}, {0,-1}, {-1,-1}, {0,-1}};

	int error;
	struct snd_miro *miro;
	struct snd_cs4231 *codec;
	struct snd_timer *timer;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_rawmidi *rmidi;

	if (!(card = snd_card_new(index, id, THIS_MODULE,
				  sizeof(struct snd_miro))))
		return -ENOMEM;

	card->private_free = snd_card_miro_free;
	miro = card->private_data;
	miro->card = card;

	if ((error = snd_card_miro_aci_detect(card, miro)) < 0) {
		snd_card_free(card);
		snd_printk(KERN_ERR "unable to detect aci chip\n");
		return -ENODEV;
	}

	/* init proc interface */
	snd_miro_proc_init(miro);

	if ((error = snd_card_miro_detect(card, miro)) < 0) {
		snd_card_free(card);
		snd_printk(KERN_ERR "unable to detect OPTi9xx chip\n");
		return -ENODEV;
	}

	if (! miro->res_mc_base &&
	    (miro->res_mc_base = request_region(miro->mc_base, miro->mc_base_size,
						"miro (OPTi9xx MC)")) == NULL) {
		snd_card_free(card);
		snd_printk(KERN_ERR "request for OPTI9xx MC failed\n");
		return -ENOMEM;
	}

	miro->wss_base = port;
	miro->fm_port = fm_port;
	miro->mpu_port = mpu_port;
	miro->irq = irq;
	miro->mpu_irq = mpu_irq;
	miro->dma1 = dma1;
	miro->dma2 = dma2;

	if (miro->wss_base == SNDRV_AUTO_PORT) {
		if ((miro->wss_base = snd_legacy_find_free_ioport(possible_ports, 4)) < 0) {
			snd_card_free(card);
			snd_printk(KERN_ERR "unable to find a free WSS port\n");
			return -EBUSY;
		}
	}

	if (miro->mpu_port == SNDRV_AUTO_PORT) {
		if ((miro->mpu_port = snd_legacy_find_free_ioport(possible_mpu_ports, 2)) < 0) {
			snd_card_free(card);
			snd_printk(KERN_ERR "unable to find a free MPU401 port\n");
			return -EBUSY;
		}
	}
	if (miro->irq == SNDRV_AUTO_IRQ) {
		if ((miro->irq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			snd_printk(KERN_ERR "unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (miro->mpu_irq == SNDRV_AUTO_IRQ) {
		if ((miro->mpu_irq = snd_legacy_find_free_irq(possible_mpu_irqs)) < 0) {
			snd_card_free(card);
			snd_printk(KERN_ERR "unable to find a free MPU401 IRQ\n");
			return -EBUSY;
		}
	}
	if (miro->dma1 == SNDRV_AUTO_DMA) {
		if ((miro->dma1 = snd_legacy_find_free_dma(possible_dma1s)) < 0) {
			snd_card_free(card);
			snd_printk(KERN_ERR "unable to find a free DMA1\n");
			return -EBUSY;
		}
	}
	if (miro->dma2 == SNDRV_AUTO_DMA) {
		if ((miro->dma2 = snd_legacy_find_free_dma(possible_dma2s[miro->dma1 % 4])) < 0) {
			snd_card_free(card);
			snd_printk(KERN_ERR "unable to find a free DMA2\n");
			return -EBUSY;
		}
	}

	if ((error = snd_miro_configure(miro))) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_cs4231_create(card, miro->wss_base + 4, -1,
				       miro->irq, miro->dma1, miro->dma2,
				       CS4231_HW_AD1845,
				       0,
				       &codec)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_cs4231_pcm(codec, 0, &pcm)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_mixer(codec)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_timer(codec, 0, &timer)) < 0) {
		snd_card_free(card);
		return error;
	}

	miro->pcm = pcm;

	if ((error = snd_miro_mixer(miro)) < 0) {
		snd_card_free(card);
		return error;
	}

	if (miro->aci_vendor == 'm') {
		/* It looks like a miro sound card. */
		switch (miro->aci_product) {
		case 'A':
			sprintf(card->shortname, 
				"miroSOUND PCM1 pro / PCM12");
			break;
		case 'B':
			sprintf(card->shortname, 
				"miroSOUND PCM12");
			break;
		case 'C':
			sprintf(card->shortname, 
				"miroSOUND PCM20 radio");
			break;
		default:
			sprintf(card->shortname, 
				"unknown miro");
			snd_printk(KERN_INFO "unknown miro aci id\n");
			break;
		}
	} else {
		snd_printk(KERN_INFO "found unsupported aci card\n");
		sprintf(card->shortname, "unknown Cardinal Technologies");
	}

	strcpy(card->driver, "miro");
	sprintf(card->longname, "%s: OPTi%s, %s at 0x%lx, irq %d, dma %d&%d",
		card->shortname, miro->name, pcm->name, miro->wss_base + 4,
		miro->irq, miro->dma1, miro->dma2);

	if (miro->mpu_port <= 0 || miro->mpu_port == SNDRV_AUTO_PORT)
		rmidi = NULL;
	else
		if ((error = snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
				miro->mpu_port, 0, miro->mpu_irq, IRQF_DISABLED,
				&rmidi)))
			snd_printk(KERN_WARNING "no MPU-401 device at 0x%lx?\n", miro->mpu_port);

	if (miro->fm_port > 0 && miro->fm_port != SNDRV_AUTO_PORT) {
		struct snd_opl3 *opl3 = NULL;
		struct snd_opl4 *opl4;
		if (snd_opl4_create(card, miro->fm_port, miro->fm_port - 8, 
				    2, &opl3, &opl4) < 0)
			snd_printk(KERN_WARNING "no OPL4 device at 0x%lx\n", miro->fm_port);
	}

	if ((error = snd_set_aci_init_values(miro)) < 0) {
		snd_card_free(card);
                return error;
	}

	snd_card_set_dev(card, devptr);

	if ((error = snd_card_register(card))) {
		snd_card_free(card);
		return error;
	}

	dev_set_drvdata(devptr, card);
	return 0;
}

static int __devexit snd_miro_remove(struct device *devptr, unsigned int dev)
{
	snd_card_free(dev_get_drvdata(devptr));
	dev_set_drvdata(devptr, NULL);
	return 0;
}

#define DEV_NAME "miro"

static struct isa_driver snd_miro_driver = {
	.match		= snd_miro_match,
	.probe		= snd_miro_probe,
	.remove		= __devexit_p(snd_miro_remove),
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= DEV_NAME
	},
};

static int __init alsa_card_miro_init(void)
{
	return isa_register_driver(&snd_miro_driver, 1);
}

static void __exit alsa_card_miro_exit(void)
{
	isa_unregister_driver(&snd_miro_driver);
}

module_init(alsa_card_miro_init)
module_exit(alsa_card_miro_exit)
