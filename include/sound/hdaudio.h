/*
 * HD-audio core stuff
 */

#ifndef __SOUND_HDAUDIO_H
#define __SOUND_HDAUDIO_H

#include <linux/device.h>
#include <sound/hda_verbs.h>

/* codec node id */
typedef u16 hda_nid_t;

struct hdac_bus;
struct hdac_device;
struct hdac_driver;
struct hdac_widget_tree;

/*
 * exported bus type
 */
extern struct bus_type snd_hda_bus_type;

/*
 * generic arrays
 */
struct snd_array {
	unsigned int used;
	unsigned int alloced;
	unsigned int elem_size;
	unsigned int alloc_align;
	void *list;
};

/*
 * HD-audio codec base device
 */
struct hdac_device {
	struct device dev;
	int type;
	struct hdac_bus *bus;
	unsigned int addr;		/* codec address */
	struct list_head list;		/* list point for bus codec_list */

	hda_nid_t afg;			/* AFG node id */
	hda_nid_t mfg;			/* MFG node id */

	/* ids */
	unsigned int vendor_id;
	unsigned int subsystem_id;
	unsigned int revision_id;
	unsigned int afg_function_id;
	unsigned int mfg_function_id;
	unsigned int afg_unsol:1;
	unsigned int mfg_unsol:1;

	unsigned int power_caps;	/* FG power caps */

	const char *vendor_name;	/* codec vendor name */
	const char *chip_name;		/* codec chip name */

	/* verb exec op override */
	int (*exec_verb)(struct hdac_device *dev, unsigned int cmd,
			 unsigned int flags, unsigned int *res);

	/* widgets */
	unsigned int num_nodes;
	hda_nid_t start_nid, end_nid;

	/* misc flags */
	atomic_t in_pm;		/* suspend/resume being performed */

	/* sysfs */
	struct hdac_widget_tree *widgets;

	/* regmap */
	struct regmap *regmap;
	bool lazy_cache:1;	/* don't wake up for writes */
	bool caps_overwriting:1; /* caps overwrite being in process */
};

/* device/driver type used for matching */
enum {
	HDA_DEV_CORE,
	HDA_DEV_LEGACY,
};

/* direction */
enum {
	HDA_INPUT, HDA_OUTPUT
};

#define dev_to_hdac_dev(_dev)	container_of(_dev, struct hdac_device, dev)

int snd_hdac_device_init(struct hdac_device *dev, struct hdac_bus *bus,
			 const char *name, unsigned int addr);
void snd_hdac_device_exit(struct hdac_device *dev);
int snd_hdac_device_register(struct hdac_device *codec);
void snd_hdac_device_unregister(struct hdac_device *codec);

int snd_hdac_refresh_widgets(struct hdac_device *codec);

unsigned int snd_hdac_make_cmd(struct hdac_device *codec, hda_nid_t nid,
			       unsigned int verb, unsigned int parm);
int snd_hdac_exec_verb(struct hdac_device *codec, unsigned int cmd,
		       unsigned int flags, unsigned int *res);
int snd_hdac_read(struct hdac_device *codec, hda_nid_t nid,
		  unsigned int verb, unsigned int parm, unsigned int *res);
int _snd_hdac_read_parm(struct hdac_device *codec, hda_nid_t nid, int parm,
			unsigned int *res);
int snd_hdac_read_parm_uncached(struct hdac_device *codec, hda_nid_t nid,
				int parm);
int snd_hdac_override_parm(struct hdac_device *codec, hda_nid_t nid,
			   unsigned int parm, unsigned int val);
int snd_hdac_get_connections(struct hdac_device *codec, hda_nid_t nid,
			     hda_nid_t *conn_list, int max_conns);
int snd_hdac_get_sub_nodes(struct hdac_device *codec, hda_nid_t nid,
			   hda_nid_t *start_id);

/**
 * snd_hdac_read_parm - read a codec parameter
 * @codec: the codec object
 * @nid: NID to read a parameter
 * @parm: parameter to read
 *
 * Returns -1 for error.  If you need to distinguish the error more
 * strictly, use _snd_hdac_read_parm() directly.
 */
static inline int snd_hdac_read_parm(struct hdac_device *codec, hda_nid_t nid,
				     int parm)
{
	unsigned int val;

	return _snd_hdac_read_parm(codec, nid, parm, &val) < 0 ? -1 : val;
}

#ifdef CONFIG_PM
void snd_hdac_power_up(struct hdac_device *codec);
void snd_hdac_power_down(struct hdac_device *codec);
#else
static inline void snd_hdac_power_up(struct hdac_device *codec) {}
static inline void snd_hdac_power_down(struct hdac_device *codec) {}
#endif

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

static inline void snd_hdac_codec_link_up(struct hdac_device *codec)
{
	set_bit(codec->addr, &codec->bus->codec_powered);
}

static inline void snd_hdac_codec_link_down(struct hdac_device *codec)
{
	clear_bit(codec->addr, &codec->bus->codec_powered);
}

/*
 * generic array helpers
 */
void *snd_array_new(struct snd_array *array);
void snd_array_free(struct snd_array *array);
static inline void snd_array_init(struct snd_array *array, unsigned int size,
				  unsigned int align)
{
	array->elem_size = size;
	array->alloc_align = align;
}

static inline void *snd_array_elem(struct snd_array *array, unsigned int idx)
{
	return array->list + idx * array->elem_size;
}

static inline unsigned int snd_array_index(struct snd_array *array, void *ptr)
{
	return (unsigned long)(ptr - array->list) / array->elem_size;
}

#endif /* __SOUND_HDAUDIO_H */
