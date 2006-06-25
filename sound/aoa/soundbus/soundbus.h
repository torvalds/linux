/*
 * soundbus generic definitions
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */
#ifndef __SOUNDBUS_H
#define __SOUNDBUS_H

#include <asm/of_device.h>
#include <sound/pcm.h>
#include <linux/list.h>


/* When switching from master to slave or the other way around,
 * you don't want to have the codec chip acting as clock source
 * while the bus still is.
 * More importantly, while switch from slave to master, you need
 * to turn off the chip's master function first, but then there's
 * no clock for a while and other chips might reset, so we notify
 * their drivers after having switched.
 * The constants here are codec-point of view, so when we switch
 * the soundbus to master we tell the codec we're going to switch
 * and give it CLOCK_SWITCH_PREPARE_SLAVE!
 */
enum clock_switch {
	CLOCK_SWITCH_PREPARE_SLAVE,
	CLOCK_SWITCH_PREPARE_MASTER,
	CLOCK_SWITCH_SLAVE,
	CLOCK_SWITCH_MASTER,
	CLOCK_SWITCH_NOTIFY,
};

/* information on a transfer the codec can take */
struct transfer_info {
	u64 formats;		/* SNDRV_PCM_FMTBIT_* */
	unsigned int rates;	/* SNDRV_PCM_RATE_* */
	/* flags */
	u32 transfer_in:1, /* input = 1, output = 0 */
	    must_be_clock_source:1;
	/* for codecs to distinguish among their TIs */
	int tag;
};

struct codec_info_item {
	struct codec_info *codec;
	void *codec_data;
	struct soundbus_dev *sdev;
	/* internal, to be used by the soundbus provider */
	struct list_head list;
};

/* for prepare, where the codecs need to know
 * what we're going to drive the bus with */
struct bus_info {
	/* see below */
	int sysclock_factor;
	int bus_factor;
};

/* information on the codec itself, plus function pointers */
struct codec_info {
	/* the module this lives in */
	struct module *owner;

	/* supported transfer possibilities, array terminated by
	 * formats or rates being 0. */
	struct transfer_info *transfers;

	/* Master clock speed factor
	 * to be used (master clock speed = sysclock_factor * sampling freq)
	 * Unused if the soundbus provider has no such notion.
	 */
	int sysclock_factor;

	/* Bus factor, bus clock speed = bus_factor * sampling freq)
	 * Unused if the soundbus provider has no such notion.
	 */
	int bus_factor;

	/* operations */
	/* clock switching, see above */
	int (*switch_clock)(struct codec_info_item *cii,
			    enum clock_switch clock);

	/* called for each transfer_info when the user
	 * opens the pcm device to determine what the
	 * hardware can support at this point in time.
	 * That can depend on other user-switchable controls.
	 * Return 1 if usable, 0 if not.
	 * out points to another instance of a transfer_info
	 * which is initialised to the values in *ti, and
	 * it's format and rate values can be modified by
	 * the callback if it is necessary to further restrict
	 * the formats that can be used at the moment, for
	 * example when one codec has multiple logical codec
	 * info structs for multiple inputs.
	 */
	int (*usable)(struct codec_info_item *cii,
		      struct transfer_info *ti,
		      struct transfer_info *out);

	/* called when pcm stream is opened, probably not implemented
	 * most of the time since it isn't too useful */
	int (*open)(struct codec_info_item *cii,
		    struct snd_pcm_substream *substream);

	/* called when the pcm stream is closed, at this point
	 * the user choices can all be unlocked (see below) */
	int (*close)(struct codec_info_item *cii,
		     struct snd_pcm_substream *substream);

	/* if the codec must forbid some user choices because
	 * they are not valid with the substream/transfer info,
	 * it must do so here. Example: no digital output for
	 * incompatible framerate, say 8KHz, on Onyx.
	 * If the selected stuff in the substream is NOT
	 * compatible, you have to reject this call! */
	int (*prepare)(struct codec_info_item *cii,
		       struct bus_info *bi,
		       struct snd_pcm_substream *substream);

	/* start() is called before data is pushed to the codec.
	 * Note that start() must be atomic! */
	int (*start)(struct codec_info_item *cii,
		     struct snd_pcm_substream *substream);

	/* stop() is called after data is no longer pushed to the codec.
	 * Note that stop() must be atomic! */
	int (*stop)(struct codec_info_item *cii,
		    struct snd_pcm_substream *substream);

	int (*suspend)(struct codec_info_item *cii, pm_message_t state);
	int (*resume)(struct codec_info_item *cii);
};

/* information on a soundbus device */
struct soundbus_dev {
	/* the bus it belongs to */
	struct list_head onbuslist;

	/* the of device it represents */
	struct of_device ofdev;

	/* what modules go by */
	char modalias[32];

	/* These fields must be before attach_codec can be called.
	 * They should be set by the owner of the alsa card object
	 * that is needed, and whoever sets them must make sure
	 * that they are unique within that alsa card object. */
	char *pcmname;
	int pcmid;

	/* this is assigned by the soundbus provider in attach_codec */
	struct snd_pcm *pcm;

	/* operations */
	/* attach a codec to this soundbus, give the alsa
	 * card object the PCMs for this soundbus should be in.
	 * The 'data' pointer must be unique, it is used as the
	 * key for detach_codec(). */
	int (*attach_codec)(struct soundbus_dev *dev, struct snd_card *card,
			    struct codec_info *ci, void *data);
	void (*detach_codec)(struct soundbus_dev *dev, void *data);
	/* TODO: suspend/resume */

	/* private for the soundbus provider */
	struct list_head codec_list;
	u32 have_out:1, have_in:1;
};
#define to_soundbus_device(d) container_of(d, struct soundbus_dev, ofdev.dev)
#define of_to_soundbus_device(d) container_of(d, struct soundbus_dev, ofdev)

extern int soundbus_add_one(struct soundbus_dev *dev);
extern void soundbus_remove_one(struct soundbus_dev *dev);

extern struct soundbus_dev *soundbus_dev_get(struct soundbus_dev *dev);
extern void soundbus_dev_put(struct soundbus_dev *dev);

struct soundbus_driver {
	char *name;
	struct module *owner;

	/* we don't implement any matching at all */

	int	(*probe)(struct soundbus_dev* dev);
	int	(*remove)(struct soundbus_dev* dev);

	int	(*suspend)(struct soundbus_dev* dev, pm_message_t state);
	int	(*resume)(struct soundbus_dev* dev);
	int	(*shutdown)(struct soundbus_dev* dev);

	struct device_driver driver;
};
#define to_soundbus_driver(drv) container_of(drv,struct soundbus_driver, driver)

extern int soundbus_register_driver(struct soundbus_driver *drv);
extern void soundbus_unregister_driver(struct soundbus_driver *drv);

#endif /* __SOUNDBUS_H */
