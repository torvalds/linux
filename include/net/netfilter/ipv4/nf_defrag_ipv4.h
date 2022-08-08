/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_DEFRAG_IPV4_H
#define _NF_DEFRAG_IPV4_H

struct net;
int nf_defrag_ipv4_enable(struct net *net);
void nf_defrag_ipv4_disable(struct net *net);

#endif /* _NF_DEFRAG_IPV4_H */
