#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <linux/soundcard.h>

#include "aml_pcm.h"
#include "aml_alsa_common.h"
#include "aml_audio_hw.h"

extern audio_tone_control_t audio_tone_control;

static int pcm_pb_volume_info(struct snd_kcontrol *kcontrol,
                              struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 100;
    uinfo->value.integer.step = 1;

    return 0;
}

static int pcm_pb_volume_get(struct snd_kcontrol *kcontrol,
                             struct snd_ctl_elem_value *uvalue)
{
    int val;

    val = get_mixer_output_volume();
    val = val & 0xff;
    uvalue->value.integer.value[0] = val;

    return 0;
}

static int pcm_pb_volume_put(struct snd_kcontrol *kcontrol,
                             struct snd_ctl_elem_value *uvalue)
{
    int volume;

    volume = uvalue->value.integer.value[0];
  //  volume = volume | (volume << 8);
    set_mixer_output_volume(volume);
    return 0;
}

static int pcm_pb_mute_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;

    return 0;
}

static int pcm_pb_mute_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *uvalue)
{
    return 0;
}

static int pcm_pb_mute_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
     int flag;

     flag = uvalue->value.integer.value[0];
     if(flag)
	 	audio_i2s_unmute();
	 else
	 	audio_i2s_mute();

     return 0;
}

static int pcm_left_mono_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;

    return 0;
}

static int pcm_left_mono_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *uvalue)
{
    return 0;
}

static int pcm_left_mono_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
     int flag;

     flag = uvalue->value.integer.value[0];
     if(flag){
	 	audio_i2s_swap_left_right(1);
     }

     return 0;
}

static int pcm_right_mono_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;

    return 0;
}

static int pcm_right_mono_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *uvalue)
{
    return 0;
}

static int pcm_right_mono_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
     int flag;

     flag = uvalue->value.integer.value[0];
     if(flag){
	 	audio_i2s_swap_left_right(2);
     }

     return 0;
}

static int pcm_stereo_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;

    return 0;
}

static int pcm_stereo_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *uvalue)
{
    return 0;
}

static int pcm_stereo_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
     int flag;

     flag = uvalue->value.integer.value[0];
     if(flag){
	 	audio_i2s_swap_left_right(0);
     }

     return 0;
}

static int pcm_swap_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;

    return 0;
}

static int pcm_swap_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *uvalue)
{
    return 0;
}

static int pcm_swap_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
     int flag;
     unsigned int reg;

     flag = uvalue->value.integer.value[0];
     if(flag){
		reg = read_i2s_mute_swap_reg();
		if((reg & 0x3))
			audio_i2s_swap_left_right(0);
		else
			audio_i2s_swap_left_right(3);
     }

     return 0;
}

static int pcm_pb_data_info(struct snd_kcontrol *kcontrol,
						struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
    uinfo->count = 128;

    return 0;
}

static int pcm_pb_data_get(struct snd_kcontrol *kcontrol,
						struct snd_ctl_elem_value *uvalue)
{
    unsigned int rd_ptr;

    rd_ptr = read_i2s_rd_ptr();
    memcpy(uvalue->value.bytes.data, (unsigned char*)rd_ptr, 128);
    return 0;
}

static int pcm_pb_tone_info(struct snd_kcontrol *kcontrol,
						struct snd_ctl_elem_info *uinfo)
{
    uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    uinfo->count = 1;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = 1;

    return 0;
}

static int pcm_pb_tone_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
     audio_tone_control.tone_flag = 1;
	
     return 0;
}
static int pcm_pb_tone_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
     return 0;
}
struct snd_kcontrol_new pcm_control_pb_vol = {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .name = "Master Playback Volume",
    .index = 0x00,
    .info = pcm_pb_volume_info,
    .get = pcm_pb_volume_get,
    .put = pcm_pb_volume_put,
    .private_value = 0x0,
};

struct snd_kcontrol_new pcm_switch_pb_mute = {
     .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
     .name = "switch playback mute",
     .index = 0x00,
     .info = pcm_pb_mute_info,
     .get = pcm_pb_mute_get,
     .put = pcm_pb_mute_put,
     .private_value = 0xff,
};

struct snd_kcontrol_new pcm_pb_left_mono = {
     .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
     .name = "Playback Left Mono",
     .index = 0x00,
     .info = pcm_left_mono_info,
     .get = pcm_left_mono_get,
     .put = pcm_left_mono_put,
     .private_value = 0xff,
};

struct snd_kcontrol_new pcm_pb_right_mono = {
     .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
     .name = "Playback Right Mono",
     .index = 0x00,
     .info = pcm_right_mono_info,
     .get = pcm_right_mono_get,
     .put = pcm_right_mono_put,
     .private_value = 0xff,
};

struct snd_kcontrol_new pcm_pb_stereo = {
     .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
     .name = "Playback Stereo Out",
     .index = 0x00,
     .info = pcm_stereo_info,
     .get = pcm_stereo_get,
     .put = pcm_stereo_put,
     .private_value = 0xff,
};

struct snd_kcontrol_new pcm_pb_swap = {
     .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
     .name = "Playback Swap Left Right",
     .index = 0x00,
     .info = pcm_swap_info,
     .get = pcm_swap_get,
     .put = pcm_swap_put,
     .private_value = 0xff,
};

struct snd_kcontrol_new pcm_data_read = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Playback Data Get",
	.info = pcm_pb_data_info,
	.get = pcm_pb_data_get,
	.access = (SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE),
};

struct snd_kcontrol_new pcm_tone_play = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Playback Tone",
	.info = pcm_pb_tone_info,
	.put = pcm_pb_tone_put,
	.get = pcm_pb_tone_get,
	.access = (SNDRV_CTL_ELEM_ACCESS_WRITE |
	          SNDRV_CTL_ELEM_ACCESS_READ), 
	          
};

int aml_alsa_create_ctrl(struct snd_card *card, void *p_value)
{
    int err = 0;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_control_pb_vol, p_value))) < 0)
        return err;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_switch_pb_mute, p_value))) < 0)
        return err;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_pb_left_mono, p_value))) < 0)
        return err;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_pb_right_mono, p_value))) < 0)
        return err;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_pb_stereo, p_value))) < 0)
        return err;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_pb_swap, p_value))) < 0)
        return err;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_data_read, p_value))) < 0)
        return err;

    if ((err =
         snd_ctl_add(card,
                     snd_ctl_new1(&pcm_tone_play, p_value))) < 0)
        return err;
	
    return 0;
}
