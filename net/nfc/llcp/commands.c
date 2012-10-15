/*
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) "llcp: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nfc.h>

#include <net/nfc/nfc.h>

#include "../nfc.h"
#include "llcp.h"

static u8 llcp_tlv_length[LLCP_TLV_MAX] = {
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

static u8 llcp_tlv8(u8 *tlv, u8 type)
{
	if (tlv[0] != type || tlv[1] != llcp_tlv_length[tlv[0]])
		return 0;

	return tlv[2];
}

static u16 llcp_tlv16(u8 *tlv, u8 type)
{
	if (tlv[0] != type || tlv[1] != llcp_tlv_length[tlv[0]])
		return 0;

	return be16_to_cpu(*((__be16 *)(tlv + 2)));
}


static u8 llcp_tlv_version(u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_VERSION);
}

static u16 llcp_tlv_miux(u8 *tlv)
{
	return llcp_tlv16(tlv, LLCP_TLV_MIUX) & 0x7ff;
}

static u16 llcp_tlv_wks(u8 *tlv)
{
	return llcp_tlv16(tlv, LLCP_TLV_WKS);
}

static u16 llcp_tlv_lto(u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_LTO);
}

static u8 llcp_tlv_opt(u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_OPT);
}

static u8 llcp_tlv_rw(u8 *tlv)
{
	return llcp_tlv8(tlv, LLCP_TLV_RW) & 0xf;
}

u8 *nfc_llcp_build_tlv(u8 type, u8 *value, u8 value_length, u8 *tlv_length)
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

int nfc_llcp_parse_gb_tlv(struct nfc_llcp_local *local,
			  u8 *tlv_array, u16 tlv_array_len)
{
	u8 *tlv = tlv_array, type, length, offset = 0;

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
				  u8 *tlv_array, u16 tlv_array_len)
{
	u8 *tlv = tlv_array, type, length, offset = 0;

	pr_debug("TLV array length %d\n", tlv_array_len);

	if (sock == NULL)
		return -ENOTCONN;

	while (offset < tlv_array_len) {
		type = tlv[0];
		length = tlv[1];

		pr_debug("type 0x%x length %d\n", type, length);

		switch (type) {
		case LLCP_TLV_MIUX:
			sock->miu = llcp_tlv_miux(tlv) + 128;
			break;
		case LLCP_TLV_RW:
			sock->rw = llcp_tlv_rw(tlv);
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

	pr_debug("sock %p rw %d miu %d\n", sock, sock->rw, sock->miu);

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

	memcpy(skb_put(pdu, LLCP_HEADER_SIZE), header, LLCP_HEADER_SIZE);

	return pdu;
}

static struct sk_buff *llcp_add_tlv(struct sk_buff *pdu, u8 *tlv,
				    u8 tlv_length)
{
	/* XXX Add an skb length check */

	if (tlv == NULL)
		return NULL;

	memcpy(skb_put(pdu, tlv_length), tlv, tlv_length);

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

int nfc_llcp_disconnect(struct nfc_llcp_sock *sock)
{
	struct sk_buff *skb;
	struct nfc_dev *dev;
	struct nfc_llcp_local *local;
	u16 size = 0;

	pr_debug("Sending DISC\n");

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	dev = sock->dev;
	if (dev == NULL)
		return -ENODEV;

	size += LLCP_HEADER_SIZE;
	size += dev->tx_headroom + dev->tx_tailroom + NFC_HEADER_SIZE;

	skb = alloc_skb(size, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	skb_reserve(skb, dev->tx_headroom + NFC_HEADER_SIZE);

	skb = llcp_add_header(skb, sock->dsap, sock->ssap, LLCP_PDU_DISC);

	skb_queue_tail(&local->tx_queue, skb);

	return 0;
}

int nfc_llcp_send_symm(struct nfc_dev *dev)
{
	struct sk_buff *skb;
	struct nfc_llcp_local *local;
	u16 size = 0;

	pr_debug("Sending SYMM\n");

	local = nfc_llcp_find_local(dev);
	if (local == NULL)
		return -ENODEV;

	size += LLCP_HEADER_SIZE;
	size += dev->tx_headroom + dev->tx_tailroom + NFC_HEADER_SIZE;

	skb = alloc_skb(size, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	skb_reserve(skb, dev->tx_headroom + NFC_HEADER_SIZE);

	skb = llcp_add_header(skb, 0, 0, LLCP_PDU_SYMM);

	nfc_llcp_send_to_raw_sock(local, skb, NFC_LLCP_DIRECTION_TX);

	return nfc_data_exchange(dev, local->target_idx, skb,
				 nfc_llcp_recv, local);
}

int nfc_llcp_send_connect(struct nfc_llcp_sock *sock)
{
	struct nfc_llcp_local *local;
	struct sk_buff *skb;
	u8 *service_name_tlv = NULL, service_name_tlv_length;
	u8 *miux_tlv = NULL, miux_tlv_length;
	u8 *rw_tlv = NULL, rw_tlv_length, rw;
	__be16 miux;
	int err;
	u16 size = 0;

	pr_debug("Sending CONNECT\n");

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	if (sock->service_name != NULL) {
		service_name_tlv = nfc_llcp_build_tlv(LLCP_TLV_SN,
						      sock->service_name,
						      sock->service_name_len,
						      &service_name_tlv_length);
		size += service_name_tlv_length;
	}

	miux = cpu_to_be16(LLCP_MAX_MIUX);
	miux_tlv = nfc_llcp_build_tlv(LLCP_TLV_MIUX, (u8 *)&miux, 0,
				      &miux_tlv_length);
	size += miux_tlv_length;

	rw = LLCP_MAX_RW;
	rw_tlv = nfc_llcp_build_tlv(LLCP_TLV_RW, &rw, 0, &rw_tlv_length);
	size += rw_tlv_length;

	pr_debug("SKB size %d SN length %zu\n", size, sock->service_name_len);

	skb = llcp_allocate_pdu(sock, LLCP_PDU_CONNECT, size);
	if (skb == NULL) {
		err = -ENOMEM;
		goto error_tlv;
	}

	if (service_name_tlv != NULL)
		skb = llcp_add_tlv(skb, service_name_tlv,
				   service_name_tlv_length);

	skb = llcp_add_tlv(skb, miux_tlv, miux_tlv_length);
	skb = llcp_add_tlv(skb, rw_tlv, rw_tlv_length);

	skb_queue_tail(&local->tx_queue, skb);

	return 0;

error_tlv:
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
	u8 *miux_tlv = NULL, miux_tlv_length;
	u8 *rw_tlv = NULL, rw_tlv_length, rw;
	__be16 miux;
	int err;
	u16 size = 0;

	pr_debug("Sending CC\n");

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	miux = cpu_to_be16(LLCP_MAX_MIUX);
	miux_tlv = nfc_llcp_build_tlv(LLCP_TLV_MIUX, (u8 *)&miux, 0,
				      &miux_tlv_length);
	size += miux_tlv_length;

	rw = LLCP_MAX_RW;
	rw_tlv = nfc_llcp_build_tlv(LLCP_TLV_RW, &rw, 0, &rw_tlv_length);
	size += rw_tlv_length;

	skb = llcp_allocate_pdu(sock, LLCP_PDU_CC, size);
	if (skb == NULL) {
		err = -ENOMEM;
		goto error_tlv;
	}

	skb = llcp_add_tlv(skb, miux_tlv, miux_tlv_length);
	skb = llcp_add_tlv(skb, rw_tlv, rw_tlv_length);

	skb_queue_tail(&local->tx_queue, skb);

	return 0;

error_tlv:
	pr_err("error %d\n", err);

	kfree(miux_tlv);
	kfree(rw_tlv);

	return err;
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

	memcpy(skb_put(skb, 1), &reason, 1);

	skb_queue_head(&local->tx_queue, skb);

	return 0;
}

int nfc_llcp_send_disconnect(struct nfc_llcp_sock *sock)
{
	struct sk_buff *skb;
	struct nfc_llcp_local *local;

	pr_debug("Send DISC\n");

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	skb = llcp_allocate_pdu(sock, LLCP_PDU_DISC, 0);
	if (skb == NULL)
		return -ENOMEM;

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

	pr_debug("Send I frame len %zd\n", len);

	local = sock->local;
	if (local == NULL)
		return -ENODEV;

	msg_data = kzalloc(len, GFP_KERNEL);
	if (msg_data == NULL)
		return -ENOMEM;

	if (memcpy_fromiovec(msg_data, msg->msg_iov, len)) {
		kfree(msg_data);
		return -EFAULT;
	}

	remaining_len = len;
	msg_ptr = msg_data;

	while (remaining_len > 0) {

		frag_len = min_t(size_t, sock->miu, remaining_len);

		pr_debug("Fragment %zd bytes remaining %zd",
			 frag_len, remaining_len);

		pdu = llcp_allocate_pdu(sock, LLCP_PDU_I,
					frag_len + LLCP_SEQUENCE_SIZE);
		if (pdu == NULL)
			return -ENOMEM;

		skb_put(pdu, LLCP_SEQUENCE_SIZE);

		memcpy(skb_put(pdu, frag_len), msg_ptr, frag_len);

		skb_queue_tail(&sock->tx_queue, pdu);

		lock_sock(sk);

		nfc_llcp_queue_i_frames(sock);

		release_sock(sk);

		remaining_len -= frag_len;
		msg_ptr += frag_len;
	}

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
