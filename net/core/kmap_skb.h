#include <linux/highmem.h>

static inline void *kmap_skb_frag(const skb_frag_t *frag)
{
#ifdef CONFIG_HIGHMEM
	BUG_ON(in_irq());

	local_bh_disable();
#endif
	return kmap_atomic(frag->page, KM_SKB_DATA_SOFTIRQ);
}

static inline void kunmap_skb_frag(void *vaddr)
{
	kunmap_atomic(vaddr, KM_SKB_DATA_SOFTIRQ);
#ifdef CONFIG_HIGHMEM
	local_bh_enable();
#endif
}
