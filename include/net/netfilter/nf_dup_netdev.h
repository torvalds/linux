#ifndef _NF_DUP_NETDEV_H_
#define _NF_DUP_NETDEV_H_

void nf_dup_netdev_egress(const struct nft_pktinfo *pkt, int oif);
void nf_fwd_netdev_egress(const struct nft_pktinfo *pkt, int oif);

#endif
