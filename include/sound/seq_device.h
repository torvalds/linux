#ifndef __SOUND_SEQ_DEVICE_H
#define __SOUND_SEQ_DEVICE_H

/*
 *  ALSA sequencer device management
 *  Copyright (c) 1999 by Takashi Iwai <tiwai@suse.de>
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
 * registered device information
 */

#define ID_LEN	32

/* status flag */
#define SNDRV_SEQ_DEVICE_FREE		0
#define SNDRV_SEQ_DEVICE_REGISTERED	1

struct snd_seq_device {
	/* device info */
	struct snd_card *card;	/* sound card */
	int device;		/* device number */
	char id[ID_LEN];	/* driver id */
	char name[80];		/* device name */
	int argsize;		/* size of the argument */
	void *driver_data;	/* private data for driver */
	int status;		/* flag - read only */
	void *private_data;	/* private data for the caller */
	void (*private_free)(struct snd_seq_device *device);
	struct list_head list;	/* link to next device */
};


/* driver operators
 * init_device:
 *	Initialize the device with given parameters.
 *	Typically,
 *		1. call snd_hwdep_new
 *		2. allocate private data and initialize it
 *		3. call snd_hwdep_register
 *		4. store the instance to dev->driver_data pointer.
 *		
 * free_device:
 *	Release the private data.
 *	Typically, call snd_device_free(dev->card, dev->driver_data)
 */
struct snd_seq_dev_ops {
	int (*init_device)(struct snd_seq_device *dev);
	int (*free_device)(struct snd_seq_device *dev);
};

/*
 * prototypes
 */
void snd_seq_device_load_drivers(void);
int snd_seq_device_new(struct snd_card *card, int device, char *id, int argsize, struct snd_seq_device **result);
int snd_seq_device_register_driver(char *id, struct snd_seq_dev_ops *entry, int argsize);
int snd_seq_device_unregister_driver(char *id);

#define SNDRV_SEQ_DEVICE_ARGPTR(dev) (void *)((char *)(dev) + sizeof(struct snd_seq_device))


/*
 * id strings for generic devices
 */
#define SNDRV_SEQ_DEV_ID_MIDISYNTH	"seq-midi"
#define SNDRV_SEQ_DEV_ID_OPL3		"opl3-synth"

#endif /* __SOUND_SEQ_DEVICE_H */
