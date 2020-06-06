/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _NET_ETHTOOL_BITSET_H
#define _NET_ETHTOOL_BITSET_H

#define ETHNL_MAX_BITSET_SIZE S16_MAX

typedef const char (*const ethnl_string_array_t)[ETH_GSTRING_LEN];

int ethnl_bitset_is_compact(const struct nlattr *bitset, bool *compact);
int ethnl_bitset_size(const unsigned long *val, const unsigned long *mask,
		      unsigned int nbits, ethnl_string_array_t names,
		      bool compact);
int ethnl_bitset32_size(const u32 *val, const u32 *mask, unsigned int nbits,
			ethnl_string_array_t names, bool compact);
int ethnl_put_bitset(struct sk_buff *skb, int attrtype,
		     const unsigned long *val, const unsigned long *mask,
		     unsigned int nbits, ethnl_string_array_t names,
		     bool compact);
int ethnl_put_bitset32(struct sk_buff *skb, int attrtype, const u32 *val,
		       const u32 *mask, unsigned int nbits,
		       ethnl_string_array_t names, bool compact);
int ethnl_update_bitset(unsigned long *bitmap, unsigned int nbits,
			const struct nlattr *attr, ethnl_string_array_t names,
			struct netlink_ext_ack *extack, bool *mod);
int ethnl_update_bitset32(u32 *bitmap, unsigned int nbits,
			  const struct nlattr *attr, ethnl_string_array_t names,
			  struct netlink_ext_ack *extack, bool *mod);

#endif /* _NET_ETHTOOL_BITSET_H */
