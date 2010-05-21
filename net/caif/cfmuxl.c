/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cfmuxl.h>
#include <net/caif/cfsrvl.h>
#include <net/caif/cffrml.h>

#define container_obj(layr) container_of(layr, struct cfmuxl, layer)

#define CAIF_CTRL_CHANNEL 0
#define UP_CACHE_SIZE 8
#define DN_CACHE_SIZE 8

struct cfmuxl {
	struct cflayer layer;
	struct list_head srvl_list;
	struct list_head frml_list;
	struct cflayer *up_cache[UP_CACHE_SIZE];
	struct cflayer *dn_cache[DN_CACHE_SIZE];
	/*
	 * Set when inserting or removing downwards layers.
	 */
	spinlock_t transmit_lock;

	/*
	 * Set when inserting or removing upwards layers.
	 */
	spinlock_t receive_lock;

};

static int cfmuxl_receive(struct cflayer *layr, struct cfpkt *pkt);
static int cfmuxl_transmit(struct cflayer *layr, struct cfpkt *pkt);
static void cfmuxl_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
				int phyid);
static struct cflayer *get_up(struct cfmuxl *muxl, u16 id);

struct cflayer *cfmuxl_create(void)
{
	struct cfmuxl *this = kmalloc(sizeof(struct cfmuxl), GFP_ATOMIC);
	if (!this)
		return NULL;
	memset(this, 0, sizeof(*this));
	this->layer.receive = cfmuxl_receive;
	this->layer.transmit = cfmuxl_transmit;
	this->layer.ctrlcmd = cfmuxl_ctrlcmd;
	INIT_LIST_HEAD(&this->srvl_list);
	INIT_LIST_HEAD(&this->frml_list);
	spin_lock_init(&this->transmit_lock);
	spin_lock_init(&this->receive_lock);
	snprintf(this->layer.name, CAIF_LAYER_NAME_SZ, "mux");
	return &this->layer;
}

int cfmuxl_set_uplayer(struct cflayer *layr, struct cflayer *up, u8 linkid)
{
	struct cfmuxl *muxl = container_obj(layr);
	spin_lock(&muxl->receive_lock);
	cfsrvl_get(up);
	list_add(&up->node, &muxl->srvl_list);
	spin_unlock(&muxl->receive_lock);
	return 0;
}

bool cfmuxl_is_phy_inuse(struct cflayer *layr, u8 phyid)
{
	struct list_head *node;
	struct cflayer *layer;
	struct cfmuxl *muxl = container_obj(layr);
	bool match = false;
	spin_lock(&muxl->receive_lock);

	list_for_each(node, &muxl->srvl_list) {
		layer = list_entry(node, struct cflayer, node);
		if (cfsrvl_phyid_match(layer, phyid)) {
			match = true;
			break;
		}

	}
	spin_unlock(&muxl->receive_lock);
	return match;
}

u8 cfmuxl_get_phyid(struct cflayer *layr, u8 channel_id)
{
	struct cflayer *up;
	int phyid;
	struct cfmuxl *muxl = container_obj(layr);
	spin_lock(&muxl->receive_lock);
	up = get_up(muxl, channel_id);
	if (up != NULL)
		phyid = cfsrvl_getphyid(up);
	else
		phyid = 0;
	spin_unlock(&muxl->receive_lock);
	return phyid;
}

int cfmuxl_set_dnlayer(struct cflayer *layr, struct cflayer *dn, u8 phyid)
{
	struct cfmuxl *muxl = (struct cfmuxl *) layr;
	spin_lock(&muxl->transmit_lock);
	list_add(&dn->node, &muxl->frml_list);
	spin_unlock(&muxl->transmit_lock);
	return 0;
}

static struct cflayer *get_from_id(struct list_head *list, u16 id)
{
	struct list_head *node;
	struct cflayer *layer;
	list_for_each(node, list) {
		layer = list_entry(node, struct cflayer, node);
		if (layer->id == id)
			return layer;
	}
	return NULL;
}

struct cflayer *cfmuxl_remove_dnlayer(struct cflayer *layr, u8 phyid)
{
	struct cfmuxl *muxl = container_obj(layr);
	struct cflayer *dn;
	spin_lock(&muxl->transmit_lock);
	memset(muxl->dn_cache, 0, sizeof(muxl->dn_cache));
	dn = get_from_id(&muxl->frml_list, phyid);
	if (dn == NULL) {
		spin_unlock(&muxl->transmit_lock);
		return NULL;
	}
	list_del(&dn->node);
	caif_assert(dn != NULL);
	spin_unlock(&muxl->transmit_lock);
	return dn;
}

/* Invariant: lock is taken */
static struct cflayer *get_up(struct cfmuxl *muxl, u16 id)
{
	struct cflayer *up;
	int idx = id % UP_CACHE_SIZE;
	up = muxl->up_cache[idx];
	if (up == NULL || up->id != id) {
		up = get_from_id(&muxl->srvl_list, id);
		muxl->up_cache[idx] = up;
	}
	return up;
}

/* Invariant: lock is taken */
static struct cflayer *get_dn(struct cfmuxl *muxl, struct dev_info *dev_info)
{
	struct cflayer *dn;
	int idx = dev_info->id % DN_CACHE_SIZE;
	dn = muxl->dn_cache[idx];
	if (dn == NULL || dn->id != dev_info->id) {
		dn = get_from_id(&muxl->frml_list, dev_info->id);
		muxl->dn_cache[idx] = dn;
	}
	return dn;
}

struct cflayer *cfmuxl_remove_uplayer(struct cflayer *layr, u8 id)
{
	struct cflayer *up;
	struct cfmuxl *muxl = container_obj(layr);
	spin_lock(&muxl->receive_lock);
	up = get_up(muxl, id);
	if (up == NULL)
		return NULL;
	memset(muxl->up_cache, 0, sizeof(muxl->up_cache));
	list_del(&up->node);
	cfsrvl_put(up);
	spin_unlock(&muxl->receive_lock);
	return up;
}

static int cfmuxl_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	int ret;
	struct cfmuxl *muxl = container_obj(layr);
	u8 id;
	struct cflayer *up;
	if (cfpkt_extr_head(pkt, &id, 1) < 0) {
		pr_err("CAIF: %s(): erroneous Caif Packet\n", __func__);
		cfpkt_destroy(pkt);
		return -EPROTO;
	}

	spin_lock(&muxl->receive_lock);
	up = get_up(muxl, id);
	spin_unlock(&muxl->receive_lock);
	if (up == NULL) {
		pr_info("CAIF: %s():Received data on unknown link ID = %d "
			"(0x%x)	 up == NULL", __func__, id, id);
		cfpkt_destroy(pkt);
		/*
		 * Don't return ERROR, since modem misbehaves and sends out
		 * flow on before linksetup response.
		 */
		return /* CFGLU_EPROT; */ 0;
	}
	cfsrvl_get(up);
	ret = up->receive(up, pkt);
	cfsrvl_put(up);
	return ret;
}

static int cfmuxl_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	int ret;
	struct cfmuxl *muxl = container_obj(layr);
	u8 linkid;
	struct cflayer *dn;
	struct caif_payload_info *info = cfpkt_info(pkt);
	dn = get_dn(muxl, cfpkt_info(pkt)->dev_info);
	if (dn == NULL) {
		pr_warning("CAIF: %s(): Send data on unknown phy "
			   "ID = %d (0x%x)\n",
			   __func__, info->dev_info->id, info->dev_info->id);
		return -ENOTCONN;
	}
	info->hdr_len += 1;
	linkid = info->channel_id;
	cfpkt_add_head(pkt, &linkid, 1);
	ret = dn->transmit(dn, pkt);
	/* Remove MUX protocol header upon error. */
	if (ret < 0)
		cfpkt_extr_head(pkt, &linkid, 1);
	return ret;
}

static void cfmuxl_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
				int phyid)
{
	struct cfmuxl *muxl = container_obj(layr);
	struct list_head *node;
	struct cflayer *layer;
	list_for_each(node, &muxl->srvl_list) {
		layer = list_entry(node, struct cflayer, node);
		if (cfsrvl_phyid_match(layer, phyid))
			layer->ctrlcmd(layer, ctrl, phyid);
	}
}
