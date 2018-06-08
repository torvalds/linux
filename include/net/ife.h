/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_IFE_H
#define __NET_IFE_H

#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <uapi/linux/ife.h>

#if IS_ENABLED(CONFIG_NET_IFE)

void *ife_encode(struct sk_buff *skb, u16 metalen);
void *ife_decode(struct sk_buff *skb, u16 *metalen);

void *ife_tlv_meta_decode(void *skbdata, const void *ifehdr_end, u16 *attrtype,
			  u16 *dlen, u16 *totlen);
int ife_tlv_meta_encode(void *skbdata, u16 attrtype, u16 dlen,
			const void *dval);

void *ife_tlv_meta_next(void *skbdata);

#else

static inline void *ife_encode(struct sk_buff *skb, u16 metalen)
{
	return NULL;
}

static inline void *ife_decode(struct sk_buff *skb, u16 *metalen)
{
	return NULL;
}

static inline void *ife_tlv_meta_decode(void *skbdata, u16 *attrtype, u16 *dlen,
					u16 *totlen)
{
	return NULL;
}

static inline int ife_tlv_meta_encode(void *skbdata, u16 attrtype, u16 dlen,
			const void *dval)
{
	return 0;
}

static inline void *ife_tlv_meta_next(void *skbdata)
{
	return NULL;
}

#endif

#endif /* __NET_IFE_H */
