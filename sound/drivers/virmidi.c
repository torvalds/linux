/*
 *  Dummy soundcard for virtual rawmidi devices
 *
 *  Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>
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

/*
 * VIRTUAL RAW MIDI DEVICE CARDS
 *
 * This dummy card contains up to 4 virtual rawmidi devices.
 * They are not real rawmidi devices but just associated with sequencer
 * clients, so that any input/output sources can be connected as a raw
 * MIDI device arbitrary.
 * Also, multiple access is allowed to a single rawmidi device.
 *
 * Typical usage is like following:
 * - Load snd-virmidi module.
 *	# modprobe snd-virmidi index=2
 *   Then, sequencer clients 72:0 to 75:0 will be created, which are
 *   mapped from /dev/snd/midiC1D0 to /dev/snd/midiC1D3, respectively.
 *
 * - Connect input/output via aconnect.
 *	% aconnect 64:0 72:0	# keyboard input redirection 64:0 -> 72:0
 *	% aconnect 72:0 65:0	# output device redirection 72:0 -> 65:0
 *
 * - Run application using a midi device (eg. /dev/snd/midiC1D0)
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/seq_kernel.h>
#include <sound/seq_virmidi.h>
#include <sound/initval.h>

/* hack: OSS defines midi_devs, so undefine it (versioned symbols) */
#undef midi_devs

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Dummy soundcard for virtual rawmidi devices");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,Virtual rawmidi device}}");

#define MAX_MIDI_DEVICES	4

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS];
static int midi_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for virmidi soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for virmidi soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this soundcard.");
module_param_array(midi_devs, int, NULL, 0444);
MODULE_PARM_DESC(midi_devs, "MIDI devices # (1-4)");

struct snd_card_virmidi {
	struct snd_card *card;
	struct snd_rawmidi *midi[MAX_MIDI_DEVICES];
};

static struct platform_device *devices[SNDRV_CARDS];


static int __init snd_virmidi_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct snd_card_virmidi *vmidi;
	int idx, err;
	int dev = devptr->id;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_card_virmidi));
	if (card == NULL)
		return -ENOMEM;
	vmidi = (struct snd_card_virmidi *)card->private_data;
	vmidi->card = card;

	if (midi_devs[dev] > MAX_MIDI_DEVICES) {
		snd_printk("too much midi devices for virmidi %d: force to use %d\n", dev, MAX_MIDI_DEVICES);
		midi_devs[dev] = MAX_MIDI_DEVICES;
	}
	for (idx = 0; idx < midi_devs[dev]; idx++) {
		struct snd_rawmidi *rmidi;
		struct snd_virmidi_dev *rdev;
		if ((err = snd_virmidi_new(card, idx, &rmidi)) < 0)
			goto __nodev;
		rdev = rmidi->private_data;
		vmidi->midi[idx] = rmidi;
		strcpy(rmidi->name, "Virtual Raw MIDI");
		rdev->seq_mode = SNDRV_VIRMIDI_SEQ_DISPATCH;
	}
	
	strcpy(card->driver, "VirMIDI");
	strcpy(card->shortname, "VirMIDI");
	sprintf(card->longname, "Virtual MIDI Card %i", dev + 1);

	snd_card_set_dev(card, &devptr->dev);

	if ((err = snd_card_register(card)) == 0) {
		platform_set_drvdata(devptr, card);
		return 0;
	}
      __nodev:
	snd_card_free(card);
	return err;
}

static int snd_virmidi_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#define SND_VIRMIDI_DRIVER	"snd_virmidi"

static struct platform_driver snd_virmidi_driver = {
	.probe		= snd_virmidi_probe,
	.remove		= snd_virmidi_remove,
	.driver		= {
		.name	= SND_VIRMIDI_DRIVER
	},
};

static void __init_or_module snd_virmidi_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_virmidi_driver);
}

static int __init alsa_card_virmidi_init(void)
{
	int i, cards, err;

	if ((err = platform_driver_register(&snd_virmidi_driver)) < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;
		if (! enable[i])
			continue;
		device = platform_device_register_simple(SND_VIRMIDI_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device))
			continue;
		if (!platform_get_drvdata(device)) {
			platform_device_unregister(device);
			continue;
		}
		devices[i] = device;
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "Card-VirMIDI soundcard not found or device busy\n");
#endif
		snd_virmidi_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_virmidi_exit(void)
{
	snd_virmidi_unregister_all();
}

module_init(alsa_card_virmidi_init)
module_exit(alsa_card_virmidi_exit)
