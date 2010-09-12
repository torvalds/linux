/*
 *  Interface for hwdep device
 *
 *  Copyright (C) 2004 Takashi Iwai <tiwai@suse.de>
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

#include <sound/core.h>
#include <sound/hwdep.h>
#include <asm/uaccess.h>
#include "emux_voice.h"


#define TMP_CLIENT_ID	0x1001

/*
 * load patch
 */
static int
snd_emux_hwdep_load_patch(struct snd_emux *emu, void __user *arg)
{
	int err;
	struct soundfont_patch_info patch;

	if (copy_from_user(&patch, arg, sizeof(patch)))
		return -EFAULT;

	if (patch.type >= SNDRV_SFNT_LOAD_INFO &&
	    patch.type <= SNDRV_SFNT_PROBE_DATA) {
		err = snd_soundfont_load(emu->sflist, arg, patch.len + sizeof(patch), TMP_CLIENT_ID);
		if (err < 0)
			return err;
	} else {
		if (emu->ops.load_fx)
			return emu->ops.load_fx(emu, patch.type, patch.optarg, arg, patch.len + sizeof(patch));
		else
			return -EINVAL;
	}
	return 0;
}

/*
 * set misc mode
 */
static int
snd_emux_hwdep_misc_mode(struct snd_emux *emu, void __user *arg)
{
	struct snd_emux_misc_mode info;
	int i;

	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;
	if (info.mode < 0 || info.mode >= EMUX_MD_END)
		return -EINVAL;

	if (info.port < 0) {
		for (i = 0; i < emu->num_ports; i++)
			emu->portptrs[i]->ctrls[info.mode] = info.value;
	} else {
		if (info.port < emu->num_ports)
			emu->portptrs[info.port]->ctrls[info.mode] = info.value;
	}
	return 0;
}


/*
 * ioctl
 */
static int
snd_emux_hwdep_ioctl(struct snd_hwdep * hw, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct snd_emux *emu = hw->private_data;

	switch (cmd) {
	case SNDRV_EMUX_IOCTL_VERSION:
		return put_user(SNDRV_EMUX_VERSION, (unsigned int __user *)arg);
	case SNDRV_EMUX_IOCTL_LOAD_PATCH:
		return snd_emux_hwdep_load_patch(emu, (void __user *)arg);
	case SNDRV_EMUX_IOCTL_RESET_SAMPLES:
		snd_soundfont_remove_samples(emu->sflist);
		break;
	case SNDRV_EMUX_IOCTL_REMOVE_LAST_SAMPLES:
		snd_soundfont_remove_unlocked(emu->sflist);
		break;
	case SNDRV_EMUX_IOCTL_MEM_AVAIL:
		if (emu->memhdr) {
			int size = snd_util_mem_avail(emu->memhdr);
			return put_user(size, (unsigned int __user *)arg);
		}
		break;
	case SNDRV_EMUX_IOCTL_MISC_MODE:
		return snd_emux_hwdep_misc_mode(emu, (void __user *)arg);
	}

	return 0;
}


/*
 * register hwdep device
 */

int
snd_emux_init_hwdep(struct snd_emux *emu)
{
	struct snd_hwdep *hw;
	int err;

	if ((err = snd_hwdep_new(emu->card, SNDRV_EMUX_HWDEP_NAME, emu->hwdep_idx, &hw)) < 0)
		return err;
	emu->hwdep = hw;
	strcpy(hw->name, SNDRV_EMUX_HWDEP_NAME);
	hw->iface = SNDRV_HWDEP_IFACE_EMUX_WAVETABLE;
	hw->ops.ioctl = snd_emux_hwdep_ioctl;
	/* The ioctl parameter types are compatible between 32- and
	 * 64-bit architectures, so use the same function. */
	hw->ops.ioctl_compat = snd_emux_hwdep_ioctl;
	hw->exclusive = 1;
	hw->private_data = emu;
	if ((err = snd_card_register(emu->card)) < 0)
		return err;

	return 0;
}


/*
 * unregister
 */
void
snd_emux_delete_hwdep(struct snd_emux *emu)
{
	if (emu->hwdep) {
		snd_device_free(emu->card, emu->hwdep);
		emu->hwdep = NULL;
	}
}
