/*
 * Copyright (C) 2014 Fraunhofer ITWM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Phoebe Buckheister <phoebe.buckheister@itwm.fraunhofer.de>
 */

#include <net/mac802154.h>
#include <net/ieee802154.h>
#include <net/ieee802154_netdev.h>

static int
ieee802154_hdr_push_addr(u8 *buf, const struct ieee802154_addr *addr,
			 bool omit_pan)
{
	int pos = 0;

	if (addr->mode == IEEE802154_ADDR_NONE)
		return 0;

	if (!omit_pan) {
		memcpy(buf + pos, &addr->pan_id, 2);
		pos += 2;
	}

	switch (addr->mode) {
	case IEEE802154_ADDR_SHORT:
		memcpy(buf + pos, &addr->short_addr, 2);
		pos += 2;
		break;

	case IEEE802154_ADDR_LONG:
		memcpy(buf + pos, &addr->extended_addr, IEEE802154_ADDR_LEN);
		pos += IEEE802154_ADDR_LEN;
		break;

	default:
		return -EINVAL;
	}

	return pos;
}

static int
ieee802154_hdr_push_sechdr(u8 *buf, const struct ieee802154_sechdr *hdr)
{
	int pos = 5;

	memcpy(buf, hdr, 1);
	memcpy(buf + 1, &hdr->frame_counter, 4);

	switch (hdr->key_id_mode) {
	case IEEE802154_SCF_KEY_IMPLICIT:
		return pos;

	case IEEE802154_SCF_KEY_INDEX:
		break;

	case IEEE802154_SCF_KEY_SHORT_INDEX:
		memcpy(buf + pos, &hdr->short_src, 4);
		pos += 4;
		break;

	case IEEE802154_SCF_KEY_HW_INDEX:
		memcpy(buf + pos, &hdr->extended_src, IEEE802154_ADDR_LEN);
		pos += IEEE802154_ADDR_LEN;
		break;
	}

	buf[pos++] = hdr->key_id;

	return pos;
}

int
ieee802154_hdr_push(struct sk_buff *skb, const struct ieee802154_hdr *hdr)
{
	u8 buf[MAC802154_FRAME_HARD_HEADER_LEN];
	int pos = 2;
	int rc;
	struct ieee802154_hdr_fc fc = hdr->fc;

	buf[pos++] = hdr->seq;

	fc.dest_addr_mode = hdr->dest.mode;

	rc = ieee802154_hdr_push_addr(buf + pos, &hdr->dest, false);
	if (rc < 0)
		return -EINVAL;
	pos += rc;

	fc.source_addr_mode = hdr->source.mode;

	if (hdr->source.pan_id == hdr->dest.pan_id &&
	    hdr->dest.mode != IEEE802154_ADDR_NONE)
		fc.intra_pan = true;

	rc = ieee802154_hdr_push_addr(buf + pos, &hdr->source, fc.intra_pan);
	if (rc < 0)
		return -EINVAL;
	pos += rc;

	if (fc.security_enabled) {
		fc.version = 1;

		rc = ieee802154_hdr_push_sechdr(buf + pos, &hdr->sec);
		if (rc < 0)
			return -EINVAL;

		pos += rc;
	}

	memcpy(buf, &fc, 2);

	memcpy(skb_push(skb, pos), buf, pos);

	return pos;
}
EXPORT_SYMBOL_GPL(ieee802154_hdr_push);

static int
ieee802154_hdr_get_addr(const u8 *buf, int mode, bool omit_pan,
			struct ieee802154_addr *addr)
{
	int pos = 0;

	addr->mode = mode;

	if (mode == IEEE802154_ADDR_NONE)
		return 0;

	if (!omit_pan) {
		memcpy(&addr->pan_id, buf + pos, 2);
		pos += 2;
	}

	if (mode == IEEE802154_ADDR_SHORT) {
		memcpy(&addr->short_addr, buf + pos, 2);
		return pos + 2;
	} else {
		memcpy(&addr->extended_addr, buf + pos, IEEE802154_ADDR_LEN);
		return pos + IEEE802154_ADDR_LEN;
	}
}

static int ieee802154_hdr_addr_len(int mode, bool omit_pan)
{
	int pan_len = omit_pan ? 0 : 2;

	switch (mode) {
	case IEEE802154_ADDR_NONE: return 0;
	case IEEE802154_ADDR_SHORT: return 2 + pan_len;
	case IEEE802154_ADDR_LONG: return IEEE802154_ADDR_LEN + pan_len;
	default: return -EINVAL;
	}
}

static int
ieee802154_hdr_get_sechdr(const u8 *buf, struct ieee802154_sechdr *hdr)
{
	int pos = 5;

	memcpy(hdr, buf, 1);
	memcpy(&hdr->frame_counter, buf + 1, 4);

	switch (hdr->key_id_mode) {
	case IEEE802154_SCF_KEY_IMPLICIT:
		return pos;

	case IEEE802154_SCF_KEY_INDEX:
		break;

	case IEEE802154_SCF_KEY_SHORT_INDEX:
		memcpy(&hdr->short_src, buf + pos, 4);
		pos += 4;
		break;

	case IEEE802154_SCF_KEY_HW_INDEX:
		memcpy(&hdr->extended_src, buf + pos, IEEE802154_ADDR_LEN);
		pos += IEEE802154_ADDR_LEN;
		break;
	}

	hdr->key_id = buf[pos++];

	return pos;
}

static int ieee802154_hdr_sechdr_len(u8 sc)
{
	switch (IEEE802154_SCF_KEY_ID_MODE(sc)) {
	case IEEE802154_SCF_KEY_IMPLICIT: return 5;
	case IEEE802154_SCF_KEY_INDEX: return 6;
	case IEEE802154_SCF_KEY_SHORT_INDEX: return 10;
	case IEEE802154_SCF_KEY_HW_INDEX: return 14;
	default: return -EINVAL;
	}
}

static int ieee802154_hdr_minlen(const struct ieee802154_hdr *hdr)
{
	int dlen, slen;

	dlen = ieee802154_hdr_addr_len(hdr->fc.dest_addr_mode, false);
	slen = ieee802154_hdr_addr_len(hdr->fc.source_addr_mode,
				       hdr->fc.intra_pan);

	if (slen < 0 || dlen < 0)
		return -EINVAL;

	return 3 + dlen + slen + hdr->fc.security_enabled;
}

static int
ieee802154_hdr_get_addrs(const u8 *buf, struct ieee802154_hdr *hdr)
{
	int pos = 0;

	pos += ieee802154_hdr_get_addr(buf + pos, hdr->fc.dest_addr_mode,
				       false, &hdr->dest);
	pos += ieee802154_hdr_get_addr(buf + pos, hdr->fc.source_addr_mode,
				       hdr->fc.intra_pan, &hdr->source);

	if (hdr->fc.intra_pan)
		hdr->source.pan_id = hdr->dest.pan_id;

	return pos;
}

int
ieee802154_hdr_pull(struct sk_buff *skb, struct ieee802154_hdr *hdr)
{
	int pos = 3, rc;

	if (!pskb_may_pull(skb, 3))
		return -EINVAL;

	memcpy(hdr, skb->data, 3);

	rc = ieee802154_hdr_minlen(hdr);
	if (rc < 0 || !pskb_may_pull(skb, rc))
		return -EINVAL;

	pos += ieee802154_hdr_get_addrs(skb->data + pos, hdr);

	if (hdr->fc.security_enabled) {
		int want = pos + ieee802154_hdr_sechdr_len(skb->data[pos]);

		if (!pskb_may_pull(skb, want))
			return -EINVAL;

		pos += ieee802154_hdr_get_sechdr(skb->data + pos, &hdr->sec);
	}

	skb_pull(skb, pos);
	return pos;
}
EXPORT_SYMBOL_GPL(ieee802154_hdr_pull);

int
ieee802154_hdr_peek_addrs(const struct sk_buff *skb, struct ieee802154_hdr *hdr)
{
	const u8 *buf = skb_mac_header(skb);
	int pos = 3, rc;

	if (buf + 3 > skb_tail_pointer(skb))
		return -EINVAL;

	memcpy(hdr, buf, 3);

	rc = ieee802154_hdr_minlen(hdr);
	if (rc < 0 || buf + rc > skb_tail_pointer(skb))
		return -EINVAL;

	pos += ieee802154_hdr_get_addrs(buf + pos, hdr);
	return pos;
}
EXPORT_SYMBOL_GPL(ieee802154_hdr_peek_addrs);
