/*
 * HD-audio core stuff
 */

#ifndef __SOUND_HDAUDIO_H
#define __SOUND_HDAUDIO_H

#include <linux/device.h>
#include <sound/hda_verbs.h>

struct hdac_bus;
struct hdac_device;
struct hdac_driver;

/*
 * exported bus type
 */
extern struct bus_type snd_hda_bus_type;

/*
 * HD-audio codec base device
 */
struct hdac_device {
	struct device dev;
	int type;
	struct hdac_bus *bus;
	unsigned int addr;		/* codec address */
	struct list_head list;		/* list point for bus codec_list */
};

/* device/driver type used for matching */
enum {
	HDA_DEV_CORE,
	HDA_DEV_LEGACY,
};

#define dev_to_hdac_dev(_dev)	container_of(_dev, struct hdac_device, dev)

/*
 * HD-audio codec base driver
 */
struct hdac_driver {
	struct device_driver driver;
	int type;
	int (*match)(struct hdac_device *dev, struct hdac_driver *drv);
	void (*unsol_event)(struct hdac_device *dev, unsigned int event);
};

#define drv_to_hdac_driver(_drv) container_of(_drv, struct hdac_driver, driver)

/*
 * HD-audio bus base driver
 */
struct hdac_bus_ops {
	/* send a single command */
	int (*command)(struct hdac_bus *bus, unsigned int cmd);
	/* get a response from the last command */
	int (*get_response)(struct hdac_bus *bus, unsigned int addr,
			    unsigned int *res);
};

#define HDA_UNSOL_QUEUE_SIZE	64

struct hdac_bus {
	struct device *dev;
	const struct hdac_bus_ops *ops;

	/* codec linked list */
	struct list_head codec_list;
	unsigned int num_codecs;

	/* link caddr -> codec */
	struct hdac_device *caddr_tbl[HDA_MAX_CODEC_ADDRESS + 1];

	/* unsolicited event queue */
	u32 unsol_queue[HDA_UNSOL_QUEUE_SIZE * 2]; /* ring buffer */
	unsigned int unsol_rp, unsol_wp;
	struct work_struct unsol_work;

	/* bit flags of powered codecs */
	unsigned long codec_powered;

	/* flags */
	bool sync_write:1;		/* sync after verb write */

	/* locks */
	struct mutex cmd_mutex;
};

int snd_hdac_bus_init(struct hdac_bus *bus, struct device *dev,
		      const struct hdac_bus_ops *ops);
void snd_hdac_bus_exit(struct hdac_bus *bus);
int snd_hdac_bus_exec_verb(struct hdac_bus *bus, unsigned int addr,
			   unsigned int cmd, unsigned int *res);
int snd_hdac_bus_exec_verb_unlocked(struct hdac_bus *bus, unsigned int addr,
				    unsigned int cmd, unsigned int *res);
void snd_hdac_bus_queue_event(struct hdac_bus *bus, u32 res, u32 res_ex);

int snd_hdac_bus_add_device(struct hdac_bus *bus, struct hdac_device *codec);
void snd_hdac_bus_remove_device(struct hdac_bus *bus,
				struct hdac_device *codec);

#endif /* __SOUND_HDAUDIO_H */
