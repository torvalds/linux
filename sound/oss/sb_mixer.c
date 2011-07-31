/*
 * sound/oss/sb_mixer.c
 *
 * The low level mixer driver for the Sound Blaster compatible cards.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer				: ioctl code reworked (vmalloc/vfree removed)
 * Rolf Fokkens (Dec 20 1998)	: Moved ESS stuff into sb_ess.[ch]
 * Stanislav Voronyi <stas@esc.kharkov.com>	: Support for AWE 3DSE device (Jun 7 1999)
 */

#include <linux/slab.h>

#include "sound_config.h"

#define __SB_MIXER_C__

#include "sb.h"
#include "sb_mixer.h"

#include "sb_ess.h"

#define SBPRO_RECORDING_DEVICES	(SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD)

/* Same as SB Pro, unless I find otherwise */
#define SGNXPRO_RECORDING_DEVICES SBPRO_RECORDING_DEVICES

#define SBPRO_MIXER_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_VOLUME)

/* SG NX Pro has treble and bass settings on the mixer. The 'speaker'
 * channel is the COVOX/DisneySoundSource emulation volume control
 * on the mixer. It does NOT control speaker volume. Should have own
 * mask eventually?
 */
#define SGNXPRO_MIXER_DEVICES	(SBPRO_MIXER_DEVICES|SOUND_MASK_BASS| \
				 SOUND_MASK_TREBLE|SOUND_MASK_SPEAKER )

#define SB16_RECORDING_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD)

#define SB16_OUTFILTER_DEVICES		(SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD)

#define SB16_MIXER_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | \
					 SOUND_MASK_IGAIN | SOUND_MASK_OGAIN | \
					 SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE | \
					SOUND_MASK_IMIX)

/* These are the only devices that are working at the moment.  Others could
 * be added once they are identified and a method is found to control them.
 */
#define ALS007_MIXER_DEVICES	(SOUND_MASK_SYNTH | SOUND_MASK_LINE | \
				 SOUND_MASK_PCM | SOUND_MASK_MIC | \
				 SOUND_MASK_CD | \
				 SOUND_MASK_VOLUME)

static mixer_tab sbpro_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,	0x22, 7, 4, 0x22, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	0x26, 7, 4, 0x26, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,	0x04, 7, 4, 0x04, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x2e, 7, 4, 0x2e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,	0x0a, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_CD,		0x28, 7, 4, 0x28, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x00, 0, 0, 0x00, 0, 0)
};

static mixer_tab sb16_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,	0x30, 7, 5, 0x31, 7, 5),
MIX_ENT(SOUND_MIXER_BASS,	0x46, 7, 4, 0x47, 7, 4),
MIX_ENT(SOUND_MIXER_TREBLE,	0x44, 7, 4, 0x45, 7, 4),
MIX_ENT(SOUND_MIXER_SYNTH,	0x34, 7, 5, 0x35, 7, 5),
MIX_ENT(SOUND_MIXER_PCM,	0x32, 7, 5, 0x33, 7, 5),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x3b, 7, 2, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x38, 7, 5, 0x39, 7, 5),
MIX_ENT(SOUND_MIXER_MIC,	0x3a, 7, 5, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_CD,		0x36, 7, 5, 0x37, 7, 5),
MIX_ENT(SOUND_MIXER_IMIX,	0x3c, 0, 1, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x3f, 7, 2, 0x40, 7, 2), /* Obsolete. Use IGAIN */
MIX_ENT(SOUND_MIXER_IGAIN,	0x3f, 7, 2, 0x40, 7, 2),
MIX_ENT(SOUND_MIXER_OGAIN,	0x41, 7, 2, 0x42, 7, 2)
};

static mixer_tab als007_mix = 
{
MIX_ENT(SOUND_MIXER_VOLUME,	0x62, 7, 4, 0x62, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	0x66, 7, 4, 0x66, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,	0x64, 7, 4, 0x64, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x6e, 7, 4, 0x6e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,	0x6a, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_CD,		0x68, 7, 4, 0x68, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x00, 0, 0, 0x00, 0, 0), /* Obsolete. Use IGAIN */
MIX_ENT(SOUND_MIXER_IGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,	0x00, 0, 0, 0x00, 0, 0)
};


/* SM_GAMES          Master volume is lower and PCM & FM volumes
			     higher than with SB Pro. This improves the
			     sound quality */

static int smg_default_levels[32] =
{
  0x2020,			/* Master Volume */
  0x4b4b,			/* Bass */
  0x4b4b,			/* Treble */
  0x6464,			/* FM */
  0x6464,			/* PCM */
  0x4b4b,			/* PC Speaker */
  0x4b4b,			/* Ext Line */
  0x0000,			/* Mic */
  0x4b4b,			/* CD */
  0x4b4b,			/* Recording monitor */
  0x4b4b,			/* SB PCM */
  0x4b4b,			/* Recording level */
  0x4b4b,			/* Input gain */
  0x4b4b,			/* Output gain */
  0x4040,			/* Line1 */
  0x4040,			/* Line2 */
  0x1515			/* Line3 */
};

static int sb_default_levels[32] =
{
  0x5a5a,			/* Master Volume */
  0x4b4b,			/* Bass */
  0x4b4b,			/* Treble */
  0x4b4b,			/* FM */
  0x4b4b,			/* PCM */
  0x4b4b,			/* PC Speaker */
  0x4b4b,			/* Ext Line */
  0x1010,			/* Mic */
  0x4b4b,			/* CD */
  0x0000,			/* Recording monitor */
  0x4b4b,			/* SB PCM */
  0x4b4b,			/* Recording level */
  0x4b4b,			/* Input gain */
  0x4b4b,			/* Output gain */
  0x4040,			/* Line1 */
  0x4040,			/* Line2 */
  0x1515			/* Line3 */
};

static unsigned char sb16_recmasks_L[SOUND_MIXER_NRDEVICES] =
{
	0x00,	/* SOUND_MIXER_VOLUME	*/
	0x00,	/* SOUND_MIXER_BASS	*/
	0x00,	/* SOUND_MIXER_TREBLE	*/
	0x40,	/* SOUND_MIXER_SYNTH	*/
	0x00,	/* SOUND_MIXER_PCM	*/
	0x00,	/* SOUND_MIXER_SPEAKER	*/
	0x10,	/* SOUND_MIXER_LINE	*/
	0x01,	/* SOUND_MIXER_MIC	*/
	0x04,	/* SOUND_MIXER_CD	*/
	0x00,	/* SOUND_MIXER_IMIX	*/
	0x00,	/* SOUND_MIXER_ALTPCM	*/
	0x00,	/* SOUND_MIXER_RECLEV	*/
	0x00,	/* SOUND_MIXER_IGAIN	*/
	0x00	/* SOUND_MIXER_OGAIN	*/
};

static unsigned char sb16_recmasks_R[SOUND_MIXER_NRDEVICES] =
{
	0x00,	/* SOUND_MIXER_VOLUME	*/
	0x00,	/* SOUND_MIXER_BASS	*/
	0x00,	/* SOUND_MIXER_TREBLE	*/
	0x20,	/* SOUND_MIXER_SYNTH	*/
	0x00,	/* SOUND_MIXER_PCM	*/
	0x00,	/* SOUND_MIXER_SPEAKER	*/
	0x08,	/* SOUND_MIXER_LINE	*/
	0x01,	/* SOUND_MIXER_MIC	*/
	0x02,	/* SOUND_MIXER_CD	*/
	0x00,	/* SOUND_MIXER_IMIX	*/
	0x00,	/* SOUND_MIXER_ALTPCM	*/
	0x00,	/* SOUND_MIXER_RECLEV	*/
	0x00,	/* SOUND_MIXER_IGAIN	*/
	0x00	/* SOUND_MIXER_OGAIN	*/
};

static char     smw_mix_regs[] =	/* Left mixer registers */
{
  0x0b,				/* SOUND_MIXER_VOLUME */
  0x0d,				/* SOUND_MIXER_BASS */
  0x0d,				/* SOUND_MIXER_TREBLE */
  0x05,				/* SOUND_MIXER_SYNTH */
  0x09,				/* SOUND_MIXER_PCM */
  0x00,				/* SOUND_MIXER_SPEAKER */
  0x03,				/* SOUND_MIXER_LINE */
  0x01,				/* SOUND_MIXER_MIC */
  0x07,				/* SOUND_MIXER_CD */
  0x00,				/* SOUND_MIXER_IMIX */
  0x00,				/* SOUND_MIXER_ALTPCM */
  0x00,				/* SOUND_MIXER_RECLEV */
  0x00,				/* SOUND_MIXER_IGAIN */
  0x00,				/* SOUND_MIXER_OGAIN */
  0x00,				/* SOUND_MIXER_LINE1 */
  0x00,				/* SOUND_MIXER_LINE2 */
  0x00				/* SOUND_MIXER_LINE3 */
};

static int      sbmixnum = 1;

static void     sb_mixer_reset(sb_devc * devc);

void sb_mixer_set_stereo(sb_devc * devc, int mode)
{
	sb_chgmixer(devc, OUT_FILTER, STEREO_DAC, (mode ? STEREO_DAC : MONO_DAC));
}

static int detect_mixer(sb_devc * devc)
{
	/* Just trust the mixer is there */
	return 1;
}

static void change_bits(sb_devc * devc, unsigned char *regval, int dev, int chn, int newval)
{
	unsigned char mask;
	int shift;

	mask = (1 << (*devc->iomap)[dev][chn].nbits) - 1;
	newval = (int) ((newval * mask) + 50) / 100;	/* Scale */

	shift = (*devc->iomap)[dev][chn].bitoffs - (*devc->iomap)[dev][LEFT_CHN].nbits + 1;

	*regval &= ~(mask << shift);	/* Mask out previous value */
	*regval |= (newval & mask) << shift;	/* Set the new value */
}

static int sb_mixer_get(sb_devc * devc, int dev)
{
	if (!((1 << dev) & devc->supported_devices))
		return -EINVAL;
	return devc->levels[dev];
}

void smw_mixer_init(sb_devc * devc)
{
	int i;

	sb_setmixer(devc, 0x00, 0x18);	/* Mute unused (Telephone) line */
	sb_setmixer(devc, 0x10, 0x38);	/* Config register 2 */

	devc->supported_devices = 0;
	for (i = 0; i < sizeof(smw_mix_regs); i++)
		if (smw_mix_regs[i] != 0)
			devc->supported_devices |= (1 << i);

	devc->supported_rec_devices = devc->supported_devices &
		~(SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_PCM | SOUND_MASK_VOLUME);
	sb_mixer_reset(devc);
}

int sb_common_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	int regoffs;
	unsigned char val;

	if ((dev < 0) || (dev >= devc->iomap_sz))
		return -EINVAL;

	regoffs = (*devc->iomap)[dev][LEFT_CHN].regno;

	if (regoffs == 0)
		return -EINVAL;

	val = sb_getmixer(devc, regoffs);
	change_bits(devc, &val, dev, LEFT_CHN, left);

	if ((*devc->iomap)[dev][RIGHT_CHN].regno != regoffs)	/*
								 * Change register
								 */
	{
		sb_setmixer(devc, regoffs, val);	/*
							 * Save the old one
							 */
		regoffs = (*devc->iomap)[dev][RIGHT_CHN].regno;

		if (regoffs == 0)
			return left | (left << 8);	/*
							 * Just left channel present
							 */

		val = sb_getmixer(devc, regoffs);	/*
							 * Read the new one
							 */
	}
	change_bits(devc, &val, dev, RIGHT_CHN, right);

	sb_setmixer(devc, regoffs, val);

	return left | (right << 8);
}

static int smw_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	int reg, val;

	switch (dev)
	{
		case SOUND_MIXER_VOLUME:
			sb_setmixer(devc, 0x0b, 96 - (96 * left / 100));	/* 96=mute, 0=max */
			sb_setmixer(devc, 0x0c, 96 - (96 * right / 100));
			break;

		case SOUND_MIXER_BASS:
		case SOUND_MIXER_TREBLE:
			devc->levels[dev] = left | (right << 8);
			/* Set left bass and treble values */
			val = ((devc->levels[SOUND_MIXER_TREBLE] & 0xff) * 16 / (unsigned) 100) << 4;
			val |= ((devc->levels[SOUND_MIXER_BASS] & 0xff) * 16 / (unsigned) 100) & 0x0f;
			sb_setmixer(devc, 0x0d, val);

			/* Set right bass and treble values */
			val = (((devc->levels[SOUND_MIXER_TREBLE] >> 8) & 0xff) * 16 / (unsigned) 100) << 4;
			val |= (((devc->levels[SOUND_MIXER_BASS] >> 8) & 0xff) * 16 / (unsigned) 100) & 0x0f;
			sb_setmixer(devc, 0x0e, val);
		
			break;

		default:
			/* bounds check */
			if (dev < 0 || dev >= ARRAY_SIZE(smw_mix_regs))
				return -EINVAL;
			reg = smw_mix_regs[dev];
			if (reg == 0)
				return -EINVAL;
			sb_setmixer(devc, reg, (24 - (24 * left / 100)) | 0x20);	/* 24=mute, 0=max */
			sb_setmixer(devc, reg + 1, (24 - (24 * right / 100)) | 0x40);
	}

	devc->levels[dev] = left | (right << 8);
	return left | (right << 8);
}

static int sb_mixer_set(sb_devc * devc, int dev, int value)
{
	int left = value & 0x000000ff;
	int right = (value & 0x0000ff00) >> 8;
	int retval;

	if (left > 100)
		left = 100;
	if (right > 100)
		right = 100;

	if ((dev < 0) || (dev > 31))
		return -EINVAL;

	if (!(devc->supported_devices & (1 << dev)))	/*
							 * Not supported
							 */
		return -EINVAL;

	/* Differentiate depending on the chipsets */
	switch (devc->model) {
	case MDL_SMW:
		retval = smw_mixer_set(devc, dev, left, right);
		break;
	case MDL_ESS:
		retval = ess_mixer_set(devc, dev, left, right);
		break;
	default:
		retval = sb_common_mixer_set(devc, dev, left, right);
	}
	if (retval >= 0) devc->levels[dev] = retval;

	return retval;
}

/*
 * set_recsrc doesn't apply to ES188x
 */
static void set_recsrc(sb_devc * devc, int src)
{
	sb_setmixer(devc, RECORD_SRC, (sb_getmixer(devc, RECORD_SRC) & ~7) | (src & 0x7));
}

static int set_recmask(sb_devc * devc, int mask)
{
	int devmask, i;
	unsigned char  regimageL, regimageR;

	devmask = mask & devc->supported_rec_devices;

	switch (devc->model)
	{
		case MDL_SBPRO:
		case MDL_ESS:
		case MDL_JAZZ:
		case MDL_SMW:
			if (devc->model == MDL_ESS && ess_set_recmask (devc, &devmask)) {
				break;
			};
			if (devmask != SOUND_MASK_MIC &&
				devmask != SOUND_MASK_LINE &&
				devmask != SOUND_MASK_CD)
			{
				/*
				 * More than one device selected. Drop the
				 * previous selection
				 */
				devmask &= ~devc->recmask;
			}
			if (devmask != SOUND_MASK_MIC &&
				devmask != SOUND_MASK_LINE &&
				devmask != SOUND_MASK_CD)
			{
				/*
				 * More than one device selected. Default to
				 * mic
				 */
				devmask = SOUND_MASK_MIC;
			}
			if (devmask ^ devc->recmask)	/*
							 *	Input source changed
							 */
			{
				switch (devmask)
				{
					case SOUND_MASK_MIC:
						set_recsrc(devc, SRC__MIC);
						break;

					case SOUND_MASK_LINE:
						set_recsrc(devc, SRC__LINE);
						break;

					case SOUND_MASK_CD:
						set_recsrc(devc, SRC__CD);
						break;

					default:
						set_recsrc(devc, SRC__MIC);
				}
			}
			break;

		case MDL_SB16:
			if (!devmask)
				devmask = SOUND_MASK_MIC;

			if (devc->submodel == SUBMDL_ALS007) 
			{
				switch (devmask) 
				{
					case SOUND_MASK_LINE:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_LINE);
						break;
					case SOUND_MASK_CD:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_CD);
						break;
					case SOUND_MASK_SYNTH:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_SYNTH);
						break;
					default:           /* Also takes care of SOUND_MASK_MIC case */
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_MIC);
						break;
				}
			}
			else
			{
				regimageL = regimageR = 0;
				for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				{
					if ((1 << i) & devmask)
					{
						regimageL |= sb16_recmasks_L[i];
						regimageR |= sb16_recmasks_R[i];
					}
					sb_setmixer (devc, SB16_IMASK_L, regimageL);
					sb_setmixer (devc, SB16_IMASK_R, regimageR);
				}
			}
			break;
	}
	devc->recmask = devmask;
	return devc->recmask;
}

static int set_outmask(sb_devc * devc, int mask)
{
	int devmask, i;
	unsigned char  regimage;

	devmask = mask & devc->supported_out_devices;

	switch (devc->model)
	{
		case MDL_SB16:
			if (devc->submodel == SUBMDL_ALS007) 
				break;
			else
			{
				regimage = 0;
				for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				{
					if ((1 << i) & devmask)
					{
						regimage |= (sb16_recmasks_L[i] | sb16_recmasks_R[i]);
					}
					sb_setmixer (devc, SB16_OMASK, regimage);
				}
			}
			break;
		default:
			break;
	}

	devc->outmask = devmask;
	return devc->outmask;
}

static int sb_mixer_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	sb_devc *devc = mixer_devs[dev]->devc;
	int val, ret;
	int __user *p = arg;

	/*
	 * Use ioctl(fd, SOUND_MIXER_AGC, &mode) to turn AGC off (0) or on (1).
	 * Use ioctl(fd, SOUND_MIXER_3DSE, &mode) to turn 3DSE off (0) or on (1)
	 *					      or mode==2 put 3DSE state to mode.
	 */
	if (devc->model == MDL_SB16) {
		if (cmd == SOUND_MIXER_AGC) 
		{
			if (get_user(val, p))
				return -EFAULT;
			sb_setmixer(devc, 0x43, (~val) & 0x01);
			return 0;
		}
		if (cmd == SOUND_MIXER_3DSE) 
		{
			/* I put here 15, but I don't know the exact version.
			   At least my 4.13 havn't 3DSE, 4.16 has it. */
			if (devc->minor < 15)
				return -EINVAL;
			if (get_user(val, p))
				return -EFAULT;
			if (val == 0 || val == 1)
				sb_chgmixer(devc, AWE_3DSE, 0x01, val);
			else if (val == 2)
			{
				ret = sb_getmixer(devc, AWE_3DSE)&0x01;
				return put_user(ret, p);
			}
			else
				return -EINVAL;
			return 0;
		}
	}
	if (((cmd >> 8) & 0xff) == 'M') 
	{
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) 
		{
			if (get_user(val, p))
				return -EFAULT;
			switch (cmd & 0xff) 
			{
				case SOUND_MIXER_RECSRC:
					ret = set_recmask(devc, val);
					break;

				case SOUND_MIXER_OUTSRC:
					ret = set_outmask(devc, val);
					break;

				default:
					ret = sb_mixer_set(devc, cmd & 0xff, val);
			}
		}
		else switch (cmd & 0xff) 
		{
			case SOUND_MIXER_RECSRC:
				ret = devc->recmask;
				break;
				  
			case SOUND_MIXER_OUTSRC:
				ret = devc->outmask;
				break;
				  
			case SOUND_MIXER_DEVMASK:
				ret = devc->supported_devices;
				break;
				  
			case SOUND_MIXER_STEREODEVS:
				ret = devc->supported_devices;
				/* The ESS seems to have stereo mic controls */
				if (devc->model == MDL_ESS)
					ret &= ~(SOUND_MASK_SPEAKER|SOUND_MASK_IMIX);
				else if (devc->model != MDL_JAZZ && devc->model != MDL_SMW)
					ret &= ~(SOUND_MASK_MIC | SOUND_MASK_SPEAKER | SOUND_MASK_IMIX);
				break;
				  
			case SOUND_MIXER_RECMASK:
				ret = devc->supported_rec_devices;
				break;
				  
			case SOUND_MIXER_OUTMASK:
				ret = devc->supported_out_devices;
				break;
				  
			case SOUND_MIXER_CAPS:
				ret = devc->mixer_caps;
				break;
				    
			default:
				ret = sb_mixer_get(devc, cmd & 0xff);
				break;
		}
		return put_user(ret, p); 
	} else
		return -EINVAL;
}

static struct mixer_operations sb_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "SB",
	.name	= "Sound Blaster",
	.ioctl	= sb_mixer_ioctl
};

static struct mixer_operations als007_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "ALS007",
	.name	= "Avance ALS-007",
	.ioctl	= sb_mixer_ioctl
};

static void sb_mixer_reset(sb_devc * devc)
{
	char name[32];
	int i;

	sprintf(name, "SB_%d", devc->sbmixnum);

	if (devc->sbmo.sm_games)
		devc->levels = load_mixer_volumes(name, smg_default_levels, 1);
	else
		devc->levels = load_mixer_volumes(name, sb_default_levels, 1);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		sb_mixer_set(devc, i, devc->levels[i]);

	if (devc->model != MDL_ESS || !ess_mixer_reset (devc)) {
		set_recmask(devc, SOUND_MASK_MIC);
	};
}

int sb_mixer_init(sb_devc * devc, struct module *owner)
{
	int mixer_type = 0;
	int m;

	devc->sbmixnum = sbmixnum++;
	devc->levels = NULL;

	sb_setmixer(devc, 0x00, 0);	/* Reset mixer */

	if (!(mixer_type = detect_mixer(devc)))
		return 0;	/* No mixer. Why? */

	switch (devc->model)
	{
		case MDL_ESSPCI:
		case MDL_YMPCI:
		case MDL_SBPRO:
		case MDL_AZTECH:
		case MDL_JAZZ:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;
			devc->supported_devices = SBPRO_MIXER_DEVICES;
			devc->supported_rec_devices = SBPRO_RECORDING_DEVICES;
			devc->iomap = &sbpro_mix;
			devc->iomap_sz = ARRAY_SIZE(sbpro_mix);
			break;

		case MDL_ESS:
			ess_mixer_init (devc);
			break;

		case MDL_SMW:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;
			devc->supported_devices = 0;
			devc->supported_rec_devices = 0;
			devc->iomap = &sbpro_mix;
			devc->iomap_sz = ARRAY_SIZE(sbpro_mix);
			smw_mixer_init(devc);
			break;

		case MDL_SB16:
			devc->mixer_caps = 0;
			devc->supported_rec_devices = SB16_RECORDING_DEVICES;
			devc->supported_out_devices = SB16_OUTFILTER_DEVICES;
			if (devc->submodel != SUBMDL_ALS007)
			{
				devc->supported_devices = SB16_MIXER_DEVICES;
				devc->iomap = &sb16_mix;
				devc->iomap_sz = ARRAY_SIZE(sb16_mix);
			}
			else
			{
				devc->supported_devices = ALS007_MIXER_DEVICES;
				devc->iomap = &als007_mix;
				devc->iomap_sz = ARRAY_SIZE(als007_mix);
			}
			break;

		default:
			printk(KERN_WARNING "sb_mixer: Unsupported mixer type %d\n", devc->model);
			return 0;
	}

	m = sound_alloc_mixerdev();
	if (m == -1)
		return 0;

	mixer_devs[m] = kmalloc(sizeof(struct mixer_operations), GFP_KERNEL);
	if (mixer_devs[m] == NULL)
	{
		printk(KERN_ERR "sb_mixer: Can't allocate memory\n");
		sound_unload_mixerdev(m);
		return 0;
	}

	if (devc->submodel != SUBMDL_ALS007)
		memcpy ((char *) mixer_devs[m], (char *) &sb_mixer_operations, sizeof (struct mixer_operations));
	else
		memcpy ((char *) mixer_devs[m], (char *) &als007_mixer_operations, sizeof (struct mixer_operations));

	mixer_devs[m]->devc = devc;

	if (owner)
			 mixer_devs[m]->owner = owner;
	
	devc->my_mixerdev = m;
	sb_mixer_reset(devc);
	return 1;
}

void sb_mixer_unload(sb_devc *devc)
{
	if (devc->my_mixerdev == -1)
		return;

	kfree(mixer_devs[devc->my_mixerdev]);
	sound_unload_mixerdev(devc->my_mixerdev);
	sbmixnum--;
}
