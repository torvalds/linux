/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfsrvl.h>
#include <net/caif/cfpkt.h>

#define container_obj(layr) container_of(layr, struct cfrfml, serv.layer)
#define RFM_SEGMENTATION_BIT 0x01
#define RFM_HEAD_SIZE 7

static int cfrfml_receive(struct cflayer *layr, struct cfpkt *pkt);
static int cfrfml_transmit(struct cflayer *layr, struct cfpkt *pkt);

struct cfrfml {
	struct cfsrvl serv;
	struct cfpkt *incomplete_frm;
	int fragment_size;
	u8  seghead[6];
	u16 pdu_size;
	/* Protects serialized processing of packets */
	spinlock_t sync;
};

static void cfrfml_release(struct cflayer *layer)
{
	struct cfsrvl *srvl = container_of(layer, struct cfsrvl, layer);
	struct cfrfml *rfml = container_obj(&srvl->layer);

	if (rfml->incomplete_frm)
		cfpkt_destroy(rfml->incomplete_frm);

	kfree(srvl);
}

struct cflayer *cfrfml_create(u8 channel_id, struct dev_info *dev_info,
					int mtu_size)
{
	int tmp;
	struct cfrfml *this = kzalloc(sizeof(struct cfrfml), GFP_ATOMIC);

	if (!this)
		return NULL;

	cfsrvl_init(&this->serv, channel_id, dev_info, false);
	this->serv.release = cfrfml_release;
	this->serv.layer.receive = cfrfml_receive;
	this->serv.layer.transmit = cfrfml_transmit;

	/* Round down to closest multiple of 16 */
	tmp = (mtu_size - RFM_HEAD_SIZE - 6) / 16;
	tmp *= 16;

	this->fragment_size = tmp;
	spin_lock_init(&this->sync);
	snprintf(this->serv.layer.name, CAIF_LAYER_NAME_SZ,
		"rfm%d", channel_id);

	return &this->serv.layer;
}

static struct cfpkt *rfm_append(struct cfrfml *rfml, char *seghead,
			struct cfpkt *pkt, int *err)
{
	struct cfpkt *tmppkt;
	*err = -EPROTO;
	/* n-th but not last segment */

	if (cfpkt_extr_head(pkt, seghead, 6) < 0)
		return NULL;

	/* Verify correct header */
	if (memcmp(seghead, rfml->seghead, 6) != 0)
		return NULL;

	tmppkt = cfpkt_append(rfml->incomplete_frm, pkt,
			rfml->pdu_size + RFM_HEAD_SIZE);

	/* If cfpkt_append failes input pkts are not freed */
	*err = -ENOMEM;
	if (tmppkt == NULL)
		return NULL;

	*err = 0;
	return tmppkt;
}

static int cfrfml_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	u8 tmp;
	bool segmented;
	int err;
	u8 seghead[6];
	struct cfrfml *rfml;
	struct cfpkt *tmppkt = NULL;

	caif_assert(layr->up != NULL);
	caif_assert(layr->receive != NULL);
	rfml = container_obj(layr);
	spin_lock(&rfml->sync);

	err = -EPROTO;
	if (cfpkt_extr_head(pkt, &tmp, 1) < 0)
		goto out;
	segmented = tmp & RFM_SEGMENTATION_BIT;

	if (segmented) {
		if (rfml->incomplete_frm == NULL) {
			/* Initial Segment */
			if (cfpkt_peek_head(pkt, rfml->seghead, 6) < 0)
				goto out;

			rfml->pdu_size = get_unaligned_le16(rfml->seghead+4);

			if (cfpkt_erroneous(pkt))
				goto out;
			rfml->incomplete_frm = pkt;
			pkt = NULL;
		} else {

			tmppkt = rfm_append(rfml, seghead, pkt, &err);
			if (tmppkt == NULL)
				goto out;

			if (cfpkt_erroneous(tmppkt))
				goto out;

			rfml->incomplete_frm = tmppkt;


			if (cfpkt_erroneous(tmppkt))
				goto out;
		}
		err = 0;
		goto out;
	}

	if (rfml->incomplete_frm) {

		/* Last Segment */
		tmppkt = rfm_append(rfml, seghead, pkt, &err);
		if (tmppkt == NULL)
			goto out;

		if (cfpkt_erroneous(tmppkt))
			goto out;

		rfml->incomplete_frm = NULL;
		pkt = tmppkt;
		tmppkt = NULL;

		/* Verify that length is correct */
		err = EPROTO;
		if (rfml->pdu_size != cfpkt_getlen(pkt) - RFM_HEAD_SIZE + 1)
			goto out;
	}

	err = rfml->serv.layer.up->receive(rfml->serv.layer.up, pkt);

out:

	if (err != 0) {
		if (tmppkt)
			cfpkt_destroy(tmppkt);
		if (pkt)
			cfpkt_destroy(pkt);
		if (rfml->incomplete_frm)
			cfpkt_destroy(rfml->incomplete_frm);
		rfml->incomplete_frm = NULL;

		pr_info("Connection error %d triggered on RFM link\n", err);

		/* Trigger connection error upon failure.*/
		layr->up->ctrlcmd(layr->up, CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND,
					rfml->serv.dev_info.id);
	}
	spin_unlock(&rfml->sync);
	return err;
}


static int cfrfml_transmit_segment(struct cfrfml *rfml, struct cfpkt *pkt)
{
	caif_assert(cfpkt_getlen(pkt) < rfml->fragment_size);

	/* Add info for MUX-layer to route the packet out. */
	cfpkt_info(pkt)->channel_id = rfml->serv.layer.id;

	/*
	 * To optimize alignment, we add up the size of CAIF header before
	 * payload.
	 */
	cfpkt_info(pkt)->hdr_len = RFM_HEAD_SIZE;
	cfpkt_info(pkt)->dev_info = &rfml->serv.dev_info;

	return rfml->serv.layer.dn->transmit(rfml->serv.layer.dn, pkt);
}

static int cfrfml_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	int err;
	u8 seg;
	u8 head[6];
	struct cfpkt *rearpkt = NULL;
	struct cfpkt *frontpkt = pkt;
	struct cfrfml *rfml = container_obj(layr);

	caif_assert(layr->dn != NULL);
	caif_assert(layr->dn->transmit != NULL);

	if (!cfsrvl_ready(&rfml->serv, &err))
		return err;

	err = -EPROTO;
	if (cfpkt_getlen(pkt) <= RFM_HEAD_SIZE-1)
		goto out;

	err = 0;
	if (cfpkt_getlen(pkt) > rfml->fragment_size + RFM_HEAD_SIZE)
		err = cfpkt_peek_head(pkt, head, 6);

	if (err < 0)
		goto out;

	while (cfpkt_getlen(frontpkt) > rfml->fragment_size + RFM_HEAD_SIZE) {

		seg = 1;
		err = -EPROTO;

		if (cfpkt_add_head(frontpkt, &seg, 1) < 0)
			goto out;
		/*
		 * On OOM error cfpkt_split returns NULL.
		 *
		 * NOTE: Segmented pdu is not correctly aligned.
		 * This has negative performance impact.
		 */

		rearpkt = cfpkt_split(frontpkt, rfml->fragment_size);
		if (rearpkt == NULL)
			goto out;

		err = cfrfml_transmit_segment(rfml, frontpkt);

		if (err != 0)
			goto out;
		frontpkt = rearpkt;
		rearpkt = NULL;

		err = -ENOMEM;
		if (frontpkt == NULL)
			goto out;
		err = -EPROTO;
		if (cfpkt_add_head(frontpkt, head, 6) < 0)
			goto out;

	}

	seg = 0;
	err = -EPROTO;

	if (cfpkt_add_head(frontpkt, &seg, 1) < 0)
		goto out;

	err = cfrfml_transmit_segment(rfml, frontpkt);

	frontpkt = NULL;
out:

	if (err != 0) {
		pr_info("Connection error %d triggered on RFM link\n", err);
		/* Trigger connection error upon failure.*/

		layr->up->ctrlcmd(layr->up, CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND,
					rfml->serv.dev_info.id);

		if (rearpkt)
			cfpkt_destroy(rearpkt);

		if (frontpkt && frontpkt != pkt) {

			cfpkt_destroy(frontpkt);
			/*
			 * Socket layer will free the original packet,
			 * but this packet may already be sent and
			 * freed. So we have to return 0 in this case
			 * to avoid socket layer to re-free this packet.
			 * The return of shutdown indication will
			 * cause connection to be invalidated anyhow.
			 */
			err = 0;
		}
	}

	return err;
}
