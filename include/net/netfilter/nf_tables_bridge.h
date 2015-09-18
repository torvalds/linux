#ifndef _NET_NF_TABLES_BRIDGE_H
#define _NET_NF_TABLES_BRIDGE_H

int nft_bridge_iphdr_validate(struct sk_buff *skb);
int nft_bridge_ip6hdr_validate(struct sk_buff *skb);

#endif /* _NET_NF_TABLES_BRIDGE_H */
