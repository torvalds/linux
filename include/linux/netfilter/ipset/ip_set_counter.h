/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _IP_SET_COUNTER_H
#define _IP_SET_COUNTER_H

/* Copyright (C) 2015 Jozsef Kadlecsik <kadlec@netfilter.org> */

#ifdef __KERNEL__

static inline void
ip_set_add_bytes(u64 bytes, struct ip_set_counter *counter)
{
	atomic64_add((long long)bytes, &(counter)->bytes);
}

static inline void
ip_set_add_packets(u64 packets, struct ip_set_counter *counter)
{
	atomic64_add((long long)packets, &(counter)->packets);
}

static inline u64
ip_set_get_bytes(const struct ip_set_counter *counter)
{
	return (u64)atomic64_read(&(counter)->bytes);
}

static inline u64
ip_set_get_packets(const struct ip_set_counter *counter)
{
	return (u64)atomic64_read(&(counter)->packets);
}

static inline bool
ip_set_match_counter(u64 counter, u64 match, u8 op)
{
	switch (op) {
	case IPSET_COUNTER_NONE:
		return true;
	case IPSET_COUNTER_EQ:
		return counter == match;
	case IPSET_COUNTER_NE:
		return counter != match;
	case IPSET_COUNTER_LT:
		return counter < match;
	case IPSET_COUNTER_GT:
		return counter > match;
	}
	return false;
}

static inline void
ip_set_update_counter(struct ip_set_counter *counter,
		      const struct ip_set_ext *ext, u32 flags)
{
	if (ext->packets != ULLONG_MAX &&
	    !(flags & IPSET_FLAG_SKIP_COUNTER_UPDATE)) {
		ip_set_add_bytes(ext->bytes, counter);
		ip_set_add_packets(ext->packets, counter);
	}
}

static inline bool
ip_set_put_counter(struct sk_buff *skb, const struct ip_set_counter *counter)
{
	return nla_put_net64(skb, IPSET_ATTR_BYTES,
			     cpu_to_be64(ip_set_get_bytes(counter)),
			     IPSET_ATTR_PAD) ||
	       nla_put_net64(skb, IPSET_ATTR_PACKETS,
			     cpu_to_be64(ip_set_get_packets(counter)),
			     IPSET_ATTR_PAD);
}

static inline void
ip_set_init_counter(struct ip_set_counter *counter,
		    const struct ip_set_ext *ext)
{
	if (ext->bytes != ULLONG_MAX)
		atomic64_set(&(counter)->bytes, (long long)(ext->bytes));
	if (ext->packets != ULLONG_MAX)
		atomic64_set(&(counter)->packets, (long long)(ext->packets));
}

#endif /* __KERNEL__ */
#endif /* _IP_SET_COUNTER_H */
