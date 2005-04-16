#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include "ac97.h"

/* Flag for mono controls. */
#define MO 0
/* And for stereo. */
#define ST 1

/* Whether or not the bits in the channel are inverted. */
#define INV 1
#define NINV 0

static struct ac97_chn_desc {
    int ac97_regnum;
    int oss_channel;
    int maxval;
    int is_stereo;
    int oss_mask;
    int recordNum;
    u16 regmask;
    int is_inverted;
} mixerRegs[] = {
    { AC97_MASTER_VOL_STEREO, SOUND_MIXER_VOLUME,   0x3f, ST, SOUND_MASK_VOLUME,   5, 0x0000, INV  },
    { AC97_MASTER_VOL_MONO,   SOUND_MIXER_PHONEOUT, 0x3f, MO, SOUND_MASK_PHONEOUT, 6, 0x0000, INV  },
    { AC97_MASTER_TONE,       SOUND_MIXER_TREBLE,   0x0f, MO, SOUND_MASK_TREBLE,  -1, 0x00ff, INV  },
    { AC97_MASTER_TONE,       SOUND_MIXER_BASS,     0x0f, MO, SOUND_MASK_BASS,    -1, 0xff00, INV  },
    { AC97_PCBEEP_VOL,        SOUND_MIXER_SPEAKER,  0x0f, MO, SOUND_MASK_SPEAKER, -1, 0x001e, INV  },
    { AC97_PHONE_VOL,         SOUND_MIXER_PHONEIN,  0x1f, MO, SOUND_MASK_PHONEIN,  7, 0x0000, INV  },
    { AC97_MIC_VOL,           SOUND_MIXER_MIC,      0x1f, MO, SOUND_MASK_MIC,      0, 0x0000, INV  },
    { AC97_LINEIN_VOL,        SOUND_MIXER_LINE,     0x1f, ST, SOUND_MASK_LINE,     4, 0x0000, INV  },
    { AC97_CD_VOL,            SOUND_MIXER_CD,       0x1f, ST, SOUND_MASK_CD,       1, 0x0000, INV  },
    { AC97_VIDEO_VOL,         SOUND_MIXER_VIDEO,    0x1f, ST, SOUND_MASK_VIDEO,    2, 0x0000, INV  },
    { AC97_AUX_VOL,           SOUND_MIXER_LINE1,    0x1f, ST, SOUND_MASK_LINE1,	   3, 0x0000, INV  },
    { AC97_PCMOUT_VOL,        SOUND_MIXER_PCM,      0x1f, ST, SOUND_MASK_PCM,     -1, 0x0000, INV  },
    { AC97_RECORD_GAIN,       SOUND_MIXER_IGAIN,    0x0f, ST, SOUND_MASK_IGAIN,   -1, 0x0000, NINV },
    { -1,		      -1,		    0xff, 0,  0,                  -1, 0x0000, 0    },
};

static struct ac97_chn_desc *
ac97_find_chndesc (struct ac97_hwint *dev, int oss_channel)
{
    int x;

    for (x = 0; mixerRegs[x].oss_channel != -1; x++) {
	if (mixerRegs[x].oss_channel == oss_channel)
	    return mixerRegs + x;
    }

    return NULL;
}

static inline int
ac97_is_valid_channel (struct ac97_hwint *dev, struct ac97_chn_desc *chn)
{
    return (dev->last_written_mixer_values[chn->ac97_regnum / 2]
	    != AC97_REG_UNSUPPORTED);
}

int
ac97_init (struct ac97_hwint *dev)
{
    int x;
    int reg0;

    /* Clear out the arrays of cached values. */
    for (x = 0; x < AC97_REG_CNT; x++)
	dev->last_written_mixer_values[x] = AC97_REGVAL_UNKNOWN;

    for (x = 0; x < SOUND_MIXER_NRDEVICES; x++)
	dev->last_written_OSS_values[x] = AC97_REGVAL_UNKNOWN;

    /* Clear the device masks.  */
    dev->mixer_devmask = 0;
    dev->mixer_stereomask = 0;
    dev->mixer_recmask = 0;

    /* ??? Do a "standard reset" via register 0? */

    /* Hardware-dependent reset.  */
    if (dev->reset_device (dev))
	return -1;

    /* Check the mixer device capabilities.  */
    reg0 = dev->read_reg (dev, AC97_RESET);

    if (reg0 < 0)
	return -1;

    /* Check for support for treble/bass controls.  */
    if (! (reg0 & 4)) {
	dev->last_written_mixer_values[AC97_MASTER_TONE / 2] 
	    = AC97_REG_UNSUPPORTED;
    }

    /* ??? There may be other tests here? */

    /* Fill in the device masks.  */
    for (x = 0; mixerRegs[x].ac97_regnum != -1; x++) {
	if (ac97_is_valid_channel (dev, mixerRegs + x)) {
	    dev->mixer_devmask |= mixerRegs[x].oss_mask;

	    if (mixerRegs[x].is_stereo)
		dev->mixer_stereomask |= mixerRegs[x].oss_mask;

	    if (mixerRegs[x].recordNum != -1)
		dev->mixer_recmask |= mixerRegs[x].oss_mask;
	}
    }

    return 0;
}

/* Reset the mixer to the currently saved settings.  */
int
ac97_reset (struct ac97_hwint *dev)
{
    int x;

    if (dev->reset_device (dev))
	return -1;

    /* Now set the registers back to their last-written values. */
    for (x = 0; mixerRegs[x].ac97_regnum != -1; x++) {
	int regnum = mixerRegs[x].ac97_regnum;
	int value = dev->last_written_mixer_values [regnum / 2];
	if (value >= 0)
	    ac97_put_register (dev, regnum, value);
    }
    return 0;
}

/* Return the contents of register REG; use the cache if the value in it
   is valid.  Returns a negative error code on failure. */
static int
ac97_get_register (struct ac97_hwint *dev, u8 reg) 
{
    if (reg > 127 || (reg & 1))
	return -EINVAL;

    /* See if it's in the cache, or if it's just plain invalid.  */
    switch (dev->last_written_mixer_values[reg / 2]) {
    case AC97_REG_UNSUPPORTED:
	return -EINVAL;
	break;
    case AC97_REGVAL_UNKNOWN:
	dev->last_written_mixer_values[reg / 2] = dev->read_reg (dev, reg);
	break;
    default:
	break;
    }
    return dev->last_written_mixer_values[reg / 2];
}

/* Write VALUE to AC97 register REG, and cache its value in the last-written
   cache.  Returns a negative error code on failure, or 0 on success. */
int
ac97_put_register (struct ac97_hwint *dev, u8 reg, u16 value)
{
    if (reg > 127 || (reg & 1))
	return -EINVAL;

    if (dev->last_written_mixer_values[reg / 2] == AC97_REG_UNSUPPORTED)
	return -EINVAL;
    else {
	int res = dev->write_reg (dev, reg, value);
	if (res >= 0) {
	    dev->last_written_mixer_values[reg / 2] = value;
	    return 0;
	}
	else
	    return res;
    }
}

/* Scale VALUE (a value fro 0 to MAXVAL) to a value from 0-100.  If
   IS_STEREO is set, VALUE is a stereo value; the left channel value
   is in the lower 8 bits, and the right channel value is in the upper
   8 bits.

   A negative error code is returned on failure, or the unsigned
   scaled value on success.  */

static int
ac97_scale_to_oss_val (int value, int maxval, int is_stereo, int inv)
{
    /* Muted?  */
    if (value & AC97_MUTE)
	return 0;

    if (is_stereo)
	return (ac97_scale_to_oss_val (value & 255, maxval, 0, inv) << 8)
	| (ac97_scale_to_oss_val ((value >> 8) & 255, maxval, 0, inv) << 0);
    else {
	int i;
	
	/* Inverted. */
	if (inv)
	    value = maxval - value;

	i = (value * 100 + (maxval / 2)) / maxval;
	if (i > 100)
	     i = 100;
	if (i < 0)
	    i = 0;
	return i;
    }
}

static int
ac97_scale_from_oss_val (int value, int maxval, int is_stereo, int inv)
{
    if (is_stereo)
	return (ac97_scale_from_oss_val (value & 255, maxval, 0, inv) << 8)
	| (ac97_scale_from_oss_val ((value >> 8) & 255, maxval, 0, inv) << 0);
    else {
	int i = ((value & 255) * maxval + 50) / 100;
	if (inv)
	    i = maxval - i;
	if (i < 0)
	    i = 0;
	if (i > maxval)
	    i = maxval;
	return i;
    }
}

static int
ac97_set_mixer (struct ac97_hwint *dev, int oss_channel, u16 oss_value)
{
    int scaled_value;
    struct ac97_chn_desc *channel = ac97_find_chndesc (dev, oss_channel);
    int result;

    if (channel == NULL)
	return -ENODEV;
    if (! ac97_is_valid_channel (dev, channel))
	return -ENODEV;
    scaled_value = ac97_scale_from_oss_val (oss_value, channel->maxval,
					    channel->is_stereo, 
					    channel->is_inverted);
    if (scaled_value < 0)
	return scaled_value;

    if (channel->regmask != 0) {
	int mv;

	int oldval = ac97_get_register (dev, channel->ac97_regnum);
	if (oldval < 0)
	    return oldval;

	for (mv = channel->regmask; ! (mv & 1); mv >>= 1)
	    scaled_value <<= 1;

	scaled_value &= channel->regmask;
	scaled_value |= (oldval & ~channel->regmask);
    }
    result = ac97_put_register (dev, channel->ac97_regnum, scaled_value);
    if (result == 0)
	dev->last_written_OSS_values[oss_channel] = oss_value;
    return result;
}

static int
ac97_get_mixer_scaled (struct ac97_hwint *dev, int oss_channel)
{
    struct ac97_chn_desc *channel = ac97_find_chndesc (dev, oss_channel);
    int regval;

    if (channel == NULL)
	return -ENODEV;

    if (! ac97_is_valid_channel (dev, channel))
	return -ENODEV;

    regval = ac97_get_register (dev, channel->ac97_regnum);

    if (regval < 0)
	return regval;

    if (channel->regmask != 0) {
	int mv;

	regval &= channel->regmask;

	for (mv = channel->regmask; ! (mv & 1); mv >>= 1)
	    regval >>= 1;
    }
    return ac97_scale_to_oss_val (regval, channel->maxval,
				  channel->is_stereo, 
				  channel->is_inverted);
}

static int
ac97_get_recmask (struct ac97_hwint *dev)
{
    int recReg = ac97_get_register (dev, AC97_RECORD_SELECT);

    if (recReg < 0)
	return recReg;
    else {
	int x;
	for (x = 0; mixerRegs[x].ac97_regnum >= 0; x++) {
	    if (mixerRegs[x].recordNum == (recReg & 7))
		return mixerRegs[x].oss_mask;
	}
	return -ENODEV;
    }
}

static int
ac97_set_recmask (struct ac97_hwint *dev, int oss_recmask)
{
    int x;

    if (oss_recmask == 0)
	oss_recmask = SOUND_MIXER_MIC;

    for (x = 0; mixerRegs[x].ac97_regnum >= 0; x++) { 
	if ((mixerRegs[x].recordNum >= 0)
	     && (oss_recmask & mixerRegs[x].oss_mask))
	    break;
    }
    if (mixerRegs[x].ac97_regnum < 0)
	return -ENODEV;
    else {
	int regval = (mixerRegs[x].recordNum << 8) | mixerRegs[x].recordNum;
	int res = ac97_put_register (dev, AC97_RECORD_SELECT, regval);
	if (res == 0)
	    return ac97_get_recmask (dev);
	else
	    return res;
    }
}

/* Set the mixer DEV to the list of values in VALUE_LIST.  Return 0 on
   success, or a negative error code.  */
int
ac97_set_values (struct ac97_hwint *dev, 
		 struct ac97_mixer_value_list *value_list)
{
    int x;

    for (x = 0; value_list[x].oss_channel != -1; x++) {
	int chnum = value_list[x].oss_channel;
	struct ac97_chn_desc *chent = ac97_find_chndesc (dev, chnum);
	if (chent != NULL) {
	    u16 val;
	    int res;

	    if (chent->is_stereo)
		val = (value_list[x].value.stereo.right << 8) 
		      | value_list[x].value.stereo.left;
	    else {
		/* We do this so the returned value looks OK in the
		   mixer app.  It's not necessary otherwise.  */
		val = (value_list[x].value.mono << 8) 
		      | value_list[x].value.mono;
	    }
	    res = ac97_set_mixer (dev, chnum, val);
	    if (res < 0)
		return res;
	}
	else
	    return -ENODEV;
    }
    return 0;
}

int
ac97_mixer_ioctl (struct ac97_hwint *dev, unsigned int cmd, void __user *arg)
{
    int ret;

    switch (cmd) {
    case SOUND_MIXER_READ_RECSRC:
	ret = ac97_get_recmask (dev);
	break;

    case SOUND_MIXER_WRITE_RECSRC:
	{
	    if (get_user (ret, (int __user *) arg))
		ret = -EFAULT;
	    else
		ret = ac97_set_recmask (dev, ret);
	}
	break;

    case SOUND_MIXER_READ_CAPS:
	ret = SOUND_CAP_EXCL_INPUT;
	break;

    case SOUND_MIXER_READ_DEVMASK:
	ret = dev->mixer_devmask;
	break;

    case SOUND_MIXER_READ_RECMASK:
	ret = dev->mixer_recmask;
	break;

    case SOUND_MIXER_READ_STEREODEVS:
	ret = dev->mixer_stereomask;
	break;

    default:
	/* Read or write request. */
	ret = -EINVAL;
	if (_IOC_TYPE (cmd) == 'M') {
	    int dir = _SIOC_DIR (cmd);
	    int channel = _IOC_NR (cmd);

	    if (channel >= 0 && channel < SOUND_MIXER_NRDEVICES) {
		ret = 0;
		if (dir & _SIOC_WRITE) {
		    int val;
		    if (get_user (val, (int __user *) arg) == 0)
			ret = ac97_set_mixer (dev, channel, val);
		    else
			ret = -EFAULT;
		}
		if (ret >= 0 && (dir & _SIOC_READ)) {
		    if (dev->last_written_OSS_values[channel]
			== AC97_REGVAL_UNKNOWN)
			dev->last_written_OSS_values[channel]
			    = ac97_get_mixer_scaled (dev, channel);
		    ret = dev->last_written_OSS_values[channel];
		}
	    }
	}
	break;
    }

    if (ret < 0)
	return ret;
    else
	return put_user(ret, (int __user *) arg);
}

EXPORT_SYMBOL(ac97_init);
EXPORT_SYMBOL(ac97_set_values);
EXPORT_SYMBOL(ac97_put_register);
EXPORT_SYMBOL(ac97_mixer_ioctl);
EXPORT_SYMBOL(ac97_reset);
MODULE_LICENSE("GPL");


/*
 * Local variables:
 *  c-basic-offset: 4
 * End:
 */
