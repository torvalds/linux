/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <sound/core.h>
#include "hda_codec.h"
#include <sound/asoundef.h>
#include <sound/tlv.h>
#include <sound/initval.h>
#include <sound/jack.h>
#include "hda_local.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include <sound/hda_hwdep.h>

#define CREATE_TRACE_POINTS
#include "hda_trace.h"

/*
 * vendor / preset table
 */

struct hda_vendor_id {
	unsigned int id;
	const char *name;
};

/* codec vendor labels */
static struct hda_vendor_id hda_vendor_ids[] = {
	{ 0x1002, "ATI" },
	{ 0x1013, "Cirrus Logic" },
	{ 0x1057, "Motorola" },
	{ 0x1095, "Silicon Image" },
	{ 0x10de, "Nvidia" },
	{ 0x10ec, "Realtek" },
	{ 0x1102, "Creative" },
	{ 0x1106, "VIA" },
	{ 0x111d, "IDT" },
	{ 0x11c1, "LSI" },
	{ 0x11d4, "Analog Devices" },
	{ 0x13f6, "C-Media" },
	{ 0x14f1, "Conexant" },
	{ 0x17e8, "Chrontel" },
	{ 0x1854, "LG" },
	{ 0x1aec, "Wolfson Microelectronics" },
	{ 0x434d, "C-Media" },
	{ 0x8086, "Intel" },
	{ 0x8384, "SigmaTel" },
	{} /* terminator */
};

static DEFINE_MUTEX(preset_mutex);
static LIST_HEAD(hda_preset_tables);

int snd_hda_add_codec_preset(struct hda_codec_preset_list *preset)
{
	mutex_lock(&preset_mutex);
	list_add_tail(&preset->list, &hda_preset_tables);
	mutex_unlock(&preset_mutex);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_add_codec_preset);

int snd_hda_delete_codec_preset(struct hda_codec_preset_list *preset)
{
	mutex_lock(&preset_mutex);
	list_del(&preset->list);
	mutex_unlock(&preset_mutex);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_delete_codec_preset);

#ifdef CONFIG_PM
#define codec_in_pm(codec)	((codec)->in_pm)
static void hda_power_work(struct work_struct *work);
static void hda_keep_power_on(struct hda_codec *codec);
#define hda_codec_is_power_on(codec)	((codec)->power_on)
static inline void hda_call_pm_notify(struct hda_bus *bus, bool power_up)
{
	if (bus->ops.pm_notify)
		bus->ops.pm_notify(bus, power_up);
}
#else
#define codec_in_pm(codec)	0
static inline void hda_keep_power_on(struct hda_codec *codec) {}
#define hda_codec_is_power_on(codec)	1
#define hda_call_pm_notify(bus, state) {}
#endif

/**
 * snd_hda_get_jack_location - Give a location string of the jack
 * @cfg: pin default config value
 *
 * Parse the pin default config value and returns the string of the
 * jack location, e.g. "Rear", "Front", etc.
 */
const char *snd_hda_get_jack_location(u32 cfg)
{
	static char *bases[7] = {
		"N/A", "Rear", "Front", "Left", "Right", "Top", "Bottom",
	};
	static unsigned char specials_idx[] = {
		0x07, 0x08,
		0x17, 0x18, 0x19,
		0x37, 0x38
	};
	static char *specials[] = {
		"Rear Panel", "Drive Bar",
		"Riser", "HDMI", "ATAPI",
		"Mobile-In", "Mobile-Out"
	};
	int i;
	cfg = (cfg & AC_DEFCFG_LOCATION) >> AC_DEFCFG_LOCATION_SHIFT;
	if ((cfg & 0x0f) < 7)
		return bases[cfg & 0x0f];
	for (i = 0; i < ARRAY_SIZE(specials_idx); i++) {
		if (cfg == specials_idx[i])
			return specials[i];
	}
	return "UNKNOWN";
}
EXPORT_SYMBOL_HDA(snd_hda_get_jack_location);

/**
 * snd_hda_get_jack_connectivity - Give a connectivity string of the jack
 * @cfg: pin default config value
 *
 * Parse the pin default config value and returns the string of the
 * jack connectivity, i.e. external or internal connection.
 */
const char *snd_hda_get_jack_connectivity(u32 cfg)
{
	static char *jack_locations[4] = { "Ext", "Int", "Sep", "Oth" };

	return jack_locations[(cfg >> (AC_DEFCFG_LOCATION_SHIFT + 4)) & 3];
}
EXPORT_SYMBOL_HDA(snd_hda_get_jack_connectivity);

/**
 * snd_hda_get_jack_type - Give a type string of the jack
 * @cfg: pin default config value
 *
 * Parse the pin default config value and returns the string of the
 * jack type, i.e. the purpose of the jack, such as Line-Out or CD.
 */
const char *snd_hda_get_jack_type(u32 cfg)
{
	static char *jack_types[16] = {
		"Line Out", "Speaker", "HP Out", "CD",
		"SPDIF Out", "Digital Out", "Modem Line", "Modem Hand",
		"Line In", "Aux", "Mic", "Telephony",
		"SPDIF In", "Digital In", "Reserved", "Other"
	};

	return jack_types[(cfg & AC_DEFCFG_DEVICE)
				>> AC_DEFCFG_DEVICE_SHIFT];
}
EXPORT_SYMBOL_HDA(snd_hda_get_jack_type);

/*
 * Compose a 32bit command word to be sent to the HD-audio controller
 */
static inline unsigned int
make_codec_cmd(struct hda_codec *codec, hda_nid_t nid, int direct,
	       unsigned int verb, unsigned int parm)
{
	u32 val;

	if ((codec->addr & ~0xf) || (direct & ~1) || (nid & ~0x7f) ||
	    (verb & ~0xfff) || (parm & ~0xffff)) {
		printk(KERN_ERR "hda-codec: out of range cmd %x:%x:%x:%x:%x\n",
		       codec->addr, direct, nid, verb, parm);
		return ~0;
	}

	val = (u32)codec->addr << 28;
	val |= (u32)direct << 27;
	val |= (u32)nid << 20;
	val |= verb << 8;
	val |= parm;
	return val;
}

/*
 * Send and receive a verb
 */
static int codec_exec_verb(struct hda_codec *codec, unsigned int cmd,
			   unsigned int *res)
{
	struct hda_bus *bus = codec->bus;
	int err;

	if (cmd == ~0)
		return -1;

	if (res)
		*res = -1;
 again:
	snd_hda_power_up(codec);
	mutex_lock(&bus->cmd_mutex);
	for (;;) {
		trace_hda_send_cmd(codec, cmd);
		err = bus->ops.command(bus, cmd);
		if (err != -EAGAIN)
			break;
		/* process pending verbs */
		bus->ops.get_response(bus, codec->addr);
	}
	if (!err && res) {
		*res = bus->ops.get_response(bus, codec->addr);
		trace_hda_get_response(codec, *res);
	}
	mutex_unlock(&bus->cmd_mutex);
	snd_hda_power_down(codec);
	if (!codec_in_pm(codec) && res && *res == -1 && bus->rirb_error) {
		if (bus->response_reset) {
			snd_printd("hda_codec: resetting BUS due to "
				   "fatal communication error\n");
			trace_hda_bus_reset(bus);
			bus->ops.bus_reset(bus);
		}
		goto again;
	}
	/* clear reset-flag when the communication gets recovered */
	if (!err || codec_in_pm(codec))
		bus->response_reset = 0;
	return err;
}

/**
 * snd_hda_codec_read - send a command and get the response
 * @codec: the HDA codec
 * @nid: NID to send the command
 * @direct: direct flag
 * @verb: the verb to send
 * @parm: the parameter for the verb
 *
 * Send a single command and read the corresponding response.
 *
 * Returns the obtained response value, or -1 for an error.
 */
unsigned int snd_hda_codec_read(struct hda_codec *codec, hda_nid_t nid,
				int direct,
				unsigned int verb, unsigned int parm)
{
	unsigned cmd = make_codec_cmd(codec, nid, direct, verb, parm);
	unsigned int res;
	if (codec_exec_verb(codec, cmd, &res))
		return -1;
	return res;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_read);

/**
 * snd_hda_codec_write - send a single command without waiting for response
 * @codec: the HDA codec
 * @nid: NID to send the command
 * @direct: direct flag
 * @verb: the verb to send
 * @parm: the parameter for the verb
 *
 * Send a single command without waiting for response.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_codec_write(struct hda_codec *codec, hda_nid_t nid, int direct,
			 unsigned int verb, unsigned int parm)
{
	unsigned int cmd = make_codec_cmd(codec, nid, direct, verb, parm);
	unsigned int res;
	return codec_exec_verb(codec, cmd,
			       codec->bus->sync_write ? &res : NULL);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_write);

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
EXPORT_SYMBOL_HDA(snd_hda_sequence_write);

/**
 * snd_hda_get_sub_nodes - get the range of sub nodes
 * @codec: the HDA codec
 * @nid: NID to parse
 * @start_id: the pointer to store the start NID
 *
 * Parse the NID and store the start NID of its sub-nodes.
 * Returns the number of sub-nodes.
 */
int snd_hda_get_sub_nodes(struct hda_codec *codec, hda_nid_t nid,
			  hda_nid_t *start_id)
{
	unsigned int parm;

	parm = snd_hda_param_read(codec, nid, AC_PAR_NODE_COUNT);
	if (parm == -1) {
		*start_id = 0;
		return 0;
	}
	*start_id = (parm >> 16) & 0x7fff;
	return (int)(parm & 0x7fff);
}
EXPORT_SYMBOL_HDA(snd_hda_get_sub_nodes);

/* connection list element */
struct hda_conn_list {
	struct list_head list;
	int len;
	hda_nid_t nid;
	hda_nid_t conns[0];
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

	p = kmalloc(sizeof(*p) + len * sizeof(hda_nid_t), GFP_KERNEL);
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
		result = kmalloc(sizeof(hda_nid_t) * len, GFP_KERNEL);
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
 * @len: number of connection list entries
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
EXPORT_SYMBOL_HDA(snd_hda_get_conn_list);

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
			snd_printk(KERN_ERR "hda_codec: "
				   "Too many connections %d for NID 0x%x\n",
				   len, nid);
			return -EINVAL;
		}
		memcpy(conn_list, list, len * sizeof(hda_nid_t));
	}

	return len;
}
EXPORT_SYMBOL_HDA(snd_hda_get_connections);

/* return CONNLIST_LEN parameter of the given widget */
static unsigned int get_num_conns(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int wcaps = get_wcaps(codec, nid);
	unsigned int parm;

	if (!(wcaps & AC_WCAP_CONN_LIST) &&
	    get_wcaps_type(wcaps) != AC_WID_VOL_KNB)
		return 0;

	parm = snd_hda_param_read(codec, nid, AC_PAR_CONNLIST_LEN);
	if (parm == -1)
		parm = 0;
	return parm;
}

int snd_hda_get_num_raw_conns(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_get_raw_connections(codec, nid, NULL, 0);
}

/**
 * snd_hda_get_raw_connections - copy connection list without cache
 * @codec: the HDA codec
 * @nid: NID to parse
 * @conn_list: connection list array
 * @max_conns: max. number of connections to store
 *
 * Like snd_hda_get_connections(), copy the connection list but without
 * checking through the connection-list cache.
 * Currently called only from hda_proc.c, so not exported.
 */
int snd_hda_get_raw_connections(struct hda_codec *codec, hda_nid_t nid,
				hda_nid_t *conn_list, int max_conns)
{
	unsigned int parm;
	int i, conn_len, conns;
	unsigned int shift, num_elems, mask;
	hda_nid_t prev_nid;
	int null_count = 0;

	parm = get_num_conns(codec, nid);
	if (!parm)
		return 0;

	if (parm & AC_CLIST_LONG) {
		/* long form */
		shift = 16;
		num_elems = 2;
	} else {
		/* short form */
		shift = 8;
		num_elems = 4;
	}
	conn_len = parm & AC_CLIST_LENGTH;
	mask = (1 << (shift-1)) - 1;

	if (!conn_len)
		return 0; /* no connection */

	if (conn_len == 1) {
		/* single connection */
		parm = snd_hda_codec_read(codec, nid, 0,
					  AC_VERB_GET_CONNECT_LIST, 0);
		if (parm == -1 && codec->bus->rirb_error)
			return -EIO;
		if (conn_list)
			conn_list[0] = parm & mask;
		return 1;
	}

	/* multi connection */
	conns = 0;
	prev_nid = 0;
	for (i = 0; i < conn_len; i++) {
		int range_val;
		hda_nid_t val, n;

		if (i % num_elems == 0) {
			parm = snd_hda_codec_read(codec, nid, 0,
						  AC_VERB_GET_CONNECT_LIST, i);
			if (parm == -1 && codec->bus->rirb_error)
				return -EIO;
		}
		range_val = !!(parm & (1 << (shift-1))); /* ranges */
		val = parm & mask;
		if (val == 0 && null_count++) {  /* no second chance */
			snd_printk(KERN_WARNING "hda_codec: "
				   "invalid CONNECT_LIST verb %x[%i]:%x\n",
				    nid, i, parm);
			return 0;
		}
		parm >>= shift;
		if (range_val) {
			/* ranges between the previous and this one */
			if (!prev_nid || prev_nid >= val) {
				snd_printk(KERN_WARNING "hda_codec: "
					   "invalid dep_range_val %x:%x\n",
					   prev_nid, val);
				continue;
			}
			for (n = prev_nid + 1; n <= val; n++) {
				if (conn_list) {
					if (conns >= max_conns)
						return -ENOSPC;
					conn_list[conns] = n;
				}
				conns++;
			}
		} else {
			if (conn_list) {
				if (conns >= max_conns)
					return -ENOSPC;
				conn_list[conns] = val;
			}
			conns++;
		}
		prev_nid = val;
	}
	return conns;
}

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
EXPORT_SYMBOL_HDA(snd_hda_override_conn_list);

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
		snd_printd("hda_codec: too deep connection for 0x%x\n", nid);
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
EXPORT_SYMBOL_HDA(snd_hda_get_conn_index);

/**
 * snd_hda_queue_unsol_event - add an unsolicited event to queue
 * @bus: the BUS
 * @res: unsolicited event (lower 32bit of RIRB entry)
 * @res_ex: codec addr and flags (upper 32bit or RIRB entry)
 *
 * Adds the given event to the queue.  The events are processed in
 * the workqueue asynchronously.  Call this function in the interrupt
 * hanlder when RIRB receives an unsolicited event.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_queue_unsol_event(struct hda_bus *bus, u32 res, u32 res_ex)
{
	struct hda_bus_unsolicited *unsol;
	unsigned int wp;

	if (!bus || !bus->workq)
		return 0;

	trace_hda_unsol_event(bus, res, res_ex);
	unsol = bus->unsol;
	if (!unsol)
		return 0;

	wp = (unsol->wp + 1) % HDA_UNSOL_QUEUE_SIZE;
	unsol->wp = wp;

	wp <<= 1;
	unsol->queue[wp] = res;
	unsol->queue[wp + 1] = res_ex;

	queue_work(bus->workq, &unsol->work);

	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_queue_unsol_event);

/*
 * process queued unsolicited events
 */
static void process_unsol_events(struct work_struct *work)
{
	struct hda_bus_unsolicited *unsol =
		container_of(work, struct hda_bus_unsolicited, work);
	struct hda_bus *bus = unsol->bus;
	struct hda_codec *codec;
	unsigned int rp, caddr, res;

	while (unsol->rp != unsol->wp) {
		rp = (unsol->rp + 1) % HDA_UNSOL_QUEUE_SIZE;
		unsol->rp = rp;
		rp <<= 1;
		res = unsol->queue[rp];
		caddr = unsol->queue[rp + 1];
		if (!(caddr & (1 << 4))) /* no unsolicited event? */
			continue;
		codec = bus->caddr_tbl[caddr & 0x0f];
		if (codec && codec->patch_ops.unsol_event)
			codec->patch_ops.unsol_event(codec, res);
	}
}

/*
 * initialize unsolicited queue
 */
static int init_unsol_queue(struct hda_bus *bus)
{
	struct hda_bus_unsolicited *unsol;

	if (bus->unsol) /* already initialized */
		return 0;

	unsol = kzalloc(sizeof(*unsol), GFP_KERNEL);
	if (!unsol) {
		snd_printk(KERN_ERR "hda_codec: "
			   "can't allocate unsolicited queue\n");
		return -ENOMEM;
	}
	INIT_WORK(&unsol->work, process_unsol_events);
	unsol->bus = bus;
	bus->unsol = unsol;
	return 0;
}

/*
 * destructor
 */
static void snd_hda_codec_free(struct hda_codec *codec);

static int snd_hda_bus_free(struct hda_bus *bus)
{
	struct hda_codec *codec, *n;

	if (!bus)
		return 0;
	if (bus->workq)
		flush_workqueue(bus->workq);
	if (bus->unsol)
		kfree(bus->unsol);
	list_for_each_entry_safe(codec, n, &bus->codec_list, list) {
		snd_hda_codec_free(codec);
	}
	if (bus->ops.private_free)
		bus->ops.private_free(bus);
	if (bus->workq)
		destroy_workqueue(bus->workq);
	kfree(bus);
	return 0;
}

static int snd_hda_bus_dev_free(struct snd_device *device)
{
	struct hda_bus *bus = device->device_data;
	bus->shutdown = 1;
	return snd_hda_bus_free(bus);
}

#ifdef CONFIG_SND_HDA_HWDEP
static int snd_hda_bus_dev_register(struct snd_device *device)
{
	struct hda_bus *bus = device->device_data;
	struct hda_codec *codec;
	list_for_each_entry(codec, &bus->codec_list, list) {
		snd_hda_hwdep_add_sysfs(codec);
		snd_hda_hwdep_add_power_sysfs(codec);
	}
	return 0;
}
#else
#define snd_hda_bus_dev_register	NULL
#endif

/**
 * snd_hda_bus_new - create a HDA bus
 * @card: the card entry
 * @temp: the template for hda_bus information
 * @busp: the pointer to store the created bus instance
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_bus_new(struct snd_card *card,
			      const struct hda_bus_template *temp,
			      struct hda_bus **busp)
{
	struct hda_bus *bus;
	int err;
	static struct snd_device_ops dev_ops = {
		.dev_register = snd_hda_bus_dev_register,
		.dev_free = snd_hda_bus_dev_free,
	};

	if (snd_BUG_ON(!temp))
		return -EINVAL;
	if (snd_BUG_ON(!temp->ops.command || !temp->ops.get_response))
		return -EINVAL;

	if (busp)
		*busp = NULL;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (bus == NULL) {
		snd_printk(KERN_ERR "can't allocate struct hda_bus\n");
		return -ENOMEM;
	}

	bus->card = card;
	bus->private_data = temp->private_data;
	bus->pci = temp->pci;
	bus->modelname = temp->modelname;
	bus->power_save = temp->power_save;
	bus->ops = temp->ops;

	mutex_init(&bus->cmd_mutex);
	mutex_init(&bus->prepare_mutex);
	INIT_LIST_HEAD(&bus->codec_list);

	snprintf(bus->workq_name, sizeof(bus->workq_name),
		 "hd-audio%d", card->number);
	bus->workq = create_singlethread_workqueue(bus->workq_name);
	if (!bus->workq) {
		snd_printk(KERN_ERR "cannot create workqueue %s\n",
			   bus->workq_name);
		kfree(bus);
		return -ENOMEM;
	}

	err = snd_device_new(card, SNDRV_DEV_BUS, bus, &dev_ops);
	if (err < 0) {
		snd_hda_bus_free(bus);
		return err;
	}
	if (busp)
		*busp = bus;
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_bus_new);

#ifdef CONFIG_SND_HDA_GENERIC
#define is_generic_config(codec) \
	(codec->modelname && !strcmp(codec->modelname, "generic"))
#else
#define is_generic_config(codec)	0
#endif

#ifdef MODULE
#define HDA_MODREQ_MAX_COUNT	2	/* two request_modules()'s */
#else
#define HDA_MODREQ_MAX_COUNT	0	/* all presets are statically linked */
#endif

/*
 * find a matching codec preset
 */
static const struct hda_codec_preset *
find_codec_preset(struct hda_codec *codec)
{
	struct hda_codec_preset_list *tbl;
	const struct hda_codec_preset *preset;
	unsigned int mod_requested = 0;

	if (is_generic_config(codec))
		return NULL; /* use the generic parser */

 again:
	mutex_lock(&preset_mutex);
	list_for_each_entry(tbl, &hda_preset_tables, list) {
		if (!try_module_get(tbl->owner)) {
			snd_printk(KERN_ERR "hda_codec: cannot module_get\n");
			continue;
		}
		for (preset = tbl->preset; preset->id; preset++) {
			u32 mask = preset->mask;
			if (preset->afg && preset->afg != codec->afg)
				continue;
			if (preset->mfg && preset->mfg != codec->mfg)
				continue;
			if (!mask)
				mask = ~0;
			if (preset->id == (codec->vendor_id & mask) &&
			    (!preset->rev ||
			     preset->rev == codec->revision_id)) {
				mutex_unlock(&preset_mutex);
				codec->owner = tbl->owner;
				return preset;
			}
		}
		module_put(tbl->owner);
	}
	mutex_unlock(&preset_mutex);

	if (mod_requested < HDA_MODREQ_MAX_COUNT) {
		char name[32];
		if (!mod_requested)
			snprintf(name, sizeof(name), "snd-hda-codec-id:%08x",
				 codec->vendor_id);
		else
			snprintf(name, sizeof(name), "snd-hda-codec-id:%04x*",
				 (codec->vendor_id >> 16) & 0xffff);
		request_module(name);
		mod_requested++;
		goto again;
	}
	return NULL;
}

/*
 * get_codec_name - store the codec name
 */
static int get_codec_name(struct hda_codec *codec)
{
	const struct hda_vendor_id *c;
	const char *vendor = NULL;
	u16 vendor_id = codec->vendor_id >> 16;
	char tmp[16];

	if (codec->vendor_name)
		goto get_chip_name;

	for (c = hda_vendor_ids; c->id; c++) {
		if (c->id == vendor_id) {
			vendor = c->name;
			break;
		}
	}
	if (!vendor) {
		sprintf(tmp, "Generic %04x", vendor_id);
		vendor = tmp;
	}
	codec->vendor_name = kstrdup(vendor, GFP_KERNEL);
	if (!codec->vendor_name)
		return -ENOMEM;

 get_chip_name:
	if (codec->chip_name)
		return 0;

	if (codec->preset && codec->preset->name)
		codec->chip_name = kstrdup(codec->preset->name, GFP_KERNEL);
	else {
		sprintf(tmp, "ID %x", codec->vendor_id & 0xffff);
		codec->chip_name = kstrdup(tmp, GFP_KERNEL);
	}
	if (!codec->chip_name)
		return -ENOMEM;
	return 0;
}

/*
 * look for an AFG and MFG nodes
 */
static void setup_fg_nodes(struct hda_codec *codec)
{
	int i, total_nodes, function_id;
	hda_nid_t nid;

	total_nodes = snd_hda_get_sub_nodes(codec, AC_NODE_ROOT, &nid);
	for (i = 0; i < total_nodes; i++, nid++) {
		function_id = snd_hda_param_read(codec, nid,
						AC_PAR_FUNCTION_TYPE);
		switch (function_id & 0xff) {
		case AC_GRP_AUDIO_FUNCTION:
			codec->afg = nid;
			codec->afg_function_id = function_id & 0xff;
			codec->afg_unsol = (function_id >> 8) & 1;
			break;
		case AC_GRP_MODEM_FUNCTION:
			codec->mfg = nid;
			codec->mfg_function_id = function_id & 0xff;
			codec->mfg_unsol = (function_id >> 8) & 1;
			break;
		default:
			break;
		}
	}
}

/*
 * read widget caps for each widget and store in cache
 */
static int read_widget_caps(struct hda_codec *codec, hda_nid_t fg_node)
{
	int i;
	hda_nid_t nid;

	codec->num_nodes = snd_hda_get_sub_nodes(codec, fg_node,
						 &codec->start_nid);
	codec->wcaps = kmalloc(codec->num_nodes * 4, GFP_KERNEL);
	if (!codec->wcaps)
		return -ENOMEM;
	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++)
		codec->wcaps[i] = snd_hda_param_read(codec, nid,
						     AC_PAR_AUDIO_WIDGET_CAP);
	return 0;
}

/* read all pin default configurations and save codec->init_pins */
static int read_pin_defaults(struct hda_codec *codec)
{
	int i;
	hda_nid_t nid = codec->start_nid;

	for (i = 0; i < codec->num_nodes; i++, nid++) {
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
	int i;
	for (i = 0; i < array->used; i++) {
		struct hda_pincfg *pin = snd_array_elem(array, i);
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
EXPORT_SYMBOL_HDA(snd_hda_codec_set_pincfg);

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

#ifdef CONFIG_SND_HDA_HWDEP
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
EXPORT_SYMBOL_HDA(snd_hda_codec_get_pincfg);

/* remember the current pinctl target value */
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
EXPORT_SYMBOL_HDA(snd_hda_codec_set_pin_target);

/* return the current pinctl target value */
int snd_hda_codec_get_pin_target(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_pincfg *pin;

	pin = look_up_pincfg(codec, &codec->init_pins, nid);
	if (!pin)
		return 0;
	return pin->target;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_get_pin_target);

/**
 * snd_hda_shutup_pins - Shut up all pins
 * @codec: the HDA codec
 *
 * Clear all pin controls to shup up before suspend for avoiding click noise.
 * The controls aren't cached so that they can be resumed properly.
 */
void snd_hda_shutup_pins(struct hda_codec *codec)
{
	int i;
	/* don't shut up pins when unloading the driver; otherwise it breaks
	 * the default pin setup at the next load of the driver
	 */
	if (codec->bus->shutdown)
		return;
	for (i = 0; i < codec->init_pins.used; i++) {
		struct hda_pincfg *pin = snd_array_elem(&codec->init_pins, i);
		/* use read here for syncing after issuing each verb */
		snd_hda_codec_read(codec, pin->nid, 0,
				   AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
	}
	codec->pins_shutup = 1;
}
EXPORT_SYMBOL_HDA(snd_hda_shutup_pins);

#ifdef CONFIG_PM
/* Restore the pin controls cleared previously via snd_hda_shutup_pins() */
static void restore_shutup_pins(struct hda_codec *codec)
{
	int i;
	if (!codec->pins_shutup)
		return;
	if (codec->bus->shutdown)
		return;
	for (i = 0; i < codec->init_pins.used; i++) {
		struct hda_pincfg *pin = snd_array_elem(&codec->init_pins, i);
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
	if (!codec->jackpoll_interval)
		return;

	snd_hda_jack_set_dirty_all(codec);
	snd_hda_jack_poll_all(codec);
	queue_delayed_work(codec->bus->workq, &codec->jackpoll_work,
			   codec->jackpoll_interval);
}

static void init_hda_cache(struct hda_cache_rec *cache,
			   unsigned int record_size);
static void free_hda_cache(struct hda_cache_rec *cache);

/* release all pincfg lists */
static void free_init_pincfgs(struct hda_codec *codec)
{
	snd_array_free(&codec->driver_pins);
#ifdef CONFIG_SND_HDA_HWDEP
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

	for (i = 0; i < codec->cvt_setups.used; i++) {
		p = snd_array_elem(&codec->cvt_setups, i);
		if (p->nid == nid)
			return p;
	}
	p = snd_array_new(&codec->cvt_setups);
	if (p)
		p->nid = nid;
	return p;
}

/*
 * codec destructor
 */
static void snd_hda_codec_free(struct hda_codec *codec)
{
	if (!codec)
		return;
	cancel_delayed_work_sync(&codec->jackpoll_work);
	snd_hda_jack_tbl_clear(codec);
	free_init_pincfgs(codec);
#ifdef CONFIG_PM
	cancel_delayed_work(&codec->power_work);
	flush_workqueue(codec->bus->workq);
#endif
	list_del(&codec->list);
	snd_array_free(&codec->mixers);
	snd_array_free(&codec->nids);
	snd_array_free(&codec->cvt_setups);
	snd_array_free(&codec->spdif_out);
	remove_conn_list(codec);
	codec->bus->caddr_tbl[codec->addr] = NULL;
	if (codec->patch_ops.free)
		codec->patch_ops.free(codec);
#ifdef CONFIG_PM
	if (!codec->pm_down_notified) /* cancel leftover refcounts */
		hda_call_pm_notify(codec->bus, false);
#endif
	module_put(codec->owner);
	free_hda_cache(&codec->amp_cache);
	free_hda_cache(&codec->cmd_cache);
	kfree(codec->vendor_name);
	kfree(codec->chip_name);
	kfree(codec->modelname);
	kfree(codec->wcaps);
	kfree(codec);
}

static bool snd_hda_codec_get_supported_ps(struct hda_codec *codec,
				hda_nid_t fg, unsigned int power_state);

static unsigned int hda_set_power_state(struct hda_codec *codec,
				unsigned int power_state);

/**
 * snd_hda_codec_new - create a HDA codec
 * @bus: the bus to assign
 * @codec_addr: the codec address
 * @codecp: the pointer to store the generated codec
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_codec_new(struct hda_bus *bus,
				unsigned int codec_addr,
				struct hda_codec **codecp)
{
	struct hda_codec *codec;
	char component[31];
	hda_nid_t fg;
	int err;

	if (snd_BUG_ON(!bus))
		return -EINVAL;
	if (snd_BUG_ON(codec_addr > HDA_MAX_CODEC_ADDRESS))
		return -EINVAL;

	if (bus->caddr_tbl[codec_addr]) {
		snd_printk(KERN_ERR "hda_codec: "
			   "address 0x%x is already occupied\n", codec_addr);
		return -EBUSY;
	}

	codec = kzalloc(sizeof(*codec), GFP_KERNEL);
	if (codec == NULL) {
		snd_printk(KERN_ERR "can't allocate struct hda_codec\n");
		return -ENOMEM;
	}

	codec->bus = bus;
	codec->addr = codec_addr;
	mutex_init(&codec->spdif_mutex);
	mutex_init(&codec->control_mutex);
	mutex_init(&codec->hash_mutex);
	init_hda_cache(&codec->amp_cache, sizeof(struct hda_amp_info));
	init_hda_cache(&codec->cmd_cache, sizeof(struct hda_cache_head));
	snd_array_init(&codec->mixers, sizeof(struct hda_nid_item), 32);
	snd_array_init(&codec->nids, sizeof(struct hda_nid_item), 32);
	snd_array_init(&codec->init_pins, sizeof(struct hda_pincfg), 16);
	snd_array_init(&codec->driver_pins, sizeof(struct hda_pincfg), 16);
	snd_array_init(&codec->cvt_setups, sizeof(struct hda_cvt_setup), 8);
	snd_array_init(&codec->spdif_out, sizeof(struct hda_spdif_out), 16);
	snd_array_init(&codec->jacktbl, sizeof(struct hda_jack_tbl), 16);
	snd_array_init(&codec->verbs, sizeof(struct hda_verb *), 8);
	INIT_LIST_HEAD(&codec->conn_list);

	INIT_DELAYED_WORK(&codec->jackpoll_work, hda_jackpoll_work);

#ifdef CONFIG_PM
	spin_lock_init(&codec->power_lock);
	INIT_DELAYED_WORK(&codec->power_work, hda_power_work);
	/* snd_hda_codec_new() marks the codec as power-up, and leave it as is.
	 * the caller has to power down appropriatley after initialization
	 * phase.
	 */
	hda_keep_power_on(codec);
	hda_call_pm_notify(bus, true);
#endif

	if (codec->bus->modelname) {
		codec->modelname = kstrdup(codec->bus->modelname, GFP_KERNEL);
		if (!codec->modelname) {
			snd_hda_codec_free(codec);
			return -ENODEV;
		}
	}

	list_add_tail(&codec->list, &bus->codec_list);
	bus->caddr_tbl[codec_addr] = codec;

	codec->vendor_id = snd_hda_param_read(codec, AC_NODE_ROOT,
					      AC_PAR_VENDOR_ID);
	if (codec->vendor_id == -1)
		/* read again, hopefully the access method was corrected
		 * in the last read...
		 */
		codec->vendor_id = snd_hda_param_read(codec, AC_NODE_ROOT,
						      AC_PAR_VENDOR_ID);
	codec->subsystem_id = snd_hda_param_read(codec, AC_NODE_ROOT,
						 AC_PAR_SUBSYSTEM_ID);
	codec->revision_id = snd_hda_param_read(codec, AC_NODE_ROOT,
						AC_PAR_REV_ID);

	setup_fg_nodes(codec);
	if (!codec->afg && !codec->mfg) {
		snd_printdd("hda_codec: no AFG or MFG node found\n");
		err = -ENODEV;
		goto error;
	}

	fg = codec->afg ? codec->afg : codec->mfg;
	err = read_widget_caps(codec, fg);
	if (err < 0) {
		snd_printk(KERN_ERR "hda_codec: cannot malloc\n");
		goto error;
	}
	err = read_pin_defaults(codec);
	if (err < 0)
		goto error;

	if (!codec->subsystem_id) {
		codec->subsystem_id =
			snd_hda_codec_read(codec, fg, 0,
					   AC_VERB_GET_SUBSYSTEM_ID, 0);
	}

#ifdef CONFIG_PM
	codec->d3_stop_clk = snd_hda_codec_get_supported_ps(codec, fg,
					AC_PWRST_CLKSTOP);
	if (!codec->d3_stop_clk)
		bus->power_keep_link_on = 1;
#endif
	codec->epss = snd_hda_codec_get_supported_ps(codec, fg,
					AC_PWRST_EPSS);

	/* power-up all before initialization */
	hda_set_power_state(codec, AC_PWRST_D0);

	snd_hda_codec_proc_new(codec);

	snd_hda_create_hwdep(codec);

	sprintf(component, "HDA:%08x,%08x,%08x", codec->vendor_id,
		codec->subsystem_id, codec->revision_id);
	snd_component_add(codec->bus->card, component);

	if (codecp)
		*codecp = codec;
	return 0;

 error:
	snd_hda_codec_free(codec);
	return err;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_new);

int snd_hda_codec_update_widgets(struct hda_codec *codec)
{
	hda_nid_t fg;
	int err;

	/* Assume the function group node does not change,
	 * only the widget nodes may change.
	 */
	kfree(codec->wcaps);
	fg = codec->afg ? codec->afg : codec->mfg;
	err = read_widget_caps(codec, fg);
	if (err < 0) {
		snd_printk(KERN_ERR "hda_codec: cannot malloc\n");
		return err;
	}

	snd_array_free(&codec->init_pins);
	err = read_pin_defaults(codec);

	return err;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_update_widgets);


/**
 * snd_hda_codec_configure - (Re-)configure the HD-audio codec
 * @codec: the HDA codec
 *
 * Start parsing of the given codec tree and (re-)initialize the whole
 * patch instance.
 *
 * Returns 0 if successful or a negative error code.
 */
int snd_hda_codec_configure(struct hda_codec *codec)
{
	int err;

	codec->preset = find_codec_preset(codec);
	if (!codec->vendor_name || !codec->chip_name) {
		err = get_codec_name(codec);
		if (err < 0)
			return err;
	}

	if (is_generic_config(codec)) {
		err = snd_hda_parse_generic_codec(codec);
		goto patched;
	}
	if (codec->preset && codec->preset->patch) {
		err = codec->preset->patch(codec);
		goto patched;
	}

	/* call the default parser */
	err = snd_hda_parse_generic_codec(codec);
	if (err < 0)
		printk(KERN_ERR "hda-codec: No codec parser is available\n");

 patched:
	if (!err && codec->patch_ops.unsol_event)
		err = init_unsol_queue(codec->bus);
	/* audio codec should override the mixer name */
	if (!err && (codec->afg || !*codec->bus->card->mixername))
		snprintf(codec->bus->card->mixername,
			 sizeof(codec->bus->card->mixername),
			 "%s %s", codec->vendor_name, codec->chip_name);
	return err;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_configure);

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

	snd_printdd("hda_codec_setup_stream: "
		    "NID=0x%x, stream=0x%x, channel=%d, format=0x%x\n",
		    nid, stream_tag, channel_id, format);
	p = get_hda_cvt_setup(codec, nid);
	if (!p)
		return;

	if (codec->pcm_format_first)
		update_pcm_format(codec, p, nid, format);
	update_pcm_stream_id(codec, p, nid, stream_tag, channel_id);
	if (!codec->pcm_format_first)
		update_pcm_format(codec, p, nid, format);

	p->active = 1;
	p->dirty = 0;

	/* make other inactive cvts with the same stream-tag dirty */
	type = get_wcaps_type(get_wcaps(codec, nid));
	list_for_each_entry(c, &codec->bus->codec_list, list) {
		for (i = 0; i < c->cvt_setups.used; i++) {
			p = snd_array_elem(&c->cvt_setups, i);
			if (!p->active && p->stream_tag == stream_tag &&
			    get_wcaps_type(get_wcaps(c, p->nid)) == type)
				p->dirty = 1;
		}
	}
}
EXPORT_SYMBOL_HDA(snd_hda_codec_setup_stream);

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

	snd_printdd("hda_codec_cleanup_stream: NID=0x%x\n", nid);
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
EXPORT_SYMBOL_HDA(__snd_hda_codec_cleanup_stream);

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
}

/* clean up the all conflicting obsolete streams */
static void purify_inactive_streams(struct hda_codec *codec)
{
	struct hda_codec *c;
	int i;

	list_for_each_entry(c, &codec->bus->codec_list, list) {
		for (i = 0; i < c->cvt_setups.used; i++) {
			struct hda_cvt_setup *p;
			p = snd_array_elem(&c->cvt_setups, i);
			if (p->dirty)
				really_cleanup_stream(c, p);
		}
	}
}

#ifdef CONFIG_PM
/* clean up all streams; called from suspend */
static void hda_cleanup_all_streams(struct hda_codec *codec)
{
	int i;

	for (i = 0; i < codec->cvt_setups.used; i++) {
		struct hda_cvt_setup *p = snd_array_elem(&codec->cvt_setups, i);
		if (p->stream_tag)
			really_cleanup_stream(codec, p);
	}
}
#endif

/*
 * amp access functions
 */

/* FIXME: more better hash key? */
#define HDA_HASH_KEY(nid, dir, idx) (u32)((nid) + ((idx) << 16) + ((dir) << 24))
#define HDA_HASH_PINCAP_KEY(nid) (u32)((nid) + (0x02 << 24))
#define HDA_HASH_PARPCM_KEY(nid) (u32)((nid) + (0x03 << 24))
#define HDA_HASH_PARSTR_KEY(nid) (u32)((nid) + (0x04 << 24))
#define INFO_AMP_CAPS	(1<<0)
#define INFO_AMP_VOL(ch)	(1 << (1 + (ch)))

/* initialize the hash table */
static void init_hda_cache(struct hda_cache_rec *cache,
				     unsigned int record_size)
{
	memset(cache, 0, sizeof(*cache));
	memset(cache->hash, 0xff, sizeof(cache->hash));
	snd_array_init(&cache->buf, record_size, 64);
}

static void free_hda_cache(struct hda_cache_rec *cache)
{
	snd_array_free(&cache->buf);
}

/* query the hash.  allocate an entry if not found. */
static struct hda_cache_head  *get_hash(struct hda_cache_rec *cache, u32 key)
{
	u16 idx = key % (u16)ARRAY_SIZE(cache->hash);
	u16 cur = cache->hash[idx];
	struct hda_cache_head *info;

	while (cur != 0xffff) {
		info = snd_array_elem(&cache->buf, cur);
		if (info->key == key)
			return info;
		cur = info->next;
	}
	return NULL;
}

/* query the hash.  allocate an entry if not found. */
static struct hda_cache_head  *get_alloc_hash(struct hda_cache_rec *cache,
					      u32 key)
{
	struct hda_cache_head *info = get_hash(cache, key);
	if (!info) {
		u16 idx, cur;
		/* add a new hash entry */
		info = snd_array_new(&cache->buf);
		if (!info)
			return NULL;
		cur = snd_array_index(&cache->buf, info);
		info->key = key;
		info->val = 0;
		info->dirty = 0;
		idx = key % (u16)ARRAY_SIZE(cache->hash);
		info->next = cache->hash[idx];
		cache->hash[idx] = cur;
	}
	return info;
}

/* query and allocate an amp hash entry */
static inline struct hda_amp_info *
get_alloc_amp_hash(struct hda_codec *codec, u32 key)
{
	return (struct hda_amp_info *)get_alloc_hash(&codec->amp_cache, key);
}

/* overwrite the value with the key in the caps hash */
static int write_caps_hash(struct hda_codec *codec, u32 key, unsigned int val)
{
	struct hda_amp_info *info;

	mutex_lock(&codec->hash_mutex);
	info = get_alloc_amp_hash(codec, key);
	if (!info) {
		mutex_unlock(&codec->hash_mutex);
		return -EINVAL;
	}
	info->amp_caps = val;
	info->head.val |= INFO_AMP_CAPS;
	mutex_unlock(&codec->hash_mutex);
	return 0;
}

/* query the value from the caps hash; if not found, fetch the current
 * value from the given function and store in the hash
 */
static unsigned int
query_caps_hash(struct hda_codec *codec, hda_nid_t nid, int dir, u32 key,
		unsigned int (*func)(struct hda_codec *, hda_nid_t, int))
{
	struct hda_amp_info *info;
	unsigned int val;

	mutex_lock(&codec->hash_mutex);
	info = get_alloc_amp_hash(codec, key);
	if (!info) {
		mutex_unlock(&codec->hash_mutex);
		return 0;
	}
	if (!(info->head.val & INFO_AMP_CAPS)) {
		mutex_unlock(&codec->hash_mutex); /* for reentrance */
		val = func(codec, nid, dir);
		write_caps_hash(codec, key, val);
	} else {
		val = info->amp_caps;
		mutex_unlock(&codec->hash_mutex);
	}
	return val;
}

static unsigned int read_amp_cap(struct hda_codec *codec, hda_nid_t nid,
				 int direction)
{
	if (!(get_wcaps(codec, nid) & AC_WCAP_AMP_OVRD))
		nid = codec->afg;
	return snd_hda_param_read(codec, nid,
				  direction == HDA_OUTPUT ?
				  AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP);
}

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
	return query_caps_hash(codec, nid, direction,
			       HDA_HASH_KEY(nid, direction, 0),
			       read_amp_cap);
}
EXPORT_SYMBOL_HDA(query_amp_caps);

/**
 * snd_hda_override_amp_caps - Override the AMP capabilities
 * @codec: the CODEC to clean up
 * @nid: the NID to clean up
 * @direction: either #HDA_INPUT or #HDA_OUTPUT
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
	return write_caps_hash(codec, HDA_HASH_KEY(nid, dir, 0), caps);
}
EXPORT_SYMBOL_HDA(snd_hda_override_amp_caps);

static unsigned int read_pin_cap(struct hda_codec *codec, hda_nid_t nid,
				 int dir)
{
	return snd_hda_param_read(codec, nid, AC_PAR_PIN_CAP);
}

/**
 * snd_hda_query_pin_caps - Query PIN capabilities
 * @codec: the HD-auio codec
 * @nid: the NID to query
 *
 * Query PIN capabilities for the given widget.
 * Returns the obtained capability bits.
 *
 * When cap bits have been already read, this doesn't read again but
 * returns the cached value.
 */
u32 snd_hda_query_pin_caps(struct hda_codec *codec, hda_nid_t nid)
{
	return query_caps_hash(codec, nid, 0, HDA_HASH_PINCAP_KEY(nid),
			       read_pin_cap);
}
EXPORT_SYMBOL_HDA(snd_hda_query_pin_caps);

/**
 * snd_hda_override_pin_caps - Override the pin capabilities
 * @codec: the CODEC
 * @nid: the NID to override
 * @caps: the capability bits to set
 *
 * Override the cached PIN capabilitiy bits value by the given one.
 *
 * Returns zero if successful or a negative error code.
 */
int snd_hda_override_pin_caps(struct hda_codec *codec, hda_nid_t nid,
			      unsigned int caps)
{
	return write_caps_hash(codec, HDA_HASH_PINCAP_KEY(nid), caps);
}
EXPORT_SYMBOL_HDA(snd_hda_override_pin_caps);

/* read or sync the hash value with the current value;
 * call within hash_mutex
 */
static struct hda_amp_info *
update_amp_hash(struct hda_codec *codec, hda_nid_t nid, int ch,
		int direction, int index, bool init_only)
{
	struct hda_amp_info *info;
	unsigned int parm, val = 0;
	bool val_read = false;

 retry:
	info = get_alloc_amp_hash(codec, HDA_HASH_KEY(nid, direction, index));
	if (!info)
		return NULL;
	if (!(info->head.val & INFO_AMP_VOL(ch))) {
		if (!val_read) {
			mutex_unlock(&codec->hash_mutex);
			parm = ch ? AC_AMP_GET_RIGHT : AC_AMP_GET_LEFT;
			parm |= direction == HDA_OUTPUT ?
				AC_AMP_GET_OUTPUT : AC_AMP_GET_INPUT;
			parm |= index;
			val = snd_hda_codec_read(codec, nid, 0,
				 AC_VERB_GET_AMP_GAIN_MUTE, parm);
			val &= 0xff;
			val_read = true;
			mutex_lock(&codec->hash_mutex);
			goto retry;
		}
		info->vol[ch] = val;
		info->head.val |= INFO_AMP_VOL(ch);
	} else if (init_only)
		return NULL;
	return info;
}

/*
 * write the current volume in info to the h/w
 */
static void put_vol_mute(struct hda_codec *codec, unsigned int amp_caps,
			 hda_nid_t nid, int ch, int direction, int index,
			 int val)
{
	u32 parm;

	parm = ch ? AC_AMP_SET_RIGHT : AC_AMP_SET_LEFT;
	parm |= direction == HDA_OUTPUT ? AC_AMP_SET_OUTPUT : AC_AMP_SET_INPUT;
	parm |= index << AC_AMP_SET_INDEX_SHIFT;
	if ((val & HDA_AMP_MUTE) && !(amp_caps & AC_AMPCAP_MUTE) &&
	    (amp_caps & AC_AMPCAP_MIN_MUTE))
		; /* set the zero value as a fake mute */
	else
		parm |= val;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, parm);
}

/**
 * snd_hda_codec_amp_read - Read AMP value
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @ch: channel (left=0 or right=1)
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @index: the index value (only for input direction)
 *
 * Read AMP value.  The volume is between 0 to 0x7f, 0x80 = mute bit.
 */
int snd_hda_codec_amp_read(struct hda_codec *codec, hda_nid_t nid, int ch,
			   int direction, int index)
{
	struct hda_amp_info *info;
	unsigned int val = 0;

	mutex_lock(&codec->hash_mutex);
	info = update_amp_hash(codec, nid, ch, direction, index, false);
	if (info)
		val = info->vol[ch];
	mutex_unlock(&codec->hash_mutex);
	return val;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_amp_read);

static int codec_amp_update(struct hda_codec *codec, hda_nid_t nid, int ch,
			    int direction, int idx, int mask, int val,
			    bool init_only)
{
	struct hda_amp_info *info;
	unsigned int caps;
	unsigned int cache_only;

	if (snd_BUG_ON(mask & ~0xff))
		mask &= 0xff;
	val &= mask;

	mutex_lock(&codec->hash_mutex);
	info = update_amp_hash(codec, nid, ch, direction, idx, init_only);
	if (!info) {
		mutex_unlock(&codec->hash_mutex);
		return 0;
	}
	val |= info->vol[ch] & ~mask;
	if (info->vol[ch] == val) {
		mutex_unlock(&codec->hash_mutex);
		return 0;
	}
	info->vol[ch] = val;
	cache_only = info->head.dirty = codec->cached_write;
	caps = info->amp_caps;
	mutex_unlock(&codec->hash_mutex);
	if (!cache_only)
		put_vol_mute(codec, caps, nid, ch, direction, idx, val);
	return 1;
}

/**
 * snd_hda_codec_amp_update - update the AMP value
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @ch: channel (left=0 or right=1)
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Update the AMP value with a bit mask.
 * Returns 0 if the value is unchanged, 1 if changed.
 */
int snd_hda_codec_amp_update(struct hda_codec *codec, hda_nid_t nid, int ch,
			     int direction, int idx, int mask, int val)
{
	return codec_amp_update(codec, nid, ch, direction, idx, mask, val, false);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_amp_update);

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
EXPORT_SYMBOL_HDA(snd_hda_codec_amp_stereo);

/* Works like snd_hda_codec_amp_update() but it writes the value only at
 * the first access.  If the amp was already initialized / updated beforehand,
 * this does nothing.
 */
int snd_hda_codec_amp_init(struct hda_codec *codec, hda_nid_t nid, int ch,
			   int dir, int idx, int mask, int val)
{
	return codec_amp_update(codec, nid, ch, dir, idx, mask, val, true);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_amp_init);

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
EXPORT_SYMBOL_HDA(snd_hda_codec_amp_init_stereo);

/**
 * snd_hda_codec_resume_amp - Resume all AMP commands from the cache
 * @codec: HD-audio codec
 *
 * Resume the all amp commands from the cache.
 */
void snd_hda_codec_resume_amp(struct hda_codec *codec)
{
	int i;

	mutex_lock(&codec->hash_mutex);
	codec->cached_write = 0;
	for (i = 0; i < codec->amp_cache.buf.used; i++) {
		struct hda_amp_info *buffer;
		u32 key;
		hda_nid_t nid;
		unsigned int idx, dir, ch;
		struct hda_amp_info info;

		buffer = snd_array_elem(&codec->amp_cache.buf, i);
		if (!buffer->head.dirty)
			continue;
		buffer->head.dirty = 0;
		info = *buffer;
		key = info.head.key;
		if (!key)
			continue;
		nid = key & 0xff;
		idx = (key >> 16) & 0xff;
		dir = (key >> 24) & 0xff;
		for (ch = 0; ch < 2; ch++) {
			if (!(info.head.val & INFO_AMP_VOL(ch)))
				continue;
			mutex_unlock(&codec->hash_mutex);
			put_vol_mute(codec, info.amp_caps, nid, ch, dir, idx,
				     info.vol[ch]);
			mutex_lock(&codec->hash_mutex);
		}
	}
	mutex_unlock(&codec->hash_mutex);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_resume_amp);

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
		printk(KERN_WARNING "hda_codec: "
		       "num_steps = 0 for NID=0x%x (ctl = %s)\n", nid,
		       kcontrol->id.name);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_amp_volume_info);


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
EXPORT_SYMBOL_HDA(snd_hda_mixer_amp_volume_get);

/**
 * snd_hda_mixer_amp_volume_put - Put callback for a standard AMP mixer volume
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

	snd_hda_power_up(codec);
	if (chs & 1) {
		change = update_amp_value(codec, nid, 0, dir, idx, ofs, *valp);
		valp++;
	}
	if (chs & 2)
		change |= update_amp_value(codec, nid, 1, dir, idx, ofs, *valp);
	snd_hda_power_down(codec);
	return change;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_amp_volume_put);

/**
 * snd_hda_mixer_amp_volume_put - TLV callback for a standard AMP mixer volume
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			  unsigned int size, unsigned int __user *_tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int dir = get_amp_direction(kcontrol);
	unsigned int ofs = get_amp_offset(kcontrol);
	bool min_mute = get_amp_min_mute(kcontrol);
	u32 caps, val1, val2;

	if (size < 4 * sizeof(unsigned int))
		return -ENOMEM;
	caps = query_amp_caps(codec, nid, dir);
	val2 = (caps & AC_AMPCAP_STEP_SIZE) >> AC_AMPCAP_STEP_SIZE_SHIFT;
	val2 = (val2 + 1) * 25;
	val1 = -((caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT);
	val1 += ofs;
	val1 = ((int)val1) * ((int)val2);
	if (min_mute || (caps & AC_AMPCAP_MIN_MUTE))
		val2 |= TLV_DB_SCALE_MUTE;
	if (put_user(SNDRV_CTL_TLVT_DB_SCALE, _tlv))
		return -EFAULT;
	if (put_user(2 * sizeof(unsigned int), _tlv + 1))
		return -EFAULT;
	if (put_user(val1, _tlv + 2))
		return -EFAULT;
	if (put_user(val2, _tlv + 3))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_amp_tlv);

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
	tlv[0] = SNDRV_CTL_TLVT_DB_SCALE;
	tlv[1] = 2 * sizeof(unsigned int);
	tlv[2] = -nums * step;
	tlv[3] = step;
}
EXPORT_SYMBOL_HDA(snd_hda_set_vmaster_tlv);

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
	return snd_ctl_find_id(codec->bus->card, &id);
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
EXPORT_SYMBOL_HDA(snd_hda_find_mixer_ctl);

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
	err = snd_ctl_add(codec->bus->card, kctl);
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
EXPORT_SYMBOL_HDA(snd_hda_ctl_add);

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
	printk(KERN_ERR "hda-codec: no NID for mapping control %s:%d:%d\n",
	       kctl->id.name, kctl->id.index, index);
	return -EINVAL;
}
EXPORT_SYMBOL_HDA(snd_hda_add_nid);

/**
 * snd_hda_ctls_clear - Clear all controls assigned to the given codec
 * @codec: HD-audio codec
 */
void snd_hda_ctls_clear(struct hda_codec *codec)
{
	int i;
	struct hda_nid_item *items = codec->mixers.list;
	for (i = 0; i < codec->mixers.used; i++)
		snd_ctl_remove(codec->bus->card, items[i].kctl);
	snd_array_free(&codec->mixers);
	snd_array_free(&codec->nids);
}

/* pseudo device locking
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

	list_for_each_entry(codec, &bus->codec_list, list) {
		int pcm;
		for (pcm = 0; pcm < codec->num_pcms; pcm++) {
			struct hda_pcm *cpcm = &codec->pcm_info[pcm];
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
EXPORT_SYMBOL_HDA(snd_hda_lock_devices);

void snd_hda_unlock_devices(struct hda_bus *bus)
{
	struct snd_card *card = bus->card;

	card = bus->card;
	spin_lock(&card->files_lock);
	card->shutdown = 0;
	spin_unlock(&card->files_lock);
}
EXPORT_SYMBOL_HDA(snd_hda_unlock_devices);

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
	struct snd_card *card = bus->card;
	int i;

	if (snd_hda_lock_devices(bus) < 0)
		return -EBUSY;

	/* OK, let it free */
	cancel_delayed_work_sync(&codec->jackpoll_work);
#ifdef CONFIG_PM
	cancel_delayed_work_sync(&codec->power_work);
	flush_workqueue(bus->workq);
#endif
	snd_hda_ctls_clear(codec);
	/* relase PCMs */
	for (i = 0; i < codec->num_pcms; i++) {
		if (codec->pcm_info[i].pcm) {
			snd_device_free(card, codec->pcm_info[i].pcm);
			clear_bit(codec->pcm_info[i].device,
				  bus->pcm_dev_bits);
		}
	}
	if (codec->patch_ops.free)
		codec->patch_ops.free(codec);
	memset(&codec->patch_ops, 0, sizeof(codec->patch_ops));
	snd_hda_jack_tbl_clear(codec);
	codec->proc_widget_hook = NULL;
	codec->spec = NULL;
	free_hda_cache(&codec->amp_cache);
	free_hda_cache(&codec->cmd_cache);
	init_hda_cache(&codec->amp_cache, sizeof(struct hda_amp_info));
	init_hda_cache(&codec->cmd_cache, sizeof(struct hda_cache_head));
	/* free only driver_pins so that init_pins + user_pins are restored */
	snd_array_free(&codec->driver_pins);
	snd_array_free(&codec->cvt_setups);
	snd_array_free(&codec->spdif_out);
	snd_array_free(&codec->verbs);
	codec->num_pcms = 0;
	codec->pcm_info = NULL;
	codec->preset = NULL;
	codec->slave_dig_outs = NULL;
	codec->spdif_status_reset = 0;
	module_put(codec->owner);
	codec->owner = NULL;

	/* allow device access again */
	snd_hda_unlock_devices(bus);
	return 0;
}

typedef int (*map_slave_func_t)(void *, struct snd_kcontrol *);

/* apply the function to all matching slave ctls in the mixer list */
static int map_slaves(struct hda_codec *codec, const char * const *slaves,
		      const char *suffix, map_slave_func_t func, void *data) 
{
	struct hda_nid_item *items;
	const char * const *s;
	int i, err;

	items = codec->mixers.list;
	for (i = 0; i < codec->mixers.used; i++) {
		struct snd_kcontrol *sctl = items[i].kctl;
		if (!sctl || !sctl->id.name ||
		    sctl->id.iface != SNDRV_CTL_ELEM_IFACE_MIXER)
			continue;
		for (s = slaves; *s; s++) {
			char tmpname[sizeof(sctl->id.name)];
			const char *name = *s;
			if (suffix) {
				snprintf(tmpname, sizeof(tmpname), "%s %s",
					 name, suffix);
				name = tmpname;
			}
			if (!strcmp(sctl->id.name, name)) {
				err = func(data, sctl);
				if (err)
					return err;
				break;
			}
		}
	}
	return 0;
}

static int check_slave_present(void *data, struct snd_kcontrol *sctl)
{
	return 1;
}

/* guess the value corresponding to 0dB */
static int get_kctl_0dB_offset(struct snd_kcontrol *kctl)
{
	int _tlv[4];
	const int *tlv = NULL;
	int val = -1;

	if (kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
		/* FIXME: set_fs() hack for obtaining user-space TLV data */
		mm_segment_t fs = get_fs();
		set_fs(get_ds());
		if (!kctl->tlv.c(kctl, 0, sizeof(_tlv), _tlv))
			tlv = _tlv;
		set_fs(fs);
	} else if (kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_READ)
		tlv = kctl->tlv.p;
	if (tlv && tlv[0] == SNDRV_CTL_TLVT_DB_SCALE)
		val = -tlv[2] / tlv[3];
	return val;
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

/* initialize the slave volume with 0dB */
static int init_slave_0dB(void *data, struct snd_kcontrol *slave)
{
	int offset = get_kctl_0dB_offset(slave);
	if (offset > 0)
		put_kctl_with_value(slave, offset);
	return 0;
}

/* unmute the slave */
static int init_slave_unmute(void *data, struct snd_kcontrol *slave)
{
	return put_kctl_with_value(slave, 1);
}

/**
 * snd_hda_add_vmaster - create a virtual master control and add slaves
 * @codec: HD-audio codec
 * @name: vmaster control name
 * @tlv: TLV data (optional)
 * @slaves: slave control names (optional)
 * @suffix: suffix string to each slave name (optional)
 * @init_slave_vol: initialize slaves to unmute/0dB
 * @ctl_ret: store the vmaster kcontrol in return
 *
 * Create a virtual master control with the given name.  The TLV data
 * must be either NULL or a valid data.
 *
 * @slaves is a NULL-terminated array of strings, each of which is a
 * slave control name.  All controls with these names are assigned to
 * the new virtual master control.
 *
 * This function returns zero if successful or a negative error code.
 */
int __snd_hda_add_vmaster(struct hda_codec *codec, char *name,
			unsigned int *tlv, const char * const *slaves,
			  const char *suffix, bool init_slave_vol,
			  struct snd_kcontrol **ctl_ret)
{
	struct snd_kcontrol *kctl;
	int err;

	if (ctl_ret)
		*ctl_ret = NULL;

	err = map_slaves(codec, slaves, suffix, check_slave_present, NULL);
	if (err != 1) {
		snd_printdd("No slave found for %s\n", name);
		return 0;
	}
	kctl = snd_ctl_make_virtual_master(name, tlv);
	if (!kctl)
		return -ENOMEM;
	err = snd_hda_ctl_add(codec, 0, kctl);
	if (err < 0)
		return err;

	err = map_slaves(codec, slaves, suffix,
			 (map_slave_func_t)snd_ctl_add_slave, kctl);
	if (err < 0)
		return err;

	/* init with master mute & zero volume */
	put_kctl_with_value(kctl, 0);
	if (init_slave_vol)
		map_slaves(codec, slaves, suffix,
			   tlv ? init_slave_0dB : init_slave_unmute, kctl);

	if (ctl_ret)
		*ctl_ret = kctl;
	return 0;
}
EXPORT_SYMBOL_HDA(__snd_hda_add_vmaster);

/*
 * mute-LED control using vmaster
 */
static int vmaster_mute_mode_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"On", "Off", "Follow Master"
	};
	unsigned int index;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	index = uinfo->value.enumerated.item;
	if (index >= 3)
		index = 2;
	strcpy(uinfo->value.enumerated.name, texts[index]);
	return 0;
}

static int vmaster_mute_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_vmaster_mute_hook *hook = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = hook->mute_mode;
	return 0;
}

static int vmaster_mute_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_vmaster_mute_hook *hook = snd_kcontrol_chip(kcontrol);
	unsigned int old_mode = hook->mute_mode;

	hook->mute_mode = ucontrol->value.enumerated.item[0];
	if (hook->mute_mode > HDA_VMUTE_FOLLOW_MASTER)
		hook->mute_mode = HDA_VMUTE_FOLLOW_MASTER;
	if (old_mode == hook->mute_mode)
		return 0;
	snd_hda_sync_vmaster_hook(hook);
	return 1;
}

static struct snd_kcontrol_new vmaster_mute_mode = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Mute-LED Mode",
	.info = vmaster_mute_mode_info,
	.get = vmaster_mute_mode_get,
	.put = vmaster_mute_mode_put,
};

/*
 * Add a mute-LED hook with the given vmaster switch kctl
 * "Mute-LED Mode" control is automatically created and associated with
 * the given hook.
 */
int snd_hda_add_vmaster_hook(struct hda_codec *codec,
			     struct hda_vmaster_mute_hook *hook,
			     bool expose_enum_ctl)
{
	struct snd_kcontrol *kctl;

	if (!hook->hook || !hook->sw_kctl)
		return 0;
	snd_ctl_add_vmaster_hook(hook->sw_kctl, hook->hook, codec);
	hook->codec = codec;
	hook->mute_mode = HDA_VMUTE_FOLLOW_MASTER;
	if (!expose_enum_ctl)
		return 0;
	kctl = snd_ctl_new1(&vmaster_mute_mode, hook);
	if (!kctl)
		return -ENOMEM;
	return snd_hda_ctl_add(codec, 0, kctl);
}
EXPORT_SYMBOL_HDA(snd_hda_add_vmaster_hook);

/*
 * Call the hook with the current value for synchronization
 * Should be called in init callback
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
	switch (hook->mute_mode) {
	case HDA_VMUTE_FOLLOW_MASTER:
		snd_ctl_sync_vmaster_hook(hook->sw_kctl);
		break;
	default:
		hook->hook(hook->codec, hook->mute_mode);
		break;
	}
}
EXPORT_SYMBOL_HDA(snd_hda_sync_vmaster_hook);


/**
 * snd_hda_mixer_amp_switch_info - Info callback for a standard AMP mixer switch
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
EXPORT_SYMBOL_HDA(snd_hda_mixer_amp_switch_info);

/**
 * snd_hda_mixer_amp_switch_get - Get callback for a standard AMP mixer switch
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
EXPORT_SYMBOL_HDA(snd_hda_mixer_amp_switch_get);

/**
 * snd_hda_mixer_amp_switch_put - Put callback for a standard AMP mixer switch
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

	snd_hda_power_up(codec);
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
	snd_hda_power_down(codec);
	return change;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_amp_switch_put);

/*
 * bound volume controls
 *
 * bind multiple volumes (# indices, from 0)
 */

#define AMP_VAL_IDX_SHIFT	19
#define AMP_VAL_IDX_MASK	(0x0f<<19)

/**
 * snd_hda_mixer_bind_switch_get - Get callback for a bound volume control
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_MUTE*() macros.
 */
int snd_hda_mixer_bind_switch_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned long pval;
	int err;

	mutex_lock(&codec->control_mutex);
	pval = kcontrol->private_value;
	kcontrol->private_value = pval & ~AMP_VAL_IDX_MASK; /* index 0 */
	err = snd_hda_mixer_amp_switch_get(kcontrol, ucontrol);
	kcontrol->private_value = pval;
	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_bind_switch_get);

/**
 * snd_hda_mixer_bind_switch_put - Put callback for a bound volume control
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_MUTE*() macros.
 */
int snd_hda_mixer_bind_switch_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned long pval;
	int i, indices, err = 0, change = 0;

	mutex_lock(&codec->control_mutex);
	pval = kcontrol->private_value;
	indices = (pval & AMP_VAL_IDX_MASK) >> AMP_VAL_IDX_SHIFT;
	for (i = 0; i < indices; i++) {
		kcontrol->private_value = (pval & ~AMP_VAL_IDX_MASK) |
			(i << AMP_VAL_IDX_SHIFT);
		err = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
		if (err < 0)
			break;
		change |= err;
	}
	kcontrol->private_value = pval;
	mutex_unlock(&codec->control_mutex);
	return err < 0 ? err : change;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_bind_switch_put);

/**
 * snd_hda_mixer_bind_ctls_info - Info callback for a generic bound control
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() or HDA_BIND_SW() macros.
 */
int snd_hda_mixer_bind_ctls_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	int err;

	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	kcontrol->private_value = *c->values;
	err = c->ops->info(kcontrol, uinfo);
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_bind_ctls_info);

/**
 * snd_hda_mixer_bind_ctls_get - Get callback for a generic bound control
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() or HDA_BIND_SW() macros.
 */
int snd_hda_mixer_bind_ctls_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	int err;

	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	kcontrol->private_value = *c->values;
	err = c->ops->get(kcontrol, ucontrol);
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_bind_ctls_get);

/**
 * snd_hda_mixer_bind_ctls_put - Put callback for a generic bound control
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() or HDA_BIND_SW() macros.
 */
int snd_hda_mixer_bind_ctls_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	unsigned long *vals;
	int err = 0, change = 0;

	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	for (vals = c->values; *vals; vals++) {
		kcontrol->private_value = *vals;
		err = c->ops->put(kcontrol, ucontrol);
		if (err < 0)
			break;
		change |= err;
	}
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->control_mutex);
	return err < 0 ? err : change;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_bind_ctls_put);

/**
 * snd_hda_mixer_bind_tlv - TLV callback for a generic bound control
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() macro.
 */
int snd_hda_mixer_bind_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			   unsigned int size, unsigned int __user *tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	int err;

	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	kcontrol->private_value = *c->values;
	err = c->ops->tlv(kcontrol, op_flag, size, tlv);
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_HDA(snd_hda_mixer_bind_tlv);

struct hda_ctl_ops snd_hda_bind_vol = {
	.info = snd_hda_mixer_amp_volume_info,
	.get = snd_hda_mixer_amp_volume_get,
	.put = snd_hda_mixer_amp_volume_put,
	.tlv = snd_hda_mixer_amp_tlv
};
EXPORT_SYMBOL_HDA(snd_hda_bind_vol);

struct hda_ctl_ops snd_hda_bind_sw = {
	.info = snd_hda_mixer_amp_switch_info,
	.get = snd_hda_mixer_amp_switch_get,
	.put = snd_hda_mixer_amp_switch_put,
	.tlv = snd_hda_mixer_amp_tlv
};
EXPORT_SYMBOL_HDA(snd_hda_bind_sw);

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

/* set digital convert verbs both for the given NID and its slaves */
static void set_dig_out(struct hda_codec *codec, hda_nid_t nid,
			int verb, int val)
{
	const hda_nid_t *d;

	snd_hda_codec_write_cache(codec, nid, 0, verb, val);
	d = codec->slave_dig_outs;
	if (!d)
		return;
	for (; *d; d++)
		snd_hda_codec_write_cache(codec, *d, 0, verb, val);
}

static inline void set_dig_out_convert(struct hda_codec *codec, hda_nid_t nid,
				       int dig1, int dig2)
{
	if (dig1 != -1)
		set_dig_out(codec, nid, AC_VERB_SET_DIGI_CONVERT_1, dig1);
	if (dig2 != -1)
		set_dig_out(codec, nid, AC_VERB_SET_DIGI_CONVERT_2, dig2);
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

static struct snd_kcontrol_new dig_mixes[] = {
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
	struct snd_kcontrol_new *dig_mix;
	int idx = 0;
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
		printk(KERN_ERR "hda_codec: too many IEC958 outputs\n");
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
	spdif->ctls = snd_hda_codec_read(codec, cvt_nid, 0,
					 AC_VERB_GET_DIGI_CONVERT_1, 0);
	spdif->status = convert_to_spdif_status(spdif->ctls);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_create_dig_out_ctls);

/* get the hda_spdif_out entry from the given NID
 * call within spdif_mutex lock
 */
struct hda_spdif_out *snd_hda_spdif_out_of_nid(struct hda_codec *codec,
					       hda_nid_t nid)
{
	int i;
	for (i = 0; i < codec->spdif_out.used; i++) {
		struct hda_spdif_out *spdif =
				snd_array_elem(&codec->spdif_out, i);
		if (spdif->nid == nid)
			return spdif;
	}
	return NULL;
}
EXPORT_SYMBOL_HDA(snd_hda_spdif_out_of_nid);

void snd_hda_spdif_ctls_unassign(struct hda_codec *codec, int idx)
{
	struct hda_spdif_out *spdif;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	spdif->nid = (u16)-1;
	mutex_unlock(&codec->spdif_mutex);
}
EXPORT_SYMBOL_HDA(snd_hda_spdif_ctls_unassign);

void snd_hda_spdif_ctls_assign(struct hda_codec *codec, int idx, hda_nid_t nid)
{
	struct hda_spdif_out *spdif;
	unsigned short val;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	if (spdif->nid != nid) {
		spdif->nid = nid;
		val = spdif->ctls;
		set_spdif_ctls(codec, nid, val & 0xff, (val >> 8) & 0xff);
	}
	mutex_unlock(&codec->spdif_mutex);
}
EXPORT_SYMBOL_HDA(snd_hda_spdif_ctls_assign);

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

static struct snd_kcontrol_new spdif_share_sw = {
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
EXPORT_SYMBOL_HDA(snd_hda_create_spdif_share_sw);

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
		snd_hda_codec_write_cache(codec, nid, 0,
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
	unsigned short val;
	unsigned int sbits;

	val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_DIGI_CONVERT_1, 0);
	sbits = convert_to_spdif_status(val);
	ucontrol->value.iec958.status[0] = sbits;
	ucontrol->value.iec958.status[1] = sbits >> 8;
	ucontrol->value.iec958.status[2] = sbits >> 16;
	ucontrol->value.iec958.status[3] = sbits >> 24;
	return 0;
}

static struct snd_kcontrol_new dig_in_ctls[] = {
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
	struct snd_kcontrol_new *dig_mix;
	int idx;

	idx = find_empty_mixer_ctl_idx(codec, "IEC958 Capture Switch", 0);
	if (idx < 0) {
		printk(KERN_ERR "hda_codec: too many IEC958 inputs\n");
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
EXPORT_SYMBOL_HDA(snd_hda_create_spdif_in_ctls);

/*
 * command cache
 */

/* build a 31bit cache key with the widget id and the command parameter */
#define build_cmd_cache_key(nid, verb)	((verb << 8) | nid)
#define get_cmd_cache_nid(key)		((key) & 0xff)
#define get_cmd_cache_cmd(key)		(((key) >> 8) & 0xffff)

/**
 * snd_hda_codec_write_cache - send a single command with caching
 * @codec: the HDA codec
 * @nid: NID to send the command
 * @direct: direct flag
 * @verb: the verb to send
 * @parm: the parameter for the verb
 *
 * Send a single command without waiting for response.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_codec_write_cache(struct hda_codec *codec, hda_nid_t nid,
			      int direct, unsigned int verb, unsigned int parm)
{
	int err;
	struct hda_cache_head *c;
	u32 key;
	unsigned int cache_only;

	cache_only = codec->cached_write;
	if (!cache_only) {
		err = snd_hda_codec_write(codec, nid, direct, verb, parm);
		if (err < 0)
			return err;
	}

	/* parm may contain the verb stuff for get/set amp */
	verb = verb | (parm >> 8);
	parm &= 0xff;
	key = build_cmd_cache_key(nid, verb);
	mutex_lock(&codec->bus->cmd_mutex);
	c = get_alloc_hash(&codec->cmd_cache, key);
	if (c) {
		c->val = parm;
		c->dirty = cache_only;
	}
	mutex_unlock(&codec->bus->cmd_mutex);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_write_cache);

/**
 * snd_hda_codec_update_cache - check cache and write the cmd only when needed
 * @codec: the HDA codec
 * @nid: NID to send the command
 * @direct: direct flag
 * @verb: the verb to send
 * @parm: the parameter for the verb
 *
 * This function works like snd_hda_codec_write_cache(), but it doesn't send
 * command if the parameter is already identical with the cached value.
 * If not, it sends the command and refreshes the cache.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_codec_update_cache(struct hda_codec *codec, hda_nid_t nid,
			       int direct, unsigned int verb, unsigned int parm)
{
	struct hda_cache_head *c;
	u32 key;

	/* parm may contain the verb stuff for get/set amp */
	verb = verb | (parm >> 8);
	parm &= 0xff;
	key = build_cmd_cache_key(nid, verb);
	mutex_lock(&codec->bus->cmd_mutex);
	c = get_hash(&codec->cmd_cache, key);
	if (c && c->val == parm) {
		mutex_unlock(&codec->bus->cmd_mutex);
		return 0;
	}
	mutex_unlock(&codec->bus->cmd_mutex);
	return snd_hda_codec_write_cache(codec, nid, direct, verb, parm);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_update_cache);

/**
 * snd_hda_codec_resume_cache - Resume the all commands from the cache
 * @codec: HD-audio codec
 *
 * Execute all verbs recorded in the command caches to resume.
 */
void snd_hda_codec_resume_cache(struct hda_codec *codec)
{
	int i;

	mutex_lock(&codec->hash_mutex);
	codec->cached_write = 0;
	for (i = 0; i < codec->cmd_cache.buf.used; i++) {
		struct hda_cache_head *buffer;
		u32 key;

		buffer = snd_array_elem(&codec->cmd_cache.buf, i);
		key = buffer->key;
		if (!key)
			continue;
		if (!buffer->dirty)
			continue;
		buffer->dirty = 0;
		mutex_unlock(&codec->hash_mutex);
		snd_hda_codec_write(codec, get_cmd_cache_nid(key), 0,
				    get_cmd_cache_cmd(key), buffer->val);
		mutex_lock(&codec->hash_mutex);
	}
	mutex_unlock(&codec->hash_mutex);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_resume_cache);

/**
 * snd_hda_sequence_write_cache - sequence writes with caching
 * @codec: the HDA codec
 * @seq: VERB array to send
 *
 * Send the commands sequentially from the given array.
 * Thte commands are recorded on cache for power-save and resume.
 * The array must be terminated with NID=0.
 */
void snd_hda_sequence_write_cache(struct hda_codec *codec,
				  const struct hda_verb *seq)
{
	for (; seq->nid; seq++)
		snd_hda_codec_write_cache(codec, seq->nid, 0, seq->verb,
					  seq->param);
}
EXPORT_SYMBOL_HDA(snd_hda_sequence_write_cache);

/**
 * snd_hda_codec_flush_cache - Execute all pending (cached) amps / verbs
 * @codec: HD-audio codec
 */
void snd_hda_codec_flush_cache(struct hda_codec *codec)
{
	snd_hda_codec_resume_amp(codec);
	snd_hda_codec_resume_cache(codec);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_flush_cache);

void snd_hda_codec_set_power_to_all(struct hda_codec *codec, hda_nid_t fg,
				    unsigned int power_state)
{
	hda_nid_t nid = codec->start_nid;
	int i;

	for (i = 0; i < codec->num_nodes; i++, nid++) {
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
EXPORT_SYMBOL_HDA(snd_hda_codec_set_power_to_all);

/*
 *  supported power states check
 */
static bool snd_hda_codec_get_supported_ps(struct hda_codec *codec, hda_nid_t fg,
				unsigned int power_state)
{
	int sup = snd_hda_param_read(codec, fg, AC_PAR_POWER_STATE);

	if (sup == -1)
		return false;
	if (sup & power_state)
		return true;
	else
		return false;
}

/*
 * wait until the state is reached, returns the current state
 */
static unsigned int hda_sync_power_state(struct hda_codec *codec,
					 hda_nid_t fg,
					 unsigned int power_state)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(500);
	unsigned int state, actual_state;

	for (;;) {
		state = snd_hda_codec_read(codec, fg, 0,
					   AC_VERB_GET_POWER_STATE, 0);
		if (state & AC_PWRST_ERROR)
			break;
		actual_state = (state >> 4) & 0x0f;
		if (actual_state == power_state)
			break;
		if (time_after_eq(jiffies, end_time))
			break;
		/* wait until the codec reachs to the target state */
		msleep(1);
	}
	return state;
}

/* don't power down the widget if it controls eapd and EAPD_BTLENABLE is set */
unsigned int snd_hda_codec_eapd_power_filter(struct hda_codec *codec,
					     hda_nid_t nid,
					     unsigned int power_state)
{
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
EXPORT_SYMBOL_HDA(snd_hda_codec_eapd_power_filter);

/*
 * set power state of the codec, and return the power state
 */
static unsigned int hda_set_power_state(struct hda_codec *codec,
					unsigned int power_state)
{
	hda_nid_t fg = codec->afg ? codec->afg : codec->mfg;
	int count;
	unsigned int state;

	/* this delay seems necessary to avoid click noise at power-down */
	if (power_state == AC_PWRST_D3) {
		/* transition time less than 10ms for power down */
		msleep(codec->epss ? 10 : 100);
	}

	/* repeat power states setting at most 10 times*/
	for (count = 0; count < 10; count++) {
		if (codec->patch_ops.set_power_state)
			codec->patch_ops.set_power_state(codec, fg,
							 power_state);
		else {
			snd_hda_codec_read(codec, fg, 0,
					   AC_VERB_SET_POWER_STATE,
					   power_state);
			snd_hda_codec_set_power_to_all(codec, fg, power_state);
		}
		state = hda_sync_power_state(codec, fg, power_state);
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
	hda_nid_t nid = codec->start_nid;
	int i;

	/* don't care if no filter is used */
	if (!codec->power_filter)
		return;

	for (i = 0; i < codec->num_nodes; i++, nid++) {
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

#ifdef CONFIG_SND_HDA_HWDEP
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
/*
 * call suspend and power-down; used both from PM and power-save
 * this function returns the power state in the end
 */
static unsigned int hda_call_codec_suspend(struct hda_codec *codec, bool in_wq)
{
	unsigned int state;

	codec->in_pm = 1;

	if (codec->patch_ops.suspend)
		codec->patch_ops.suspend(codec);
	hda_cleanup_all_streams(codec);
	state = hda_set_power_state(codec, AC_PWRST_D3);
	/* Cancel delayed work if we aren't currently running from it. */
	if (!in_wq)
		cancel_delayed_work_sync(&codec->power_work);
	spin_lock(&codec->power_lock);
	snd_hda_update_power_acct(codec);
	trace_hda_power_down(codec);
	codec->power_on = 0;
	codec->power_transition = 0;
	codec->power_jiffies = jiffies;
	spin_unlock(&codec->power_lock);
	codec->in_pm = 0;
	return state;
}

/* mark all entries of cmd and amp caches dirty */
static void hda_mark_cmd_cache_dirty(struct hda_codec *codec)
{
	int i;
	for (i = 0; i < codec->cmd_cache.buf.used; i++) {
		struct hda_cache_head *cmd;
		cmd = snd_array_elem(&codec->cmd_cache.buf, i);
		cmd->dirty = 1;
	}
	for (i = 0; i < codec->amp_cache.buf.used; i++) {
		struct hda_amp_info *amp;
		amp = snd_array_elem(&codec->amp_cache.buf, i);
		amp->head.dirty = 1;
	}
}

/*
 * kick up codec; used both from PM and power-save
 */
static void hda_call_codec_resume(struct hda_codec *codec)
{
	codec->in_pm = 1;

	hda_mark_cmd_cache_dirty(codec);

	/* set as if powered on for avoiding re-entering the resume
	 * in the resume / power-save sequence
	 */
	hda_keep_power_on(codec);
	if (codec->pm_down_notified) {
		codec->pm_down_notified = 0;
		hda_call_pm_notify(codec->bus, true);
	}
	hda_set_power_state(codec, AC_PWRST_D0);
	restore_shutup_pins(codec);
	hda_exec_init_verbs(codec);
	snd_hda_jack_set_dirty_all(codec);
	if (codec->patch_ops.resume)
		codec->patch_ops.resume(codec);
	else {
		if (codec->patch_ops.init)
			codec->patch_ops.init(codec);
		snd_hda_codec_resume_amp(codec);
		snd_hda_codec_resume_cache(codec);
	}

	if (codec->jackpoll_interval)
		hda_jackpoll_work(&codec->jackpoll_work.work);
	else
		snd_hda_jack_report_sync(codec);

	codec->in_pm = 0;
	snd_hda_power_down(codec); /* flag down before returning */
}
#endif /* CONFIG_PM */


/**
 * snd_hda_build_controls - build mixer controls
 * @bus: the BUS
 *
 * Creates mixer controls for each codec included in the bus.
 *
 * Returns 0 if successful, otherwise a negative error code.
 */
int snd_hda_build_controls(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		int err = snd_hda_codec_build_controls(codec);
		if (err < 0) {
			printk(KERN_ERR "hda_codec: cannot build controls "
			       "for #%d (error %d)\n", codec->addr, err);
			err = snd_hda_codec_reset(codec);
			if (err < 0) {
				printk(KERN_ERR
				       "hda_codec: cannot revert codec\n");
				return err;
			}
		}
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_build_controls);

/*
 * add standard channel maps if not specified
 */
static int add_std_chmaps(struct hda_codec *codec)
{
	int i, str, err;

	for (i = 0; i < codec->num_pcms; i++) {
		for (str = 0; str < 2; str++) {
			struct snd_pcm *pcm = codec->pcm_info[i].pcm;
			struct hda_pcm_stream *hinfo =
				&codec->pcm_info[i].stream[str];
			struct snd_pcm_chmap *chmap;
			const struct snd_pcm_chmap_elem *elem;

			if (codec->pcm_info[i].own_chmap)
				continue;
			if (!pcm || !hinfo->substreams)
				continue;
			elem = hinfo->chmap ? hinfo->chmap : snd_pcm_std_chmaps;
			err = snd_pcm_add_chmap_ctls(pcm, str, elem,
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

/*
 * stream formats
 */
struct hda_rate_tbl {
	unsigned int hz;
	unsigned int alsa_bits;
	unsigned int hda_fmt;
};

/* rate = base * mult / div */
#define HDA_RATE(base, mult, div) \
	(AC_FMT_BASE_##base##K | (((mult) - 1) << AC_FMT_MULT_SHIFT) | \
	 (((div) - 1) << AC_FMT_DIV_SHIFT))

static struct hda_rate_tbl rate_bits[] = {
	/* rate in Hz, ALSA rate bitmask, HDA format value */

	/* autodetected value used in snd_hda_query_supported_pcm */
	{ 8000, SNDRV_PCM_RATE_8000, HDA_RATE(48, 1, 6) },
	{ 11025, SNDRV_PCM_RATE_11025, HDA_RATE(44, 1, 4) },
	{ 16000, SNDRV_PCM_RATE_16000, HDA_RATE(48, 1, 3) },
	{ 22050, SNDRV_PCM_RATE_22050, HDA_RATE(44, 1, 2) },
	{ 32000, SNDRV_PCM_RATE_32000, HDA_RATE(48, 2, 3) },
	{ 44100, SNDRV_PCM_RATE_44100, HDA_RATE(44, 1, 1) },
	{ 48000, SNDRV_PCM_RATE_48000, HDA_RATE(48, 1, 1) },
	{ 88200, SNDRV_PCM_RATE_88200, HDA_RATE(44, 2, 1) },
	{ 96000, SNDRV_PCM_RATE_96000, HDA_RATE(48, 2, 1) },
	{ 176400, SNDRV_PCM_RATE_176400, HDA_RATE(44, 4, 1) },
	{ 192000, SNDRV_PCM_RATE_192000, HDA_RATE(48, 4, 1) },
#define AC_PAR_PCM_RATE_BITS	11
	/* up to bits 10, 384kHZ isn't supported properly */

	/* not autodetected value */
	{ 9600, SNDRV_PCM_RATE_KNOT, HDA_RATE(48, 1, 5) },

	{ 0 } /* terminator */
};

/**
 * snd_hda_calc_stream_format - calculate format bitset
 * @rate: the sample rate
 * @channels: the number of channels
 * @format: the PCM format (SNDRV_PCM_FORMAT_XXX)
 * @maxbps: the max. bps
 *
 * Calculate the format bitset from the given rate, channels and th PCM format.
 *
 * Return zero if invalid.
 */
unsigned int snd_hda_calc_stream_format(unsigned int rate,
					unsigned int channels,
					unsigned int format,
					unsigned int maxbps,
					unsigned short spdif_ctls)
{
	int i;
	unsigned int val = 0;

	for (i = 0; rate_bits[i].hz; i++)
		if (rate_bits[i].hz == rate) {
			val = rate_bits[i].hda_fmt;
			break;
		}
	if (!rate_bits[i].hz) {
		snd_printdd("invalid rate %d\n", rate);
		return 0;
	}

	if (channels == 0 || channels > 8) {
		snd_printdd("invalid channels %d\n", channels);
		return 0;
	}
	val |= channels - 1;

	switch (snd_pcm_format_width(format)) {
	case 8:
		val |= AC_FMT_BITS_8;
		break;
	case 16:
		val |= AC_FMT_BITS_16;
		break;
	case 20:
	case 24:
	case 32:
		if (maxbps >= 32 || format == SNDRV_PCM_FORMAT_FLOAT_LE)
			val |= AC_FMT_BITS_32;
		else if (maxbps >= 24)
			val |= AC_FMT_BITS_24;
		else
			val |= AC_FMT_BITS_20;
		break;
	default:
		snd_printdd("invalid format width %d\n",
			    snd_pcm_format_width(format));
		return 0;
	}

	if (spdif_ctls & AC_DIG1_NONAUDIO)
		val |= AC_FMT_TYPE_NON_PCM;

	return val;
}
EXPORT_SYMBOL_HDA(snd_hda_calc_stream_format);

static unsigned int get_pcm_param(struct hda_codec *codec, hda_nid_t nid,
				  int dir)
{
	unsigned int val = 0;
	if (nid != codec->afg &&
	    (get_wcaps(codec, nid) & AC_WCAP_FORMAT_OVRD))
		val = snd_hda_param_read(codec, nid, AC_PAR_PCM);
	if (!val || val == -1)
		val = snd_hda_param_read(codec, codec->afg, AC_PAR_PCM);
	if (!val || val == -1)
		return 0;
	return val;
}

static unsigned int query_pcm_param(struct hda_codec *codec, hda_nid_t nid)
{
	return query_caps_hash(codec, nid, 0, HDA_HASH_PARPCM_KEY(nid),
			       get_pcm_param);
}

static unsigned int get_stream_param(struct hda_codec *codec, hda_nid_t nid,
				     int dir)
{
	unsigned int streams = snd_hda_param_read(codec, nid, AC_PAR_STREAM);
	if (!streams || streams == -1)
		streams = snd_hda_param_read(codec, codec->afg, AC_PAR_STREAM);
	if (!streams || streams == -1)
		return 0;
	return streams;
}

static unsigned int query_stream_param(struct hda_codec *codec, hda_nid_t nid)
{
	return query_caps_hash(codec, nid, 0, HDA_HASH_PARSTR_KEY(nid),
			       get_stream_param);
}

/**
 * snd_hda_query_supported_pcm - query the supported PCM rates and formats
 * @codec: the HDA codec
 * @nid: NID to query
 * @ratesp: the pointer to store the detected rate bitflags
 * @formatsp: the pointer to store the detected formats
 * @bpsp: the pointer to store the detected format widths
 *
 * Queries the supported PCM rates and formats.  The NULL @ratesp, @formatsp
 * or @bsps argument is ignored.
 *
 * Returns 0 if successful, otherwise a negative error code.
 */
int snd_hda_query_supported_pcm(struct hda_codec *codec, hda_nid_t nid,
				u32 *ratesp, u64 *formatsp, unsigned int *bpsp)
{
	unsigned int i, val, wcaps;

	wcaps = get_wcaps(codec, nid);
	val = query_pcm_param(codec, nid);

	if (ratesp) {
		u32 rates = 0;
		for (i = 0; i < AC_PAR_PCM_RATE_BITS; i++) {
			if (val & (1 << i))
				rates |= rate_bits[i].alsa_bits;
		}
		if (rates == 0) {
			snd_printk(KERN_ERR "hda_codec: rates == 0 "
				   "(nid=0x%x, val=0x%x, ovrd=%i)\n",
					nid, val,
					(wcaps & AC_WCAP_FORMAT_OVRD) ? 1 : 0);
			return -EIO;
		}
		*ratesp = rates;
	}

	if (formatsp || bpsp) {
		u64 formats = 0;
		unsigned int streams, bps;

		streams = query_stream_param(codec, nid);
		if (!streams)
			return -EIO;

		bps = 0;
		if (streams & AC_SUPFMT_PCM) {
			if (val & AC_SUPPCM_BITS_8) {
				formats |= SNDRV_PCM_FMTBIT_U8;
				bps = 8;
			}
			if (val & AC_SUPPCM_BITS_16) {
				formats |= SNDRV_PCM_FMTBIT_S16_LE;
				bps = 16;
			}
			if (wcaps & AC_WCAP_DIGITAL) {
				if (val & AC_SUPPCM_BITS_32)
					formats |= SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE;
				if (val & (AC_SUPPCM_BITS_20|AC_SUPPCM_BITS_24))
					formats |= SNDRV_PCM_FMTBIT_S32_LE;
				if (val & AC_SUPPCM_BITS_24)
					bps = 24;
				else if (val & AC_SUPPCM_BITS_20)
					bps = 20;
			} else if (val & (AC_SUPPCM_BITS_20|AC_SUPPCM_BITS_24|
					  AC_SUPPCM_BITS_32)) {
				formats |= SNDRV_PCM_FMTBIT_S32_LE;
				if (val & AC_SUPPCM_BITS_32)
					bps = 32;
				else if (val & AC_SUPPCM_BITS_24)
					bps = 24;
				else if (val & AC_SUPPCM_BITS_20)
					bps = 20;
			}
		}
#if 0 /* FIXME: CS4206 doesn't work, which is the only codec supporting float */
		if (streams & AC_SUPFMT_FLOAT32) {
			formats |= SNDRV_PCM_FMTBIT_FLOAT_LE;
			if (!bps)
				bps = 32;
		}
#endif
		if (streams == AC_SUPFMT_AC3) {
			/* should be exclusive */
			/* temporary hack: we have still no proper support
			 * for the direct AC3 stream...
			 */
			formats |= SNDRV_PCM_FMTBIT_U8;
			bps = 8;
		}
		if (formats == 0) {
			snd_printk(KERN_ERR "hda_codec: formats == 0 "
				   "(nid=0x%x, val=0x%x, ovrd=%i, "
				   "streams=0x%x)\n",
					nid, val,
					(wcaps & AC_WCAP_FORMAT_OVRD) ? 1 : 0,
					streams);
			return -EIO;
		}
		if (formatsp)
			*formatsp = formats;
		if (bpsp)
			*bpsp = bps;
	}

	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_query_supported_pcm);

/**
 * snd_hda_is_supported_format - Check the validity of the format
 * @codec: HD-audio codec
 * @nid: NID to check
 * @format: the HD-audio format value to check
 *
 * Check whether the given node supports the format value.
 *
 * Returns 1 if supported, 0 if not.
 */
int snd_hda_is_supported_format(struct hda_codec *codec, hda_nid_t nid,
				unsigned int format)
{
	int i;
	unsigned int val = 0, rate, stream;

	val = query_pcm_param(codec, nid);
	if (!val)
		return 0;

	rate = format & 0xff00;
	for (i = 0; i < AC_PAR_PCM_RATE_BITS; i++)
		if (rate_bits[i].hda_fmt == rate) {
			if (val & (1 << i))
				break;
			return 0;
		}
	if (i >= AC_PAR_PCM_RATE_BITS)
		return 0;

	stream = query_stream_param(codec, nid);
	if (!stream)
		return 0;

	if (stream & AC_SUPFMT_PCM) {
		switch (format & 0xf0) {
		case 0x00:
			if (!(val & AC_SUPPCM_BITS_8))
				return 0;
			break;
		case 0x10:
			if (!(val & AC_SUPPCM_BITS_16))
				return 0;
			break;
		case 0x20:
			if (!(val & AC_SUPPCM_BITS_20))
				return 0;
			break;
		case 0x30:
			if (!(val & AC_SUPPCM_BITS_24))
				return 0;
			break;
		case 0x40:
			if (!(val & AC_SUPPCM_BITS_32))
				return 0;
			break;
		default:
			return 0;
		}
	} else {
		/* FIXME: check for float32 and AC3? */
	}

	return 1;
}
EXPORT_SYMBOL_HDA(snd_hda_is_supported_format);

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
int snd_hda_codec_prepare(struct hda_codec *codec,
			  struct hda_pcm_stream *hinfo,
			  unsigned int stream,
			  unsigned int format,
			  struct snd_pcm_substream *substream)
{
	int ret;
	mutex_lock(&codec->bus->prepare_mutex);
	ret = hinfo->ops.prepare(hinfo, codec, stream, format, substream);
	if (ret >= 0)
		purify_inactive_streams(codec);
	mutex_unlock(&codec->bus->prepare_mutex);
	return ret;
}
EXPORT_SYMBOL_HDA(snd_hda_codec_prepare);

void snd_hda_codec_cleanup(struct hda_codec *codec,
			   struct hda_pcm_stream *hinfo,
			   struct snd_pcm_substream *substream)
{
	mutex_lock(&codec->bus->prepare_mutex);
	hinfo->ops.cleanup(hinfo, codec, substream);
	mutex_unlock(&codec->bus->prepare_mutex);
}
EXPORT_SYMBOL_HDA(snd_hda_codec_cleanup);

/* global */
const char *snd_hda_pcm_type_name[HDA_PCM_NTYPES] = {
	"Audio", "SPDIF", "HDMI", "Modem"
};

/*
 * get the empty PCM device number to assign
 *
 * note the max device number is limited by HDA_MAX_PCMS, currently 10
 */
static int get_empty_pcm_device(struct hda_bus *bus, int type)
{
	/* audio device indices; not linear to keep compatibility */
	static int audio_idx[HDA_PCM_NTYPES][5] = {
		[HDA_PCM_TYPE_AUDIO] = { 0, 2, 4, 5, -1 },
		[HDA_PCM_TYPE_SPDIF] = { 1, -1 },
		[HDA_PCM_TYPE_HDMI]  = { 3, 7, 8, 9, -1 },
		[HDA_PCM_TYPE_MODEM] = { 6, -1 },
	};
	int i;

	if (type >= HDA_PCM_NTYPES) {
		snd_printk(KERN_WARNING "Invalid PCM type %d\n", type);
		return -EINVAL;
	}

	for (i = 0; audio_idx[type][i] >= 0 ; i++)
		if (!test_and_set_bit(audio_idx[type][i], bus->pcm_dev_bits))
			return audio_idx[type][i];

	/* non-fixed slots starting from 10 */
	for (i = 10; i < 32; i++) {
		if (!test_and_set_bit(i, bus->pcm_dev_bits))
			return i;
	}

	snd_printk(KERN_WARNING "Too many %s devices\n",
		snd_hda_pcm_type_name[type]);
	return -EAGAIN;
}

/*
 * attach a new PCM stream
 */
static int snd_hda_attach_pcm(struct hda_codec *codec, struct hda_pcm *pcm)
{
	struct hda_bus *bus = codec->bus;
	struct hda_pcm_stream *info;
	int stream, err;

	if (snd_BUG_ON(!pcm->name))
		return -EINVAL;
	for (stream = 0; stream < 2; stream++) {
		info = &pcm->stream[stream];
		if (info->substreams) {
			err = set_pcm_default_values(codec, info);
			if (err < 0)
				return err;
		}
	}
	return bus->ops.attach_pcm(bus, codec, pcm);
}

/* assign all PCMs of the given codec */
int snd_hda_codec_build_pcms(struct hda_codec *codec)
{
	unsigned int pcm;
	int err;

	if (!codec->num_pcms) {
		if (!codec->patch_ops.build_pcms)
			return 0;
		err = codec->patch_ops.build_pcms(codec);
		if (err < 0) {
			printk(KERN_ERR "hda_codec: cannot build PCMs"
			       "for #%d (error %d)\n", codec->addr, err);
			err = snd_hda_codec_reset(codec);
			if (err < 0) {
				printk(KERN_ERR
				       "hda_codec: cannot revert codec\n");
				return err;
			}
		}
	}
	for (pcm = 0; pcm < codec->num_pcms; pcm++) {
		struct hda_pcm *cpcm = &codec->pcm_info[pcm];
		int dev;

		if (!cpcm->stream[0].substreams && !cpcm->stream[1].substreams)
			continue; /* no substreams assigned */

		if (!cpcm->pcm) {
			dev = get_empty_pcm_device(codec->bus, cpcm->pcm_type);
			if (dev < 0)
				continue; /* no fatal error */
			cpcm->device = dev;
			err = snd_hda_attach_pcm(codec, cpcm);
			if (err < 0) {
				printk(KERN_ERR "hda_codec: cannot attach "
				       "PCM stream %d for codec #%d\n",
				       dev, codec->addr);
				continue; /* no fatal error */
			}
		}
	}
	return 0;
}

/**
 * snd_hda_build_pcms - build PCM information
 * @bus: the BUS
 *
 * Create PCM information for each codec included in the bus.
 *
 * The build_pcms codec patch is requested to set up codec->num_pcms and
 * codec->pcm_info properly.  The array is referred by the top-level driver
 * to create its PCM instances.
 * The allocated codec->pcm_info should be released in codec->patch_ops.free
 * callback.
 *
 * At least, substreams, channels_min and channels_max must be filled for
 * each stream.  substreams = 0 indicates that the stream doesn't exist.
 * When rates and/or formats are zero, the supported values are queried
 * from the given nid.  The nid is used also by the default ops.prepare
 * and ops.cleanup callbacks.
 *
 * The driver needs to call ops.open in its open callback.  Similarly,
 * ops.close is supposed to be called in the close callback.
 * ops.prepare should be called in the prepare or hw_params callback
 * with the proper parameters for set up.
 * ops.cleanup should be called in hw_free for clean up of streams.
 *
 * This function returns 0 if successful, or a negative error code.
 */
int snd_hda_build_pcms(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		int err = snd_hda_codec_build_pcms(codec);
		if (err < 0)
			return err;
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_build_pcms);

/**
 * snd_hda_check_board_config - compare the current codec with the config table
 * @codec: the HDA codec
 * @num_configs: number of config enums
 * @models: array of model name strings
 * @tbl: configuration table, terminated by null entries
 *
 * Compares the modelname or PCI subsystem id of the current codec with the
 * given configuration table.  If a matching entry is found, returns its
 * config value (supposed to be 0 or positive).
 *
 * If no entries are matching, the function returns a negative value.
 */
int snd_hda_check_board_config(struct hda_codec *codec,
			       int num_configs, const char * const *models,
			       const struct snd_pci_quirk *tbl)
{
	if (codec->modelname && models) {
		int i;
		for (i = 0; i < num_configs; i++) {
			if (models[i] &&
			    !strcmp(codec->modelname, models[i])) {
				snd_printd(KERN_INFO "hda_codec: model '%s' is "
					   "selected\n", models[i]);
				return i;
			}
		}
	}

	if (!codec->bus->pci || !tbl)
		return -1;

	tbl = snd_pci_quirk_lookup(codec->bus->pci, tbl);
	if (!tbl)
		return -1;
	if (tbl->value >= 0 && tbl->value < num_configs) {
#ifdef CONFIG_SND_DEBUG_VERBOSE
		char tmp[10];
		const char *model = NULL;
		if (models)
			model = models[tbl->value];
		if (!model) {
			sprintf(tmp, "#%d", tbl->value);
			model = tmp;
		}
		snd_printdd(KERN_INFO "hda_codec: model '%s' is selected "
			    "for config %x:%x (%s)\n",
			    model, tbl->subvendor, tbl->subdevice,
			    (tbl->name ? tbl->name : "Unknown device"));
#endif
		return tbl->value;
	}
	return -1;
}
EXPORT_SYMBOL_HDA(snd_hda_check_board_config);

/**
 * snd_hda_check_board_codec_sid_config - compare the current codec
					subsystem ID with the
					config table

	   This is important for Gateway notebooks with SB450 HDA Audio
	   where the vendor ID of the PCI device is:
		ATI Technologies Inc SB450 HDA Audio [1002:437b]
	   and the vendor/subvendor are found only at the codec.

 * @codec: the HDA codec
 * @num_configs: number of config enums
 * @models: array of model name strings
 * @tbl: configuration table, terminated by null entries
 *
 * Compares the modelname or PCI subsystem id of the current codec with the
 * given configuration table.  If a matching entry is found, returns its
 * config value (supposed to be 0 or positive).
 *
 * If no entries are matching, the function returns a negative value.
 */
int snd_hda_check_board_codec_sid_config(struct hda_codec *codec,
			       int num_configs, const char * const *models,
			       const struct snd_pci_quirk *tbl)
{
	const struct snd_pci_quirk *q;

	/* Search for codec ID */
	for (q = tbl; q->subvendor; q++) {
		unsigned int mask = 0xffff0000 | q->subdevice_mask;
		unsigned int id = (q->subdevice | (q->subvendor << 16)) & mask;
		if ((codec->subsystem_id & mask) == id)
			break;
	}

	if (!q->subvendor)
		return -1;

	tbl = q;

	if (tbl->value >= 0 && tbl->value < num_configs) {
#ifdef CONFIG_SND_DEBUG_VERBOSE
		char tmp[10];
		const char *model = NULL;
		if (models)
			model = models[tbl->value];
		if (!model) {
			sprintf(tmp, "#%d", tbl->value);
			model = tmp;
		}
		snd_printdd(KERN_INFO "hda_codec: model '%s' is selected "
			    "for config %x:%x (%s)\n",
			    model, tbl->subvendor, tbl->subdevice,
			    (tbl->name ? tbl->name : "Unknown device"));
#endif
		return tbl->value;
	}
	return -1;
}
EXPORT_SYMBOL_HDA(snd_hda_check_board_codec_sid_config);

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
		if (knew->iface == -1)	/* skip this codec private value */
			continue;
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
			if (!addr && codec->addr)
				addr = codec->addr;
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
EXPORT_SYMBOL_HDA(snd_hda_add_new_ctls);

#ifdef CONFIG_PM
static void hda_power_work(struct work_struct *work)
{
	struct hda_codec *codec =
		container_of(work, struct hda_codec, power_work.work);
	struct hda_bus *bus = codec->bus;
	unsigned int state;

	spin_lock(&codec->power_lock);
	if (codec->power_transition > 0) { /* during power-up sequence? */
		spin_unlock(&codec->power_lock);
		return;
	}
	if (!codec->power_on || codec->power_count) {
		codec->power_transition = 0;
		spin_unlock(&codec->power_lock);
		return;
	}
	spin_unlock(&codec->power_lock);

	state = hda_call_codec_suspend(codec, true);
	if (!codec->pm_down_notified &&
	    !bus->power_keep_link_on && (state & AC_PWRST_CLK_STOP_OK)) {
		codec->pm_down_notified = 1;
		hda_call_pm_notify(bus, false);
	}
}

static void hda_keep_power_on(struct hda_codec *codec)
{
	spin_lock(&codec->power_lock);
	codec->power_count++;
	codec->power_on = 1;
	codec->power_jiffies = jiffies;
	spin_unlock(&codec->power_lock);
}

/* update the power on/off account with the current jiffies */
void snd_hda_update_power_acct(struct hda_codec *codec)
{
	unsigned long delta = jiffies - codec->power_jiffies;
	if (codec->power_on)
		codec->power_on_acct += delta;
	else
		codec->power_off_acct += delta;
	codec->power_jiffies += delta;
}

/* Transition to powered up, if wait_power_down then wait for a pending
 * transition to D3 to complete. A pending D3 transition is indicated
 * with power_transition == -1. */
/* call this with codec->power_lock held! */
static void __snd_hda_power_up(struct hda_codec *codec, bool wait_power_down)
{
	struct hda_bus *bus = codec->bus;

	/* Return if power_on or transitioning to power_on, unless currently
	 * powering down. */
	if ((codec->power_on || codec->power_transition > 0) &&
	    !(wait_power_down && codec->power_transition < 0))
		return;
	spin_unlock(&codec->power_lock);

	cancel_delayed_work_sync(&codec->power_work);

	spin_lock(&codec->power_lock);
	/* If the power down delayed work was cancelled above before starting,
	 * then there is no need to go through power up here.
	 */
	if (codec->power_on) {
		if (codec->power_transition < 0)
			codec->power_transition = 0;
		return;
	}

	trace_hda_power_up(codec);
	snd_hda_update_power_acct(codec);
	codec->power_on = 1;
	codec->power_jiffies = jiffies;
	codec->power_transition = 1; /* avoid reentrance */
	spin_unlock(&codec->power_lock);

	if (codec->pm_down_notified) {
		codec->pm_down_notified = 0;
		hda_call_pm_notify(bus, true);
	}

	hda_call_codec_resume(codec);

	spin_lock(&codec->power_lock);
	codec->power_transition = 0;
}

#define power_save(codec)	\
	((codec)->bus->power_save ? *(codec)->bus->power_save : 0)

/* Transition to powered down */
static void __snd_hda_power_down(struct hda_codec *codec)
{
	if (!codec->power_on || codec->power_count || codec->power_transition)
		return;

	if (power_save(codec)) {
		codec->power_transition = -1; /* avoid reentrance */
		queue_delayed_work(codec->bus->workq, &codec->power_work,
				msecs_to_jiffies(power_save(codec) * 1000));
	}
}

/**
 * snd_hda_power_save - Power-up/down/sync the codec
 * @codec: HD-audio codec
 * @delta: the counter delta to change
 *
 * Change the power-up counter via @delta, and power up or down the hardware
 * appropriately.  For the power-down, queue to the delayed action.
 * Passing zero to @delta means to synchronize the power state.
 */
void snd_hda_power_save(struct hda_codec *codec, int delta, bool d3wait)
{
	spin_lock(&codec->power_lock);
	codec->power_count += delta;
	trace_hda_power_count(codec);
	if (delta > 0)
		__snd_hda_power_up(codec, d3wait);
	else
		__snd_hda_power_down(codec);
	spin_unlock(&codec->power_lock);
}
EXPORT_SYMBOL_HDA(snd_hda_power_save);

/**
 * snd_hda_check_amp_list_power - Check the amp list and update the power
 * @codec: HD-audio codec
 * @check: the object containing an AMP list and the status
 * @nid: NID to check / update
 *
 * Check whether the given NID is in the amp list.  If it's in the list,
 * check the current AMP status, and update the the power-status according
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
					snd_hda_power_up(codec);
				}
				return 1;
			}
		}
	}
	if (check->power_on) {
		check->power_on = 0;
		snd_hda_power_down(codec);
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_check_amp_list_power);
#endif

/*
 * Channel mode helper
 */

/**
 * snd_hda_ch_mode_info - Info callback helper for the channel mode enum
 */
int snd_hda_ch_mode_info(struct hda_codec *codec,
			 struct snd_ctl_elem_info *uinfo,
			 const struct hda_channel_mode *chmode,
			 int num_chmodes)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = num_chmodes;
	if (uinfo->value.enumerated.item >= num_chmodes)
		uinfo->value.enumerated.item = num_chmodes - 1;
	sprintf(uinfo->value.enumerated.name, "%dch",
		chmode[uinfo->value.enumerated.item].channels);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_ch_mode_info);

/**
 * snd_hda_ch_mode_get - Get callback helper for the channel mode enum
 */
int snd_hda_ch_mode_get(struct hda_codec *codec,
			struct snd_ctl_elem_value *ucontrol,
			const struct hda_channel_mode *chmode,
			int num_chmodes,
			int max_channels)
{
	int i;

	for (i = 0; i < num_chmodes; i++) {
		if (max_channels == chmode[i].channels) {
			ucontrol->value.enumerated.item[0] = i;
			break;
		}
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_ch_mode_get);

/**
 * snd_hda_ch_mode_put - Put callback helper for the channel mode enum
 */
int snd_hda_ch_mode_put(struct hda_codec *codec,
			struct snd_ctl_elem_value *ucontrol,
			const struct hda_channel_mode *chmode,
			int num_chmodes,
			int *max_channelsp)
{
	unsigned int mode;

	mode = ucontrol->value.enumerated.item[0];
	if (mode >= num_chmodes)
		return -EINVAL;
	if (*max_channelsp == chmode[mode].channels)
		return 0;
	/* change the current channel setting */
	*max_channelsp = chmode[mode].channels;
	if (chmode[mode].sequence)
		snd_hda_sequence_write_cache(codec, chmode[mode].sequence);
	return 1;
}
EXPORT_SYMBOL_HDA(snd_hda_ch_mode_put);

/*
 * input MUX helper
 */

/**
 * snd_hda_input_mux_info_info - Info callback helper for the input-mux enum
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
EXPORT_SYMBOL_HDA(snd_hda_input_mux_info);

/**
 * snd_hda_input_mux_info_put - Put callback helper for the input-mux enum
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
EXPORT_SYMBOL_HDA(snd_hda_input_mux_put);


/*
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

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = num_items;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_enum_helper_info);

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
	if (codec->slave_dig_outs) {
		const hda_nid_t *d;
		for (d = codec->slave_dig_outs; *d; d++)
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
	if (codec->slave_dig_outs) {
		const hda_nid_t *d;
		for (d = codec->slave_dig_outs; *d; d++)
			snd_hda_codec_cleanup_stream(codec, *d);
	}
}

/**
 * snd_hda_bus_reboot_notify - call the reboot notifier of each codec
 * @bus: HD-audio bus
 */
void snd_hda_bus_reboot_notify(struct hda_bus *bus)
{
	struct hda_codec *codec;

	if (!bus)
		return;
	list_for_each_entry(codec, &bus->codec_list, list) {
		if (hda_codec_is_power_on(codec) &&
		    codec->patch_ops.reboot_notify)
			codec->patch_ops.reboot_notify(codec);
	}
}
EXPORT_SYMBOL_HDA(snd_hda_bus_reboot_notify);

/**
 * snd_hda_multi_out_dig_open - open the digital out in the exclusive mode
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
EXPORT_SYMBOL_HDA(snd_hda_multi_out_dig_open);

/**
 * snd_hda_multi_out_dig_prepare - prepare the digital out stream
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
EXPORT_SYMBOL_HDA(snd_hda_multi_out_dig_prepare);

/**
 * snd_hda_multi_out_dig_cleanup - clean-up the digital out stream
 */
int snd_hda_multi_out_dig_cleanup(struct hda_codec *codec,
				  struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	cleanup_dig_out_stream(codec, mout->dig_out_nid);
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_multi_out_dig_cleanup);

/**
 * snd_hda_multi_out_dig_close - release the digital out stream
 */
int snd_hda_multi_out_dig_close(struct hda_codec *codec,
				struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	mout->dig_out_used = 0;
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_multi_out_dig_close);

/**
 * snd_hda_multi_out_analog_open - open analog outputs
 *
 * Open analog outputs and set up the hw-constraints.
 * If the digital outputs can be opened as slave, open the digital
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
EXPORT_SYMBOL_HDA(snd_hda_multi_out_analog_open);

/**
 * snd_hda_multi_out_analog_prepare - Preapre the analog outputs.
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
		if (chs == 2 &&
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
	for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++)
		if (!mout->no_share_stream && mout->extra_out_nid[i])
			snd_hda_codec_setup_stream(codec,
						   mout->extra_out_nid[i],
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
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_multi_out_analog_prepare);

/**
 * snd_hda_multi_out_analog_cleanup - clean up the setting for analog out
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
EXPORT_SYMBOL_HDA(snd_hda_multi_out_analog_cleanup);

/**
 * snd_hda_get_default_vref - Get the default (mic) VREF pin bits
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
EXPORT_SYMBOL_HDA(snd_hda_get_default_vref);

/* correct the pin ctl value for matching with the pin cap */
unsigned int snd_hda_correct_pin_ctl(struct hda_codec *codec,
				     hda_nid_t pin, unsigned int val)
{
	static unsigned int cap_lists[][2] = {
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
EXPORT_SYMBOL_HDA(snd_hda_correct_pin_ctl);

int _snd_hda_set_pin_ctl(struct hda_codec *codec, hda_nid_t pin,
			 unsigned int val, bool cached)
{
	val = snd_hda_correct_pin_ctl(codec, pin, val);
	snd_hda_codec_set_pin_target(codec, pin, val);
	if (cached)
		return snd_hda_codec_update_cache(codec, pin, 0,
				AC_VERB_SET_PIN_WIDGET_CONTROL, val);
	else
		return snd_hda_codec_write(codec, pin, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL, val);
}
EXPORT_SYMBOL_HDA(_snd_hda_set_pin_ctl);

/**
 * snd_hda_add_imux_item - Add an item to input_mux
 *
 * When the same label is used already in the existing items, the number
 * suffix is appended to the label.  This label index number is stored
 * to type_idx when non-NULL pointer is given.
 */
int snd_hda_add_imux_item(struct hda_input_mux *imux, const char *label,
			  int index, int *type_idx)
{
	int i, label_idx = 0;
	if (imux->num_items >= HDA_MAX_NUM_INPUTS) {
		snd_printd(KERN_ERR "hda_codec: Too many imux items!\n");
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
		strlcpy(imux->items[imux->num_items].label, label,
			sizeof(imux->items[imux->num_items].label));
	imux->items[imux->num_items].index = index;
	imux->num_items++;
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_add_imux_item);


#ifdef CONFIG_PM
/*
 * power management
 */

/**
 * snd_hda_suspend - suspend the codecs
 * @bus: the HDA bus
 *
 * Returns 0 if successful.
 */
int snd_hda_suspend(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		cancel_delayed_work_sync(&codec->jackpoll_work);
		if (hda_codec_is_power_on(codec))
			hda_call_codec_suspend(codec, false);
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_suspend);

/**
 * snd_hda_resume - resume the codecs
 * @bus: the HDA bus
 *
 * Returns 0 if successful.
 */
int snd_hda_resume(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		hda_call_codec_resume(codec);
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_resume);
#endif /* CONFIG_PM */

/*
 * generic arrays
 */

/**
 * snd_array_new - get a new element from the given array
 * @array: the array object
 *
 * Get a new element from the given array.  If it exceeds the
 * pre-allocated array size, re-allocate the array.
 *
 * Returns NULL if allocation failed.
 */
void *snd_array_new(struct snd_array *array)
{
	if (snd_BUG_ON(!array->elem_size))
		return NULL;
	if (array->used >= array->alloced) {
		int num = array->alloced + array->alloc_align;
		int size = (num + 1) * array->elem_size;
		void *nlist;
		if (snd_BUG_ON(num >= 4096))
			return NULL;
		nlist = krealloc(array->list, size, GFP_KERNEL | __GFP_ZERO);
		if (!nlist)
			return NULL;
		array->list = nlist;
		array->alloced = num;
	}
	return snd_array_elem(array, array->used++);
}
EXPORT_SYMBOL_HDA(snd_array_new);

/**
 * snd_array_free - free the given array elements
 * @array: the array object
 */
void snd_array_free(struct snd_array *array)
{
	kfree(array->list);
	array->used = 0;
	array->alloced = 0;
	array->list = NULL;
}
EXPORT_SYMBOL_HDA(snd_array_free);

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
	static unsigned int bits[] = { 8, 16, 20, 24, 32 };
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(bits); i++)
		if (pcm & (AC_SUPPCM_BITS_8 << i))
			j += snprintf(buf + j, buflen - j,  " %d", bits[i]);

	buf[j] = '\0'; /* necessary when j == 0 */
}
EXPORT_SYMBOL_HDA(snd_print_pcm_bits);

MODULE_DESCRIPTION("HDA codec core");
MODULE_LICENSE("GPL");
