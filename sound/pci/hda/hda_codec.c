// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/hda_codec.h>
#include <sound/asoundef.h>
#include <sound/tlv.h>
#include <sound/initval.h>
#include <sound/jack.h>
#include "hda_local.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include <sound/hda_hwdep.h>
#include <sound/hda_component.h>

#define codec_in_pm(codec)		snd_hdac_is_in_pm(&codec->core)
#define hda_codec_is_power_on(codec)	snd_hdac_is_power_on(&codec->core)
#define codec_has_epss(codec) \
	((codec)->core.power_caps & AC_PWRST_EPSS)
#define codec_has_clkstop(codec) \
	((codec)->core.power_caps & AC_PWRST_CLKSTOP)

/*
 * Send and receive a verb - passed to exec_verb override for hdac_device
 */
static int codec_exec_verb(struct hdac_device *dev, unsigned int cmd,
			   unsigned int flags, unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct hda_bus *bus = codec->bus;
	int err;

	if (cmd == ~0)
		return -1;

 again:
	snd_hda_power_up_pm(codec);
	mutex_lock(&bus->core.cmd_mutex);
	if (flags & HDA_RW_NO_RESPONSE_FALLBACK)
		bus->no_response_fallback = 1;
	err = snd_hdac_bus_exec_verb_unlocked(&bus->core, codec->core.addr,
					      cmd, res);
	bus->no_response_fallback = 0;
	mutex_unlock(&bus->core.cmd_mutex);
	snd_hda_power_down_pm(codec);
	if (!codec_in_pm(codec) && res && err == -EAGAIN) {
		if (bus->response_reset) {
			codec_dbg(codec,
				  "resetting BUS due to fatal communication error\n");
			snd_hda_bus_reset(bus);
		}
		goto again;
	}
	/* clear reset-flag when the communication gets recovered */
	if (!err || codec_in_pm(codec))
		bus->response_reset = 0;
	return err;
}

/**
 * snd_hda_sequence_write - sequence writes
 * @codec: the HDA codec
 * @seq: VERB array to send
 *
 * Send the commands sequentially from the given array.
 * The array must be terminated with NID=0.
 */
void snd_hda_sequence_write(struct hda_codec *codec, const struct hda_verb *seq)
{
	for (; seq->nid; seq++)
		snd_hda_codec_write(codec, seq->nid, 0, seq->verb, seq->param);
}
EXPORT_SYMBOL_GPL(snd_hda_sequence_write);

/* connection list element */
struct hda_conn_list {
	struct list_head list;
	int len;
	hda_nid_t nid;
	hda_nid_t conns[];
};

/* look up the cached results */
static struct hda_conn_list *
lookup_conn_list(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_conn_list *p;
	list_for_each_entry(p, &codec->conn_list, list) {
		if (p->nid == nid)
			return p;
	}
	return NULL;
}

static int add_conn_list(struct hda_codec *codec, hda_nid_t nid, int len,
			 const hda_nid_t *list)
{
	struct hda_conn_list *p;

	p = kmalloc(struct_size(p, conns, len), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->len = len;
	p->nid = nid;
	memcpy(p->conns, list, len * sizeof(hda_nid_t));
	list_add(&p->list, &codec->conn_list);
	return 0;
}

static void remove_conn_list(struct hda_codec *codec)
{
	while (!list_empty(&codec->conn_list)) {
		struct hda_conn_list *p;
		p = list_first_entry(&codec->conn_list, typeof(*p), list);
		list_del(&p->list);
		kfree(p);
	}
}

/* read the connection and add to the cache */
static int read_and_add_raw_conns(struct hda_codec *codec, hda_nid_t nid)
{
	hda_nid_t list[32];
	hda_nid_t *result = list;
	int len;

	len = snd_hda_get_raw_connections(codec, nid, list, ARRAY_SIZE(list));
	if (len == -ENOSPC) {
		len = snd_hda_get_num_raw_conns(codec, nid);
		result = kmalloc_array(len, sizeof(hda_nid_t), GFP_KERNEL);
		if (!result)
			return -ENOMEM;
		len = snd_hda_get_raw_connections(codec, nid, result, len);
	}
	if (len >= 0)
		len = snd_hda_override_conn_list(codec, nid, len, result);
	if (result != list)
		kfree(result);
	return len;
}

/**
 * snd_hda_get_conn_list - get connection list
 * @codec: the HDA codec
 * @nid: NID to parse
 * @listp: the pointer to store NID list
 *
 * Parses the connection list of the given widget and stores the pointer
 * to the list of NIDs.
 *
 * Returns the number of connections, or a negative error code.
 *
 * Note that the returned pointer isn't protected against the list
 * modification.  If snd_hda_override_conn_list() might be called
 * concurrently, protect with a mutex appropriately.
 */
int snd_hda_get_conn_list(struct hda_codec *codec, hda_nid_t nid,
			  const hda_nid_t **listp)
{
	bool added = false;

	for (;;) {
		int err;
		const struct hda_conn_list *p;

		/* if the connection-list is already cached, read it */
		p = lookup_conn_list(codec, nid);
		if (p) {
			if (listp)
				*listp = p->conns;
			return p->len;
		}
		if (snd_BUG_ON(added))
			return -EINVAL;

		err = read_and_add_raw_conns(codec, nid);
		if (err < 0)
			return err;
		added = true;
	}
}
EXPORT_SYMBOL_GPL(snd_hda_get_conn_list);

/**
 * snd_hda_get_connections - copy connection list
 * @codec: the HDA codec
 * @nid: NID to parse
 * @conn_list: connection list array; when NULL, checks only the size
 * @max_conns: max. number of connections to store
 *
 * Parses the connection list of the given widget and stores the list
 * of NIDs.
 *
 * Returns the number of connections, or a negative error code.
 */
int snd_hda_get_connections(struct hda_codec *codec, hda_nid_t nid,
			    hda_nid_t *conn_list, int max_conns)
{
	const hda_nid_t *list;
	int len = snd_hda_get_conn_list(codec, nid, &list);

	if (len > 0 && conn_list) {
		if (len > max_conns) {
			codec_err(codec, "Too many connections %d for NID 0x%x\n",
				   len, nid);
			return -EINVAL;
		}
		memcpy(conn_list, list, len * sizeof(hda_nid_t));
	}

	return len;
}
EXPORT_SYMBOL_GPL(snd_hda_get_connections);

/**
 * snd_hda_override_conn_list - add/modify the connection-list to cache
 * @codec: the HDA codec
 * @nid: NID to parse
 * @len: number of connection list entries
 * @list: the list of connection entries
 *
 * Add or modify the given connection-list to the cache.  If the corresponding
 * cache already exists, invalidate it and append a new one.
 *
 * Returns zero or a negative error code.
 */
int snd_hda_override_conn_list(struct hda_codec *codec, hda_nid_t nid, int len,
			       const hda_nid_t *list)
{
	struct hda_conn_list *p;

	p = lookup_conn_list(codec, nid);
	if (p) {
		list_del(&p->list);
		kfree(p);
	}

	return add_conn_list(codec, nid, len, list);
}
EXPORT_SYMBOL_GPL(snd_hda_override_conn_list);

/**
 * snd_hda_get_conn_index - get the connection index of the given NID
 * @codec: the HDA codec
 * @mux: NID containing the list
 * @nid: NID to select
 * @recursive: 1 when searching NID recursively, otherwise 0
 *
 * Parses the connection list of the widget @mux and checks whether the
 * widget @nid is present.  If it is, return the connection index.
 * Otherwise it returns -1.
 */
int snd_hda_get_conn_index(struct hda_codec *codec, hda_nid_t mux,
			   hda_nid_t nid, int recursive)
{
	const hda_nid_t *conn;
	int i, nums;

	nums = snd_hda_get_conn_list(codec, mux, &conn);
	for (i = 0; i < nums; i++)
		if (conn[i] == nid)
			return i;
	if (!recursive)
		return -1;
	if (recursive > 10) {
		codec_dbg(codec, "too deep connection for 0x%x\n", nid);
		return -1;
	}
	recursive++;
	for (i = 0; i < nums; i++) {
		unsigned int type = get_wcaps_type(get_wcaps(codec, conn[i]));
		if (type == AC_WID_PIN || type == AC_WID_AUD_OUT)
			continue;
		if (snd_hda_get_conn_index(codec, conn[i], nid, recursive) >= 0)
			return i;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(snd_hda_get_conn_index);

/**
 * snd_hda_get_num_devices - get DEVLIST_LEN parameter of the given widget
 *  @codec: the HDA codec
 *  @nid: NID of the pin to parse
 *
 * Get the device entry number on the given widget. This is a feature of
 * DP MST audio. Each pin can have several device entries in it.
 */
unsigned int snd_hda_get_num_devices(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int wcaps = get_wcaps(codec, nid);
	unsigned int parm;

	if (!codec->dp_mst || !(wcaps & AC_WCAP_DIGITAL) ||
	    get_wcaps_type(wcaps) != AC_WID_PIN)
		return 0;

	parm = snd_hdac_read_parm_uncached(&codec->core, nid, AC_PAR_DEVLIST_LEN);
	if (parm == -1)
		parm = 0;
	return parm & AC_DEV_LIST_LEN_MASK;
}
EXPORT_SYMBOL_GPL(snd_hda_get_num_devices);

/**
 * snd_hda_get_devices - copy device list without cache
 * @codec: the HDA codec
 * @nid: NID of the pin to parse
 * @dev_list: device list array
 * @max_devices: max. number of devices to store
 *
 * Copy the device list. This info is dynamic and so not cached.
 * Currently called only from hda_proc.c, so not exported.
 */
int snd_hda_get_devices(struct hda_codec *codec, hda_nid_t nid,
			u8 *dev_list, int max_devices)
{
	unsigned int parm;
	int i, dev_len, devices;

	parm = snd_hda_get_num_devices(codec, nid);
	if (!parm)	/* not multi-stream capable */
		return 0;

	dev_len = parm + 1;
	dev_len = dev_len < max_devices ? dev_len : max_devices;

	devices = 0;
	while (devices < dev_len) {
		if (snd_hdac_read(&codec->core, nid,
				  AC_VERB_GET_DEVICE_LIST, devices, &parm))
			break; /* error */

		for (i = 0; i < 8; i++) {
			dev_list[devices] = (u8)parm;
			parm >>= 4;
			devices++;
			if (devices >= dev_len)
				break;
		}
	}
	return devices;
}

/**
 * snd_hda_get_dev_select - get device entry select on the pin
 * @codec: the HDA codec
 * @nid: NID of the pin to get device entry select
 *
 * Get the devcie entry select on the pin. Return the device entry
 * id selected on the pin. Return 0 means the first device entry
 * is selected or MST is not supported.
 */
int snd_hda_get_dev_select(struct hda_codec *codec, hda_nid_t nid)
{
	/* not support dp_mst will always return 0, using first dev_entry */
	if (!codec->dp_mst)
		return 0;

	return snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_DEVICE_SEL, 0);
}
EXPORT_SYMBOL_GPL(snd_hda_get_dev_select);

/**
 * snd_hda_set_dev_select - set device entry select on the pin
 * @codec: the HDA codec
 * @nid: NID of the pin to set device entry select
 * @dev_id: device entry id to be set
 *
 * Set the device entry select on the pin nid.
 */
int snd_hda_set_dev_select(struct hda_codec *codec, hda_nid_t nid, int dev_id)
{
	int ret, num_devices;

	/* not support dp_mst will always return 0, using first dev_entry */
	if (!codec->dp_mst)
		return 0;

	/* AC_PAR_DEVLIST_LEN is 0 based. */
	num_devices = snd_hda_get_num_devices(codec, nid) + 1;
	/* If Device List Length is 0 (num_device = 1),
	 * the pin is not multi stream capable.
	 * Do nothing in this case.
	 */
	if (num_devices == 1)
		return 0;

	/* Behavior of setting index being equal to or greater than
	 * Device List Length is not predictable
	 */
	if (num_devices <= dev_id)
		return -EINVAL;

	ret = snd_hda_codec_write(codec, nid, 0,
			AC_VERB_SET_DEVICE_SEL, dev_id);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_set_dev_select);

/*
 * read widget caps for each widget and store in cache
 */
static int read_widget_caps(struct hda_codec *codec, hda_nid_t fg_node)
{
	int i;
	hda_nid_t nid;

	codec->wcaps = kmalloc_array(codec->core.num_nodes, 4, GFP_KERNEL);
	if (!codec->wcaps)
		return -ENOMEM;
	nid = codec->core.start_nid;
	for (i = 0; i < codec->core.num_nodes; i++, nid++)
		codec->wcaps[i] = snd_hdac_read_parm_uncached(&codec->core,
					nid, AC_PAR_AUDIO_WIDGET_CAP);
	return 0;
}

/* read all pin default configurations and save codec->init_pins */
static int read_pin_defaults(struct hda_codec *codec)
{
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec) {
		struct hda_pincfg *pin;
		unsigned int wcaps = get_wcaps(codec, nid);
		unsigned int wid_type = get_wcaps_type(wcaps);
		if (wid_type != AC_WID_PIN)
			continue;
		pin = snd_array_new(&codec->init_pins);
		if (!pin)
			return -ENOMEM;
		pin->nid = nid;
		pin->cfg = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_CONFIG_DEFAULT, 0);
		/*
		 * all device entries are the same widget control so far
		 * fixme: if any codec is different, need fix here
		 */
		pin->ctrl = snd_hda_codec_read(codec, nid, 0,
					       AC_VERB_GET_PIN_WIDGET_CONTROL,
					       0);
	}
	return 0;
}

/* look up the given pin config list and return the item matching with NID */
static struct hda_pincfg *look_up_pincfg(struct hda_codec *codec,
					 struct snd_array *array,
					 hda_nid_t nid)
{
	struct hda_pincfg *pin;
	int i;

	snd_array_for_each(array, i, pin) {
		if (pin->nid == nid)
			return pin;
	}
	return NULL;
}

/* set the current pin config value for the given NID.
 * the value is cached, and read via snd_hda_codec_get_pincfg()
 */
int snd_hda_add_pincfg(struct hda_codec *codec, struct snd_array *list,
		       hda_nid_t nid, unsigned int cfg)
{
	struct hda_pincfg *pin;

	/* the check below may be invalid when pins are added by a fixup
	 * dynamically (e.g. via snd_hda_codec_update_widgets()), so disabled
	 * for now
	 */
	/*
	if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
		return -EINVAL;
	*/

	pin = look_up_pincfg(codec, list, nid);
	if (!pin) {
		pin = snd_array_new(list);
		if (!pin)
			return -ENOMEM;
		pin->nid = nid;
	}
	pin->cfg = cfg;
	return 0;
}

/**
 * snd_hda_codec_set_pincfg - Override a pin default configuration
 * @codec: the HDA codec
 * @nid: NID to set the pin config
 * @cfg: the pin default config value
 *
 * Override a pin default configuration value in the cache.
 * This value can be read by snd_hda_codec_get_pincfg() in a higher
 * priority than the real hardware value.
 */
int snd_hda_codec_set_pincfg(struct hda_codec *codec,
			     hda_nid_t nid, unsigned int cfg)
{
	return snd_hda_add_pincfg(codec, &codec->driver_pins, nid, cfg);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_set_pincfg);

/**
 * snd_hda_codec_get_pincfg - Obtain a pin-default configuration
 * @codec: the HDA codec
 * @nid: NID to get the pin config
 *
 * Get the current pin config value of the given pin NID.
 * If the pincfg value is cached or overridden via sysfs or driver,
 * returns the cached value.
 */
unsigned int snd_hda_codec_get_pincfg(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_pincfg *pin;

#ifdef CONFIG_SND_HDA_RECONFIG
	{
		unsigned int cfg = 0;
		mutex_lock(&codec->user_mutex);
		pin = look_up_pincfg(codec, &codec->user_pins, nid);
		if (pin)
			cfg = pin->cfg;
		mutex_unlock(&codec->user_mutex);
		if (cfg)
			return cfg;
	}
#endif
	pin = look_up_pincfg(codec, &codec->driver_pins, nid);
	if (pin)
		return pin->cfg;
	pin = look_up_pincfg(codec, &codec->init_pins, nid);
	if (pin)
		return pin->cfg;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_get_pincfg);

/**
 * snd_hda_codec_set_pin_target - remember the current pinctl target value
 * @codec: the HDA codec
 * @nid: pin NID
 * @val: assigned pinctl value
 *
 * This function stores the given value to a pinctl target value in the
 * pincfg table.  This isn't always as same as the actually written value
 * but can be referred at any time via snd_hda_codec_get_pin_target().
 */
int snd_hda_codec_set_pin_target(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int val)
{
	struct hda_pincfg *pin;

	pin = look_up_pincfg(codec, &codec->init_pins, nid);
	if (!pin)
		return -EINVAL;
	pin->target = val;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_set_pin_target);

/**
 * snd_hda_codec_get_pin_target - return the current pinctl target value
 * @codec: the HDA codec
 * @nid: pin NID
 */
int snd_hda_codec_get_pin_target(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_pincfg *pin;

	pin = look_up_pincfg(codec, &codec->init_pins, nid);
	if (!pin)
		return 0;
	return pin->target;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_get_pin_target);

/**
 * snd_hda_shutup_pins - Shut up all pins
 * @codec: the HDA codec
 *
 * Clear all pin controls to shup up before suspend for avoiding click noise.
 * The controls aren't cached so that they can be resumed properly.
 */
void snd_hda_shutup_pins(struct hda_codec *codec)
{
	const struct hda_pincfg *pin;
	int i;

	/* don't shut up pins when unloading the driver; otherwise it breaks
	 * the default pin setup at the next load of the driver
	 */
	if (codec->bus->shutdown)
		return;
	snd_array_for_each(&codec->init_pins, i, pin) {
		/* use read here for syncing after issuing each verb */
		snd_hda_codec_read(codec, pin->nid, 0,
				   AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
	}
	codec->pins_shutup = 1;
}
EXPORT_SYMBOL_GPL(snd_hda_shutup_pins);

#ifdef CONFIG_PM
/* Restore the pin controls cleared previously via snd_hda_shutup_pins() */
static void restore_shutup_pins(struct hda_codec *codec)
{
	const struct hda_pincfg *pin;
	int i;

	if (!codec->pins_shutup)
		return;
	if (codec->bus->shutdown)
		return;
	snd_array_for_each(&codec->init_pins, i, pin) {
		snd_hda_codec_write(codec, pin->nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    pin->ctrl);
	}
	codec->pins_shutup = 0;
}
#endif

static void hda_jackpoll_work(struct work_struct *work)
{
	struct hda_codec *codec =
		container_of(work, struct hda_codec, jackpoll_work.work);

	/* for non-polling trigger: we need nothing if already powered on */
	if (!codec->jackpoll_interval && snd_hdac_is_power_on(&codec->core))
		return;

	/* the power-up/down sequence triggers the runtime resume */
	snd_hda_power_up_pm(codec);
	/* update jacks manually if polling is required, too */
	if (codec->jackpoll_interval) {
		snd_hda_jack_set_dirty_all(codec);
		snd_hda_jack_poll_all(codec);
	}
	snd_hda_power_down_pm(codec);

	if (!codec->jackpoll_interval)
		return;

	schedule_delayed_work(&codec->jackpoll_work,
			      codec->jackpoll_interval);
}

/* release all pincfg lists */
static void free_init_pincfgs(struct hda_codec *codec)
{
	snd_array_free(&codec->driver_pins);
#ifdef CONFIG_SND_HDA_RECONFIG
	snd_array_free(&codec->user_pins);
#endif
	snd_array_free(&codec->init_pins);
}

/*
 * audio-converter setup caches
 */
struct hda_cvt_setup {
	hda_nid_t nid;
	u8 stream_tag;
	u8 channel_id;
	u16 format_id;
	unsigned char active;	/* cvt is currently used */
	unsigned char dirty;	/* setups should be cleared */
};

/* get or create a cache entry for the given audio converter NID */
static struct hda_cvt_setup *
get_hda_cvt_setup(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_cvt_setup *p;
	int i;

	snd_array_for_each(&codec->cvt_setups, i, p) {
		if (p->nid == nid)
			return p;
	}
	p = snd_array_new(&codec->cvt_setups);
	if (p)
		p->nid = nid;
	return p;
}

/*
 * PCM device
 */
void snd_hda_codec_pcm_put(struct hda_pcm *pcm)
{
	if (refcount_dec_and_test(&pcm->codec->pcm_ref))
		wake_up(&pcm->codec->remove_sleep);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_pcm_put);

struct hda_pcm *snd_hda_codec_pcm_new(struct hda_codec *codec,
				      const char *fmt, ...)
{
	struct hda_pcm *pcm;
	va_list args;

	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return NULL;

	pcm->codec = codec;
	va_start(args, fmt);
	pcm->name = kvasprintf(GFP_KERNEL, fmt, args);
	va_end(args);
	if (!pcm->name) {
		kfree(pcm);
		return NULL;
	}

	list_add_tail(&pcm->list, &codec->pcm_list_head);
	refcount_inc(&codec->pcm_ref);
	return pcm;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_pcm_new);

/*
 * codec destructor
 */
void snd_hda_codec_disconnect_pcms(struct hda_codec *codec)
{
	struct hda_pcm *pcm;

	list_for_each_entry(pcm, &codec->pcm_list_head, list) {
		if (pcm->disconnected)
			continue;
		if (pcm->pcm)
			snd_device_disconnect(codec->card, pcm->pcm);
		snd_hda_codec_pcm_put(pcm);
		pcm->disconnected = 1;
	}
}

static void codec_release_pcms(struct hda_codec *codec)
{
	struct hda_pcm *pcm, *n;

	list_for_each_entry_safe(pcm, n, &codec->pcm_list_head, list) {
		list_del(&pcm->list);
		if (pcm->pcm)
			snd_device_free(pcm->codec->card, pcm->pcm);
		clear_bit(pcm->device, pcm->codec->bus->pcm_dev_bits);
		kfree(pcm->name);
		kfree(pcm);
	}
}

void snd_hda_codec_cleanup_for_unbind(struct hda_codec *codec)
{
	if (codec->registered) {
		/* pm_runtime_put() is called in snd_hdac_device_exit() */
		pm_runtime_get_noresume(hda_codec_dev(codec));
		pm_runtime_disable(hda_codec_dev(codec));
		codec->registered = 0;
	}

	snd_hda_codec_disconnect_pcms(codec);
	cancel_delayed_work_sync(&codec->jackpoll_work);
	if (!codec->in_freeing)
		snd_hda_ctls_clear(codec);
	codec_release_pcms(codec);
	snd_hda_detach_beep_device(codec);
	memset(&codec->patch_ops, 0, sizeof(codec->patch_ops));
	snd_hda_jack_tbl_clear(codec);
	codec->proc_widget_hook = NULL;
	codec->spec = NULL;

	/* free only driver_pins so that init_pins + user_pins are restored */
	snd_array_free(&codec->driver_pins);
	snd_array_free(&codec->cvt_setups);
	snd_array_free(&codec->spdif_out);
	snd_array_free(&codec->verbs);
	codec->preset = NULL;
	codec->follower_dig_outs = NULL;
	codec->spdif_status_reset = 0;
	snd_array_free(&codec->mixers);
	snd_array_free(&codec->nids);
	remove_conn_list(codec);
	snd_hdac_regmap_exit(&codec->core);
	codec->configured = 0;
	refcount_set(&codec->pcm_ref, 1); /* reset refcount */
}
EXPORT_SYMBOL_GPL(snd_hda_codec_cleanup_for_unbind);

static unsigned int hda_set_power_state(struct hda_codec *codec,
				unsigned int power_state);

/* enable/disable display power per codec */
void snd_hda_codec_display_power(struct hda_codec *codec, bool enable)
{
	if (codec->display_power_control)
		snd_hdac_display_power(&codec->bus->core, codec->addr, enable);
}

/* also called from hda_bind.c */
void snd_hda_codec_register(struct hda_codec *codec)
{
	if (codec->registered)
		return;
	if (device_is_registered(hda_codec_dev(codec))) {
		snd_hda_codec_display_power(codec, true);
		pm_runtime_enable(hda_codec_dev(codec));
		/* it was powered up in snd_hda_codec_new(), now all done */
		snd_hda_power_down(codec);
		codec->registered = 1;
	}
}

static int snd_hda_codec_dev_register(struct snd_device *device)
{
	snd_hda_codec_register(device->device_data);
	return 0;
}

static int snd_hda_codec_dev_free(struct snd_device *device)
{
	struct hda_codec *codec = device->device_data;

	codec->in_freeing = 1;
	/*
	 * snd_hda_codec_device_new() is used by legacy HDA and ASoC driver.
	 * We can't unregister ASoC device since it will be unregistered in
	 * snd_hdac_ext_bus_device_remove().
	 */
	if (codec->core.type == HDA_DEV_LEGACY)
		snd_hdac_device_unregister(&codec->core);
	snd_hda_codec_display_power(codec, false);

	/*
	 * In the case of ASoC HD-audio bus, the device refcount is released in
	 * snd_hdac_ext_bus_device_remove() explicitly.
	 */
	if (codec->core.type == HDA_DEV_LEGACY)
		put_device(hda_codec_dev(codec));

	return 0;
}

static void snd_hda_codec_dev_release(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);

	free_init_pincfgs(codec);
	snd_hdac_device_exit(&codec->core);
	snd_hda_sysfs_clear(codec);
	kfree(codec->modelname);
	kfree(codec->wcaps);

	/*
	 * In the case of ASoC HD-audio, hda_codec is device managed.
	 * It will be freed when the ASoC device is removed.
	 */
	if (codec->core.type == HDA_DEV_LEGACY)
		kfree(codec);
}

#define DEV_NAME_LEN 31

static int snd_hda_codec_device_init(struct hda_bus *bus, struct snd_card *card,
			unsigned int codec_addr, struct hda_codec **codecp)
{
	char name[DEV_NAME_LEN];
	struct hda_codec *codec;
	int err;

	dev_dbg(card->dev, "%s: entry\n", __func__);

	if (snd_BUG_ON(!bus))
		return -EINVAL;
	if (snd_BUG_ON(codec_addr > HDA_MAX_CODEC_ADDRESS))
		return -EINVAL;

	codec = kzalloc(sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	sprintf(name, "hdaudioC%dD%d", card->number, codec_addr);
	err = snd_hdac_device_init(&codec->core, &bus->core, name, codec_addr);
	if (err < 0) {
		kfree(codec);
		return err;
	}

	codec->core.type = HDA_DEV_LEGACY;
	*codecp = codec;

	return err;
}

/**
 * snd_hda_codec_new - create a HDA codec
 * @bus: the bus to assign
 * @card: card for this codec
 * @codec_addr: the codec address
 * @codecp: the pointer to store the generated codec
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_codec_new(struct hda_bus *bus, struct snd_card *card,
		      unsigned int codec_addr, struct hda_codec **codecp)
{
	int ret;

	ret = snd_hda_codec_device_init(bus, card, codec_addr, codecp);
	if (ret < 0)
		return ret;

	return snd_hda_codec_device_new(bus, card, codec_addr, *codecp);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_new);

int snd_hda_codec_device_new(struct hda_bus *bus, struct snd_card *card,
			unsigned int codec_addr, struct hda_codec *codec)
{
	char component[31];
	hda_nid_t fg;
	int err;
	static const struct snd_device_ops dev_ops = {
		.dev_register = snd_hda_codec_dev_register,
		.dev_free = snd_hda_codec_dev_free,
	};

	dev_dbg(card->dev, "%s: entry\n", __func__);

	if (snd_BUG_ON(!bus))
		return -EINVAL;
	if (snd_BUG_ON(codec_addr > HDA_MAX_CODEC_ADDRESS))
		return -EINVAL;

	codec->core.dev.release = snd_hda_codec_dev_release;
	codec->core.exec_verb = codec_exec_verb;

	codec->bus = bus;
	codec->card = card;
	codec->addr = codec_addr;
	mutex_init(&codec->spdif_mutex);
	mutex_init(&codec->control_mutex);
	snd_array_init(&codec->mixers, sizeof(struct hda_nid_item), 32);
	snd_array_init(&codec->nids, sizeof(struct hda_nid_item), 32);
	snd_array_init(&codec->init_pins, sizeof(struct hda_pincfg), 16);
	snd_array_init(&codec->driver_pins, sizeof(struct hda_pincfg), 16);
	snd_array_init(&codec->cvt_setups, sizeof(struct hda_cvt_setup), 8);
	snd_array_init(&codec->spdif_out, sizeof(struct hda_spdif_out), 16);
	snd_array_init(&codec->jacktbl, sizeof(struct hda_jack_tbl), 16);
	snd_array_init(&codec->verbs, sizeof(struct hda_verb *), 8);
	INIT_LIST_HEAD(&codec->conn_list);
	INIT_LIST_HEAD(&codec->pcm_list_head);
	refcount_set(&codec->pcm_ref, 1);
	init_waitqueue_head(&codec->remove_sleep);

	INIT_DELAYED_WORK(&codec->jackpoll_work, hda_jackpoll_work);
	codec->depop_delay = -1;
	codec->fixup_id = HDA_FIXUP_ID_NOT_SET;

#ifdef CONFIG_PM
	codec->power_jiffies = jiffies;
#endif

	snd_hda_sysfs_init(codec);

	if (codec->bus->modelname) {
		codec->modelname = kstrdup(codec->bus->modelname, GFP_KERNEL);
		if (!codec->modelname) {
			err = -ENOMEM;
			goto error;
		}
	}

	fg = codec->core.afg ? codec->core.afg : codec->core.mfg;
	err = read_widget_caps(codec, fg);
	if (err < 0)
		goto error;
	err = read_pin_defaults(codec);
	if (err < 0)
		goto error;

	/* power-up all before initialization */
	hda_set_power_state(codec, AC_PWRST_D0);
	codec->core.dev.power.power_state = PMSG_ON;

	snd_hda_codec_proc_new(codec);

	snd_hda_create_hwdep(codec);

	sprintf(component, "HDA:%08x,%08x,%08x", codec->core.vendor_id,
		codec->core.subsystem_id, codec->core.revision_id);
	snd_component_add(card, component);

	err = snd_device_new(card, SNDRV_DEV_CODEC, codec, &dev_ops);
	if (err < 0)
		goto error;

	/* PM runtime needs to be enabled later after binding codec */
	pm_runtime_forbid(&codec->core.dev);

	return 0;

 error:
	put_device(hda_codec_dev(codec));
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_device_new);

/**
 * snd_hda_codec_update_widgets - Refresh widget caps and pin defaults
 * @codec: the HDA codec
 *
 * Forcibly refresh the all widget caps and the init pin configurations of
 * the given codec.
 */
int snd_hda_codec_update_widgets(struct hda_codec *codec)
{
	hda_nid_t fg;
	int err;

	err = snd_hdac_refresh_widgets(&codec->core);
	if (err < 0)
		return err;

	/* Assume the function group node does not change,
	 * only the widget nodes may change.
	 */
	kfree(codec->wcaps);
	fg = codec->core.afg ? codec->core.afg : codec->core.mfg;
	err = read_widget_caps(codec, fg);
	if (err < 0)
		return err;

	snd_array_free(&codec->init_pins);
	err = read_pin_defaults(codec);

	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_update_widgets);

/* update the stream-id if changed */
static void update_pcm_stream_id(struct hda_codec *codec,
				 struct hda_cvt_setup *p, hda_nid_t nid,
				 u32 stream_tag, int channel_id)
{
	unsigned int oldval, newval;

	if (p->stream_tag != stream_tag || p->channel_id != channel_id) {
		oldval = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONV, 0);
		newval = (stream_tag << 4) | channel_id;
		if (oldval != newval)
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_CHANNEL_STREAMID,
					    newval);
		p->stream_tag = stream_tag;
		p->channel_id = channel_id;
	}
}

/* update the format-id if changed */
static void update_pcm_format(struct hda_codec *codec, struct hda_cvt_setup *p,
			      hda_nid_t nid, int format)
{
	unsigned int oldval;

	if (p->format_id != format) {
		oldval = snd_hda_codec_read(codec, nid, 0,
					    AC_VERB_GET_STREAM_FORMAT, 0);
		if (oldval != format) {
			msleep(1);
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_STREAM_FORMAT,
					    format);
		}
		p->format_id = format;
	}
}

/**
 * snd_hda_codec_setup_stream - set up the codec for streaming
 * @codec: the CODEC to set up
 * @nid: the NID to set up
 * @stream_tag: stream tag to pass, it's between 0x1 and 0xf.
 * @channel_id: channel id to pass, zero based.
 * @format: stream format.
 */
void snd_hda_codec_setup_stream(struct hda_codec *codec, hda_nid_t nid,
				u32 stream_tag,
				int channel_id, int format)
{
	struct hda_codec *c;
	struct hda_cvt_setup *p;
	int type;
	int i;

	if (!nid)
		return;

	codec_dbg(codec,
		  "hda_codec_setup_stream: NID=0x%x, stream=0x%x, channel=%d, format=0x%x\n",
		  nid, stream_tag, channel_id, format);
	p = get_hda_cvt_setup(codec, nid);
	if (!p)
		return;

	if (codec->patch_ops.stream_pm)
		codec->patch_ops.stream_pm(codec, nid, true);
	if (codec->pcm_format_first)
		update_pcm_format(codec, p, nid, format);
	update_pcm_stream_id(codec, p, nid, stream_tag, channel_id);
	if (!codec->pcm_format_first)
		update_pcm_format(codec, p, nid, format);

	p->active = 1;
	p->dirty = 0;

	/* make other inactive cvts with the same stream-tag dirty */
	type = get_wcaps_type(get_wcaps(codec, nid));
	list_for_each_codec(c, codec->bus) {
		snd_array_for_each(&c->cvt_setups, i, p) {
			if (!p->active && p->stream_tag == stream_tag &&
			    get_wcaps_type(get_wcaps(c, p->nid)) == type)
				p->dirty = 1;
		}
	}
}
EXPORT_SYMBOL_GPL(snd_hda_codec_setup_stream);

static void really_cleanup_stream(struct hda_codec *codec,
				  struct hda_cvt_setup *q);

/**
 * __snd_hda_codec_cleanup_stream - clean up the codec for closing
 * @codec: the CODEC to clean up
 * @nid: the NID to clean up
 * @do_now: really clean up the stream instead of clearing the active flag
 */
void __snd_hda_codec_cleanup_stream(struct hda_codec *codec, hda_nid_t nid,
				    int do_now)
{
	struct hda_cvt_setup *p;

	if (!nid)
		return;

	if (codec->no_sticky_stream)
		do_now = 1;

	codec_dbg(codec, "hda_codec_cleanup_stream: NID=0x%x\n", nid);
	p = get_hda_cvt_setup(codec, nid);
	if (p) {
		/* here we just clear the active flag when do_now isn't set;
		 * actual clean-ups will be done later in
		 * purify_inactive_streams() called from snd_hda_codec_prpapre()
		 */
		if (do_now)
			really_cleanup_stream(codec, p);
		else
			p->active = 0;
	}
}
EXPORT_SYMBOL_GPL(__snd_hda_codec_cleanup_stream);

static void really_cleanup_stream(struct hda_codec *codec,
				  struct hda_cvt_setup *q)
{
	hda_nid_t nid = q->nid;
	if (q->stream_tag || q->channel_id)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0);
	if (q->format_id)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_STREAM_FORMAT, 0
);
	memset(q, 0, sizeof(*q));
	q->nid = nid;
	if (codec->patch_ops.stream_pm)
		codec->patch_ops.stream_pm(codec, nid, false);
}

/* clean up the all conflicting obsolete streams */
static void purify_inactive_streams(struct hda_codec *codec)
{
	struct hda_codec *c;
	struct hda_cvt_setup *p;
	int i;

	list_for_each_codec(c, codec->bus) {
		snd_array_for_each(&c->cvt_setups, i, p) {
			if (p->dirty)
				really_cleanup_stream(c, p);
		}
	}
}

#ifdef CONFIG_PM
/* clean up all streams; called from suspend */
static void hda_cleanup_all_streams(struct hda_codec *codec)
{
	struct hda_cvt_setup *p;
	int i;

	snd_array_for_each(&codec->cvt_setups, i, p) {
		if (p->stream_tag)
			really_cleanup_stream(codec, p);
	}
}
#endif

/*
 * amp access functions
 */

/**
 * query_amp_caps - query AMP capabilities
 * @codec: the HD-auio codec
 * @nid: the NID to query
 * @direction: either #HDA_INPUT or #HDA_OUTPUT
 *
 * Query AMP capabilities for the given widget and direction.
 * Returns the obtained capability bits.
 *
 * When cap bits have been already read, this doesn't read again but
 * returns the cached value.
 */
u32 query_amp_caps(struct hda_codec *codec, hda_nid_t nid, int direction)
{
	if (!(get_wcaps(codec, nid) & AC_WCAP_AMP_OVRD))
		nid = codec->core.afg;
	return snd_hda_param_read(codec, nid,
				  direction == HDA_OUTPUT ?
				  AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP);
}
EXPORT_SYMBOL_GPL(query_amp_caps);

/**
 * snd_hda_check_amp_caps - query AMP capabilities
 * @codec: the HD-audio codec
 * @nid: the NID to query
 * @dir: either #HDA_INPUT or #HDA_OUTPUT
 * @bits: bit mask to check the result
 *
 * Check whether the widget has the given amp capability for the direction.
 */
bool snd_hda_check_amp_caps(struct hda_codec *codec, hda_nid_t nid,
			   int dir, unsigned int bits)
{
	if (!nid)
		return false;
	if (get_wcaps(codec, nid) & (1 << (dir + 1)))
		if (query_amp_caps(codec, nid, dir) & bits)
			return true;
	return false;
}
EXPORT_SYMBOL_GPL(snd_hda_check_amp_caps);

/**
 * snd_hda_override_amp_caps - Override the AMP capabilities
 * @codec: the CODEC to clean up
 * @nid: the NID to clean up
 * @dir: either #HDA_INPUT or #HDA_OUTPUT
 * @caps: the capability bits to set
 *
 * Override the cached AMP caps bits value by the given one.
 * This function is useful if the driver needs to adjust the AMP ranges,
 * e.g. limit to 0dB, etc.
 *
 * Returns zero if successful or a negative error code.
 */
int snd_hda_override_amp_caps(struct hda_codec *codec, hda_nid_t nid, int dir,
			      unsigned int caps)
{
	unsigned int parm;

	snd_hda_override_wcaps(codec, nid,
			       get_wcaps(codec, nid) | AC_WCAP_AMP_OVRD);
	parm = dir == HDA_OUTPUT ? AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP;
	return snd_hdac_override_parm(&codec->core, nid, parm, caps);
}
EXPORT_SYMBOL_GPL(snd_hda_override_amp_caps);

static unsigned int encode_amp(struct hda_codec *codec, hda_nid_t nid,
			       int ch, int dir, int idx)
{
	unsigned int cmd = snd_hdac_regmap_encode_amp(nid, ch, dir, idx);

	/* enable fake mute if no h/w mute but min=mute */
	if ((query_amp_caps(codec, nid, dir) &
	     (AC_AMPCAP_MUTE | AC_AMPCAP_MIN_MUTE)) == AC_AMPCAP_MIN_MUTE)
		cmd |= AC_AMP_FAKE_MUTE;
	return cmd;
}

/**
 * snd_hda_codec_amp_update - update the AMP mono value
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @ch: channel to update (0 or 1)
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Update the AMP values for the given channel, direction and index.
 */
int snd_hda_codec_amp_update(struct hda_codec *codec, hda_nid_t nid,
			     int ch, int dir, int idx, int mask, int val)
{
	unsigned int cmd = encode_amp(codec, nid, ch, dir, idx);

	return snd_hdac_regmap_update_raw(&codec->core, cmd, mask, val);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_update);

/**
 * snd_hda_codec_amp_stereo - update the AMP stereo values
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Update the AMP values like snd_hda_codec_amp_update(), but for a
 * stereo widget with the same mask and value.
 */
int snd_hda_codec_amp_stereo(struct hda_codec *codec, hda_nid_t nid,
			     int direction, int idx, int mask, int val)
{
	int ch, ret = 0;

	if (snd_BUG_ON(mask & ~0xff))
		mask &= 0xff;
	for (ch = 0; ch < 2; ch++)
		ret |= snd_hda_codec_amp_update(codec, nid, ch, direction,
						idx, mask, val);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_stereo);

/**
 * snd_hda_codec_amp_init - initialize the AMP value
 * @codec: the HDA codec
 * @nid: NID to read the AMP value
 * @ch: channel (left=0 or right=1)
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Works like snd_hda_codec_amp_update() but it writes the value only at
 * the first access.  If the amp was already initialized / updated beforehand,
 * this does nothing.
 */
int snd_hda_codec_amp_init(struct hda_codec *codec, hda_nid_t nid, int ch,
			   int dir, int idx, int mask, int val)
{
	unsigned int cmd = encode_amp(codec, nid, ch, dir, idx);

	if (!codec->core.regmap)
		return -EINVAL;
	return snd_hdac_regmap_update_raw_once(&codec->core, cmd, mask, val);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_init);

/**
 * snd_hda_codec_amp_init_stereo - initialize the stereo AMP value
 * @codec: the HDA codec
 * @nid: NID to read the AMP value
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Call snd_hda_codec_amp_init() for both stereo channels.
 */
int snd_hda_codec_amp_init_stereo(struct hda_codec *codec, hda_nid_t nid,
				  int dir, int idx, int mask, int val)
{
	int ch, ret = 0;

	if (snd_BUG_ON(mask & ~0xff))
		mask &= 0xff;
	for (ch = 0; ch < 2; ch++)
		ret |= snd_hda_codec_amp_init(codec, nid, ch, dir,
					      idx, mask, val);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_init_stereo);

static u32 get_amp_max_value(struct hda_codec *codec, hda_nid_t nid, int dir,
			     unsigned int ofs)
{
	u32 caps = query_amp_caps(codec, nid, dir);
	/* get num steps */
	caps = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	if (ofs < caps)
		caps -= ofs;
	return caps;
}

/**
 * snd_hda_mixer_amp_volume_info - Info callback for a standard AMP mixer
 * @kcontrol: referred ctl element
 * @uinfo: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 nid = get_amp_nid(kcontrol);
	u8 chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	unsigned int ofs = get_amp_offset(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = get_amp_max_value(codec, nid, dir, ofs);
	if (!uinfo->value.integer.max) {
		codec_warn(codec,
			   "num_steps = 0 for NID=0x%x (ctl = %s)\n",
			   nid, kcontrol->id.name);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_volume_info);


static inline unsigned int
read_amp_value(struct hda_codec *codec, hda_nid_t nid,
	       int ch, int dir, int idx, unsigned int ofs)
{
	unsigned int val;
	val = snd_hda_codec_amp_read(codec, nid, ch, dir, idx);
	val &= HDA_AMP_VOLMASK;
	if (val >= ofs)
		val -= ofs;
	else
		val = 0;
	return val;
}

static inline int
update_amp_value(struct hda_codec *codec, hda_nid_t nid,
		 int ch, int dir, int idx, unsigned int ofs,
		 unsigned int val)
{
	unsigned int maxval;

	if (val > 0)
		val += ofs;
	/* ofs = 0: raw max value */
	maxval = get_amp_max_value(codec, nid, dir, 0);
	if (val > maxval)
		val = maxval;
	return snd_hda_codec_amp_update(codec, nid, ch, dir, idx,
					HDA_AMP_VOLMASK, val);
}

/**
 * snd_hda_mixer_amp_volume_get - Get callback for a standard AMP mixer volume
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_volume_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	unsigned int ofs = get_amp_offset(kcontrol);
	long *valp = ucontrol->value.integer.value;

	if (chs & 1)
		*valp++ = read_amp_value(codec, nid, 0, dir, idx, ofs);
	if (chs & 2)
		*valp = read_amp_value(codec, nid, 1, dir, idx, ofs);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_volume_get);

/**
 * snd_hda_mixer_amp_volume_put - Put callback for a standard AMP mixer volume
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_volume_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	unsigned int ofs = get_amp_offset(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change = 0;

	if (chs & 1) {
		change = update_amp_value(codec, nid, 0, dir, idx, ofs, *valp);
		valp++;
	}
	if (chs & 2)
		change |= update_amp_value(codec, nid, 1, dir, idx, ofs, *valp);
	return change;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_volume_put);

/* inquiry the amp caps and convert to TLV */
static void get_ctl_amp_tlv(struct snd_kcontrol *kcontrol, unsigned int *tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int dir = get_amp_direction(kcontrol);
	unsigned int ofs = get_amp_offset(kcontrol);
	bool min_mute = get_amp_min_mute(kcontrol);
	u32 caps, val1, val2;

	caps = query_amp_caps(codec, nid, dir);
	val2 = (caps & AC_AMPCAP_STEP_SIZE) >> AC_AMPCAP_STEP_SIZE_SHIFT;
	val2 = (val2 + 1) * 25;
	val1 = -((caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT);
	val1 += ofs;
	val1 = ((int)val1) * ((int)val2);
	if (min_mute || (caps & AC_AMPCAP_MIN_MUTE))
		val2 |= TLV_DB_SCALE_MUTE;
	tlv[SNDRV_CTL_TLVO_TYPE] = SNDRV_CTL_TLVT_DB_SCALE;
	tlv[SNDRV_CTL_TLVO_LEN] = 2 * sizeof(unsigned int);
	tlv[SNDRV_CTL_TLVO_DB_SCALE_MIN] = val1;
	tlv[SNDRV_CTL_TLVO_DB_SCALE_MUTE_AND_STEP] = val2;
}

/**
 * snd_hda_mixer_amp_tlv - TLV callback for a standard AMP mixer volume
 * @kcontrol: ctl element
 * @op_flag: operation flag
 * @size: byte size of input TLV
 * @_tlv: TLV data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			  unsigned int size, unsigned int __user *_tlv)
{
	unsigned int tlv[4];

	if (size < 4 * sizeof(unsigned int))
		return -ENOMEM;
	get_ctl_amp_tlv(kcontrol, tlv);
	if (copy_to_user(_tlv, tlv, sizeof(tlv)))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_tlv);

/**
 * snd_hda_set_vmaster_tlv - Set TLV for a virtual master control
 * @codec: HD-audio codec
 * @nid: NID of a reference widget
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @tlv: TLV data to be stored, at least 4 elements
 *
 * Set (static) TLV data for a virtual master volume using the AMP caps
 * obtained from the reference NID.
 * The volume range is recalculated as if the max volume is 0dB.
 */
void snd_hda_set_vmaster_tlv(struct hda_codec *codec, hda_nid_t nid, int dir,
			     unsigned int *tlv)
{
	u32 caps;
	int nums, step;

	caps = query_amp_caps(codec, nid, dir);
	nums = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	step = (caps & AC_AMPCAP_STEP_SIZE) >> AC_AMPCAP_STEP_SIZE_SHIFT;
	step = (step + 1) * 25;
	tlv[SNDRV_CTL_TLVO_TYPE] = SNDRV_CTL_TLVT_DB_SCALE;
	tlv[SNDRV_CTL_TLVO_LEN] = 2 * sizeof(unsigned int);
	tlv[SNDRV_CTL_TLVO_DB_SCALE_MIN] = -nums * step;
	tlv[SNDRV_CTL_TLVO_DB_SCALE_MUTE_AND_STEP] = step;
}
EXPORT_SYMBOL_GPL(snd_hda_set_vmaster_tlv);

/* find a mixer control element with the given name */
static struct snd_kcontrol *
find_mixer_ctl(struct hda_codec *codec, const char *name, int dev, int idx)
{
	struct snd_ctl_elem_id id;
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	id.device = dev;
	id.index = idx;
	if (snd_BUG_ON(strlen(name) >= sizeof(id.name)))
		return NULL;
	strcpy(id.name, name);
	return snd_ctl_find_id(codec->card, &id);
}

/**
 * snd_hda_find_mixer_ctl - Find a mixer control element with the given name
 * @codec: HD-audio codec
 * @name: ctl id name string
 *
 * Get the control element with the given id string and IFACE_MIXER.
 */
struct snd_kcontrol *snd_hda_find_mixer_ctl(struct hda_codec *codec,
					    const char *name)
{
	return find_mixer_ctl(codec, name, 0, 0);
}
EXPORT_SYMBOL_GPL(snd_hda_find_mixer_ctl);

static int find_empty_mixer_ctl_idx(struct hda_codec *codec, const char *name,
				    int start_idx)
{
	int i, idx;
	/* 16 ctlrs should be large enough */
	for (i = 0, idx = start_idx; i < 16; i++, idx++) {
		if (!find_mixer_ctl(codec, name, 0, idx))
			return idx;
	}
	return -EBUSY;
}

/**
 * snd_hda_ctl_add - Add a control element and assign to the codec
 * @codec: HD-audio codec
 * @nid: corresponding NID (optional)
 * @kctl: the control element to assign
 *
 * Add the given control element to an array inside the codec instance.
 * All control elements belonging to a codec are supposed to be added
 * by this function so that a proper clean-up works at the free or
 * reconfiguration time.
 *
 * If non-zero @nid is passed, the NID is assigned to the control element.
 * The assignment is shown in the codec proc file.
 *
 * snd_hda_ctl_add() checks the control subdev id field whether
 * #HDA_SUBDEV_NID_FLAG bit is set.  If set (and @nid is zero), the lower
 * bits value is taken as the NID to assign. The #HDA_NID_ITEM_AMP bit
 * specifies if kctl->private_value is a HDA amplifier value.
 */
int snd_hda_ctl_add(struct hda_codec *codec, hda_nid_t nid,
		    struct snd_kcontrol *kctl)
{
	int err;
	unsigned short flags = 0;
	struct hda_nid_item *item;

	if (kctl->id.subdevice & HDA_SUBDEV_AMP_FLAG) {
		flags |= HDA_NID_ITEM_AMP;
		if (nid == 0)
			nid = get_amp_nid_(kctl->private_value);
	}
	if ((kctl->id.subdevice & HDA_SUBDEV_NID_FLAG) != 0 && nid == 0)
		nid = kctl->id.subdevice & 0xffff;
	if (kctl->id.subdevice & (HDA_SUBDEV_NID_FLAG|HDA_SUBDEV_AMP_FLAG))
		kctl->id.subdevice = 0;
	err = snd_ctl_add(codec->card, kctl);
	if (err < 0)
		return err;
	item = snd_array_new(&codec->mixers);
	if (!item)
		return -ENOMEM;
	item->kctl = kctl;
	item->nid = nid;
	item->flags = flags;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_ctl_add);

/**
 * snd_hda_add_nid - Assign a NID to a control element
 * @codec: HD-audio codec
 * @nid: corresponding NID (optional)
 * @kctl: the control element to assign
 * @index: index to kctl
 *
 * Add the given control element to an array inside the codec instance.
 * This function is used when #snd_hda_ctl_add cannot be used for 1:1
 * NID:KCTL mapping - for example "Capture Source" selector.
 */
int snd_hda_add_nid(struct hda_codec *codec, struct snd_kcontrol *kctl,
		    unsigned int index, hda_nid_t nid)
{
	struct hda_nid_item *item;

	if (nid > 0) {
		item = snd_array_new(&codec->nids);
		if (!item)
			return -ENOMEM;
		item->kctl = kctl;
		item->index = index;
		item->nid = nid;
		return 0;
	}
	codec_err(codec, "no NID for mapping control %s:%d:%d\n",
		  kctl->id.name, kctl->id.index, index);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_hda_add_nid);

/**
 * snd_hda_ctls_clear - Clear all controls assigned to the given codec
 * @codec: HD-audio codec
 */
void snd_hda_ctls_clear(struct hda_codec *codec)
{
	int i;
	struct hda_nid_item *items = codec->mixers.list;

	down_write(&codec->card->controls_rwsem);
	for (i = 0; i < codec->mixers.used; i++)
		snd_ctl_remove(codec->card, items[i].kctl);
	up_write(&codec->card->controls_rwsem);
	snd_array_free(&codec->mixers);
	snd_array_free(&codec->nids);
}

/**
 * snd_hda_lock_devices - pseudo device locking
 * @bus: the BUS
 *
 * toggle card->shutdown to allow/disallow the device access (as a hack)
 */
int snd_hda_lock_devices(struct hda_bus *bus)
{
	struct snd_card *card = bus->card;
	struct hda_codec *codec;

	spin_lock(&card->files_lock);
	if (card->shutdown)
		goto err_unlock;
	card->shutdown = 1;
	if (!list_empty(&card->ctl_files))
		goto err_clear;

	list_for_each_codec(codec, bus) {
		struct hda_pcm *cpcm;
		list_for_each_entry(cpcm, &codec->pcm_list_head, list) {
			if (!cpcm->pcm)
				continue;
			if (cpcm->pcm->streams[0].substream_opened ||
			    cpcm->pcm->streams[1].substream_opened)
				goto err_clear;
		}
	}
	spin_unlock(&card->files_lock);
	return 0;

 err_clear:
	card->shutdown = 0;
 err_unlock:
	spin_unlock(&card->files_lock);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_hda_lock_devices);

/**
 * snd_hda_unlock_devices - pseudo device unlocking
 * @bus: the BUS
 */
void snd_hda_unlock_devices(struct hda_bus *bus)
{
	struct snd_card *card = bus->card;

	spin_lock(&card->files_lock);
	card->shutdown = 0;
	spin_unlock(&card->files_lock);
}
EXPORT_SYMBOL_GPL(snd_hda_unlock_devices);

/**
 * snd_hda_codec_reset - Clear all objects assigned to the codec
 * @codec: HD-audio codec
 *
 * This frees the all PCM and control elements assigned to the codec, and
 * clears the caches and restores the pin default configurations.
 *
 * When a device is being used, it returns -EBSY.  If successfully freed,
 * returns zero.
 */
int snd_hda_codec_reset(struct hda_codec *codec)
{
	struct hda_bus *bus = codec->bus;

	if (snd_hda_lock_devices(bus) < 0)
		return -EBUSY;

	/* OK, let it free */
	device_release_driver(hda_codec_dev(codec));

	/* allow device access again */
	snd_hda_unlock_devices(bus);
	return 0;
}

typedef int (*map_follower_func_t)(struct hda_codec *, void *, struct snd_kcontrol *);

/* apply the function to all matching follower ctls in the mixer list */
static int map_followers(struct hda_codec *codec, const char * const *followers,
			 const char *suffix, map_follower_func_t func, void *data)
{
	struct hda_nid_item *items;
	const char * const *s;
	int i, err;

	items = codec->mixers.list;
	for (i = 0; i < codec->mixers.used; i++) {
		struct snd_kcontrol *sctl = items[i].kctl;
		if (!sctl || sctl->id.iface != SNDRV_CTL_ELEM_IFACE_MIXER)
			continue;
		for (s = followers; *s; s++) {
			char tmpname[sizeof(sctl->id.name)];
			const char *name = *s;
			if (suffix) {
				snprintf(tmpname, sizeof(tmpname), "%s %s",
					 name, suffix);
				name = tmpname;
			}
			if (!strcmp(sctl->id.name, name)) {
				err = func(codec, data, sctl);
				if (err)
					return err;
				break;
			}
		}
	}
	return 0;
}

static int check_follower_present(struct hda_codec *codec,
				  void *data, struct snd_kcontrol *sctl)
{
	return 1;
}

/* call kctl->put with the given value(s) */
static int put_kctl_with_value(struct snd_kcontrol *kctl, int val)
{
	struct snd_ctl_elem_value *ucontrol;
	ucontrol = kzalloc(sizeof(*ucontrol), GFP_KERNEL);
	if (!ucontrol)
		return -ENOMEM;
	ucontrol->value.integer.value[0] = val;
	ucontrol->value.integer.value[1] = val;
	kctl->put(kctl, ucontrol);
	kfree(ucontrol);
	return 0;
}

struct follower_init_arg {
	struct hda_codec *codec;
	int step;
};

/* initialize the follower volume with 0dB via snd_ctl_apply_vmaster_followers() */
static int init_follower_0dB(struct snd_kcontrol *follower,
			     struct snd_kcontrol *kctl,
			     void *_arg)
{
	struct follower_init_arg *arg = _arg;
	int _tlv[4];
	const int *tlv = NULL;
	int step;
	int val;

	if (kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
		if (kctl->tlv.c != snd_hda_mixer_amp_tlv) {
			codec_err(arg->codec,
				  "Unexpected TLV callback for follower %s:%d\n",
				  kctl->id.name, kctl->id.index);
			return 0; /* ignore */
		}
		get_ctl_amp_tlv(kctl, _tlv);
		tlv = _tlv;
	} else if (kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_READ)
		tlv = kctl->tlv.p;

	if (!tlv || tlv[SNDRV_CTL_TLVO_TYPE] != SNDRV_CTL_TLVT_DB_SCALE)
		return 0;

	step = tlv[SNDRV_CTL_TLVO_DB_SCALE_MUTE_AND_STEP];
	step &= ~TLV_DB_SCALE_MUTE;
	if (!step)
		return 0;
	if (arg->step && arg->step != step) {
		codec_err(arg->codec,
			  "Mismatching dB step for vmaster follower (%d!=%d)\n",
			  arg->step, step);
		return 0;
	}

	arg->step = step;
	val = -tlv[SNDRV_CTL_TLVO_DB_SCALE_MIN] / step;
	if (val > 0) {
		put_kctl_with_value(follower, val);
		return val;
	}

	return 0;
}

/* unmute the follower via snd_ctl_apply_vmaster_followers() */
static int init_follower_unmute(struct snd_kcontrol *follower,
				struct snd_kcontrol *kctl,
				void *_arg)
{
	return put_kctl_with_value(follower, 1);
}

static int add_follower(struct hda_codec *codec,
			void *data, struct snd_kcontrol *follower)
{
	return snd_ctl_add_follower(data, follower);
}

/**
 * __snd_hda_add_vmaster - create a virtual master control and add followers
 * @codec: HD-audio codec
 * @name: vmaster control name
 * @tlv: TLV data (optional)
 * @followers: follower control names (optional)
 * @suffix: suffix string to each follower name (optional)
 * @init_follower_vol: initialize followers to unmute/0dB
 * @access: kcontrol access rights
 * @ctl_ret: store the vmaster kcontrol in return
 *
 * Create a virtual master control with the given name.  The TLV data
 * must be either NULL or a valid data.
 *
 * @followers is a NULL-terminated array of strings, each of which is a
 * follower control name.  All controls with these names are assigned to
 * the new virtual master control.
 *
 * This function returns zero if successful or a negative error code.
 */
int __snd_hda_add_vmaster(struct hda_codec *codec, char *name,
			  unsigned int *tlv, const char * const *followers,
			  const char *suffix, bool init_follower_vol,
			  unsigned int access, struct snd_kcontrol **ctl_ret)
{
	struct snd_kcontrol *kctl;
	int err;

	if (ctl_ret)
		*ctl_ret = NULL;

	err = map_followers(codec, followers, suffix, check_follower_present, NULL);
	if (err != 1) {
		codec_dbg(codec, "No follower found for %s\n", name);
		return 0;
	}
	kctl = snd_ctl_make_virtual_master(name, tlv);
	if (!kctl)
		return -ENOMEM;
	kctl->vd[0].access |= access;
	err = snd_hda_ctl_add(codec, 0, kctl);
	if (err < 0)
		return err;

	err = map_followers(codec, followers, suffix, add_follower, kctl);
	if (err < 0)
		return err;

	/* init with master mute & zero volume */
	put_kctl_with_value(kctl, 0);
	if (init_follower_vol) {
		struct follower_init_arg arg = {
			.codec = codec,
			.step = 0,
		};
		snd_ctl_apply_vmaster_followers(kctl,
						tlv ? init_follower_0dB : init_follower_unmute,
						&arg);
	}

	if (ctl_ret)
		*ctl_ret = kctl;
	return 0;
}
EXPORT_SYMBOL_GPL(__snd_hda_add_vmaster);

/* meta hook to call each driver's vmaster hook */
static void vmaster_hook(void *private_data, int enabled)
{
	struct hda_vmaster_mute_hook *hook = private_data;

	hook->hook(hook->codec, enabled);
}

/**
 * snd_hda_add_vmaster_hook - Add a vmaster hw specific hook
 * @codec: the HDA codec
 * @hook: the vmaster hook object
 *
 * Add a hw specific hook (like EAPD) with the given vmaster switch kctl.
 */
int snd_hda_add_vmaster_hook(struct hda_codec *codec,
			     struct hda_vmaster_mute_hook *hook)
{
	if (!hook->hook || !hook->sw_kctl)
		return 0;
	hook->codec = codec;
	snd_ctl_add_vmaster_hook(hook->sw_kctl, vmaster_hook, hook);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_add_vmaster_hook);

/**
 * snd_hda_sync_vmaster_hook - Sync vmaster hook
 * @hook: the vmaster hook
 *
 * Call the hook with the current value for synchronization.
 * Should be called in init callback.
 */
void snd_hda_sync_vmaster_hook(struct hda_vmaster_mute_hook *hook)
{
	if (!hook->hook || !hook->codec)
		return;
	/* don't call vmaster hook in the destructor since it might have
	 * been already destroyed
	 */
	if (hook->codec->bus->shutdown)
		return;
	snd_ctl_sync_vmaster_hook(hook->sw_kctl);
}
EXPORT_SYMBOL_GPL(snd_hda_sync_vmaster_hook);


/**
 * snd_hda_mixer_amp_switch_info - Info callback for a standard AMP mixer switch
 * @kcontrol: referred ctl element
 * @uinfo: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_switch_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	int chs = get_amp_channels(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_info);

/**
 * snd_hda_mixer_amp_switch_get - Get callback for a standard AMP mixer switch
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_switch_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	long *valp = ucontrol->value.integer.value;

	if (chs & 1)
		*valp++ = (snd_hda_codec_amp_read(codec, nid, 0, dir, idx) &
			   HDA_AMP_MUTE) ? 0 : 1;
	if (chs & 2)
		*valp = (snd_hda_codec_amp_read(codec, nid, 1, dir, idx) &
			 HDA_AMP_MUTE) ? 0 : 1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_get);

/**
 * snd_hda_mixer_amp_switch_put - Put callback for a standard AMP mixer switch
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_switch_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change = 0;

	if (chs & 1) {
		change = snd_hda_codec_amp_update(codec, nid, 0, dir, idx,
						  HDA_AMP_MUTE,
						  *valp ? 0 : HDA_AMP_MUTE);
		valp++;
	}
	if (chs & 2)
		change |= snd_hda_codec_amp_update(codec, nid, 1, dir, idx,
						   HDA_AMP_MUTE,
						   *valp ? 0 : HDA_AMP_MUTE);
	hda_call_check_power_status(codec, nid);
	return change;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_put);

/*
 * SPDIF out controls
 */

static int snd_hda_spdif_mask_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hda_spdif_cmask_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_CON_EMPHASIS_5015 |
					   IEC958_AES0_CON_NOT_COPYRIGHT;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_CATEGORY |
					   IEC958_AES1_CON_ORIGINAL;
	return 0;
}

static int snd_hda_spdif_pmask_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_PRO_EMPHASIS_5015;
	return 0;
}

static int snd_hda_spdif_default_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;

	if (WARN_ON(codec->spdif_out.used <= idx))
		return -EINVAL;
	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	ucontrol->value.iec958.status[0] = spdif->status & 0xff;
	ucontrol->value.iec958.status[1] = (spdif->status >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (spdif->status >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (spdif->status >> 24) & 0xff;
	mutex_unlock(&codec->spdif_mutex);

	return 0;
}

/* convert from SPDIF status bits to HDA SPDIF bits
 * bit 0 (DigEn) is always set zero (to be filled later)
 */
static unsigned short convert_from_spdif_status(unsigned int sbits)
{
	unsigned short val = 0;

	if (sbits & IEC958_AES0_PROFESSIONAL)
		val |= AC_DIG1_PROFESSIONAL;
	if (sbits & IEC958_AES0_NONAUDIO)
		val |= AC_DIG1_NONAUDIO;
	if (sbits & IEC958_AES0_PROFESSIONAL) {
		if ((sbits & IEC958_AES0_PRO_EMPHASIS) ==
		    IEC958_AES0_PRO_EMPHASIS_5015)
			val |= AC_DIG1_EMPHASIS;
	} else {
		if ((sbits & IEC958_AES0_CON_EMPHASIS) ==
		    IEC958_AES0_CON_EMPHASIS_5015)
			val |= AC_DIG1_EMPHASIS;
		if (!(sbits & IEC958_AES0_CON_NOT_COPYRIGHT))
			val |= AC_DIG1_COPYRIGHT;
		if (sbits & (IEC958_AES1_CON_ORIGINAL << 8))
			val |= AC_DIG1_LEVEL;
		val |= sbits & (IEC958_AES1_CON_CATEGORY << 8);
	}
	return val;
}

/* convert to SPDIF status bits from HDA SPDIF bits
 */
static unsigned int convert_to_spdif_status(unsigned short val)
{
	unsigned int sbits = 0;

	if (val & AC_DIG1_NONAUDIO)
		sbits |= IEC958_AES0_NONAUDIO;
	if (val & AC_DIG1_PROFESSIONAL)
		sbits |= IEC958_AES0_PROFESSIONAL;
	if (sbits & IEC958_AES0_PROFESSIONAL) {
		if (val & AC_DIG1_EMPHASIS)
			sbits |= IEC958_AES0_PRO_EMPHASIS_5015;
	} else {
		if (val & AC_DIG1_EMPHASIS)
			sbits |= IEC958_AES0_CON_EMPHASIS_5015;
		if (!(val & AC_DIG1_COPYRIGHT))
			sbits |= IEC958_AES0_CON_NOT_COPYRIGHT;
		if (val & AC_DIG1_LEVEL)
			sbits |= (IEC958_AES1_CON_ORIGINAL << 8);
		sbits |= val & (0x7f << 8);
	}
	return sbits;
}

/* set digital convert verbs both for the given NID and its followers */
static void set_dig_out(struct hda_codec *codec, hda_nid_t nid,
			int mask, int val)
{
	const hda_nid_t *d;

	snd_hdac_regmap_update(&codec->core, nid, AC_VERB_SET_DIGI_CONVERT_1,
			       mask, val);
	d = codec->follower_dig_outs;
	if (!d)
		return;
	for (; *d; d++)
		snd_hdac_regmap_update(&codec->core, *d,
				       AC_VERB_SET_DIGI_CONVERT_1, mask, val);
}

static inline void set_dig_out_convert(struct hda_codec *codec, hda_nid_t nid,
				       int dig1, int dig2)
{
	unsigned int mask = 0;
	unsigned int val = 0;

	if (dig1 != -1) {
		mask |= 0xff;
		val = dig1;
	}
	if (dig2 != -1) {
		mask |= 0xff00;
		val |= dig2 << 8;
	}
	set_dig_out(codec, nid, mask, val);
}

static int snd_hda_spdif_default_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;
	hda_nid_t nid;
	unsigned short val;
	int change;

	if (WARN_ON(codec->spdif_out.used <= idx))
		return -EINVAL;
	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	nid = spdif->nid;
	spdif->status = ucontrol->value.iec958.status[0] |
		((unsigned int)ucontrol->value.iec958.status[1] << 8) |
		((unsigned int)ucontrol->value.iec958.status[2] << 16) |
		((unsigned int)ucontrol->value.iec958.status[3] << 24);
	val = convert_from_spdif_status(spdif->status);
	val |= spdif->ctls & 1;
	change = spdif->ctls != val;
	spdif->ctls = val;
	if (change && nid != (u16)-1)
		set_dig_out_convert(codec, nid, val & 0xff, (val >> 8) & 0xff);
	mutex_unlock(&codec->spdif_mutex);
	return change;
}

#define snd_hda_spdif_out_switch_info	snd_ctl_boolean_mono_info

static int snd_hda_spdif_out_switch_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;

	if (WARN_ON(codec->spdif_out.used <= idx))
		return -EINVAL;
	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	ucontrol->value.integer.value[0] = spdif->ctls & AC_DIG1_ENABLE;
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

static inline void set_spdif_ctls(struct hda_codec *codec, hda_nid_t nid,
				  int dig1, int dig2)
{
	set_dig_out_convert(codec, nid, dig1, dig2);
	/* unmute amp switch (if any) */
	if ((get_wcaps(codec, nid) & AC_WCAP_OUT_AMP) &&
	    (dig1 & AC_DIG1_ENABLE))
		snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
					    HDA_AMP_MUTE, 0);
}

static int snd_hda_spdif_out_switch_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;
	hda_nid_t nid;
	unsigned short val;
	int change;

	if (WARN_ON(codec->spdif_out.used <= idx))
		return -EINVAL;
	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	nid = spdif->nid;
	val = spdif->ctls & ~AC_DIG1_ENABLE;
	if (ucontrol->value.integer.value[0])
		val |= AC_DIG1_ENABLE;
	change = spdif->ctls != val;
	spdif->ctls = val;
	if (change && nid != (u16)-1)
		set_spdif_ctls(codec, nid, val & 0xff, -1);
	mutex_unlock(&codec->spdif_mutex);
	return change;
}

static const struct snd_kcontrol_new dig_mixes[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_cmask_get,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, PRO_MASK),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_pmask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_default_get,
		.put = snd_hda_spdif_default_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		.info = snd_hda_spdif_out_switch_info,
		.get = snd_hda_spdif_out_switch_get,
		.put = snd_hda_spdif_out_switch_put,
	},
	{ } /* end */
};

/**
 * snd_hda_create_dig_out_ctls - create Output SPDIF-related controls
 * @codec: the HDA codec
 * @associated_nid: NID that new ctls associated with
 * @cvt_nid: converter NID
 * @type: HDA_PCM_TYPE_*
 * Creates controls related with the digital output.
 * Called from each patch supporting the digital out.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_create_dig_out_ctls(struct hda_codec *codec,
				hda_nid_t associated_nid,
				hda_nid_t cvt_nid,
				int type)
{
	int err;
	struct snd_kcontrol *kctl;
	const struct snd_kcontrol_new *dig_mix;
	int idx = 0;
	int val = 0;
	const int spdif_index = 16;
	struct hda_spdif_out *spdif;
	struct hda_bus *bus = codec->bus;

	if (bus->primary_dig_out_type == HDA_PCM_TYPE_HDMI &&
	    type == HDA_PCM_TYPE_SPDIF) {
		idx = spdif_index;
	} else if (bus->primary_dig_out_type == HDA_PCM_TYPE_SPDIF &&
		   type == HDA_PCM_TYPE_HDMI) {
		/* suppose a single SPDIF device */
		for (dig_mix = dig_mixes; dig_mix->name; dig_mix++) {
			kctl = find_mixer_ctl(codec, dig_mix->name, 0, 0);
			if (!kctl)
				break;
			kctl->id.index = spdif_index;
		}
		bus->primary_dig_out_type = HDA_PCM_TYPE_HDMI;
	}
	if (!bus->primary_dig_out_type)
		bus->primary_dig_out_type = type;

	idx = find_empty_mixer_ctl_idx(codec, "IEC958 Playback Switch", idx);
	if (idx < 0) {
		codec_err(codec, "too many IEC958 outputs\n");
		return -EBUSY;
	}
	spdif = snd_array_new(&codec->spdif_out);
	if (!spdif)
		return -ENOMEM;
	for (dig_mix = dig_mixes; dig_mix->name; dig_mix++) {
		kctl = snd_ctl_new1(dig_mix, codec);
		if (!kctl)
			return -ENOMEM;
		kctl->id.index = idx;
		kctl->private_value = codec->spdif_out.used - 1;
		err = snd_hda_ctl_add(codec, associated_nid, kctl);
		if (err < 0)
			return err;
	}
	spdif->nid = cvt_nid;
	snd_hdac_regmap_read(&codec->core, cvt_nid,
			     AC_VERB_GET_DIGI_CONVERT_1, &val);
	spdif->ctls = val;
	spdif->status = convert_to_spdif_status(spdif->ctls);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_create_dig_out_ctls);

/**
 * snd_hda_spdif_out_of_nid - get the hda_spdif_out entry from the given NID
 * @codec: the HDA codec
 * @nid: widget NID
 *
 * call within spdif_mutex lock
 */
struct hda_spdif_out *snd_hda_spdif_out_of_nid(struct hda_codec *codec,
					       hda_nid_t nid)
{
	struct hda_spdif_out *spdif;
	int i;

	snd_array_for_each(&codec->spdif_out, i, spdif) {
		if (spdif->nid == nid)
			return spdif;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_spdif_out_of_nid);

/**
 * snd_hda_spdif_ctls_unassign - Unassign the given SPDIF ctl
 * @codec: the HDA codec
 * @idx: the SPDIF ctl index
 *
 * Unassign the widget from the given SPDIF control.
 */
void snd_hda_spdif_ctls_unassign(struct hda_codec *codec, int idx)
{
	struct hda_spdif_out *spdif;

	if (WARN_ON(codec->spdif_out.used <= idx))
		return;
	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	spdif->nid = (u16)-1;
	mutex_unlock(&codec->spdif_mutex);
}
EXPORT_SYMBOL_GPL(snd_hda_spdif_ctls_unassign);

/**
 * snd_hda_spdif_ctls_assign - Assign the SPDIF controls to the given NID
 * @codec: the HDA codec
 * @idx: the SPDIF ctl idx
 * @nid: widget NID
 *
 * Assign the widget to the SPDIF control with the given index.
 */
void snd_hda_spdif_ctls_assign(struct hda_codec *codec, int idx, hda_nid_t nid)
{
	struct hda_spdif_out *spdif;
	unsigned short val;

	if (WARN_ON(codec->spdif_out.used <= idx))
		return;
	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	if (spdif->nid != nid) {
		spdif->nid = nid;
		val = spdif->ctls;
		set_spdif_ctls(codec, nid, val & 0xff, (val >> 8) & 0xff);
	}
	mutex_unlock(&codec->spdif_mutex);
}
EXPORT_SYMBOL_GPL(snd_hda_spdif_ctls_assign);

/*
 * SPDIF sharing with analog output
 */
static int spdif_share_sw_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_multi_out *mout = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = mout->share_spdif;
	return 0;
}

static int spdif_share_sw_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_multi_out *mout = snd_kcontrol_chip(kcontrol);
	mout->share_spdif = !!ucontrol->value.integer.value[0];
	return 0;
}

static const struct snd_kcontrol_new spdif_share_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Default PCM Playback Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = spdif_share_sw_get,
	.put = spdif_share_sw_put,
};

/**
 * snd_hda_create_spdif_share_sw - create Default PCM switch
 * @codec: the HDA codec
 * @mout: multi-out instance
 */
int snd_hda_create_spdif_share_sw(struct hda_codec *codec,
				  struct hda_multi_out *mout)
{
	struct snd_kcontrol *kctl;

	if (!mout->dig_out_nid)
		return 0;

	kctl = snd_ctl_new1(&spdif_share_sw, mout);
	if (!kctl)
		return -ENOMEM;
	/* ATTENTION: here mout is passed as private_data, instead of codec */
	return snd_hda_ctl_add(codec, mout->dig_out_nid, kctl);
}
EXPORT_SYMBOL_GPL(snd_hda_create_spdif_share_sw);

/*
 * SPDIF input
 */

#define snd_hda_spdif_in_switch_info	snd_hda_spdif_out_switch_info

static int snd_hda_spdif_in_switch_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = codec->spdif_in_enable;
	return 0;
}

static int snd_hda_spdif_in_switch_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val = !!ucontrol->value.integer.value[0];
	int change;

	mutex_lock(&codec->spdif_mutex);
	change = codec->spdif_in_enable != val;
	if (change) {
		codec->spdif_in_enable = val;
		snd_hdac_regmap_write(&codec->core, nid,
				      AC_VERB_SET_DIGI_CONVERT_1, val);
	}
	mutex_unlock(&codec->spdif_mutex);
	return change;
}

static int snd_hda_spdif_in_status_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val;
	unsigned int sbits;

	snd_hdac_regmap_read(&codec->core, nid,
			     AC_VERB_GET_DIGI_CONVERT_1, &val);
	sbits = convert_to_spdif_status(val);
	ucontrol->value.iec958.status[0] = sbits;
	ucontrol->value.iec958.status[1] = sbits >> 8;
	ucontrol->value.iec958.status[2] = sbits >> 16;
	ucontrol->value.iec958.status[3] = sbits >> 24;
	return 0;
}

static const struct snd_kcontrol_new dig_in_ctls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, SWITCH),
		.info = snd_hda_spdif_in_switch_info,
		.get = snd_hda_spdif_in_switch_get,
		.put = snd_hda_spdif_in_switch_put,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_in_status_get,
	},
	{ } /* end */
};

/**
 * snd_hda_create_spdif_in_ctls - create Input SPDIF-related controls
 * @codec: the HDA codec
 * @nid: audio in widget NID
 *
 * Creates controls related with the SPDIF input.
 * Called from each patch supporting the SPDIF in.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_create_spdif_in_ctls(struct hda_codec *codec, hda_nid_t nid)
{
	int err;
	struct snd_kcontrol *kctl;
	const struct snd_kcontrol_new *dig_mix;
	int idx;

	idx = find_empty_mixer_ctl_idx(codec, "IEC958 Capture Switch", 0);
	if (idx < 0) {
		codec_err(codec, "too many IEC958 inputs\n");
		return -EBUSY;
	}
	for (dig_mix = dig_in_ctls; dig_mix->name; dig_mix++) {
		kctl = snd_ctl_new1(dig_mix, codec);
		if (!kctl)
			return -ENOMEM;
		kctl->private_value = nid;
		err = snd_hda_ctl_add(codec, nid, kctl);
		if (err < 0)
			return err;
	}
	codec->spdif_in_enable =
		snd_hda_codec_read(codec, nid, 0,
				   AC_VERB_GET_DIGI_CONVERT_1, 0) &
		AC_DIG1_ENABLE;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_create_spdif_in_ctls);

/**
 * snd_hda_codec_set_power_to_all - Set the power state to all widgets
 * @codec: the HDA codec
 * @fg: function group (not used now)
 * @power_state: the power state to set (AC_PWRST_*)
 *
 * Set the given power state to all widgets that have the power control.
 * If the codec has power_filter set, it evaluates the power state and
 * filter out if it's unchanged as D3.
 */
void snd_hda_codec_set_power_to_all(struct hda_codec *codec, hda_nid_t fg,
				    unsigned int power_state)
{
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec) {
		unsigned int wcaps = get_wcaps(codec, nid);
		unsigned int state = power_state;
		if (!(wcaps & AC_WCAP_POWER))
			continue;
		if (codec->power_filter) {
			state = codec->power_filter(codec, nid, power_state);
			if (state != power_state && power_state == AC_PWRST_D3)
				continue;
		}
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE,
				    state);
	}
}
EXPORT_SYMBOL_GPL(snd_hda_codec_set_power_to_all);

/**
 * snd_hda_codec_eapd_power_filter - A power filter callback for EAPD
 * @codec: the HDA codec
 * @nid: widget NID
 * @power_state: power state to evalue
 *
 * Don't power down the widget if it controls eapd and EAPD_BTLENABLE is set.
 * This can be used a codec power_filter callback.
 */
unsigned int snd_hda_codec_eapd_power_filter(struct hda_codec *codec,
					     hda_nid_t nid,
					     unsigned int power_state)
{
	if (nid == codec->core.afg || nid == codec->core.mfg)
		return power_state;
	if (power_state == AC_PWRST_D3 &&
	    get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_PIN &&
	    (snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD)) {
		int eapd = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_EAPD_BTLENABLE, 0);
		if (eapd & 0x02)
			return AC_PWRST_D0;
	}
	return power_state;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_eapd_power_filter);

/*
 * set power state of the codec, and return the power state
 */
static unsigned int hda_set_power_state(struct hda_codec *codec,
					unsigned int power_state)
{
	hda_nid_t fg = codec->core.afg ? codec->core.afg : codec->core.mfg;
	int count;
	unsigned int state;
	int flags = 0;

	/* this delay seems necessary to avoid click noise at power-down */
	if (power_state == AC_PWRST_D3) {
		if (codec->depop_delay < 0)
			msleep(codec_has_epss(codec) ? 10 : 100);
		else if (codec->depop_delay > 0)
			msleep(codec->depop_delay);
		flags = HDA_RW_NO_RESPONSE_FALLBACK;
	}

	/* repeat power states setting at most 10 times*/
	for (count = 0; count < 10; count++) {
		if (codec->patch_ops.set_power_state)
			codec->patch_ops.set_power_state(codec, fg,
							 power_state);
		else {
			state = power_state;
			if (codec->power_filter)
				state = codec->power_filter(codec, fg, state);
			if (state == power_state || power_state != AC_PWRST_D3)
				snd_hda_codec_read(codec, fg, flags,
						   AC_VERB_SET_POWER_STATE,
						   state);
			snd_hda_codec_set_power_to_all(codec, fg, power_state);
		}
		state = snd_hda_sync_power_state(codec, fg, power_state);
		if (!(state & AC_PWRST_ERROR))
			break;
	}

	return state;
}

/* sync power states of all widgets;
 * this is called at the end of codec parsing
 */
static void sync_power_up_states(struct hda_codec *codec)
{
	hda_nid_t nid;

	/* don't care if no filter is used */
	if (!codec->power_filter)
		return;

	for_each_hda_codec_node(nid, codec) {
		unsigned int wcaps = get_wcaps(codec, nid);
		unsigned int target;
		if (!(wcaps & AC_WCAP_POWER))
			continue;
		target = codec->power_filter(codec, nid, AC_PWRST_D0);
		if (target == AC_PWRST_D0)
			continue;
		if (!snd_hda_check_power_state(codec, nid, target))
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_POWER_STATE, target);
	}
}

#ifdef CONFIG_SND_HDA_RECONFIG
/* execute additional init verbs */
static void hda_exec_init_verbs(struct hda_codec *codec)
{
	if (codec->init_verbs.list)
		snd_hda_sequence_write(codec, codec->init_verbs.list);
}
#else
static inline void hda_exec_init_verbs(struct hda_codec *codec) {}
#endif

#ifdef CONFIG_PM
/* update the power on/off account with the current jiffies */
static void update_power_acct(struct hda_codec *codec, bool on)
{
	unsigned long delta = jiffies - codec->power_jiffies;

	if (on)
		codec->power_on_acct += delta;
	else
		codec->power_off_acct += delta;
	codec->power_jiffies += delta;
}

void snd_hda_update_power_acct(struct hda_codec *codec)
{
	update_power_acct(codec, hda_codec_is_power_on(codec));
}

/*
 * call suspend and power-down; used both from PM and power-save
 * this function returns the power state in the end
 */
static unsigned int hda_call_codec_suspend(struct hda_codec *codec)
{
	unsigned int state;

	snd_hdac_enter_pm(&codec->core);
	if (codec->patch_ops.suspend)
		codec->patch_ops.suspend(codec);
	hda_cleanup_all_streams(codec);
	state = hda_set_power_state(codec, AC_PWRST_D3);
	update_power_acct(codec, true);
	snd_hdac_leave_pm(&codec->core);
	return state;
}

/*
 * kick up codec; used both from PM and power-save
 */
static void hda_call_codec_resume(struct hda_codec *codec)
{
	snd_hdac_enter_pm(&codec->core);
	if (codec->core.regmap)
		regcache_mark_dirty(codec->core.regmap);

	codec->power_jiffies = jiffies;

	hda_set_power_state(codec, AC_PWRST_D0);
	restore_shutup_pins(codec);
	hda_exec_init_verbs(codec);
	snd_hda_jack_set_dirty_all(codec);
	if (codec->patch_ops.resume)
		codec->patch_ops.resume(codec);
	else {
		if (codec->patch_ops.init)
			codec->patch_ops.init(codec);
		snd_hda_regmap_sync(codec);
	}

	if (codec->jackpoll_interval)
		hda_jackpoll_work(&codec->jackpoll_work.work);
	else
		snd_hda_jack_report_sync(codec);
	codec->core.dev.power.power_state = PMSG_ON;
	snd_hdac_leave_pm(&codec->core);
}

static int hda_codec_runtime_suspend(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);
	unsigned int state;

	/* Nothing to do if card registration fails and the component driver never probes */
	if (!codec->card)
		return 0;

	cancel_delayed_work_sync(&codec->jackpoll_work);
	state = hda_call_codec_suspend(codec);
	if (codec->link_down_at_suspend ||
	    (codec_has_clkstop(codec) && codec_has_epss(codec) &&
	     (state & AC_PWRST_CLK_STOP_OK)))
		snd_hdac_codec_link_down(&codec->core);
	snd_hda_codec_display_power(codec, false);
	return 0;
}

static int hda_codec_runtime_resume(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);

	/* Nothing to do if card registration fails and the component driver never probes */
	if (!codec->card)
		return 0;

	snd_hda_codec_display_power(codec, true);
	snd_hdac_codec_link_up(&codec->core);
	hda_call_codec_resume(codec);
	pm_runtime_mark_last_busy(dev);
	return 0;
}

#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int hda_codec_pm_prepare(struct device *dev)
{
	dev->power.power_state = PMSG_SUSPEND;
	return pm_runtime_suspended(dev);
}

static void hda_codec_pm_complete(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);

	/* If no other pm-functions are called between prepare() and complete() */
	if (dev->power.power_state.event == PM_EVENT_SUSPEND)
		dev->power.power_state = PMSG_RESUME;

	if (pm_runtime_suspended(dev) && (codec->jackpoll_interval ||
	    hda_codec_need_resume(codec) || codec->forced_resume))
		pm_request_resume(dev);
}

static int hda_codec_pm_suspend(struct device *dev)
{
	dev->power.power_state = PMSG_SUSPEND;
	return pm_runtime_force_suspend(dev);
}

static int hda_codec_pm_resume(struct device *dev)
{
	dev->power.power_state = PMSG_RESUME;
	return pm_runtime_force_resume(dev);
}

static int hda_codec_pm_freeze(struct device *dev)
{
	dev->power.power_state = PMSG_FREEZE;
	return pm_runtime_force_suspend(dev);
}

static int hda_codec_pm_thaw(struct device *dev)
{
	dev->power.power_state = PMSG_THAW;
	return pm_runtime_force_resume(dev);
}

static int hda_codec_pm_restore(struct device *dev)
{
	dev->power.power_state = PMSG_RESTORE;
	return pm_runtime_force_resume(dev);
}
#endif /* CONFIG_PM_SLEEP */

/* referred in hda_bind.c */
const struct dev_pm_ops hda_codec_driver_pm = {
#ifdef CONFIG_PM_SLEEP
	.prepare = hda_codec_pm_prepare,
	.complete = hda_codec_pm_complete,
	.suspend = hda_codec_pm_suspend,
	.resume = hda_codec_pm_resume,
	.freeze = hda_codec_pm_freeze,
	.thaw = hda_codec_pm_thaw,
	.poweroff = hda_codec_pm_suspend,
	.restore = hda_codec_pm_restore,
#endif /* CONFIG_PM_SLEEP */
	SET_RUNTIME_PM_OPS(hda_codec_runtime_suspend, hda_codec_runtime_resume,
			   NULL)
};

/* suspend the codec at shutdown; called from driver's shutdown callback */
void snd_hda_codec_shutdown(struct hda_codec *codec)
{
	struct hda_pcm *cpcm;

	list_for_each_entry(cpcm, &codec->pcm_list_head, list)
		snd_pcm_suspend_all(cpcm->pcm);

	pm_runtime_force_suspend(hda_codec_dev(codec));
	pm_runtime_disable(hda_codec_dev(codec));
}

/*
 * add standard channel maps if not specified
 */
static int add_std_chmaps(struct hda_codec *codec)
{
	struct hda_pcm *pcm;
	int str, err;

	list_for_each_entry(pcm, &codec->pcm_list_head, list) {
		for (str = 0; str < 2; str++) {
			struct hda_pcm_stream *hinfo = &pcm->stream[str];
			struct snd_pcm_chmap *chmap;
			const struct snd_pcm_chmap_elem *elem;

			if (!pcm->pcm || pcm->own_chmap || !hinfo->substreams)
				continue;
			elem = hinfo->chmap ? hinfo->chmap : snd_pcm_std_chmaps;
			err = snd_pcm_add_chmap_ctls(pcm->pcm, str, elem,
						     hinfo->channels_max,
						     0, &chmap);
			if (err < 0)
				return err;
			chmap->channel_mask = SND_PCM_CHMAP_MASK_2468;
		}
	}
	return 0;
}

/* default channel maps for 2.1 speakers;
 * since HD-audio supports only stereo, odd number channels are omitted
 */
const struct snd_pcm_chmap_elem snd_pcm_2_1_chmaps[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 4,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_LFE, SNDRV_CHMAP_LFE } },
	{ }
};
EXPORT_SYMBOL_GPL(snd_pcm_2_1_chmaps);

int snd_hda_codec_build_controls(struct hda_codec *codec)
{
	int err = 0;
	hda_exec_init_verbs(codec);
	/* continue to initialize... */
	if (codec->patch_ops.init)
		err = codec->patch_ops.init(codec);
	if (!err && codec->patch_ops.build_controls)
		err = codec->patch_ops.build_controls(codec);
	if (err < 0)
		return err;

	/* we create chmaps here instead of build_pcms */
	err = add_std_chmaps(codec);
	if (err < 0)
		return err;

	if (codec->jackpoll_interval)
		hda_jackpoll_work(&codec->jackpoll_work.work);
	else
		snd_hda_jack_report_sync(codec); /* call at the last init point */
	sync_power_up_states(codec);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_build_controls);

/*
 * PCM stuff
 */
static int hda_pcm_default_open_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	return 0;
}

static int hda_pcm_default_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	snd_hda_codec_setup_stream(codec, hinfo->nid, stream_tag, 0, format);
	return 0;
}

static int hda_pcm_default_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	return 0;
}

static int set_pcm_default_values(struct hda_codec *codec,
				  struct hda_pcm_stream *info)
{
	int err;

	/* query support PCM information from the given NID */
	if (info->nid && (!info->rates || !info->formats)) {
		err = snd_hda_query_supported_pcm(codec, info->nid,
				info->rates ? NULL : &info->rates,
				info->formats ? NULL : &info->formats,
				info->maxbps ? NULL : &info->maxbps);
		if (err < 0)
			return err;
	}
	if (info->ops.open == NULL)
		info->ops.open = hda_pcm_default_open_close;
	if (info->ops.close == NULL)
		info->ops.close = hda_pcm_default_open_close;
	if (info->ops.prepare == NULL) {
		if (snd_BUG_ON(!info->nid))
			return -EINVAL;
		info->ops.prepare = hda_pcm_default_prepare;
	}
	if (info->ops.cleanup == NULL) {
		if (snd_BUG_ON(!info->nid))
			return -EINVAL;
		info->ops.cleanup = hda_pcm_default_cleanup;
	}
	return 0;
}

/*
 * codec prepare/cleanup entries
 */
/**
 * snd_hda_codec_prepare - Prepare a stream
 * @codec: the HDA codec
 * @hinfo: PCM information
 * @stream: stream tag to assign
 * @format: format id to assign
 * @substream: PCM substream to assign
 *
 * Calls the prepare callback set by the codec with the given arguments.
 * Clean up the inactive streams when successful.
 */
int snd_hda_codec_prepare(struct hda_codec *codec,
			  struct hda_pcm_stream *hinfo,
			  unsigned int stream,
			  unsigned int format,
			  struct snd_pcm_substream *substream)
{
	int ret;
	mutex_lock(&codec->bus->prepare_mutex);
	if (hinfo->ops.prepare)
		ret = hinfo->ops.prepare(hinfo, codec, stream, format,
					 substream);
	else
		ret = -ENODEV;
	if (ret >= 0)
		purify_inactive_streams(codec);
	mutex_unlock(&codec->bus->prepare_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_prepare);

/**
 * snd_hda_codec_cleanup - Clean up stream resources
 * @codec: the HDA codec
 * @hinfo: PCM information
 * @substream: PCM substream
 *
 * Calls the cleanup callback set by the codec with the given arguments.
 */
void snd_hda_codec_cleanup(struct hda_codec *codec,
			   struct hda_pcm_stream *hinfo,
			   struct snd_pcm_substream *substream)
{
	mutex_lock(&codec->bus->prepare_mutex);
	if (hinfo->ops.cleanup)
		hinfo->ops.cleanup(hinfo, codec, substream);
	mutex_unlock(&codec->bus->prepare_mutex);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_cleanup);

/* global */
const char *snd_hda_pcm_type_name[HDA_PCM_NTYPES] = {
	"Audio", "SPDIF", "HDMI", "Modem"
};

/*
 * get the empty PCM device number to assign
 */
static int get_empty_pcm_device(struct hda_bus *bus, unsigned int type)
{
	/* audio device indices; not linear to keep compatibility */
	/* assigned to static slots up to dev#10; if more needed, assign
	 * the later slot dynamically (when CONFIG_SND_DYNAMIC_MINORS=y)
	 */
	static const int audio_idx[HDA_PCM_NTYPES][5] = {
		[HDA_PCM_TYPE_AUDIO] = { 0, 2, 4, 5, -1 },
		[HDA_PCM_TYPE_SPDIF] = { 1, -1 },
		[HDA_PCM_TYPE_HDMI]  = { 3, 7, 8, 9, -1 },
		[HDA_PCM_TYPE_MODEM] = { 6, -1 },
	};
	int i;

	if (type >= HDA_PCM_NTYPES) {
		dev_err(bus->card->dev, "Invalid PCM type %d\n", type);
		return -EINVAL;
	}

	for (i = 0; audio_idx[type][i] >= 0; i++) {
#ifndef CONFIG_SND_DYNAMIC_MINORS
		if (audio_idx[type][i] >= 8)
			break;
#endif
		if (!test_and_set_bit(audio_idx[type][i], bus->pcm_dev_bits))
			return audio_idx[type][i];
	}

#ifdef CONFIG_SND_DYNAMIC_MINORS
	/* non-fixed slots starting from 10 */
	for (i = 10; i < 32; i++) {
		if (!test_and_set_bit(i, bus->pcm_dev_bits))
			return i;
	}
#endif

	dev_warn(bus->card->dev, "Too many %s devices\n",
		snd_hda_pcm_type_name[type]);
#ifndef CONFIG_SND_DYNAMIC_MINORS
	dev_warn(bus->card->dev,
		 "Consider building the kernel with CONFIG_SND_DYNAMIC_MINORS=y\n");
#endif
	return -EAGAIN;
}

/* call build_pcms ops of the given codec and set up the default parameters */
int snd_hda_codec_parse_pcms(struct hda_codec *codec)
{
	struct hda_pcm *cpcm;
	int err;

	if (!list_empty(&codec->pcm_list_head))
		return 0; /* already parsed */

	if (!codec->patch_ops.build_pcms)
		return 0;

	err = codec->patch_ops.build_pcms(codec);
	if (err < 0) {
		codec_err(codec, "cannot build PCMs for #%d (error %d)\n",
			  codec->core.addr, err);
		return err;
	}

	list_for_each_entry(cpcm, &codec->pcm_list_head, list) {
		int stream;

		for (stream = 0; stream < 2; stream++) {
			struct hda_pcm_stream *info = &cpcm->stream[stream];

			if (!info->substreams)
				continue;
			err = set_pcm_default_values(codec, info);
			if (err < 0) {
				codec_warn(codec,
					   "fail to setup default for PCM %s\n",
					   cpcm->name);
				return err;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_parse_pcms);

/* assign all PCMs of the given codec */
int snd_hda_codec_build_pcms(struct hda_codec *codec)
{
	struct hda_bus *bus = codec->bus;
	struct hda_pcm *cpcm;
	int dev, err;

	err = snd_hda_codec_parse_pcms(codec);
	if (err < 0)
		return err;

	/* attach a new PCM streams */
	list_for_each_entry(cpcm, &codec->pcm_list_head, list) {
		if (cpcm->pcm)
			continue; /* already attached */
		if (!cpcm->stream[0].substreams && !cpcm->stream[1].substreams)
			continue; /* no substreams assigned */

		dev = get_empty_pcm_device(bus, cpcm->pcm_type);
		if (dev < 0) {
			cpcm->device = SNDRV_PCM_INVALID_DEVICE;
			continue; /* no fatal error */
		}
		cpcm->device = dev;
		err =  snd_hda_attach_pcm_stream(bus, codec, cpcm);
		if (err < 0) {
			codec_err(codec,
				  "cannot attach PCM stream %d for codec #%d\n",
				  dev, codec->core.addr);
			continue; /* no fatal error */
		}
	}

	return 0;
}

/**
 * snd_hda_add_new_ctls - create controls from the array
 * @codec: the HDA codec
 * @knew: the array of struct snd_kcontrol_new
 *
 * This helper function creates and add new controls in the given array.
 * The array must be terminated with an empty entry as terminator.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_add_new_ctls(struct hda_codec *codec,
			 const struct snd_kcontrol_new *knew)
{
	int err;

	for (; knew->name; knew++) {
		struct snd_kcontrol *kctl;
		int addr = 0, idx = 0;
		if (knew->iface == (__force snd_ctl_elem_iface_t)-1)
			continue; /* skip this codec private value */
		for (;;) {
			kctl = snd_ctl_new1(knew, codec);
			if (!kctl)
				return -ENOMEM;
			if (addr > 0)
				kctl->id.device = addr;
			if (idx > 0)
				kctl->id.index = idx;
			err = snd_hda_ctl_add(codec, 0, kctl);
			if (!err)
				break;
			/* try first with another device index corresponding to
			 * the codec addr; if it still fails (or it's the
			 * primary codec), then try another control index
			 */
			if (!addr && codec->core.addr)
				addr = codec->core.addr;
			else if (!idx && !knew->index) {
				idx = find_empty_mixer_ctl_idx(codec,
							       knew->name, 0);
				if (idx <= 0)
					return err;
			} else
				return err;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_add_new_ctls);

#ifdef CONFIG_PM
static void codec_set_power_save(struct hda_codec *codec, int delay)
{
	struct device *dev = hda_codec_dev(codec);

	if (delay == 0 && codec->auto_runtime_pm)
		delay = 3000;

	if (delay > 0) {
		pm_runtime_set_autosuspend_delay(dev, delay);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_allow(dev);
		if (!pm_runtime_suspended(dev))
			pm_runtime_mark_last_busy(dev);
	} else {
		pm_runtime_dont_use_autosuspend(dev);
		pm_runtime_forbid(dev);
	}
}

/**
 * snd_hda_set_power_save - reprogram autosuspend for the given delay
 * @bus: HD-audio bus
 * @delay: autosuspend delay in msec, 0 = off
 *
 * Synchronize the runtime PM autosuspend state from the power_save option.
 */
void snd_hda_set_power_save(struct hda_bus *bus, int delay)
{
	struct hda_codec *c;

	list_for_each_codec(c, bus)
		codec_set_power_save(c, delay);
}
EXPORT_SYMBOL_GPL(snd_hda_set_power_save);

/**
 * snd_hda_check_amp_list_power - Check the amp list and update the power
 * @codec: HD-audio codec
 * @check: the object containing an AMP list and the status
 * @nid: NID to check / update
 *
 * Check whether the given NID is in the amp list.  If it's in the list,
 * check the current AMP status, and update the power-status according
 * to the mute status.
 *
 * This function is supposed to be set or called from the check_power_status
 * patch ops.
 */
int snd_hda_check_amp_list_power(struct hda_codec *codec,
				 struct hda_loopback_check *check,
				 hda_nid_t nid)
{
	const struct hda_amp_list *p;
	int ch, v;

	if (!check->amplist)
		return 0;
	for (p = check->amplist; p->nid; p++) {
		if (p->nid == nid)
			break;
	}
	if (!p->nid)
		return 0; /* nothing changed */

	for (p = check->amplist; p->nid; p++) {
		for (ch = 0; ch < 2; ch++) {
			v = snd_hda_codec_amp_read(codec, p->nid, ch, p->dir,
						   p->idx);
			if (!(v & HDA_AMP_MUTE) && v > 0) {
				if (!check->power_on) {
					check->power_on = 1;
					snd_hda_power_up_pm(codec);
				}
				return 1;
			}
		}
	}
	if (check->power_on) {
		check->power_on = 0;
		snd_hda_power_down_pm(codec);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_check_amp_list_power);
#endif

/*
 * input MUX helper
 */

/**
 * snd_hda_input_mux_info - Info callback helper for the input-mux enum
 * @imux: imux helper object
 * @uinfo: pointer to get/store the data
 */
int snd_hda_input_mux_info(const struct hda_input_mux *imux,
			   struct snd_ctl_elem_info *uinfo)
{
	unsigned int index;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = imux->num_items;
	if (!imux->num_items)
		return 0;
	index = uinfo->value.enumerated.item;
	if (index >= imux->num_items)
		index = imux->num_items - 1;
	strcpy(uinfo->value.enumerated.name, imux->items[index].label);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_input_mux_info);

/**
 * snd_hda_input_mux_put - Put callback helper for the input-mux enum
 * @codec: the HDA codec
 * @imux: imux helper object
 * @ucontrol: pointer to get/store the data
 * @nid: input mux NID
 * @cur_val: pointer to get/store the current imux value
 */
int snd_hda_input_mux_put(struct hda_codec *codec,
			  const struct hda_input_mux *imux,
			  struct snd_ctl_elem_value *ucontrol,
			  hda_nid_t nid,
			  unsigned int *cur_val)
{
	unsigned int idx;

	if (!imux->num_items)
		return 0;
	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (*cur_val == idx)
		return 0;
	snd_hda_codec_write_cache(codec, nid, 0, AC_VERB_SET_CONNECT_SEL,
				  imux->items[idx].index);
	*cur_val = idx;
	return 1;
}
EXPORT_SYMBOL_GPL(snd_hda_input_mux_put);


/**
 * snd_hda_enum_helper_info - Helper for simple enum ctls
 * @kcontrol: ctl element
 * @uinfo: pointer to get/store the data
 * @num_items: number of enum items
 * @texts: enum item string array
 *
 * process kcontrol info callback of a simple string enum array
 * when @num_items is 0 or @texts is NULL, assume a boolean enum array
 */
int snd_hda_enum_helper_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo,
			     int num_items, const char * const *texts)
{
	static const char * const texts_default[] = {
		"Disabled", "Enabled"
	};

	if (!texts || !num_items) {
		num_items = 2;
		texts = texts_default;
	}

	return snd_ctl_enum_info(uinfo, 1, num_items, texts);
}
EXPORT_SYMBOL_GPL(snd_hda_enum_helper_info);

/*
 * Multi-channel / digital-out PCM helper functions
 */

/* setup SPDIF output stream */
static void setup_dig_out_stream(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int stream_tag, unsigned int format)
{
	struct hda_spdif_out *spdif;
	unsigned int curr_fmt;
	bool reset;

	spdif = snd_hda_spdif_out_of_nid(codec, nid);
	/* Add sanity check to pass klockwork check.
	 * This should never happen.
	 */
	if (WARN_ON(spdif == NULL))
		return;

	curr_fmt = snd_hda_codec_read(codec, nid, 0,
				      AC_VERB_GET_STREAM_FORMAT, 0);
	reset = codec->spdif_status_reset &&
		(spdif->ctls & AC_DIG1_ENABLE) &&
		curr_fmt != format;

	/* turn off SPDIF if needed; otherwise the IEC958 bits won't be
	   updated */
	if (reset)
		set_dig_out_convert(codec, nid,
				    spdif->ctls & ~AC_DIG1_ENABLE & 0xff,
				    -1);
	snd_hda_codec_setup_stream(codec, nid, stream_tag, 0, format);
	if (codec->follower_dig_outs) {
		const hda_nid_t *d;
		for (d = codec->follower_dig_outs; *d; d++)
			snd_hda_codec_setup_stream(codec, *d, stream_tag, 0,
						   format);
	}
	/* turn on again (if needed) */
	if (reset)
		set_dig_out_convert(codec, nid,
				    spdif->ctls & 0xff, -1);
}

static void cleanup_dig_out_stream(struct hda_codec *codec, hda_nid_t nid)
{
	snd_hda_codec_cleanup_stream(codec, nid);
	if (codec->follower_dig_outs) {
		const hda_nid_t *d;
		for (d = codec->follower_dig_outs; *d; d++)
			snd_hda_codec_cleanup_stream(codec, *d);
	}
}

/**
 * snd_hda_multi_out_dig_open - open the digital out in the exclusive mode
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_dig_open(struct hda_codec *codec,
			       struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	if (mout->dig_out_used == HDA_DIG_ANALOG_DUP)
		/* already opened as analog dup; reset it once */
		cleanup_dig_out_stream(codec, mout->dig_out_nid);
	mout->dig_out_used = HDA_DIG_EXCLUSIVE;
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_open);

/**
 * snd_hda_multi_out_dig_prepare - prepare the digital out stream
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 * @stream_tag: stream tag to assign
 * @format: format id to assign
 * @substream: PCM substream to assign
 */
int snd_hda_multi_out_dig_prepare(struct hda_codec *codec,
				  struct hda_multi_out *mout,
				  unsigned int stream_tag,
				  unsigned int format,
				  struct snd_pcm_substream *substream)
{
	mutex_lock(&codec->spdif_mutex);
	setup_dig_out_stream(codec, mout->dig_out_nid, stream_tag, format);
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_prepare);

/**
 * snd_hda_multi_out_dig_cleanup - clean-up the digital out stream
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_dig_cleanup(struct hda_codec *codec,
				  struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	cleanup_dig_out_stream(codec, mout->dig_out_nid);
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_cleanup);

/**
 * snd_hda_multi_out_dig_close - release the digital out stream
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_dig_close(struct hda_codec *codec,
				struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	mout->dig_out_used = 0;
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_close);

/**
 * snd_hda_multi_out_analog_open - open analog outputs
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 * @substream: PCM substream to assign
 * @hinfo: PCM information to assign
 *
 * Open analog outputs and set up the hw-constraints.
 * If the digital outputs can be opened as follower, open the digital
 * outputs, too.
 */
int snd_hda_multi_out_analog_open(struct hda_codec *codec,
				  struct hda_multi_out *mout,
				  struct snd_pcm_substream *substream,
				  struct hda_pcm_stream *hinfo)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	runtime->hw.channels_max = mout->max_channels;
	if (mout->dig_out_nid) {
		if (!mout->analog_rates) {
			mout->analog_rates = hinfo->rates;
			mout->analog_formats = hinfo->formats;
			mout->analog_maxbps = hinfo->maxbps;
		} else {
			runtime->hw.rates = mout->analog_rates;
			runtime->hw.formats = mout->analog_formats;
			hinfo->maxbps = mout->analog_maxbps;
		}
		if (!mout->spdif_rates) {
			snd_hda_query_supported_pcm(codec, mout->dig_out_nid,
						    &mout->spdif_rates,
						    &mout->spdif_formats,
						    &mout->spdif_maxbps);
		}
		mutex_lock(&codec->spdif_mutex);
		if (mout->share_spdif) {
			if ((runtime->hw.rates & mout->spdif_rates) &&
			    (runtime->hw.formats & mout->spdif_formats)) {
				runtime->hw.rates &= mout->spdif_rates;
				runtime->hw.formats &= mout->spdif_formats;
				if (mout->spdif_maxbps < hinfo->maxbps)
					hinfo->maxbps = mout->spdif_maxbps;
			} else {
				mout->share_spdif = 0;
				/* FIXME: need notify? */
			}
		}
		mutex_unlock(&codec->spdif_mutex);
	}
	return snd_pcm_hw_constraint_step(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_CHANNELS, 2);
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_analog_open);

/**
 * snd_hda_multi_out_analog_prepare - Preapre the analog outputs.
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 * @stream_tag: stream tag to assign
 * @format: format id to assign
 * @substream: PCM substream to assign
 *
 * Set up the i/o for analog out.
 * When the digital out is available, copy the front out to digital out, too.
 */
int snd_hda_multi_out_analog_prepare(struct hda_codec *codec,
				     struct hda_multi_out *mout,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream)
{
	const hda_nid_t *nids = mout->dac_nids;
	int chs = substream->runtime->channels;
	struct hda_spdif_out *spdif;
	int i;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_hda_spdif_out_of_nid(codec, mout->dig_out_nid);
	if (mout->dig_out_nid && mout->share_spdif &&
	    mout->dig_out_used != HDA_DIG_EXCLUSIVE) {
		if (chs == 2 && spdif != NULL &&
		    snd_hda_is_supported_format(codec, mout->dig_out_nid,
						format) &&
		    !(spdif->status & IEC958_AES0_NONAUDIO)) {
			mout->dig_out_used = HDA_DIG_ANALOG_DUP;
			setup_dig_out_stream(codec, mout->dig_out_nid,
					     stream_tag, format);
		} else {
			mout->dig_out_used = 0;
			cleanup_dig_out_stream(codec, mout->dig_out_nid);
		}
	}
	mutex_unlock(&codec->spdif_mutex);

	/* front */
	snd_hda_codec_setup_stream(codec, nids[HDA_FRONT], stream_tag,
				   0, format);
	if (!mout->no_share_stream &&
	    mout->hp_nid && mout->hp_nid != nids[HDA_FRONT])
		/* headphone out will just decode front left/right (stereo) */
		snd_hda_codec_setup_stream(codec, mout->hp_nid, stream_tag,
					   0, format);
	/* extra outputs copied from front */
	for (i = 0; i < ARRAY_SIZE(mout->hp_out_nid); i++)
		if (!mout->no_share_stream && mout->hp_out_nid[i])
			snd_hda_codec_setup_stream(codec,
						   mout->hp_out_nid[i],
						   stream_tag, 0, format);

	/* surrounds */
	for (i = 1; i < mout->num_dacs; i++) {
		if (chs >= (i + 1) * 2) /* independent out */
			snd_hda_codec_setup_stream(codec, nids[i], stream_tag,
						   i * 2, format);
		else if (!mout->no_share_stream) /* copy front */
			snd_hda_codec_setup_stream(codec, nids[i], stream_tag,
						   0, format);
	}

	/* extra surrounds */
	for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++) {
		int ch = 0;
		if (!mout->extra_out_nid[i])
			break;
		if (chs >= (i + 1) * 2)
			ch = i * 2;
		else if (!mout->no_share_stream)
			break;
		snd_hda_codec_setup_stream(codec, mout->extra_out_nid[i],
					   stream_tag, ch, format);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_analog_prepare);

/**
 * snd_hda_multi_out_analog_cleanup - clean up the setting for analog out
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_analog_cleanup(struct hda_codec *codec,
				     struct hda_multi_out *mout)
{
	const hda_nid_t *nids = mout->dac_nids;
	int i;

	for (i = 0; i < mout->num_dacs; i++)
		snd_hda_codec_cleanup_stream(codec, nids[i]);
	if (mout->hp_nid)
		snd_hda_codec_cleanup_stream(codec, mout->hp_nid);
	for (i = 0; i < ARRAY_SIZE(mout->hp_out_nid); i++)
		if (mout->hp_out_nid[i])
			snd_hda_codec_cleanup_stream(codec,
						     mout->hp_out_nid[i]);
	for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++)
		if (mout->extra_out_nid[i])
			snd_hda_codec_cleanup_stream(codec,
						     mout->extra_out_nid[i]);
	mutex_lock(&codec->spdif_mutex);
	if (mout->dig_out_nid && mout->dig_out_used == HDA_DIG_ANALOG_DUP) {
		cleanup_dig_out_stream(codec, mout->dig_out_nid);
		mout->dig_out_used = 0;
	}
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_analog_cleanup);

/**
 * snd_hda_get_default_vref - Get the default (mic) VREF pin bits
 * @codec: the HDA codec
 * @pin: referred pin NID
 *
 * Guess the suitable VREF pin bits to be set as the pin-control value.
 * Note: the function doesn't set the AC_PINCTL_IN_EN bit.
 */
unsigned int snd_hda_get_default_vref(struct hda_codec *codec, hda_nid_t pin)
{
	unsigned int pincap;
	unsigned int oldval;
	oldval = snd_hda_codec_read(codec, pin, 0,
				    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	pincap = snd_hda_query_pin_caps(codec, pin);
	pincap = (pincap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
	/* Exception: if the default pin setup is vref50, we give it priority */
	if ((pincap & AC_PINCAP_VREF_80) && oldval != PIN_VREF50)
		return AC_PINCTL_VREF_80;
	else if (pincap & AC_PINCAP_VREF_50)
		return AC_PINCTL_VREF_50;
	else if (pincap & AC_PINCAP_VREF_100)
		return AC_PINCTL_VREF_100;
	else if (pincap & AC_PINCAP_VREF_GRD)
		return AC_PINCTL_VREF_GRD;
	return AC_PINCTL_VREF_HIZ;
}
EXPORT_SYMBOL_GPL(snd_hda_get_default_vref);

/**
 * snd_hda_correct_pin_ctl - correct the pin ctl value for matching with the pin cap
 * @codec: the HDA codec
 * @pin: referred pin NID
 * @val: pin ctl value to audit
 */
unsigned int snd_hda_correct_pin_ctl(struct hda_codec *codec,
				     hda_nid_t pin, unsigned int val)
{
	static const unsigned int cap_lists[][2] = {
		{ AC_PINCTL_VREF_100, AC_PINCAP_VREF_100 },
		{ AC_PINCTL_VREF_80, AC_PINCAP_VREF_80 },
		{ AC_PINCTL_VREF_50, AC_PINCAP_VREF_50 },
		{ AC_PINCTL_VREF_GRD, AC_PINCAP_VREF_GRD },
	};
	unsigned int cap;

	if (!val)
		return 0;
	cap = snd_hda_query_pin_caps(codec, pin);
	if (!cap)
		return val; /* don't know what to do... */

	if (val & AC_PINCTL_OUT_EN) {
		if (!(cap & AC_PINCAP_OUT))
			val &= ~(AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
		else if ((val & AC_PINCTL_HP_EN) && !(cap & AC_PINCAP_HP_DRV))
			val &= ~AC_PINCTL_HP_EN;
	}

	if (val & AC_PINCTL_IN_EN) {
		if (!(cap & AC_PINCAP_IN))
			val &= ~(AC_PINCTL_IN_EN | AC_PINCTL_VREFEN);
		else {
			unsigned int vcap, vref;
			int i;
			vcap = (cap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
			vref = val & AC_PINCTL_VREFEN;
			for (i = 0; i < ARRAY_SIZE(cap_lists); i++) {
				if (vref == cap_lists[i][0] &&
				    !(vcap & cap_lists[i][1])) {
					if (i == ARRAY_SIZE(cap_lists) - 1)
						vref = AC_PINCTL_VREF_HIZ;
					else
						vref = cap_lists[i + 1][0];
				}
			}
			val &= ~AC_PINCTL_VREFEN;
			val |= vref;
		}
	}

	return val;
}
EXPORT_SYMBOL_GPL(snd_hda_correct_pin_ctl);

/**
 * _snd_hda_set_pin_ctl - Helper to set pin ctl value
 * @codec: the HDA codec
 * @pin: referred pin NID
 * @val: pin control value to set
 * @cached: access over codec pinctl cache or direct write
 *
 * This function is a helper to set a pin ctl value more safely.
 * It corrects the pin ctl value via snd_hda_correct_pin_ctl(), stores the
 * value in pin target array via snd_hda_codec_set_pin_target(), then
 * actually writes the value via either snd_hda_codec_write_cache() or
 * snd_hda_codec_write() depending on @cached flag.
 */
int _snd_hda_set_pin_ctl(struct hda_codec *codec, hda_nid_t pin,
			 unsigned int val, bool cached)
{
	val = snd_hda_correct_pin_ctl(codec, pin, val);
	snd_hda_codec_set_pin_target(codec, pin, val);
	if (cached)
		return snd_hda_codec_write_cache(codec, pin, 0,
				AC_VERB_SET_PIN_WIDGET_CONTROL, val);
	else
		return snd_hda_codec_write(codec, pin, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL, val);
}
EXPORT_SYMBOL_GPL(_snd_hda_set_pin_ctl);

/**
 * snd_hda_add_imux_item - Add an item to input_mux
 * @codec: the HDA codec
 * @imux: imux helper object
 * @label: the name of imux item to assign
 * @index: index number of imux item to assign
 * @type_idx: pointer to store the resultant label index
 *
 * When the same label is used already in the existing items, the number
 * suffix is appended to the label.  This label index number is stored
 * to type_idx when non-NULL pointer is given.
 */
int snd_hda_add_imux_item(struct hda_codec *codec,
			  struct hda_input_mux *imux, const char *label,
			  int index, int *type_idx)
{
	int i, label_idx = 0;
	if (imux->num_items >= HDA_MAX_NUM_INPUTS) {
		codec_err(codec, "hda_codec: Too many imux items!\n");
		return -EINVAL;
	}
	for (i = 0; i < imux->num_items; i++) {
		if (!strncmp(label, imux->items[i].label, strlen(label)))
			label_idx++;
	}
	if (type_idx)
		*type_idx = label_idx;
	if (label_idx > 0)
		snprintf(imux->items[imux->num_items].label,
			 sizeof(imux->items[imux->num_items].label),
			 "%s %d", label, label_idx);
	else
		strscpy(imux->items[imux->num_items].label, label,
			sizeof(imux->items[imux->num_items].label));
	imux->items[imux->num_items].index = index;
	imux->num_items++;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_add_imux_item);

/**
 * snd_hda_bus_reset_codecs - Reset the bus
 * @bus: HD-audio bus
 */
void snd_hda_bus_reset_codecs(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_codec(codec, bus) {
		/* FIXME: maybe a better way needed for forced reset */
		if (current_work() != &codec->jackpoll_work.work)
			cancel_delayed_work_sync(&codec->jackpoll_work);
#ifdef CONFIG_PM
		if (hda_codec_is_power_on(codec)) {
			hda_call_codec_suspend(codec);
			hda_call_codec_resume(codec);
		}
#endif
	}
}

/**
 * snd_print_pcm_bits - Print the supported PCM fmt bits to the string buffer
 * @pcm: PCM caps bits
 * @buf: the string buffer to write
 * @buflen: the max buffer length
 *
 * used by hda_proc.c and hda_eld.c
 */
void snd_print_pcm_bits(int pcm, char *buf, int buflen)
{
	static const unsigned int bits[] = { 8, 16, 20, 24, 32 };
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(bits); i++)
		if (pcm & (AC_SUPPCM_BITS_8 << i))
			j += scnprintf(buf + j, buflen - j,  " %d", bits[i]);

	buf[j] = '\0'; /* necessary when j == 0 */
}
EXPORT_SYMBOL_GPL(snd_print_pcm_bits);

MODULE_DESCRIPTION("HDA codec core");
MODULE_LICENSE("GPL");
