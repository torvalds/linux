// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Routines for control of the TEA6330T circuit via i2c bus
 *  Sound fader control circuit for car radios by Philips Semiconductors
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tea6330t.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Routines for control of the TEA6330T circuit via i2c bus");
MODULE_LICENSE("GPL");

#define TEA6330T_ADDR			(0x80>>1) /* fixed address */

#define TEA6330T_SADDR_VOLUME_LEFT	0x00	/* volume left */
#define TEA6330T_SADDR_VOLUME_RIGHT	0x01	/* volume right */
#define TEA6330T_SADDR_BASS		0x02	/* bass control */
#define TEA6330T_SADDR_TREBLE		0x03	/* treble control */
#define TEA6330T_SADDR_FADER		0x04	/* fader control */
#define   TEA6330T_MFN			0x20	/* mute control for selected channels */
#define   TEA6330T_FCH			0x10	/* select fader channels - front or rear */
#define TEA6330T_SADDR_AUDIO_SWITCH	0x05	/* audio switch */
#define   TEA6330T_GMU			0x80	/* mute control, general mute */
#define   TEA6330T_EQN			0x40	/* equalizer switchover (0=equalizer-on) */


struct tea6330t {
	struct snd_i2c_device *device;
	struct snd_i2c_bus *bus;
	int equalizer;
	int fader;
	unsigned char regs[8];
	unsigned char mleft, mright;
	unsigned char bass, treble;
	unsigned char max_bass, max_treble;
};


int snd_tea6330t_detect(struct snd_i2c_bus *bus, int equalizer)
{
	int res;

	snd_i2c_lock(bus);
	res = snd_i2c_probeaddr(bus, TEA6330T_ADDR);
	snd_i2c_unlock(bus);
	return res;
}

#if 0
static void snd_tea6330t_set(struct tea6330t *tea,
			     unsigned char addr, unsigned char value)
{
#if 0
	printk(KERN_DEBUG "set - 0x%x/0x%x\n", addr, value);
#endif
	snd_i2c_write(tea->bus, TEA6330T_ADDR, addr, value, 1);
}
#endif

#define TEA6330T_MASTER_VOLUME(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_tea6330t_info_master_volume, \
  .get = snd_tea6330t_get_master_volume, .put = snd_tea6330t_put_master_volume }

static int snd_tea6330t_info_master_volume(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 43;
	return 0;
}

static int snd_tea6330t_get_master_volume(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	
	snd_i2c_lock(tea->bus);
	ucontrol->value.integer.value[0] = tea->mleft - 0x14;
	ucontrol->value.integer.value[1] = tea->mright - 0x14;
	snd_i2c_unlock(tea->bus);
	return 0;
}

static int snd_tea6330t_put_master_volume(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	int change, count, err;
	unsigned char bytes[3];
	unsigned char val1, val2;
	
	val1 = (ucontrol->value.integer.value[0] % 44) + 0x14;
	val2 = (ucontrol->value.integer.value[1] % 44) + 0x14;
	snd_i2c_lock(tea->bus);
	change = val1 != tea->mleft || val2 != tea->mright;
	tea->mleft = val1;
	tea->mright = val2;
	count = 0;
	if (tea->regs[TEA6330T_SADDR_VOLUME_LEFT] != 0) {
		bytes[count++] = TEA6330T_SADDR_VOLUME_LEFT;
		bytes[count++] = tea->regs[TEA6330T_SADDR_VOLUME_LEFT] = tea->mleft;
	}
	if (tea->regs[TEA6330T_SADDR_VOLUME_RIGHT] != 0) {
		if (count == 0)
			bytes[count++] = TEA6330T_SADDR_VOLUME_RIGHT;
		bytes[count++] = tea->regs[TEA6330T_SADDR_VOLUME_RIGHT] = tea->mright;
	}
	if (count > 0) {
		if ((err = snd_i2c_sendbytes(tea->device, bytes, count)) < 0)
			change = err;
	}
	snd_i2c_unlock(tea->bus);
	return change;
}

#define TEA6330T_MASTER_SWITCH(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_tea6330t_info_master_switch, \
  .get = snd_tea6330t_get_master_switch, .put = snd_tea6330t_put_master_switch }

#define snd_tea6330t_info_master_switch		snd_ctl_boolean_stereo_info

static int snd_tea6330t_get_master_switch(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	
	snd_i2c_lock(tea->bus);
	ucontrol->value.integer.value[0] = tea->regs[TEA6330T_SADDR_VOLUME_LEFT] == 0 ? 0 : 1;
	ucontrol->value.integer.value[1] = tea->regs[TEA6330T_SADDR_VOLUME_RIGHT] == 0 ? 0 : 1;
	snd_i2c_unlock(tea->bus);
	return 0;
}

static int snd_tea6330t_put_master_switch(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	int change, err;
	unsigned char bytes[3];
	unsigned char oval1, oval2, val1, val2;
	
	val1 = ucontrol->value.integer.value[0] & 1;
	val2 = ucontrol->value.integer.value[1] & 1;
	snd_i2c_lock(tea->bus);
	oval1 = tea->regs[TEA6330T_SADDR_VOLUME_LEFT] == 0 ? 0 : 1;
	oval2 = tea->regs[TEA6330T_SADDR_VOLUME_RIGHT] == 0 ? 0 : 1;
	change = val1 != oval1 || val2 != oval2;
	tea->regs[TEA6330T_SADDR_VOLUME_LEFT] = val1 ? tea->mleft : 0;
	tea->regs[TEA6330T_SADDR_VOLUME_RIGHT] = val2 ? tea->mright : 0;
	bytes[0] = TEA6330T_SADDR_VOLUME_LEFT;
	bytes[1] = tea->regs[TEA6330T_SADDR_VOLUME_LEFT];
	bytes[2] = tea->regs[TEA6330T_SADDR_VOLUME_RIGHT];
	if ((err = snd_i2c_sendbytes(tea->device, bytes, 3)) < 0)
		change = err;
	snd_i2c_unlock(tea->bus);
	return change;
}

#define TEA6330T_BASS(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_tea6330t_info_bass, \
  .get = snd_tea6330t_get_bass, .put = snd_tea6330t_put_bass }

static int snd_tea6330t_info_bass(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tea->max_bass;
	return 0;
}

static int snd_tea6330t_get_bass(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = tea->bass;
	return 0;
}

static int snd_tea6330t_put_bass(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	int change, err;
	unsigned char bytes[2];
	unsigned char val1;
	
	val1 = ucontrol->value.integer.value[0] % (tea->max_bass + 1);
	snd_i2c_lock(tea->bus);
	tea->bass = val1;
	val1 += tea->equalizer ? 7 : 3;
	change = tea->regs[TEA6330T_SADDR_BASS] != val1;
	bytes[0] = TEA6330T_SADDR_BASS;
	bytes[1] = tea->regs[TEA6330T_SADDR_BASS] = val1;
	if ((err = snd_i2c_sendbytes(tea->device, bytes, 2)) < 0)
		change = err;
	snd_i2c_unlock(tea->bus);
	return change;
}

#define TEA6330T_TREBLE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_tea6330t_info_treble, \
  .get = snd_tea6330t_get_treble, .put = snd_tea6330t_put_treble }

static int snd_tea6330t_info_treble(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tea->max_treble;
	return 0;
}

static int snd_tea6330t_get_treble(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = tea->treble;
	return 0;
}

static int snd_tea6330t_put_treble(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct tea6330t *tea = snd_kcontrol_chip(kcontrol);
	int change, err;
	unsigned char bytes[2];
	unsigned char val1;
	
	val1 = ucontrol->value.integer.value[0] % (tea->max_treble + 1);
	snd_i2c_lock(tea->bus);
	tea->treble = val1;
	val1 += 3;
	change = tea->regs[TEA6330T_SADDR_TREBLE] != val1;
	bytes[0] = TEA6330T_SADDR_TREBLE;
	bytes[1] = tea->regs[TEA6330T_SADDR_TREBLE] = val1;
	if ((err = snd_i2c_sendbytes(tea->device, bytes, 2)) < 0)
		change = err;
	snd_i2c_unlock(tea->bus);
	return change;
}

static struct snd_kcontrol_new snd_tea6330t_controls[] = {
TEA6330T_MASTER_SWITCH("Master Playback Switch", 0),
TEA6330T_MASTER_VOLUME("Master Playback Volume", 0),
TEA6330T_BASS("Tone Control - Bass", 0),
TEA6330T_TREBLE("Tone Control - Treble", 0)
};

static void snd_tea6330_free(struct snd_i2c_device *device)
{
	kfree(device->private_data);
}
                                        
int snd_tea6330t_update_mixer(struct snd_card *card,
			      struct snd_i2c_bus *bus,
			      int equalizer, int fader)
{
	struct snd_i2c_device *device;
	struct tea6330t *tea;
	struct snd_kcontrol_new *knew;
	unsigned int idx;
	int err = -ENOMEM;
	u8 default_treble, default_bass;
	unsigned char bytes[7];

	tea = kzalloc(sizeof(*tea), GFP_KERNEL);
	if (tea == NULL)
		return -ENOMEM;
	if ((err = snd_i2c_device_create(bus, "TEA6330T", TEA6330T_ADDR, &device)) < 0) {
		kfree(tea);
		return err;
	}
	tea->device = device;
	tea->bus = bus;
	tea->equalizer = equalizer;
	tea->fader = fader;
	device->private_data = tea;
	device->private_free = snd_tea6330_free;

	snd_i2c_lock(bus);

	/* turn fader off and handle equalizer */
	tea->regs[TEA6330T_SADDR_FADER] = 0x3f;
	tea->regs[TEA6330T_SADDR_AUDIO_SWITCH] = equalizer ? 0 : TEA6330T_EQN;
	/* initialize mixer */
	if (!tea->equalizer) {
		tea->max_bass = 9;
		tea->max_treble = 8;
		default_bass = 3 + 4;
		tea->bass = 4;
		default_treble = 3 + 4;
		tea->treble = 4;
	} else {
		tea->max_bass = 5;
		tea->max_treble = 0;
		default_bass = 7 + 4;
		tea->bass = 4;
		default_treble = 3;
		tea->treble = 0;
	}
	tea->mleft = tea->mright = 0x14;
	tea->regs[TEA6330T_SADDR_BASS] = default_bass;
	tea->regs[TEA6330T_SADDR_TREBLE] = default_treble;

	/* compose I2C message and put the hardware to initial state */
	bytes[0] = TEA6330T_SADDR_VOLUME_LEFT;
	for (idx = 0; idx < 6; idx++)
		bytes[idx+1] = tea->regs[idx];
	if ((err = snd_i2c_sendbytes(device, bytes, 7)) < 0)
		goto __error;

	strcat(card->mixername, ",TEA6330T");
	if ((err = snd_component_add(card, "TEA6330T")) < 0)
		goto __error;

	for (idx = 0; idx < ARRAY_SIZE(snd_tea6330t_controls); idx++) {
		knew = &snd_tea6330t_controls[idx];
		if (tea->treble == 0 && !strcmp(knew->name, "Tone Control - Treble"))
			continue;
		if ((err = snd_ctl_add(card, snd_ctl_new1(knew, tea))) < 0)
			goto __error;
	}

	snd_i2c_unlock(bus);
	return 0;
	
      __error:
      	snd_i2c_unlock(bus);
      	snd_i2c_device_free(device);
      	return err;
}

EXPORT_SYMBOL(snd_tea6330t_detect);
EXPORT_SYMBOL(snd_tea6330t_update_mixer);
