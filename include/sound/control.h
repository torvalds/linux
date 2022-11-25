/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_CONTROL_H
#define __SOUND_CONTROL_H

/*
 *  Header file for control interface
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/wait.h>
#include <linux/nospec.h>
#include <sound/asound.h>

#define snd_kcontrol_chip(kcontrol) ((kcontrol)->private_data)

struct snd_kcontrol;
typedef int (snd_kcontrol_info_t) (struct snd_kcontrol * kcontrol, struct snd_ctl_elem_info * uinfo);
typedef int (snd_kcontrol_get_t) (struct snd_kcontrol * kcontrol, struct snd_ctl_elem_value * ucontrol);
typedef int (snd_kcontrol_put_t) (struct snd_kcontrol * kcontrol, struct snd_ctl_elem_value * ucontrol);
typedef int (snd_kcontrol_tlv_rw_t)(struct snd_kcontrol *kcontrol,
				    int op_flag, /* SNDRV_CTL_TLV_OP_XXX */
				    unsigned int size,
				    unsigned int __user *tlv);

/* internal flag for skipping validations */
#ifdef CONFIG_SND_CTL_DEBUG
#define SNDRV_CTL_ELEM_ACCESS_SKIP_CHECK	(1 << 24)
#define snd_ctl_skip_validation(info) \
	((info)->access & SNDRV_CTL_ELEM_ACCESS_SKIP_CHECK)
#else
#define SNDRV_CTL_ELEM_ACCESS_SKIP_CHECK	0
#define snd_ctl_skip_validation(info)		true
#endif

/* kernel only - LED bits */
#define SNDRV_CTL_ELEM_ACCESS_LED_SHIFT		25
#define SNDRV_CTL_ELEM_ACCESS_LED_MASK		(7<<25) /* kernel three bits - LED group */
#define SNDRV_CTL_ELEM_ACCESS_SPK_LED		(1<<25) /* kernel speaker (output) LED flag */
#define SNDRV_CTL_ELEM_ACCESS_MIC_LED		(2<<25) /* kernel microphone (input) LED flag */

enum {
	SNDRV_CTL_TLV_OP_READ = 0,
	SNDRV_CTL_TLV_OP_WRITE = 1,
	SNDRV_CTL_TLV_OP_CMD = -1,
};

struct snd_kcontrol_new {
	snd_ctl_elem_iface_t iface;	/* interface identifier */
	unsigned int device;		/* device/client number */
	unsigned int subdevice;		/* subdevice (substream) number */
	const char *name;		/* ASCII name of item */
	unsigned int index;		/* index of item */
	unsigned int access;		/* access rights */
	unsigned int count;		/* count of same elements */
	snd_kcontrol_info_t *info;
	snd_kcontrol_get_t *get;
	snd_kcontrol_put_t *put;
	union {
		snd_kcontrol_tlv_rw_t *c;
		const unsigned int *p;
	} tlv;
	unsigned long private_value;
};

struct snd_kcontrol_volatile {
	struct snd_ctl_file *owner;	/* locked */
	unsigned int access;	/* access rights */
};

struct snd_kcontrol {
	struct list_head list;		/* list of controls */
	struct snd_ctl_elem_id id;
	unsigned int count;		/* count of same elements */
	snd_kcontrol_info_t *info;
	snd_kcontrol_get_t *get;
	snd_kcontrol_put_t *put;
	union {
		snd_kcontrol_tlv_rw_t *c;
		const unsigned int *p;
	} tlv;
	unsigned long private_value;
	void *private_data;
	void (*private_free)(struct snd_kcontrol *kcontrol);
	struct snd_kcontrol_volatile vd[];	/* volatile data */
};

#define snd_kcontrol(n) list_entry(n, struct snd_kcontrol, list)

struct snd_kctl_event {
	struct list_head list;	/* list of events */
	struct snd_ctl_elem_id id;
	unsigned int mask;
};

#define snd_kctl_event(n) list_entry(n, struct snd_kctl_event, list)

struct pid;

enum {
	SND_CTL_SUBDEV_PCM,
	SND_CTL_SUBDEV_RAWMIDI,
	SND_CTL_SUBDEV_ITEMS,
};

struct snd_ctl_file {
	struct list_head list;		/* list of all control files */
	struct snd_card *card;
	struct pid *pid;
	int preferred_subdevice[SND_CTL_SUBDEV_ITEMS];
	wait_queue_head_t change_sleep;
	spinlock_t read_lock;
	struct snd_fasync *fasync;
	int subscribed;			/* read interface is activated */
	struct list_head events;	/* waiting events for read */
};

struct snd_ctl_layer_ops {
	struct snd_ctl_layer_ops *next;
	const char *module_name;
	void (*lregister)(struct snd_card *card);
	void (*ldisconnect)(struct snd_card *card);
	void (*lnotify)(struct snd_card *card, unsigned int mask, struct snd_kcontrol *kctl, unsigned int ioff);
};

#define snd_ctl_file(n) list_entry(n, struct snd_ctl_file, list)

typedef int (*snd_kctl_ioctl_func_t) (struct snd_card * card,
				      struct snd_ctl_file * control,
				      unsigned int cmd, unsigned long arg);

void snd_ctl_notify(struct snd_card * card, unsigned int mask, struct snd_ctl_elem_id * id);
void snd_ctl_notify_one(struct snd_card * card, unsigned int mask, struct snd_kcontrol * kctl, unsigned int ioff);

struct snd_kcontrol *snd_ctl_new1(const struct snd_kcontrol_new * kcontrolnew, void * private_data);
void snd_ctl_free_one(struct snd_kcontrol * kcontrol);
int snd_ctl_add(struct snd_card * card, struct snd_kcontrol * kcontrol);
int snd_ctl_remove(struct snd_card * card, struct snd_kcontrol * kcontrol);
int snd_ctl_replace(struct snd_card *card, struct snd_kcontrol *kcontrol, bool add_on_replace);
int snd_ctl_remove_id(struct snd_card * card, struct snd_ctl_elem_id *id);
int snd_ctl_rename_id(struct snd_card * card, struct snd_ctl_elem_id *src_id, struct snd_ctl_elem_id *dst_id);
void snd_ctl_rename(struct snd_card *card, struct snd_kcontrol *kctl, const char *name);
int snd_ctl_activate_id(struct snd_card *card, struct snd_ctl_elem_id *id, int active);
struct snd_kcontrol *snd_ctl_find_numid(struct snd_card * card, unsigned int numid);
struct snd_kcontrol *snd_ctl_find_id(struct snd_card * card, struct snd_ctl_elem_id *id);

int snd_ctl_create(struct snd_card *card);

int snd_ctl_register_ioctl(snd_kctl_ioctl_func_t fcn);
int snd_ctl_unregister_ioctl(snd_kctl_ioctl_func_t fcn);
#ifdef CONFIG_COMPAT
int snd_ctl_register_ioctl_compat(snd_kctl_ioctl_func_t fcn);
int snd_ctl_unregister_ioctl_compat(snd_kctl_ioctl_func_t fcn);
#else
#define snd_ctl_register_ioctl_compat(fcn)
#define snd_ctl_unregister_ioctl_compat(fcn)
#endif

int snd_ctl_request_layer(const char *module_name);
void snd_ctl_register_layer(struct snd_ctl_layer_ops *lops);
void snd_ctl_disconnect_layer(struct snd_ctl_layer_ops *lops);

int snd_ctl_get_preferred_subdevice(struct snd_card *card, int type);

static inline unsigned int snd_ctl_get_ioffnum(struct snd_kcontrol *kctl, struct snd_ctl_elem_id *id)
{
	unsigned int ioff = id->numid - kctl->id.numid;
	return array_index_nospec(ioff, kctl->count);
}

static inline unsigned int snd_ctl_get_ioffidx(struct snd_kcontrol *kctl, struct snd_ctl_elem_id *id)
{
	unsigned int ioff = id->index - kctl->id.index;
	return array_index_nospec(ioff, kctl->count);
}

static inline unsigned int snd_ctl_get_ioff(struct snd_kcontrol *kctl, struct snd_ctl_elem_id *id)
{
	if (id->numid) {
		return snd_ctl_get_ioffnum(kctl, id);
	} else {
		return snd_ctl_get_ioffidx(kctl, id);
	}
}

static inline struct snd_ctl_elem_id *snd_ctl_build_ioff(struct snd_ctl_elem_id *dst_id,
						    struct snd_kcontrol *src_kctl,
						    unsigned int offset)
{
	*dst_id = src_kctl->id;
	dst_id->index += offset;
	dst_id->numid += offset;
	return dst_id;
}

/*
 * Frequently used control callbacks/helpers
 */
int snd_ctl_boolean_mono_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo);
int snd_ctl_boolean_stereo_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo);
int snd_ctl_enum_info(struct snd_ctl_elem_info *info, unsigned int channels,
		      unsigned int items, const char *const names[]);

/*
 * virtual master control
 */
struct snd_kcontrol *snd_ctl_make_virtual_master(char *name,
						 const unsigned int *tlv);
int _snd_ctl_add_follower(struct snd_kcontrol *master,
			  struct snd_kcontrol *follower,
			  unsigned int flags);
/* optional flags for follower */
#define SND_CTL_FOLLOWER_NEED_UPDATE	(1 << 0)

/**
 * snd_ctl_add_follower - Add a virtual follower control
 * @master: vmaster element
 * @follower: follower element to add
 *
 * Add a virtual follower control to the given master element created via
 * snd_ctl_create_virtual_master() beforehand.
 *
 * All followers must be the same type (returning the same information
 * via info callback).  The function doesn't check it, so it's your
 * responsibility.
 *
 * Also, some additional limitations:
 * at most two channels,
 * logarithmic volume control (dB level) thus no linear volume,
 * master can only attenuate the volume without gain
 *
 * Return: Zero if successful or a negative error code.
 */
static inline int
snd_ctl_add_follower(struct snd_kcontrol *master, struct snd_kcontrol *follower)
{
	return _snd_ctl_add_follower(master, follower, 0);
}

/**
 * snd_ctl_add_follower_uncached - Add a virtual follower control
 * @master: vmaster element
 * @follower: follower element to add
 *
 * Add a virtual follower control to the given master.
 * Unlike snd_ctl_add_follower(), the element added via this function
 * is supposed to have volatile values, and get callback is called
 * at each time queried from the master.
 *
 * When the control peeks the hardware values directly and the value
 * can be changed by other means than the put callback of the element,
 * this function should be used to keep the value always up-to-date.
 *
 * Return: Zero if successful or a negative error code.
 */
static inline int
snd_ctl_add_follower_uncached(struct snd_kcontrol *master,
			      struct snd_kcontrol *follower)
{
	return _snd_ctl_add_follower(master, follower, SND_CTL_FOLLOWER_NEED_UPDATE);
}

int snd_ctl_add_vmaster_hook(struct snd_kcontrol *kctl,
			     void (*hook)(void *private_data, int),
			     void *private_data);
void snd_ctl_sync_vmaster(struct snd_kcontrol *kctl, bool hook_only);
#define snd_ctl_sync_vmaster_hook(kctl)	snd_ctl_sync_vmaster(kctl, true)
int snd_ctl_apply_vmaster_followers(struct snd_kcontrol *kctl,
				    int (*func)(struct snd_kcontrol *vfollower,
						struct snd_kcontrol *follower,
						void *arg),
				    void *arg);

/*
 * Control LED trigger layer
 */
#define SND_CTL_LAYER_MODULE_LED	"snd-ctl-led"

#if IS_MODULE(CONFIG_SND_CTL_LED)
static inline int snd_ctl_led_request(void) { return snd_ctl_request_layer(SND_CTL_LAYER_MODULE_LED); }
#else
static inline int snd_ctl_led_request(void) { return 0; }
#endif

/*
 * Helper functions for jack-detection controls
 */
struct snd_kcontrol *
snd_kctl_jack_new(const char *name, struct snd_card *card);
void snd_kctl_jack_report(struct snd_card *card,
			  struct snd_kcontrol *kctl, bool status);

#endif	/* __SOUND_CONTROL_H */
