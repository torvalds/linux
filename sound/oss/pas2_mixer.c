
/*
 * sound/pas2_mixer.c
 *
 * Mixer routines for the Pro Audio Spectrum cards.
 */

/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 * Bartlomiej Zolnierkiewicz : added __init to pas_init_mixer()
 */
#include <linux/init.h>
#include "sound_config.h"

#include "pas2.h"

#ifndef DEB
#define DEB(what)		/* (what) */
#endif

extern int      pas_translate_code;
extern char     pas_model;
extern int     *pas_osp;
extern int      pas_audiodev;

static int      rec_devices = (SOUND_MASK_MIC);		/* Default recording source */
static int      mode_control;

#define POSSIBLE_RECORDING_DEVICES	(SOUND_MASK_SYNTH | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_ALTPCM)

#define SUPPORTED_MIXER_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_ALTPCM | SOUND_MASK_IMIX | \
					 SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_RECLEV)

static int     *levels;

static int      default_levels[32] =
{
	0x3232,			/* Master Volume */
	0x3232,			/* Bass */
	0x3232,			/* Treble */
	0x5050,			/* FM */
	0x4b4b,			/* PCM */
	0x3232,			/* PC Speaker */
	0x4b4b,			/* Ext Line */
	0x4b4b,			/* Mic */
	0x4b4b,			/* CD */
	0x6464,			/* Recording monitor */
	0x4b4b,			/* SB PCM */
	0x6464			/* Recording level */
};

void
mix_write(unsigned char data, int ioaddr)
{
	/*
	 * The Revision D cards have a problem with their MVA508 interface. The
	 * kludge-o-rama fix is to make a 16-bit quantity with identical LSB and
	 * MSBs out of the output byte and to do a 16-bit out to the mixer port -
	 * 1. We need to do this because it isn't timing problem but chip access
	 * sequence problem.
	 */

	if (pas_model == 4)
	  {
		  outw(data | (data << 8), (ioaddr + pas_translate_code) - 1);
		  outb((0x80), 0);
	} else
		pas_write(data, ioaddr);
}

static int
mixer_output(int right_vol, int left_vol, int div, int bits,
	     int mixer)		/* Input or output mixer */
{
	int             left = left_vol * div / 100;
	int             right = right_vol * div / 100;


	if (bits & 0x10)
	  {
		  left |= mixer;
		  right |= mixer;
	  }
	if (bits == 0x03 || bits == 0x04)
	  {
		  mix_write(0x80 | bits, 0x078B);
		  mix_write(left, 0x078B);
		  right_vol = left_vol;
	} else
	  {
		  mix_write(0x80 | 0x20 | bits, 0x078B);
		  mix_write(left, 0x078B);
		  mix_write(0x80 | 0x40 | bits, 0x078B);
		  mix_write(right, 0x078B);
	  }

	return (left_vol | (right_vol << 8));
}

static void
set_mode(int new_mode)
{
	mix_write(0x80 | 0x05, 0x078B);
	mix_write(new_mode, 0x078B);

	mode_control = new_mode;
}

static int
pas_mixer_set(int whichDev, unsigned int level)
{
	int             left, right, devmask, changed, i, mixer = 0;

	DEB(printk("static int pas_mixer_set(int whichDev = %d, unsigned int level = %X)\n", whichDev, level));

	left = level & 0x7f;
	right = (level & 0x7f00) >> 8;

	if (whichDev < SOUND_MIXER_NRDEVICES) {
		if ((1 << whichDev) & rec_devices)
			mixer = 0x20;
		else
			mixer = 0x00;
	}

	switch (whichDev)
	  {
	  case SOUND_MIXER_VOLUME:	/* Master volume (0-63) */
		  levels[whichDev] = mixer_output(right, left, 63, 0x01, 0);
		  break;

		  /*
		   * Note! Bass and Treble are mono devices. Will use just the left
		   * channel.
		   */
	  case SOUND_MIXER_BASS:	/* Bass (0-12) */
		  levels[whichDev] = mixer_output(right, left, 12, 0x03, 0);
		  break;
	  case SOUND_MIXER_TREBLE:	/* Treble (0-12) */
		  levels[whichDev] = mixer_output(right, left, 12, 0x04, 0);
		  break;

	  case SOUND_MIXER_SYNTH:	/* Internal synthesizer (0-31) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x00, mixer);
		  break;
	  case SOUND_MIXER_PCM:	/* PAS PCM (0-31) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x05, mixer);
		  break;
	  case SOUND_MIXER_ALTPCM:	/* SB PCM (0-31) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x07, mixer);
		  break;
	  case SOUND_MIXER_SPEAKER:	/* PC speaker (0-31) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x06, mixer);
		  break;
	  case SOUND_MIXER_LINE:	/* External line (0-31) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x02, mixer);
		  break;
	  case SOUND_MIXER_CD:	/* CD (0-31) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x03, mixer);
		  break;
	  case SOUND_MIXER_MIC:	/* External microphone (0-31) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x04, mixer);
		  break;
	  case SOUND_MIXER_IMIX:	/* Recording monitor (0-31) (Output mixer only) */
		  levels[whichDev] = mixer_output(right, left, 31, 0x10 | 0x01,
						  0x00);
		  break;
	  case SOUND_MIXER_RECLEV:	/* Recording level (0-15) */
		  levels[whichDev] = mixer_output(right, left, 15, 0x02, 0);
		  break;


	  case SOUND_MIXER_RECSRC:
		  devmask = level & POSSIBLE_RECORDING_DEVICES;

		  changed = devmask ^ rec_devices;
		  rec_devices = devmask;

		  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
			  if (changed & (1 << i))
			    {
				    pas_mixer_set(i, levels[i]);
			    }
		  return rec_devices;
		  break;

	  default:
		  return -EINVAL;
	  }

	return (levels[whichDev]);
}

/*****/

static void
pas_mixer_reset(void)
{
	int             foo;

	DEB(printk("pas2_mixer.c: void pas_mixer_reset(void)\n"));

	for (foo = 0; foo < SOUND_MIXER_NRDEVICES; foo++)
		pas_mixer_set(foo, levels[foo]);

	set_mode(0x04 | 0x01);
}

static int pas_mixer_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	int level,v ;
	int __user *p = (int __user *)arg;

	DEB(printk("pas2_mixer.c: int pas_mixer_ioctl(unsigned int cmd = %X, unsigned int arg = %X)\n", cmd, arg));
	if (cmd == SOUND_MIXER_PRIVATE1) { /* Set loudness bit */
		if (get_user(level, p))
			return -EFAULT;
		if (level == -1)  /* Return current settings */
			level = (mode_control & 0x04);
		else {
			mode_control &= ~0x04;
			if (level)
				mode_control |= 0x04;
			set_mode(mode_control);
		}
		level = !!level;
		return put_user(level, p);
	}
	if (cmd == SOUND_MIXER_PRIVATE2) { /* Set enhance bit */
		if (get_user(level, p))
			return -EFAULT;
		if (level == -1) { /* Return current settings */
			if (!(mode_control & 0x03))
				level = 0;
			else
				level = ((mode_control & 0x03) + 1) * 20;
		} else {
			int i = 0;
			
			level &= 0x7f;
			if (level)
				i = (level / 20) - 1;
			mode_control &= ~0x03;
			mode_control |= i & 0x03;
			set_mode(mode_control);
			if (i)
				i = (i + 1) * 20;
			level = i;
		}
		return put_user(level, p);
	}
	if (cmd == SOUND_MIXER_PRIVATE3) { /* Set mute bit */
		if (get_user(level, p))
			return -EFAULT;
		if (level == -1)	/* Return current settings */
			level = !(pas_read(0x0B8A) & 0x20);
		else {
			if (level)
				pas_write(pas_read(0x0B8A) & (~0x20), 0x0B8A);
			else
				pas_write(pas_read(0x0B8A) | 0x20, 0x0B8A);

			level = !(pas_read(0x0B8A) & 0x20);
		}
		return put_user(level, p);
	}
	if (((cmd >> 8) & 0xff) == 'M') {
		if (get_user(v, p))
			return -EFAULT;
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
			v = pas_mixer_set(cmd & 0xff, v);
		} else {
			switch (cmd & 0xff) {
			case SOUND_MIXER_RECSRC:
				v = rec_devices;
				break;
				
			case SOUND_MIXER_STEREODEVS:
				v = SUPPORTED_MIXER_DEVICES & ~(SOUND_MASK_BASS | SOUND_MASK_TREBLE);
				break;
				
			case SOUND_MIXER_DEVMASK:
				v = SUPPORTED_MIXER_DEVICES;
				break;
				
			case SOUND_MIXER_RECMASK:
				v = POSSIBLE_RECORDING_DEVICES & SUPPORTED_MIXER_DEVICES;
				break;
				
			case SOUND_MIXER_CAPS:
				v = 0;	/* No special capabilities */
				break;
				
			default:
				v = levels[cmd & 0xff];
				break;
			}
		}
		return put_user(v, p);
	}
	return -EINVAL;
}

static struct mixer_operations pas_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "PAS16",
	.name	= "Pro Audio Spectrum 16",
	.ioctl	= pas_mixer_ioctl
};

int __init
pas_init_mixer(void)
{
	int             d;

	levels = load_mixer_volumes("PAS16_1", default_levels, 1);

	pas_mixer_reset();

	if ((d = sound_alloc_mixerdev()) != -1)
	  {
		  audio_devs[pas_audiodev]->mixer_dev = d;
		  mixer_devs[d] = &pas_mixer_operations;
	  }
	return 1;
}
