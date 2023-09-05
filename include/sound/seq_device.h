/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_SEQ_DEVICE_H
#define __SOUND_SEQ_DEVICE_H

/*
 *  ALSA sequencer device management
 *  Copyright (c) 1999 by Takashi Iwai <tiwai@suse.de>
 */

/*
 * registered device information
 */

struct snd_seq_device {
	/* device info */
	struct snd_card *card;	/* sound card */
	int device;		/* device number */
	const char *id;		/* driver id */
	char name[80];		/* device name */
	int argsize;		/* size of the argument */
	void *driver_data;	/* private data for driver */
	void *private_data;	/* private data for the caller */
	void (*private_free)(struct snd_seq_device *device);
	struct device dev;
};

#define to_seq_dev(_dev) \
	container_of(_dev, struct snd_seq_device, dev)

/* sequencer driver */

/* driver operators
 * probe:
 *	Initialize the device with given parameters.
 *	Typically,
 *		1. call snd_hwdep_new
 *		2. allocate private data and initialize it
 *		3. call snd_hwdep_register
 *		4. store the instance to dev->driver_data pointer.
 *		
 * remove:
 *	Release the private data.
 *	Typically, call snd_device_free(dev->card, dev->driver_data)
 */
struct snd_seq_driver {
	struct device_driver driver;
	char *id;
	int argsize;
};

#define to_seq_drv(_drv) \
	container_of(_drv, struct snd_seq_driver, driver)

/*
 * prototypes
 */
#ifdef CONFIG_MODULES
void snd_seq_device_load_drivers(void);
#else
#define snd_seq_device_load_drivers()
#endif
int snd_seq_device_new(struct snd_card *card, int device, const char *id,
		       int argsize, struct snd_seq_device **result);

#define SNDRV_SEQ_DEVICE_ARGPTR(dev) (void *)((char *)(dev) + sizeof(struct snd_seq_device))

int __must_check __snd_seq_driver_register(struct snd_seq_driver *drv,
					   struct module *mod);
#define snd_seq_driver_register(drv) \
	__snd_seq_driver_register(drv, THIS_MODULE)
void snd_seq_driver_unregister(struct snd_seq_driver *drv);

#define module_snd_seq_driver(drv) \
	module_driver(drv, snd_seq_driver_register, snd_seq_driver_unregister)

/*
 * id strings for generic devices
 */
#define SNDRV_SEQ_DEV_ID_MIDISYNTH	"seq-midi"
#define SNDRV_SEQ_DEV_ID_OPL3		"opl3-synth"
#define SNDRV_SEQ_DEV_ID_UMP		"seq-ump-client"

#endif /* __SOUND_SEQ_DEVICE_H */
