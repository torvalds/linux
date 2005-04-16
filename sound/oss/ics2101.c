/*
 * sound/ics2101.c
 *
 * Driver for the ICS2101 mixer of GUS v3.7.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 * Bartlomiej Zolnierkiewicz : added __init to ics2101_mixer_init()
 */
#include <linux/init.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include <linux/ultrasound.h>

#include "gus.h"
#include "gus_hw.h"

#define MIX_DEVS	(SOUND_MASK_MIC|SOUND_MASK_LINE| \
			 SOUND_MASK_SYNTH| \
  			 SOUND_MASK_CD | SOUND_MASK_VOLUME)

extern int     *gus_osp;
extern int      gus_base;
extern spinlock_t gus_lock;
static int      volumes[ICS_MIXDEVS];
static int      left_fix[ICS_MIXDEVS] =
{1, 1, 1, 2, 1, 2};
static int      right_fix[ICS_MIXDEVS] =
{2, 2, 2, 1, 2, 1};

static int scale_vol(int vol)
{
	/*
	 *  Experimental volume scaling by Risto Kankkunen.
	 *  This should give smoother volume response than just
	 *  a plain multiplication.
	 */
	 
	int e;

	if (vol < 0)
		vol = 0;
	if (vol > 100)
		vol = 100;
	vol = (31 * vol + 50) / 100;
	e = 0;
	if (vol)
	{
		while (vol < 16)
		{
			vol <<= 1;
			e--;
		}
		vol -= 16;
		e += 7;
	}
	return ((e << 4) + vol);
}

static void write_mix(int dev, int chn, int vol)
{
	int *selector;
	unsigned long flags;
	int ctrl_addr = dev << 3;
	int attn_addr = dev << 3;

	vol = scale_vol(vol);

	if (chn == CHN_LEFT)
	{
		selector = left_fix;
		ctrl_addr |= 0x00;
		attn_addr |= 0x02;
	}
	else
	{
		selector = right_fix;
		ctrl_addr |= 0x01;
		attn_addr |= 0x03;
	}

	spin_lock_irqsave(&gus_lock, flags);
	outb((ctrl_addr), u_MixSelect);
	outb((selector[dev]), u_MixData);
	outb((attn_addr), u_MixSelect);
	outb(((unsigned char) vol), u_MixData);
	spin_unlock_irqrestore(&gus_lock,flags);
}

static int set_volumes(int dev, int vol)
{
	int left = vol & 0x00ff;
	int right = (vol >> 8) & 0x00ff;

	if (left < 0)
		left = 0;
	if (left > 100)
		left = 100;
	if (right < 0)
		right = 0;
	if (right > 100)
		right = 100;

	write_mix(dev, CHN_LEFT, left);
	write_mix(dev, CHN_RIGHT, right);

	vol = left + (right << 8);
	volumes[dev] = vol;
	return vol;
}

static int ics2101_mixer_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	int val;
	
	if (((cmd >> 8) & 0xff) == 'M') {
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
			
			if (get_user(val, (int __user *)arg))
				return -EFAULT;
			switch (cmd & 0xff) {
			case SOUND_MIXER_RECSRC:
				return gus_default_mixer_ioctl(dev, cmd, arg);

			case SOUND_MIXER_MIC:
				val = set_volumes(DEV_MIC, val);
				break;
				
			case SOUND_MIXER_CD:
				val = set_volumes(DEV_CD, val);
				break;

			case SOUND_MIXER_LINE:
				val = set_volumes(DEV_LINE, val);
				break;

			case SOUND_MIXER_SYNTH:
				val = set_volumes(DEV_GF1, val);
				break;

			case SOUND_MIXER_VOLUME:
				val = set_volumes(DEV_VOL, val);
				break;

			default:
				return -EINVAL;
			}
			return put_user(val, (int __user *)arg);
		} else {
			switch (cmd & 0xff) {
				/*
				 * Return parameters
				 */
			case SOUND_MIXER_RECSRC:
				return gus_default_mixer_ioctl(dev, cmd, arg);

			case SOUND_MIXER_DEVMASK:
				val = MIX_DEVS; 
				break;

			case SOUND_MIXER_STEREODEVS:
				val = SOUND_MASK_LINE | SOUND_MASK_CD | SOUND_MASK_SYNTH | SOUND_MASK_VOLUME | SOUND_MASK_MIC; 
				break;

			case SOUND_MIXER_RECMASK:
				val = SOUND_MASK_MIC | SOUND_MASK_LINE; 
				break;
				
			case SOUND_MIXER_CAPS:
				val = 0; 
				break;

			case SOUND_MIXER_MIC:
				val = volumes[DEV_MIC];
				break;
				
			case SOUND_MIXER_LINE:
				val = volumes[DEV_LINE];
				break;

			case SOUND_MIXER_CD:
				val = volumes[DEV_CD];
				break;

			case SOUND_MIXER_VOLUME:
				val = volumes[DEV_VOL];
				break;

			case SOUND_MIXER_SYNTH:
				val = volumes[DEV_GF1]; 
				break;

			default:
				return -EINVAL;
			}
			return put_user(val, (int __user *)arg);
		}
	}
	return -EINVAL;
}

static struct mixer_operations ics2101_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "ICS2101",
	.name	= "ICS2101 Multimedia Mixer",
	.ioctl	= ics2101_mixer_ioctl
};

int __init ics2101_mixer_init(void)
{
	int i;
	int n;

	if ((n = sound_alloc_mixerdev()) != -1)
	{
		mixer_devs[n] = &ics2101_mixer_operations;

		/*
		 * Some GUS v3.7 cards had some channels flipped. Disable
		 * the flipping feature if the model id is other than 5.
		 */

		if (inb(u_MixSelect) != 5)
		{
			for (i = 0; i < ICS_MIXDEVS; i++)
				left_fix[i] = 1;
			for (i = 0; i < ICS_MIXDEVS; i++)
				right_fix[i] = 2;
		}
		set_volumes(DEV_GF1, 0x5a5a);
		set_volumes(DEV_CD, 0x5a5a);
		set_volumes(DEV_MIC, 0x0000);
		set_volumes(DEV_LINE, 0x5a5a);
		set_volumes(DEV_VOL, 0x5a5a);
		set_volumes(DEV_UNUSED, 0x0000);
	}
	return n;
}
