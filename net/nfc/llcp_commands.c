// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
 */

#define pr_fmt(fmt) "llcp: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nfc.h>

#include <net/nfc/nfc.h>

#include "nfc.h"
#include "llcp.h"

static const u8 llcp_tlv_length[LLCP_TLV_MAX] = {
	0,
	1, /* VERSION */
	2, /* MIUX */
	2, /* WKS */
	1, /* LTO */
	1, /* RW */
	0, /* SN */
	1, /* OPT */
	0, /* SDREQ */
	2, /* SDRES */

};

static u8 llcp_tlv8(const u8 *tlv, u8 type)
{
	if (tlv[0] != type || tlv[1] != llcp_tlv_length[tlv[0]])
		return 0;

	return tlv[2];
}

static u16 llcp_tlv16(const u8 *tlv, u8 type)
{
	if (tlv[0] != type || tlv[1] != llcp_tlv_length[tlv[0]])
		return 0;

	return be16_to_cpu(*((__be16 *)(tlv + 2)));
}


static u8 llcp_tlv_version(const u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_VERSION);
}

static u16 llcp_tlv_miux(const u8 *tlv)
{
	return llcp_tlv16(tlv, LLCP_TLV_MIUX) & 0x7ff;
}

static u16 llcp_tlv_wks(const u8 *tlv)
{
	return llcp_tlv16(tlv, LLCP_TLV_WKS);
}

static u16 llcp_tlv_lto(const u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_LTO);
}

static u8 llcp_tlv_opt(const u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_OPT);
}

static u8 llcp_tlv_rw(const u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_RW) & 0xf;
}

u8 *nfc_llcp_build_tlv(u8 type, const u8 *value, u8 value_length, u8 *tlv_length)
{
	u8 *tlv, length;

	pr_debug("type %d\n", type);

	if (type >= LLCP_TLV_MAX)
		return NULL;

	length = llcp_tlv_length[type];
	if (length == 0 && value_length == 0)
		return NULL;
	else if (length == 0)
		length = value_length;

	*tlv_length = 2 + length;
	tlv = kzalloc(2 + length, GFP_KERNEL);
	if (tlv == NULL)
		return tlv;

	tlv[0] = type;
	tlv[1] = length;
	memcpy(tlv + 2, value, length);

	return tlv;
}

struct nfc_llcp_sdp_tlv *nfc_llcp_build_sdres_tlv(u8 tid, u8 sap)
{
	struct nfc_llcp_sdp_tlv *sdres;
	u8 value[2];

	sdres = kzalloc(sizeof(struct nfc_llcp_sdp_tlv), GFP_KERNEL);
	if (sdres == NULL)
		return NULL;

	value[0] = tid;
	value[1] = sap;

	sdres->tlv = nfc_llcp_build_tlv(LLCP_TLV_SDRES, value, 2,
					&sdres->tlv_len);
	if (sdres->tlv == NULL) {
		kfree(sdres);
		return NULL;
	}

	sdres->tid = tid;
	sdres->sap = sap;

	INIT_HLIST_NODE(&sdres->node);

	return sdres;
}

struct nfc_llcp_sdp_tlv *nfc_llcp_build_sdreq_tlv(u8 tid, const char *uri,
						  size_t uri_len)
{
	struct nfc_llcp_sdp_tlv *sdreq;

	pr_debug("uri: %s, len: %zu\n", uri, uri_len);

	/* sdreq->tlv_len is u8, takes uri_len, + 3 for header, + 1 for NULL */
	if (WARN_ON_ONCE(uri_len > U8_MAX - 4))
		return NULL;

	sdreq = kzalloc(sizeof(struct nfc_llcp_sdp_tlv), GFP_KERNEL);
	if (sdreq == NULL)
		return NULL;

	sdreq->tlv_len = uri_len + 3;

	if (uri[uri_len - 1] == 0)
		sdreq->tlv_len--;

	sdreq->tlv = kzalloc(sdreq->tlv_len + 1, GFP_KERNEL);
	if (sdreq->tlv == NULL) {
		kfree(sdreq);
		return NULL;
	}

	sdreq->tlv[0] = LLCP_TLV_SDREQ;
	sdreq->tlv[1] = sdreq->tlv_len - 2;
	sdreq->tlv[2] = tid;

	sdreq->tid = tid;
	sdreq->uri = sdreq->tlv + 3;
	memcpy(sdreq->uri, uri, uri_len);

	sdreq->time = jiffies;

	INIT_HLIST_NODE(&sdreq->node);

	return sdreq;
}

void nfc_llcp_free_sdp_tlv(struct nfc_llcp_sdp_tlv *sdp)
{
	kfree(sdp->tlv);
	kfree(sdp);
}

void nfc_llcp_free_sdp_tlv_list(struct hlist_head *head)
{
	struct nfc_llcp_sdp_tlv *sdp;
	struct hlist_node *n;

	hlist_for_each_entry_safe(sdp, n, head, node) {
		hlist_del(&sdp->node);

		nfc_llcp_free_sdp_tlv(sdp);
	}
}

int nfc_llcp_parse_gb_tlv(struct nfc_llcp_local *local,
			  const u8 *tlv_array, u16 tlv_array_len)
{
	const u8 *tlv = tlv_array;
	u8 type, length, offset = 0;

	pr_debug("TLV array length %d\n", tlv_array_len);

	if (local == NULL)
		return -ENODEV;

	while (offset < tlv_array_len) {
		type = tlv[0];
		length = tlv[1];

		pr_debug("type 0x%x length %d\n", type, length);

		switch (type) {
		case LLCP_TLV_VERSION:
			local->remote_version = llcp_tlv_version(tlv);
			break;
		case LLCP_TLV_MIUX:
			local->remote_miu = llcp_tlv_miux(tlv) + 128;
			break;
		case LLCP_TLV_WKS:
			local->remote_wks = llcp_tlv_wks(tlv);
			break;
		case LLCP_TLV_LTO:
			local->remote_lto = llcp_tlv_lto(tlv) * 10;
			break;
		case LLCP_TLV_OPT:
			local->remote_opt = llcp_tlv_opt(tlv);
			break;
		default:
			pr_err("Invalid gt tlv value 0x%x\n", type);
			break;
		}

		offset += length + 2;
		tlv += length + 2;
	}

	pr_debug("version 0x%x miu %d lto %d opt 0x%x wks 0x%x\n",
		 local->remote_version, local->remote_miu,
		 local->remote_lto, local->remote_opt,
		 local->remote_wks);

	return 0;
}

int nfc_llcp_parse_connection_tlv(struct nfc_llcp_sock *sock,
				  const u8 *tlv_array, u16 tlv_array_len)
{
	const u8 *tlv = tlv_array;
	u8 type, length, offset = 0;

	pr_debug("TLV array length %d\n", tlv_array_len);

	if (sock == NULL)
		return -ENOTCONN;

	while (offset < tlv_array_len) {
		type = tlv[0];
		length = tlv[1];

		pr_debug("type 0x%x length %d\n", type, length);

		switch (type) {
		case LLCP_TLV_MIUX:
			sock->remote_miu = llcp_tlv_miux(tlv) + 128;
			break;
		case LLCP_TLV_RW:
			sock->remote_rw = llcp_tlv_rw(tlv);
			break;
		case LLCP_TLV_SN:
			break;
		default:
			pr_err("Invalid gt tlv value 0x%x\n", type);
			break;
		}

		offset += length + 2;
		tlv += length + 2;
	}

	pr_debug("sock %p rw %d miu %d\n", sock,
		 sock->remote_rw, sock->remote_miu);

	return 0;
}

static struct sk_buff *llcp_add_header(struct sk_buff *pdu,
				       u8 dsap, u8 ssap, u8 ptype)
{
	u8 header[2];

	pr_debug("ptype 0x%x dsap 0x%x ssap 0x%x\n", ptype, dsap, ssap);

	header[0] = (u8)((dsap << 2) | (ptype >> 2));
	header[1] = (u8)((ptype << 6) | ssap);

	pr_debug("header 0x%x 0x%x\n", header[0], header[1]);

	skb_put_data(pdu, header, LLCP_HEADER_SIZE);

	return pdu;
}

static struct sk_buff *llcp_add_tlv(struct sk_buff *pdu, const u8 *tlv,
				    u8 tlv_length)
{
	/* XXX Add an skb length check */

	if (tlv == NULL)
		return NULL;

	skb_put_data(pdu, tlv, tlv_length);

	return pdu;
}

static struct sk_buff *llcp_allocate_pdu(struct nfc_llcp_sock *sock,
					 u8 cmd, u16 size)
{
	struct sk_buff *skb;
	int err;

	if (sock->ssap == 0)
		return NULL;

	skb = nfc_alloc_send_skb(sock->dev, &sock->sk, MSG_DONTWAIT,
				 size + LLCP_HEADER_SIZE, &err);
	if (skb == NULL) {
		pr_err("Could not allocate PDU\n");
		return NULL;
	}

	skb = llcp_add_header(skb, sock->dsap, sock->ssap, cmd);

	return skb;
}

int nfc_llcp_send_disconnect(struct nfc_llcp_sock *sock)
{
	struct sk_buff *skb;
	struct nfc_dev *dev;
	struct nfc_llcp_local *local;

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	dev = sock->dev;
	if (dev == NULL)
		return -ENODEV;

	skb = llcp_allocate_pdu(sock, LLCP_PDU_DISC, 0);
	if (skb == NULL)
		return -ENOMEM;

	skb_queue_tail(&local->tx_queue, skb);

	return 0;
}

int nfc_llcp_send_symm(struct nfc_dev *dev)
{
	struct sk_buff *skb;
	struct nfc_llcp_local *local;
	u16 size = 0;
	int err;

	local = nfc_llcp_find_local(dev);
	if (local == NULL)
		return -ENODEV;

	size += LLCP_HEADER_SIZE;
	size += dev->tx_headroom + dev->tx_tailroom + NFC_HEADER_SIZE;

	skb = alloc_skb(size, GFP_KERNEL);
	if (skb == NULL) {
		err = -ENOMEM;
		goto out;
	}

	skb_reserve(skb, dev->tx_headroom + NFC_HEADER_SIZE);

	skb = llcp_add_header(skb, 0, 0, LLCP_PDU_SYMM);

	__net_timestamp(skb);

	nfc_llcp_send_to_raw_sock(local, skb, NFC_DIRECTION_TX);

	err = nfc_data_exchange(dev, local->target_idx, skb,
				 nfc_llcp_recv, local);
out:
	nfc_llcp_local_put(local);
	return err;
}

int nfc_llcp_send_connect(struct nfc_llcp_sock *sock)
{
	struct nfc_llcp_local *local;
	struct sk_buff *skb;
	const u8 *service_name_tlv = NULL;
	const u8 *miux_tlv = NULL;
	const u8 *rw_tlv = NULL;
	u8 service_name_tlv_length, miux_tlv_length,  rw_tlv_length, rw;
	int err;
	u16 size = 0;
	__be16 miux;

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	if (sock->service_name != NULL) {
		service_name_tlv = nfc_llcp_build_tlv(LLCP_TLV_SN,
						      sock->service_name,
						      sock->service_name_len,
						      &service_name_tlv_length);
		if (!service_name_tlv) {
			err = -ENOMEM;
			goto error_tlv;
		}
		size += service_name_tlv_length;
	}

	/* If the socket parameters are not set, use the local ones */
	miux = be16_to_cpu(sock->miux) > LLCP_MAX_MIUX ?
		local->miux : sock->miux;
	rw = sock->rw > LLCP_MAX_RW ? local->rw : sock->rw;

	miux_tlv = nfc_llcp_build_tlv(LLCP_TLV_MIUX, (u8 *)&miux, 0,
				      &miux_tlv_length);
	if (!miux_tlv) {
		err = -ENOMEM;
		goto error_tlv;
	}
	size += miux_tlv_length;

	rw_tlv = nfc_llcp_build_tlv(LLCP_TLV_RW, &rw, 0, &rw_tlv_length);
	if (!rw_tlv) {
		err = -ENOMEM;
		goto error_tlv;
	}
	size += rw_tlv_length;

	pr_debug("SKB size %d SN length %zu\n", size, sock->service_name_len);

	skb = llcp_allocate_pdu(sock, LLCP_PDU_CONNECT, size);
	if (skb == NULL) {
		err = -ENOMEM;
		goto error_tlv;
	}

	llcp_add_tlv(skb, service_name_tlv, service_name_tlv_length);
	llcp_add_tlv(skb, miux_tlv, miux_tlv_length);
	llcp_add_tlv(skb, rw_tlv, rw_tlv_length);

	skb_queue_tail(&local->tx_queue, skb);

	err = 0;

error_tlv:
	if (err)
		pr_err("error %d\n", err);

	kfree(service_name_tlv);
	kfree(miux_tlv);
	kfree(rw_tlv);

	return err;
}

int nfc_llcp_send_cc(struct nfc_llcp_sock *sock)
{
	struct nfc_llcp_local *local;
	struct sk_buff *skb;
	const u8 *miux_tlv = NULL;
	const u8 *rw_tlv = NULL;
	u8 miux_tlv_length, rw_tlv_length, rw;
	int err;
	u16 size = 0;
	__be16 miux;

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	/* If the socket parameters are not set, use the local ones */
	miux = be16_to_cpu(sock->miux) > LLCP_MAX_MIUX ?
		local->miux : sock->miux;
	rw = sock->rw > LLCP_MAX_RW ? local->rw : sock->rw;

	miux_tlv = nfc_llcp_build_tlv(LLCP_TLV_MIUX, (u8 *)&miux, 0,
				      &miux_tlv_length);
	if (!miux_tlv) {
		err = -ENOMEM;
		goto error_tlv;
	}
	size += miux_tlv_length;

	rw_tlv = nfc_llcp_build_tlv(LLCP_TLV_RW, &rw, 0, &rw_tlv_length);
	if (!rw_tlv) {
		err = -ENOMEM;
		goto error_tlv;
	}
	size += rw_tlv_length;

	skb = llcp_allocate_pdu(sock, LLCP_PDU_CC, size);
	if (skb == NULL) {
		err = -ENOMEM;
		goto error_tlv;
	}

	llcp_add_tlv(skb, miux_tlv, miux_tlv_length);
	llcp_add_tlv(skb, rw_tlv, rw_tlv_length);

	skb_queue_tail(&local->tx_queue, skb);

	err = 0;

error_tlv:
	if (err)
		pr_err("error %d\n", err);

	kfree(miux_tlv);
	kfree(rw_tlv);

	return err;
}

static struct sk_buff *nfc_llcp_allocate_snl(struct nfc_llcp_local *local,
					     size_t tlv_length)
{
	struct sk_buff *skb;
	struct nfc_dev *dev;
	u16 size = 0;

	if (local == NULL)
		return ERR_PTR(-ENODEV);

	dev = local->dev;
	if (dev == NULL)
		return ERR_PTR(-ENODEV);

	size += LLCP_HEADER_SIZE;
	size += dev->tx_headroom + dev->tx_tailroom + NFC_HEADER_SIZE;
	size += tlv_length;

	skb = alloc_skb(size, GFP_KERNEL);
	if (skb == NULL)
		return ERR_PTR(-ENOMEM);

	skb_reserve(skb, dev->tx_headroom + NFC_HEADER_SIZE);

	skb = llcp_add_header(skb, LLCP_SAP_SDP, LLCP_SAP_SDP, LLCP_PDU_SNL);

	return skb;
}

int nfc_llcp_send_snl_sdres(struct nfc_llcp_local *local,
			    struct hlist_head *tlv_list, size_t tlvs_len)
{
	struct nfc_llcp_sdp_tlv *sdp;
	struct hlist_node *n;
	struct sk_buff *skb;

	skb = nfc_llcp_allocate_snl(local, tlvs_len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	hlist_for_each_entry_safe(sdp, n, tlv_list, node) {
		skb_put_data(skb, sdp->tlv, sdp->tlv_len);

		hlist_del(&sdp->node);

		nfc_llcp_free_sdp_tlv(sdp);
	}

	skb_queue_tail(&local->tx_queue, skb);

	return 0;
}

int nfc_llcp_send_snl_sdreq(struct nfc_llcp_local *local,
			    struct hlist_head *tlv_list, size_t tlvs_len)
{
	struct nfc_llcp_sdp_tlv *sdreq;
	struct hlist_node *n;
	struct sk_buff *skb;

	skb = nfc_llcp_allocate_snl(local, tlvs_len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mutex_lock(&local->sdreq_lock);

	if (hlist_empty(&local->pending_sdreqs))
		mod_timer(&local->sdreq_timer,
			  jiffies + msecs_to_jiffies(3 * local->remote_lto));

	hlist_for_each_entry_safe(sdreq, n, tlv_list, node) {
		pr_debug("tid %d for %s\n", sdreq->tid, sdreq->uri);

		skb_put_data(skb, sdreq->tlv, sdreq->tlv_len);

		hlist_del(&sdreq->node);

		hlist_add_head(&sdreq->node, &local->pending_sdreqs);
	}

	mutex_unlock(&local->sdreq_lock);

	skb_queue_tail(&local->tx_queue, skb);

	return 0;
}

int nfc_llcp_send_dm(struct nfc_llcp_local *local, u8 ssap, u8 dsap, u8 reason)
{
	struct sk_buff *skb;
	struct nfc_dev *dev;
	u16 size = 1; /* Reason code */

	pr_debug("Sending DM reason 0x%x\n", reason);

	if (local == NULL)
		return -ENODEV;

	dev = local->dev;
	if (dev == NULL)
		return -ENODEV;

	size += LLCP_HEADER_SIZE;
	size += dev->tx_headroom + dev->tx_tailroom + NFC_HEADER_SIZE;

	skb = alloc_skb(size, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	skb_reserve(skb, dev->tx_headroom + NFC_HEADER_SIZE);

	skb = llcp_add_header(skb, dsap, ssap, LLCP_PDU_DM);

	skb_put_data(skb, &reason, 1);

	skb_queue_head(&local->tx_queue, skb);

	return 0;
}

int nfc_llcp_send_i_frame(struct nfc_llcp_sock *sock,
			  struct msghdr *msg, size_t len)
{
	struct sk_buff *pdu;
	struct sock *sk = &sock->sk;
	struct nfc_llcp_local *local;
	size_t frag_len = 0, remaining_len;
	u8 *msg_data, *msg_ptr;
	u16 remote_miu;

	pr_debug("Send I frame len %zd\n", len);

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	/* Remote is ready but has not acknowledged our frames */
	if((sock->remote_ready &&
	    skb_queue_len(&sock->tx_pending_queue) >= sock->remote_rw &&
	    skb_queue_len(&sock->tx_queue) >= 2 * sock->remote_rw)) {
		pr_err("Pending queue is full %d frames\n",
		       skb_queue_len(&sock->tx_pending_queue));
		return -ENOBUFS;
	}

	/* Remote is not ready and we've been queueing enough frames */
	if ((!sock->remote_ready &&
	     skb_queue_len(&sock->tx_queue) >= 2 * sock->remote_rw)) {
		pr_err("Tx queue is full %d frames\n",
		       skb_queue_len(&sock->tx_queue));
		return -ENOBUFS;
	}

	msg_data = kmalloc(len, GFP_USER | __GFP_NOWARN);
	if (msg_data == NULL)
		return -ENOMEM;

	if (memcpy_from_msg(msg_data, msg, len)) {
		kfree(msg_data);
		return -EFAULT;
	}

	remaining_len = len;
	msg_ptr = msg_data;

	do {
		remote_miu = sock->remote_miu > LLCP_MAX_MIU ?
				LLCP_DEFAULT_MIU : sock->remote_miu;

		frag_len = min_t(size_t, remote_miu, remaining_len);

		pr_debug("Fragment %zd bytes remaining %zd",
			 frag_len, remaining_len);

		pdu = llcp_allocate_pdu(sock, LLCP_PDU_I,
					frag_len + LLCP_SEQUENCE_SIZE);
		if (pdu == NULL) {
			kfree(msg_data);
			return -ENOMEM;
		}

		skb_put(pdu, LLCP_SEQUENCE_SIZE);

		if (likely(frag_len > 0))
			skb_put_data(pdu, msg_ptr, frag_len);

		skb_queue_tail(&sock->tx_queue, pdu);

		lock_sock(sk);

		nfc_llcp_queue_i_frames(sock);

		release_sock(sk);

		remaining_len -= frag_len;
		msg_ptr += frag_len;
	} while (remaining_len > 0);

	kfree(msg_data);

	return len;
}

int nfc_llcp_send_ui_frame(struct nfc_llcp_sock *sock, u8 ssap, u8 dsap,
			   struct msghdr *msg, size_t len)
{
	struct sk_buff *pdu;
	struct nfc_llcp_local *local;
	size_t frag_len = 0, remaining_len;
	u8 *msg_ptr, *msg_data;
	u16 remote_miu;
	int err;

	pr_debug("Send UI frame len %zd\n", len);

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	msg_data = kmalloc(len, GFP_USER | __GFP_NOWARN);
	if (msg_data == NULL)
		return -ENOMEM;

	if (memcpy_from_msg(msg_data, msg, len)) {
		kfree(msg_data);
		return -EFAULT;
	}

	remaining_len = len;
	msg_ptr = msg_data;

	do {
		remote_miu = sock->remote_miu > LLCP_MAX_MIU ?
				local->remote_miu : sock->remote_miu;

		frag_len = min_t(size_t, remote_miu, remaining_len);

		pr_debug("Fragment %zd bytes remaining %zd",
			 frag_len, remaining_len);

		pdu = nfc_alloc_send_skb(sock->dev, &sock->sk, 0,
					 frag_len + LLCP_HEADER_SIZE, &err);
		if (pdu == NULL) {
			pr_err("Could not allocate PDU (error=%d)\n", err);
			len -= remaining_len;
			if (len == 0)
				len = err;
			break;
		}

		pdu = llcp_add_header(pdu, dsap, ssap, LLCP_PDU_UI);

		if (likely(frag_len > 0))
			skb_put_data(pdu, msg_ptr, frag_len);

		/* No need to check for the peer RW for UI frames */
		skb_queue_tail(&local->tx_queue, pdu);

		remaining_len -= frag_len;
		msg_ptr += frag_len;
	} while (remaining_len > 0);

	kfree(msg_data);

	return len;
}

int nfc_llcp_send_rr(struct nfc_llcp_sock *sock)
{
	struct sk_buff *skb;
	struct nfc_llcp_local *local;

	pr_debug("Send rr nr %d\n", sock->recv_n);

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	skb = llcp_allocate_pdu(sock, LLCP_PDU_RR, LLCP_SEQUENCE_SIZE);
	if (skb == NULL)
		return -ENOMEM;

	skb_put(skb, LLCP_SEQUENCE_SIZE);

	skb->data[2] = sock->recv_n;

	skb_queue_head(&local->tx_queue, skb);

	return 0;
}
