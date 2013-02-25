/*
 * Vortex Mixer support.
 *
 * There is much more than just the AC97 mixer...
 *
 */

#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include "au88x0.h"

static int remove_ctl(struct snd_card *card, const char *name)
{
	struct snd_ctl_elem_id id;
	memset(&id, 0, sizeof(id));
	strcpy(id.name, name);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_remove_id(card, &id);
}

static int snd_vortex_mixer(vortex_t *vortex)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.write = vortex_codec_write,
		.read = vortex_codec_read,
	};

	if ((err = snd_ac97_bus(vortex->card, 0, &ops, NULL, &pbus)) < 0)
		return err;
	memset(&ac97, 0, sizeof(ac97));
	// Initialize AC97 codec stuff.
	ac97.private_data = vortex;
	ac97.scaps = AC97_SCAP_NO_SPDIF;
	err = snd_ac97_mixer(pbus, &ac97, &vortex->codec);
	vortex->isquad = ((vortex->codec == NULL) ?  0 : (vortex->codec->ext_id&0x80));
	remove_ctl(vortex->card, "Master Mono Playback Volume");
	remove_ctl(vortex->card, "Master Mono Playback Switch");
	return err;
}
