/*
 * C-Media CMI8788 driver - mixer code
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/mutex.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include "oxygen.h"
#include "cm9780.h"

static int dac_volume_info(struct snd_kcontrol *ctl,
			   struct snd_ctl_elem_info *info)
{
	struct oxygen *chip = ctl->private_data;

	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = chip->model.dac_channels_mixer;
	info->value.integer.min = chip->model.dac_volume_min;
	info->value.integer.max = chip->model.dac_volume_max;
	return 0;
}

static int dac_volume_get(struct snd_kcontrol *ctl,
			  struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int i;

	mutex_lock(&chip->mutex);
	for (i = 0; i < chip->model.dac_channels_mixer; ++i)
		value->value.integer.value[i] = chip->dac_volume[i];
	mutex_unlock(&chip->mutex);
	return 0;
}

static int dac_volume_put(struct snd_kcontrol *ctl,
			  struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int i;
	int changed;

	changed = 0;
	mutex_lock(&chip->mutex);
	for (i = 0; i < chip->model.dac_channels_mixer; ++i)
		if (value->value.integer.value[i] != chip->dac_volume[i]) {
			chip->dac_volume[i] = value->value.integer.value[i];
			changed = 1;
		}
	if (changed)
		chip->model.update_dac_volume(chip);
	mutex_unlock(&chip->mutex);
	return changed;
}

static int dac_mute_get(struct snd_kcontrol *ctl,
			struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;

	mutex_lock(&chip->mutex);
	value->value.integer.value[0] = !chip->dac_mute;
	mutex_unlock(&chip->mutex);
	return 0;
}

static int dac_mute_put(struct snd_kcontrol *ctl,
			  struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	int changed;

	mutex_lock(&chip->mutex);
	changed = !value->value.integer.value[0] != chip->dac_mute;
	if (changed) {
		chip->dac_mute = !value->value.integer.value[0];
		chip->model.update_dac_mute(chip);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static unsigned int upmix_item_count(struct oxygen *chip)
{
	if (chip->model.dac_channels_pcm < 8)
		return 2;
	else if (chip->model.update_center_lfe_mix)
		return 5;
	else
		return 3;
}

static int upmix_info(struct snd_kcontrol *ctl, struct snd_ctl_elem_info *info)
{
	static const char *const names[5] = {
		"Front",
		"Front+Surround",
		"Front+Surround+Back",
		"Front+Surround+Center/LFE",
		"Front+Surround+Center/LFE+Back",
	};
	struct oxygen *chip = ctl->private_data;
	unsigned int count = upmix_item_count(chip);

	return snd_ctl_enum_info(info, 1, count, names);
}

static int upmix_get(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;

	mutex_lock(&chip->mutex);
	value->value.enumerated.item[0] = chip->dac_routing;
	mutex_unlock(&chip->mutex);
	return 0;
}

void oxygen_update_dac_routing(struct oxygen *chip)
{
	/* DAC 0: front, DAC 1: surround, DAC 2: center/LFE, DAC 3: back */
	static const unsigned int reg_values[5] = {
		/* stereo -> front */
		(0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
		(1 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
		(2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
		(3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT),
		/* stereo -> front+surround */
		(0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
		(2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
		(3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT),
		/* stereo -> front+surround+back */
		(0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
		(2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT),
		/* stereo -> front+surround+center/LFE */
		(0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
		(3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT),
		/* stereo -> front+surround+center/LFE+back */
		(0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
		(0 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT),
	};
	u8 channels;
	unsigned int reg_value;

	channels = oxygen_read8(chip, OXYGEN_PLAY_CHANNELS) &
		OXYGEN_PLAY_CHANNELS_MASK;
	if (channels == OXYGEN_PLAY_CHANNELS_2)
		reg_value = reg_values[chip->dac_routing];
	else if (channels == OXYGEN_PLAY_CHANNELS_8)
		/* in 7.1 mode, "rear" channels go to the "back" jack */
		reg_value = (0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
			    (3 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
			    (2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
			    (1 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT);
	else
		reg_value = (0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
			    (1 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
			    (2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
			    (3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT);
	oxygen_write16_masked(chip, OXYGEN_PLAY_ROUTING, reg_value,
			      OXYGEN_PLAY_DAC0_SOURCE_MASK |
			      OXYGEN_PLAY_DAC1_SOURCE_MASK |
			      OXYGEN_PLAY_DAC2_SOURCE_MASK |
			      OXYGEN_PLAY_DAC3_SOURCE_MASK);
	if (chip->model.update_center_lfe_mix)
		chip->model.update_center_lfe_mix(chip, chip->dac_routing > 2);
}

static int upmix_put(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int count = upmix_item_count(chip);
	int changed;

	if (value->value.enumerated.item[0] >= count)
		return -EINVAL;
	mutex_lock(&chip->mutex);
	changed = value->value.enumerated.item[0] != chip->dac_routing;
	if (changed) {
		chip->dac_routing = value->value.enumerated.item[0];
		oxygen_update_dac_routing(chip);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static int spdif_switch_get(struct snd_kcontrol *ctl,
			    struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;

	mutex_lock(&chip->mutex);
	value->value.integer.value[0] = chip->spdif_playback_enable;
	mutex_unlock(&chip->mutex);
	return 0;
}

static unsigned int oxygen_spdif_rate(unsigned int oxygen_rate)
{
	switch (oxygen_rate) {
	case OXYGEN_RATE_32000:
		return IEC958_AES3_CON_FS_32000 << OXYGEN_SPDIF_CS_RATE_SHIFT;
	case OXYGEN_RATE_44100:
		return IEC958_AES3_CON_FS_44100 << OXYGEN_SPDIF_CS_RATE_SHIFT;
	default: /* OXYGEN_RATE_48000 */
		return IEC958_AES3_CON_FS_48000 << OXYGEN_SPDIF_CS_RATE_SHIFT;
	case OXYGEN_RATE_64000:
		return 0xb << OXYGEN_SPDIF_CS_RATE_SHIFT;
	case OXYGEN_RATE_88200:
		return IEC958_AES3_CON_FS_88200 << OXYGEN_SPDIF_CS_RATE_SHIFT;
	case OXYGEN_RATE_96000:
		return IEC958_AES3_CON_FS_96000 << OXYGEN_SPDIF_CS_RATE_SHIFT;
	case OXYGEN_RATE_176400:
		return IEC958_AES3_CON_FS_176400 << OXYGEN_SPDIF_CS_RATE_SHIFT;
	case OXYGEN_RATE_192000:
		return IEC958_AES3_CON_FS_192000 << OXYGEN_SPDIF_CS_RATE_SHIFT;
	}
}

void oxygen_update_spdif_source(struct oxygen *chip)
{
	u32 old_control, new_control;
	u16 old_routing, new_routing;
	unsigned int oxygen_rate;

	old_control = oxygen_read32(chip, OXYGEN_SPDIF_CONTROL);
	old_routing = oxygen_read16(chip, OXYGEN_PLAY_ROUTING);
	if (chip->pcm_active & (1 << PCM_SPDIF)) {
		new_control = old_control | OXYGEN_SPDIF_OUT_ENABLE;
		new_routing = (old_routing & ~OXYGEN_PLAY_SPDIF_MASK)
			| OXYGEN_PLAY_SPDIF_SPDIF;
		oxygen_rate = (old_control >> OXYGEN_SPDIF_OUT_RATE_SHIFT)
			& OXYGEN_I2S_RATE_MASK;
		/* S/PDIF rate was already set by the caller */
	} else if ((chip->pcm_active & (1 << PCM_MULTICH)) &&
		   chip->spdif_playback_enable) {
		new_routing = (old_routing & ~OXYGEN_PLAY_SPDIF_MASK)
			| OXYGEN_PLAY_SPDIF_MULTICH_01;
		oxygen_rate = oxygen_read16(chip, OXYGEN_I2S_MULTICH_FORMAT)
			& OXYGEN_I2S_RATE_MASK;
		new_control = (old_control & ~OXYGEN_SPDIF_OUT_RATE_MASK) |
			(oxygen_rate << OXYGEN_SPDIF_OUT_RATE_SHIFT) |
			OXYGEN_SPDIF_OUT_ENABLE;
	} else {
		new_control = old_control & ~OXYGEN_SPDIF_OUT_ENABLE;
		new_routing = old_routing;
		oxygen_rate = OXYGEN_RATE_44100;
	}
	if (old_routing != new_routing) {
		oxygen_write32(chip, OXYGEN_SPDIF_CONTROL,
			       new_control & ~OXYGEN_SPDIF_OUT_ENABLE);
		oxygen_write16(chip, OXYGEN_PLAY_ROUTING, new_routing);
	}
	if (new_control & OXYGEN_SPDIF_OUT_ENABLE)
		oxygen_write32(chip, OXYGEN_SPDIF_OUTPUT_BITS,
			       oxygen_spdif_rate(oxygen_rate) |
			       ((chip->pcm_active & (1 << PCM_SPDIF)) ?
				chip->spdif_pcm_bits : chip->spdif_bits));
	oxygen_write32(chip, OXYGEN_SPDIF_CONTROL, new_control);
}

static int spdif_switch_put(struct snd_kcontrol *ctl,
			    struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	int changed;

	mutex_lock(&chip->mutex);
	changed = value->value.integer.value[0] != chip->spdif_playback_enable;
	if (changed) {
		chip->spdif_playback_enable = !!value->value.integer.value[0];
		spin_lock_irq(&chip->reg_lock);
		oxygen_update_spdif_source(chip);
		spin_unlock_irq(&chip->reg_lock);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static int spdif_info(struct snd_kcontrol *ctl, struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	info->count = 1;
	return 0;
}

static void oxygen_to_iec958(u32 bits, struct snd_ctl_elem_value *value)
{
	value->value.iec958.status[0] =
		bits & (OXYGEN_SPDIF_NONAUDIO | OXYGEN_SPDIF_C |
			OXYGEN_SPDIF_PREEMPHASIS);
	value->value.iec958.status[1] = /* category and original */
		bits >> OXYGEN_SPDIF_CATEGORY_SHIFT;
}

static u32 iec958_to_oxygen(struct snd_ctl_elem_value *value)
{
	u32 bits;

	bits = value->value.iec958.status[0] &
		(OXYGEN_SPDIF_NONAUDIO | OXYGEN_SPDIF_C |
		 OXYGEN_SPDIF_PREEMPHASIS);
	bits |= value->value.iec958.status[1] << OXYGEN_SPDIF_CATEGORY_SHIFT;
	if (bits & OXYGEN_SPDIF_NONAUDIO)
		bits |= OXYGEN_SPDIF_V;
	return bits;
}

static inline void write_spdif_bits(struct oxygen *chip, u32 bits)
{
	oxygen_write32_masked(chip, OXYGEN_SPDIF_OUTPUT_BITS, bits,
			      OXYGEN_SPDIF_NONAUDIO |
			      OXYGEN_SPDIF_C |
			      OXYGEN_SPDIF_PREEMPHASIS |
			      OXYGEN_SPDIF_CATEGORY_MASK |
			      OXYGEN_SPDIF_ORIGINAL |
			      OXYGEN_SPDIF_V);
}

static int spdif_default_get(struct snd_kcontrol *ctl,
			     struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;

	mutex_lock(&chip->mutex);
	oxygen_to_iec958(chip->spdif_bits, value);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int spdif_default_put(struct snd_kcontrol *ctl,
			     struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u32 new_bits;
	int changed;

	new_bits = iec958_to_oxygen(value);
	mutex_lock(&chip->mutex);
	changed = new_bits != chip->spdif_bits;
	if (changed) {
		chip->spdif_bits = new_bits;
		if (!(chip->pcm_active & (1 << PCM_SPDIF)))
			write_spdif_bits(chip, new_bits);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static int spdif_mask_get(struct snd_kcontrol *ctl,
			  struct snd_ctl_elem_value *value)
{
	value->value.iec958.status[0] = IEC958_AES0_NONAUDIO |
		IEC958_AES0_CON_NOT_COPYRIGHT | IEC958_AES0_CON_EMPHASIS;
	value->value.iec958.status[1] =
		IEC958_AES1_CON_CATEGORY | IEC958_AES1_CON_ORIGINAL;
	return 0;
}

static int spdif_pcm_get(struct snd_kcontrol *ctl,
			 struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;

	mutex_lock(&chip->mutex);
	oxygen_to_iec958(chip->spdif_pcm_bits, value);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int spdif_pcm_put(struct snd_kcontrol *ctl,
			 struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u32 new_bits;
	int changed;

	new_bits = iec958_to_oxygen(value);
	mutex_lock(&chip->mutex);
	changed = new_bits != chip->spdif_pcm_bits;
	if (changed) {
		chip->spdif_pcm_bits = new_bits;
		if (chip->pcm_active & (1 << PCM_SPDIF))
			write_spdif_bits(chip, new_bits);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static int spdif_input_mask_get(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_value *value)
{
	value->value.iec958.status[0] = 0xff;
	value->value.iec958.status[1] = 0xff;
	value->value.iec958.status[2] = 0xff;
	value->value.iec958.status[3] = 0xff;
	return 0;
}

static int spdif_input_default_get(struct snd_kcontrol *ctl,
				   struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u32 bits;

	bits = oxygen_read32(chip, OXYGEN_SPDIF_INPUT_BITS);
	value->value.iec958.status[0] = bits;
	value->value.iec958.status[1] = bits >> 8;
	value->value.iec958.status[2] = bits >> 16;
	value->value.iec958.status[3] = bits >> 24;
	return 0;
}

static int spdif_loopback_get(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;

	value->value.integer.value[0] =
		!!(oxygen_read32(chip, OXYGEN_SPDIF_CONTROL)
		   & OXYGEN_SPDIF_LOOPBACK);
	return 0;
}

static int spdif_loopback_put(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u32 oldreg, newreg;
	int changed;

	spin_lock_irq(&chip->reg_lock);
	oldreg = oxygen_read32(chip, OXYGEN_SPDIF_CONTROL);
	if (value->value.integer.value[0])
		newreg = oldreg | OXYGEN_SPDIF_LOOPBACK;
	else
		newreg = oldreg & ~OXYGEN_SPDIF_LOOPBACK;
	changed = newreg != oldreg;
	if (changed)
		oxygen_write32(chip, OXYGEN_SPDIF_CONTROL, newreg);
	spin_unlock_irq(&chip->reg_lock);
	return changed;
}

static int monitor_volume_info(struct snd_kcontrol *ctl,
			       struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 1;
	info->value.integer.min = 0;
	info->value.integer.max = 1;
	return 0;
}

static int monitor_get(struct snd_kcontrol *ctl,
		       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u8 bit = ctl->private_value;
	int invert = ctl->private_value & (1 << 8);

	value->value.integer.value[0] =
		!!invert ^ !!(oxygen_read8(chip, OXYGEN_ADC_MONITOR) & bit);
	return 0;
}

static int monitor_put(struct snd_kcontrol *ctl,
		       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u8 bit = ctl->private_value;
	int invert = ctl->private_value & (1 << 8);
	u8 oldreg, newreg;
	int changed;

	spin_lock_irq(&chip->reg_lock);
	oldreg = oxygen_read8(chip, OXYGEN_ADC_MONITOR);
	if ((!!value->value.integer.value[0] ^ !!invert) != 0)
		newreg = oldreg | bit;
	else
		newreg = oldreg & ~bit;
	changed = newreg != oldreg;
	if (changed)
		oxygen_write8(chip, OXYGEN_ADC_MONITOR, newreg);
	spin_unlock_irq(&chip->reg_lock);
	return changed;
}

static int ac97_switch_get(struct snd_kcontrol *ctl,
			   struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int codec = (ctl->private_value >> 24) & 1;
	unsigned int index = ctl->private_value & 0xff;
	unsigned int bitnr = (ctl->private_value >> 8) & 0xff;
	int invert = ctl->private_value & (1 << 16);
	u16 reg;

	mutex_lock(&chip->mutex);
	reg = oxygen_read_ac97(chip, codec, index);
	mutex_unlock(&chip->mutex);
	if (!(reg & (1 << bitnr)) ^ !invert)
		value->value.integer.value[0] = 1;
	else
		value->value.integer.value[0] = 0;
	return 0;
}

static void mute_ac97_ctl(struct oxygen *chip, unsigned int control)
{
	unsigned int priv_idx;
	u16 value;

	if (!chip->controls[control])
		return;
	priv_idx = chip->controls[control]->private_value & 0xff;
	value = oxygen_read_ac97(chip, 0, priv_idx);
	if (!(value & 0x8000)) {
		oxygen_write_ac97(chip, 0, priv_idx, value | 0x8000);
		if (chip->model.ac97_switch)
			chip->model.ac97_switch(chip, priv_idx, 0x8000);
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->controls[control]->id);
	}
}

static int ac97_switch_put(struct snd_kcontrol *ctl,
			   struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int codec = (ctl->private_value >> 24) & 1;
	unsigned int index = ctl->private_value & 0xff;
	unsigned int bitnr = (ctl->private_value >> 8) & 0xff;
	int invert = ctl->private_value & (1 << 16);
	u16 oldreg, newreg;
	int change;

	mutex_lock(&chip->mutex);
	oldreg = oxygen_read_ac97(chip, codec, index);
	newreg = oldreg;
	if (!value->value.integer.value[0] ^ !invert)
		newreg |= 1 << bitnr;
	else
		newreg &= ~(1 << bitnr);
	change = newreg != oldreg;
	if (change) {
		oxygen_write_ac97(chip, codec, index, newreg);
		if (codec == 0 && chip->model.ac97_switch)
			chip->model.ac97_switch(chip, index, newreg & 0x8000);
		if (index == AC97_LINE) {
			oxygen_write_ac97_masked(chip, 0, CM9780_GPIO_STATUS,
						 newreg & 0x8000 ?
						 CM9780_GPO0 : 0, CM9780_GPO0);
			if (!(newreg & 0x8000)) {
				mute_ac97_ctl(chip, CONTROL_MIC_CAPTURE_SWITCH);
				mute_ac97_ctl(chip, CONTROL_CD_CAPTURE_SWITCH);
				mute_ac97_ctl(chip, CONTROL_AUX_CAPTURE_SWITCH);
			}
		} else if ((index == AC97_MIC || index == AC97_CD ||
			    index == AC97_VIDEO || index == AC97_AUX) &&
			   bitnr == 15 && !(newreg & 0x8000)) {
			mute_ac97_ctl(chip, CONTROL_LINE_CAPTURE_SWITCH);
			oxygen_write_ac97_masked(chip, 0, CM9780_GPIO_STATUS,
						 CM9780_GPO0, CM9780_GPO0);
		}
	}
	mutex_unlock(&chip->mutex);
	return change;
}

static int ac97_volume_info(struct snd_kcontrol *ctl,
			    struct snd_ctl_elem_info *info)
{
	int stereo = (ctl->private_value >> 16) & 1;

	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = stereo ? 2 : 1;
	info->value.integer.min = 0;
	info->value.integer.max = 0x1f;
	return 0;
}

static int ac97_volume_get(struct snd_kcontrol *ctl,
			   struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int codec = (ctl->private_value >> 24) & 1;
	int stereo = (ctl->private_value >> 16) & 1;
	unsigned int index = ctl->private_value & 0xff;
	u16 reg;

	mutex_lock(&chip->mutex);
	reg = oxygen_read_ac97(chip, codec, index);
	mutex_unlock(&chip->mutex);
	value->value.integer.value[0] = 31 - (reg & 0x1f);
	if (stereo)
		value->value.integer.value[1] = 31 - ((reg >> 8) & 0x1f);
	return 0;
}

static int ac97_volume_put(struct snd_kcontrol *ctl,
			   struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int codec = (ctl->private_value >> 24) & 1;
	int stereo = (ctl->private_value >> 16) & 1;
	unsigned int index = ctl->private_value & 0xff;
	u16 oldreg, newreg;
	int change;

	mutex_lock(&chip->mutex);
	oldreg = oxygen_read_ac97(chip, codec, index);
	newreg = oldreg;
	newreg = (newreg & ~0x1f) |
		(31 - (value->value.integer.value[0] & 0x1f));
	if (stereo)
		newreg = (newreg & ~0x1f00) |
			((31 - (value->value.integer.value[1] & 0x1f)) << 8);
	else
		newreg = (newreg & ~0x1f00) | ((newreg & 0x1f) << 8);
	change = newreg != oldreg;
	if (change)
		oxygen_write_ac97(chip, codec, index, newreg);
	mutex_unlock(&chip->mutex);
	return change;
}

static int mic_fmic_source_info(struct snd_kcontrol *ctl,
			   struct snd_ctl_elem_info *info)
{
	static const char *const names[] = { "Mic Jack", "Front Panel" };

	return snd_ctl_enum_info(info, 1, 2, names);
}

static int mic_fmic_source_get(struct snd_kcontrol *ctl,
			       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;

	mutex_lock(&chip->mutex);
	value->value.enumerated.item[0] =
		!!(oxygen_read_ac97(chip, 0, CM9780_JACK) & CM9780_FMIC2MIC);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int mic_fmic_source_put(struct snd_kcontrol *ctl,
			       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u16 oldreg, newreg;
	int change;

	mutex_lock(&chip->mutex);
	oldreg = oxygen_read_ac97(chip, 0, CM9780_JACK);
	if (value->value.enumerated.item[0])
		newreg = oldreg | CM9780_FMIC2MIC;
	else
		newreg = oldreg & ~CM9780_FMIC2MIC;
	change = newreg != oldreg;
	if (change)
		oxygen_write_ac97(chip, 0, CM9780_JACK, newreg);
	mutex_unlock(&chip->mutex);
	return change;
}

static int ac97_fp_rec_volume_info(struct snd_kcontrol *ctl,
				   struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 2;
	info->value.integer.min = 0;
	info->value.integer.max = 7;
	return 0;
}

static int ac97_fp_rec_volume_get(struct snd_kcontrol *ctl,
				  struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u16 reg;

	mutex_lock(&chip->mutex);
	reg = oxygen_read_ac97(chip, 1, AC97_REC_GAIN);
	mutex_unlock(&chip->mutex);
	value->value.integer.value[0] = reg & 7;
	value->value.integer.value[1] = (reg >> 8) & 7;
	return 0;
}

static int ac97_fp_rec_volume_put(struct snd_kcontrol *ctl,
				  struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u16 oldreg, newreg;
	int change;

	mutex_lock(&chip->mutex);
	oldreg = oxygen_read_ac97(chip, 1, AC97_REC_GAIN);
	newreg = oldreg & ~0x0707;
	newreg = newreg | (value->value.integer.value[0] & 7);
	newreg = newreg | ((value->value.integer.value[0] & 7) << 8);
	change = newreg != oldreg;
	if (change)
		oxygen_write_ac97(chip, 1, AC97_REC_GAIN, newreg);
	mutex_unlock(&chip->mutex);
	return change;
}

#define AC97_SWITCH(xname, codec, index, bitnr, invert) { \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = xname, \
		.info = snd_ctl_boolean_mono_info, \
		.get = ac97_switch_get, \
		.put = ac97_switch_put, \
		.private_value = ((codec) << 24) | ((invert) << 16) | \
				 ((bitnr) << 8) | (index), \
	}
#define AC97_VOLUME(xname, codec, index, stereo) { \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = xname, \
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
			  SNDRV_CTL_ELEM_ACCESS_TLV_READ, \
		.info = ac97_volume_info, \
		.get = ac97_volume_get, \
		.put = ac97_volume_put, \
		.tlv = { .p = ac97_db_scale, }, \
		.private_value = ((codec) << 24) | ((stereo) << 16) | (index), \
	}

static DECLARE_TLV_DB_SCALE(monitor_db_scale, -600, 600, 0);
static DECLARE_TLV_DB_SCALE(ac97_db_scale, -3450, 150, 0);
static DECLARE_TLV_DB_SCALE(ac97_rec_db_scale, 0, 150, 0);

static const struct snd_kcontrol_new controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dac_volume_info,
		.get = dac_volume_get,
		.put = dac_volume_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = dac_mute_get,
		.put = dac_mute_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Stereo Upmixing",
		.info = upmix_info,
		.get = upmix_get,
		.put = upmix_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		.info = snd_ctl_boolean_mono_info,
		.get = spdif_switch_get,
		.put = spdif_switch_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.device = 1,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = spdif_info,
		.get = spdif_default_get,
		.put = spdif_default_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.device = 1,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.info = spdif_info,
		.get = spdif_mask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.device = 1,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, PCM_STREAM),
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			  SNDRV_CTL_ELEM_ACCESS_INACTIVE,
		.info = spdif_info,
		.get = spdif_pcm_get,
		.put = spdif_pcm_put,
	},
};

static const struct snd_kcontrol_new spdif_input_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.device = 1,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, MASK),
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.info = spdif_info,
		.get = spdif_input_mask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.device = 1,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.info = spdif_info,
		.get = spdif_input_default_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("Loopback ", NONE, SWITCH),
		.info = snd_ctl_boolean_mono_info,
		.get = spdif_loopback_get,
		.put = spdif_loopback_put,
	},
};

static const struct {
	unsigned int pcm_dev;
	struct snd_kcontrol_new controls[2];
} monitor_controls[] = {
	{
		.pcm_dev = CAPTURE_0_FROM_I2S_1,
		.controls = {
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Analog Input Monitor Playback Switch",
				.info = snd_ctl_boolean_mono_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_A,
			},
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Analog Input Monitor Playback Volume",
				.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
				.info = monitor_volume_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_A_HALF_VOL
						| (1 << 8),
				.tlv = { .p = monitor_db_scale, },
			},
		},
	},
	{
		.pcm_dev = CAPTURE_0_FROM_I2S_2,
		.controls = {
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Analog Input Monitor Playback Switch",
				.info = snd_ctl_boolean_mono_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_B,
			},
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Analog Input Monitor Playback Volume",
				.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
				.info = monitor_volume_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_B_HALF_VOL
						| (1 << 8),
				.tlv = { .p = monitor_db_scale, },
			},
		},
	},
	{
		.pcm_dev = CAPTURE_2_FROM_I2S_2,
		.controls = {
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Analog Input Monitor Playback Switch",
				.index = 1,
				.info = snd_ctl_boolean_mono_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_B,
			},
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Analog Input Monitor Playback Volume",
				.index = 1,
				.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
				.info = monitor_volume_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_B_HALF_VOL
						| (1 << 8),
				.tlv = { .p = monitor_db_scale, },
			},
		},
	},
	{
		.pcm_dev = CAPTURE_1_FROM_SPDIF,
		.controls = {
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Digital Input Monitor Playback Switch",
				.info = snd_ctl_boolean_mono_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_C,
			},
			{
				.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
				.name = "Digital Input Monitor Playback Volume",
				.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
				.info = monitor_volume_info,
				.get = monitor_get,
				.put = monitor_put,
				.private_value = OXYGEN_ADC_MONITOR_C_HALF_VOL
						| (1 << 8),
				.tlv = { .p = monitor_db_scale, },
			},
		},
	},
};

static const struct snd_kcontrol_new ac97_controls[] = {
	AC97_VOLUME("Mic Capture Volume", 0, AC97_MIC, 0),
	AC97_SWITCH("Mic Capture Switch", 0, AC97_MIC, 15, 1),
	AC97_SWITCH("Mic Boost (+20dB)", 0, AC97_MIC, 6, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mic Source Capture Enum",
		.info = mic_fmic_source_info,
		.get = mic_fmic_source_get,
		.put = mic_fmic_source_put,
	},
	AC97_SWITCH("Line Capture Switch", 0, AC97_LINE, 15, 1),
	AC97_VOLUME("CD Capture Volume", 0, AC97_CD, 1),
	AC97_SWITCH("CD Capture Switch", 0, AC97_CD, 15, 1),
	AC97_VOLUME("Aux Capture Volume", 0, AC97_AUX, 1),
	AC97_SWITCH("Aux Capture Switch", 0, AC97_AUX, 15, 1),
};

static const struct snd_kcontrol_new ac97_fp_controls[] = {
	AC97_VOLUME("Front Panel Playback Volume", 1, AC97_HEADPHONE, 1),
	AC97_SWITCH("Front Panel Playback Switch", 1, AC97_HEADPHONE, 15, 1),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Front Panel Capture Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = ac97_fp_rec_volume_info,
		.get = ac97_fp_rec_volume_get,
		.put = ac97_fp_rec_volume_put,
		.tlv = { .p = ac97_rec_db_scale, },
	},
	AC97_SWITCH("Front Panel Capture Switch", 1, AC97_REC_GAIN, 15, 1),
};

static void oxygen_any_ctl_free(struct snd_kcontrol *ctl)
{
	struct oxygen *chip = ctl->private_data;
	unsigned int i;

	/* I'm too lazy to write a function for each control :-) */
	for (i = 0; i < ARRAY_SIZE(chip->controls); ++i)
		chip->controls[i] = NULL;
}

static int add_controls(struct oxygen *chip,
			const struct snd_kcontrol_new controls[],
			unsigned int count)
{
	static const char *const known_ctl_names[CONTROL_COUNT] = {
		[CONTROL_SPDIF_PCM] =
			SNDRV_CTL_NAME_IEC958("", PLAYBACK, PCM_STREAM),
		[CONTROL_SPDIF_INPUT_BITS] =
			SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		[CONTROL_MIC_CAPTURE_SWITCH] = "Mic Capture Switch",
		[CONTROL_LINE_CAPTURE_SWITCH] = "Line Capture Switch",
		[CONTROL_CD_CAPTURE_SWITCH] = "CD Capture Switch",
		[CONTROL_AUX_CAPTURE_SWITCH] = "Aux Capture Switch",
	};
	unsigned int i, j;
	struct snd_kcontrol_new template;
	struct snd_kcontrol *ctl;
	int err;

	for (i = 0; i < count; ++i) {
		template = controls[i];
		if (chip->model.control_filter) {
			err = chip->model.control_filter(&template);
			if (err < 0)
				return err;
			if (err == 1)
				continue;
		}
		if (!strcmp(template.name, "Stereo Upmixing") &&
		    chip->model.dac_channels_pcm == 2)
			continue;
		if (!strcmp(template.name, "Mic Source Capture Enum") &&
		    !(chip->model.device_config & AC97_FMIC_SWITCH))
			continue;
		if (!strncmp(template.name, "CD Capture ", 11) &&
		    !(chip->model.device_config & AC97_CD_INPUT))
			continue;
		if (!strcmp(template.name, "Master Playback Volume") &&
		    chip->model.dac_tlv) {
			template.tlv.p = chip->model.dac_tlv;
			template.access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		}
		ctl = snd_ctl_new1(&template, chip);
		if (!ctl)
			return -ENOMEM;
		err = snd_ctl_add(chip->card, ctl);
		if (err < 0)
			return err;
		for (j = 0; j < CONTROL_COUNT; ++j)
			if (!strcmp(ctl->id.name, known_ctl_names[j])) {
				chip->controls[j] = ctl;
				ctl->private_free = oxygen_any_ctl_free;
			}
	}
	return 0;
}

int oxygen_mixer_init(struct oxygen *chip)
{
	unsigned int i;
	int err;

	err = add_controls(chip, controls, ARRAY_SIZE(controls));
	if (err < 0)
		return err;
	if (chip->model.device_config & CAPTURE_1_FROM_SPDIF) {
		err = add_controls(chip, spdif_input_controls,
				   ARRAY_SIZE(spdif_input_controls));
		if (err < 0)
			return err;
	}
	for (i = 0; i < ARRAY_SIZE(monitor_controls); ++i) {
		if (!(chip->model.device_config & monitor_controls[i].pcm_dev))
			continue;
		err = add_controls(chip, monitor_controls[i].controls,
				   ARRAY_SIZE(monitor_controls[i].controls));
		if (err < 0)
			return err;
	}
	if (chip->has_ac97_0) {
		err = add_controls(chip, ac97_controls,
				   ARRAY_SIZE(ac97_controls));
		if (err < 0)
			return err;
	}
	if (chip->has_ac97_1) {
		err = add_controls(chip, ac97_fp_controls,
				   ARRAY_SIZE(ac97_fp_controls));
		if (err < 0)
			return err;
	}
	return chip->model.mixer_init ? chip->model.mixer_init(chip) : 0;
}
