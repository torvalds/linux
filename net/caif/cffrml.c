/*
 * CAIF Framing Layer.
 *
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/crc-ccitt.h>
#include <linux/netdevice.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cffrml.h>

#define container_obj(layr) container_of(layr, struct cffrml, layer)

struct cffrml {
	struct cflayer layer;
	bool dofcs;		/* !< FCS active */
	int __percpu		*pcpu_refcnt;
};

static int cffrml_receive(struct cflayer *layr, struct cfpkt *pkt);
static int cffrml_transmit(struct cflayer *layr, struct cfpkt *pkt);
static void cffrml_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
				int phyid);

static u32 cffrml_rcv_error;
static u32 cffrml_rcv_checsum_error;
struct cflayer *cffrml_create(u16 phyid, bool use_fcs)
{
	struct cffrml *this = kzalloc(sizeof(struct cffrml), GFP_ATOMIC);
	if (!this)
		return NULL;
	this->pcpu_refcnt = alloc_percpu(int);
	if (this->pcpu_refcnt == NULL) {
		kfree(this);
		return NULL;
	}

	caif_assert(offsetof(struct cffrml, layer) == 0);

	this->layer.receive = cffrml_receive;
	this->layer.transmit = cffrml_transmit;
	this->layer.ctrlcmd = cffrml_ctrlcmd;
	snprintf(this->layer.name, CAIF_LAYER_NAME_SZ, "frm%d", phyid);
	this->dofcs = use_fcs;
	this->layer.id = phyid;
	return (struct cflayer *) this;
}

void cffrml_free(struct cflayer *layer)
{
	struct cffrml *this = container_obj(layer);
	free_percpu(this->pcpu_refcnt);
	kfree(layer);
}

void cffrml_set_uplayer(struct cflayer *this, struct cflayer *up)
{
	this->up = up;
}

void cffrml_set_dnlayer(struct cflayer *this, struct cflayer *dn)
{
	this->dn = dn;
}

static u16 cffrml_checksum(u16 chks, void *buf, u16 len)
{
	/* FIXME: FCS should be moved to glue in order to use OS-Specific
	 * solutions
	 */
	return crc_ccitt(chks, buf, len);
}

static int cffrml_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	u16 tmp;
	u16 len;
	u16 hdrchks;
	u16 pktchks;
	struct cffrml *this;
	this = container_obj(layr);

	cfpkt_extr_head(pkt, &tmp, 2);
	len = le16_to_cpu(tmp);

	/* Subtract for FCS on length if FCS is not used. */
	if (!this->dofcs)
		len -= 2;

	if (cfpkt_setlen(pkt, len) < 0) {
		++cffrml_rcv_error;
		pr_err("Framing length error (%d)\n", len);
		cfpkt_destroy(pkt);
		return -EPROTO;
	}
	/*
	 * Don't do extract if FCS is false, rather do setlen - then we don't
	 * get a cache-miss.
	 */
	if (this->dofcs) {
		cfpkt_extr_trail(pkt, &tmp, 2);
		hdrchks = le16_to_cpu(tmp);
		pktchks = cfpkt_iterate(pkt, cffrml_checksum, 0xffff);
		if (pktchks != hdrchks) {
			cfpkt_add_trail(pkt, &tmp, 2);
			++cffrml_rcv_error;
			++cffrml_rcv_checsum_error;
			pr_info("Frame checksum error (0x%x != 0x%x)\n",
				hdrchks, pktchks);
			return -EILSEQ;
		}
	}
	if (cfpkt_erroneous(pkt)) {
		++cffrml_rcv_error;
		pr_err("Packet is erroneous!\n");
		cfpkt_destroy(pkt);
		return -EPROTO;
	}

	if (layr->up == NULL) {
		pr_err("Layr up is missing!\n");
		cfpkt_destroy(pkt);
		return -EINVAL;
	}

	return layr->up->receive(layr->up, pkt);
}

static int cffrml_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	int tmp;
	u16 chks;
	u16 len;
	struct cffrml *this = container_obj(layr);
	if (this->dofcs) {
		chks = cfpkt_iterate(pkt, cffrml_checksum, 0xffff);
		tmp = cpu_to_le16(chks);
		cfpkt_add_trail(pkt, &tmp, 2);
	} else {
		cfpkt_pad_trail(pkt, 2);
	}
	len = cfpkt_getlen(pkt);
	tmp = cpu_to_le16(len);
	cfpkt_add_head(pkt, &tmp, 2);
	cfpkt_info(pkt)->hdr_len += 2;
	if (cfpkt_erroneous(pkt)) {
		pr_err("Packet is erroneous!\n");
		cfpkt_destroy(pkt);
		return -EPROTO;
	}

	if (layr->dn == NULL) {
		cfpkt_destroy(pkt);
		return -ENODEV;

	}
	return layr->dn->transmit(layr->dn, pkt);
}

static void cffrml_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
					int phyid)
{
	if (layr->up && layr->up->ctrlcmd)
		layr->up->ctrlcmd(layr->up, ctrl, layr->id);
}

void cffrml_put(struct cflayer *layr)
{
	struct cffrml *this = container_obj(layr);
	if (layr != NULL && this->pcpu_refcnt != NULL)
		irqsafe_cpu_dec(*this->pcpu_refcnt);
}

void cffrml_hold(struct cflayer *layr)
{
	struct cffrml *this = container_obj(layr);
	if (layr != NULL && this->pcpu_refcnt != NULL)
		irqsafe_cpu_inc(*this->pcpu_refcnt);
}

int cffrml_refcnt_read(struct cflayer *layr)
{
	int i, refcnt = 0;
	struct cffrml *this = container_obj(layr);
	for_each_possible_cpu(i)
		refcnt += *per_cpu_ptr(this->pcpu_refcnt, i);
	return refcnt;
}
