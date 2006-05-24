/*
 * xfrm_input.c
 *
 * Changes:
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific portion
 * 	
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>

static kmem_cache_t *secpath_cachep __read_mostly;

void __secpath_destroy(struct sec_path *sp)
{
	int i;
	for (i = 0; i < sp->len; i++)
		xfrm_state_put(sp->xvec[i]);
	kmem_cache_free(secpath_cachep, sp);
}
EXPORT_SYMBOL(__secpath_destroy);

struct sec_path *secpath_dup(struct sec_path *src)
{
	struct sec_path *sp;

	sp = kmem_cache_alloc(secpath_cachep, SLAB_ATOMIC);
	if (!sp)
		return NULL;

	sp->len = 0;
	if (src) {
		int i;

		memcpy(sp, src, sizeof(*sp));
		for (i = 0; i < sp->len; i++)
			xfrm_state_hold(sp->xvec[i]);
	}
	atomic_set(&sp->refcnt, 1);
	return sp;
}
EXPORT_SYMBOL(secpath_dup);

/* Fetch spi and seq from ipsec header */

int xfrm_parse_spi(struct sk_buff *skb, u8 nexthdr, u32 *spi, u32 *seq)
{
	int offset, offset_seq;

	switch (nexthdr) {
	case IPPROTO_AH:
		offset = offsetof(struct ip_auth_hdr, spi);
		offset_seq = offsetof(struct ip_auth_hdr, seq_no);
		break;
	case IPPROTO_ESP:
		offset = offsetof(struct ip_esp_hdr, spi);
		offset_seq = offsetof(struct ip_esp_hdr, seq_no);
		break;
	case IPPROTO_COMP:
		if (!pskb_may_pull(skb, sizeof(struct ip_comp_hdr)))
			return -EINVAL;
		*spi = htonl(ntohs(*(u16*)(skb->h.raw + 2)));
		*seq = 0;
		return 0;
	default:
		return 1;
	}

	if (!pskb_may_pull(skb, 16))
		return -EINVAL;

	*spi = *(u32*)(skb->h.raw + offset);
	*seq = *(u32*)(skb->h.raw + offset_seq);
	return 0;
}
EXPORT_SYMBOL(xfrm_parse_spi);

void __init xfrm_input_init(void)
{
	secpath_cachep = kmem_cache_create("secpath_cache",
					   sizeof(struct sec_path),
					   0, SLAB_HWCACHE_ALIGN,
					   NULL, NULL);
	if (!secpath_cachep)
		panic("XFRM: failed to allocate secpath_cache\n");
}
