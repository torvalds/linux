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

#define MAX_MIDI_DEVICES	8

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};
static int midi_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for virmidi soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for virmidi soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this soundcard.");
module_param_array(midi_devs, int, NULL, 0444);
MODULE_PARM_DESC(midi_devs, "MIDI devices # (1-8)");

typedef struct snd_card_virmidi {
	snd_card_t *card;
	snd_rawmidi_t *midi[MAX_MIDI_DEVICES];
} snd_card_virmidi_t;

static snd_card_t *snd_virmidi_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_card_virmidi_probe(int dev)
{
	snd_card_t *card;
	struct snd_card_virmidi *vmidi;
	int idx, err;

	if (!enable[dev])
		return -ENODEV;
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
		snd_rawmidi_t *rmidi;
		snd_virmidi_dev_t *rdev;
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

	if ((err = snd_card_set_generic_dev(card)) < 0)
		goto __nodev;

	if ((err = snd_card_register(card)) == 0) {
		snd_virmidi_cards[dev] = card;
		return 0;
	}
      __nodev:
	snd_card_free(card);
	return err;
}

static int __init alsa_card_virmidi_init(void)
{
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && enable[dev]; dev++) {
		if (snd_card_virmidi_probe(dev) < 0) {
#ifdef MODULE
			printk(KERN_ERR "Card-VirMIDI #%i not found or device busy\n", dev + 1);
#endif
			break;
		}
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "Card-VirMIDI soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_virmidi_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_virmidi_cards[dev]);
}

module_init(alsa_card_virmidi_init)
module_exit(alsa_card_virmidi_exit)
