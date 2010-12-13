#ifndef _NET_DST_OPS_H
#define _NET_DST_OPS_H
#include <linux/types.h>
#include <linux/percpu_counter.h>
#include <linux/cache.h>

struct dst_entry;
struct kmem_cachep;
struct net_device;
struct sk_buff;

struct dst_ops {
	unsigned short		family;
	__be16			protocol;
	unsigned		gc_thresh;

	int			(*gc)(struct dst_ops *ops);
	struct dst_entry *	(*check)(struct dst_entry *, __u32 cookie);
	unsigned int		(*default_advmss)(const struct dst_entry *);
	void			(*destroy)(struct dst_entry *);
	void			(*ifdown)(struct dst_entry *,
					  struct net_device *dev, int how);
	struct dst_entry *	(*negative_advice)(struct dst_entry *);
	void			(*link_failure)(struct sk_buff *);
	void			(*update_pmtu)(struct dst_entry *dst, u32 mtu);
	int			(*local_out)(struct sk_buff *skb);

	struct kmem_cache	*kmem_cachep;

	struct percpu_counter	pcpuc_entries ____cacheline_aligned_in_smp;
};

static inline int dst_entries_get_fast(struct dst_ops *dst)
{
	return percpu_counter_read_positive(&dst->pcpuc_entries);
}

static inline int dst_entries_get_slow(struct dst_ops *dst)
{
	int res;

	local_bh_disable();
	res = percpu_counter_sum_positive(&dst->pcpuc_entries);
	local_bh_enable();
	return res;
}

static inline void dst_entries_add(struct dst_ops *dst, int val)
{
	local_bh_disable();
	percpu_counter_add(&dst->pcpuc_entries, val);
	local_bh_enable();
}

static inline int dst_entries_init(struct dst_ops *dst)
{
	return percpu_counter_init(&dst->pcpuc_entries, 0);
}

static inline void dst_entries_destroy(struct dst_ops *dst)
{
	percpu_counter_destroy(&dst->pcpuc_entries);
}

#endif
