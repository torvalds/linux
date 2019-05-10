# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the netfilter modules on top of IPv6.
#

# Link order matters here.
obj-$(CONFIG_IP6_NF_IPTABLES) += ip6_tables.o
obj-$(CONFIG_IP6_NF_FILTER) += ip6table_filter.o
obj-$(CONFIG_IP6_NF_MANGLE) += ip6table_mangle.o
obj-$(CONFIG_IP6_NF_RAW) += ip6table_raw.o
obj-$(CONFIG_IP6_NF_SECURITY) += ip6table_security.o
obj-$(CONFIG_IP6_NF_NAT) += ip6table_nat.o

# defrag
nf_defrag_ipv6-y := nf_defrag_ipv6_hooks.o nf_conntrack_reasm.o
obj-$(CONFIG_NF_DEFRAG_IPV6) += nf_defrag_ipv6.o

obj-$(CONFIG_NF_SOCKET_IPV6) += nf_socket_ipv6.o
obj-$(CONFIG_NF_TPROXY_IPV6) += nf_tproxy_ipv6.o

# logging
obj-$(CONFIG_NF_LOG_IPV6) += nf_log_ipv6.o

# reject
obj-$(CONFIG_NF_REJECT_IPV6) += nf_reject_ipv6.o

obj-$(CONFIG_NF_DUP_IPV6) += nf_dup_ipv6.o

# nf_tables
obj-$(CONFIG_NFT_CHAIN_ROUTE_IPV6) += nft_chain_route_ipv6.o
obj-$(CONFIG_NFT_REJECT_IPV6) += nft_reject_ipv6.o
obj-$(CONFIG_NFT_DUP_IPV6) += nft_dup_ipv6.o
obj-$(CONFIG_NFT_FIB_IPV6) += nft_fib_ipv6.o

# flow table support
obj-$(CONFIG_NF_FLOW_TABLE_IPV6) += nf_flow_table_ipv6.o

# matches
obj-$(CONFIG_IP6_NF_MATCH_AH) += ip6t_ah.o
obj-$(CONFIG_IP6_NF_MATCH_EUI64) += ip6t_eui64.o
obj-$(CONFIG_IP6_NF_MATCH_FRAG) += ip6t_frag.o
obj-$(CONFIG_IP6_NF_MATCH_IPV6HEADER) += ip6t_ipv6header.o
obj-$(CONFIG_IP6_NF_MATCH_MH) += ip6t_mh.o
obj-$(CONFIG_IP6_NF_MATCH_OPTS) += ip6t_hbh.o
obj-$(CONFIG_IP6_NF_MATCH_RPFILTER) += ip6t_rpfilter.o
obj-$(CONFIG_IP6_NF_MATCH_RT) += ip6t_rt.o
obj-$(CONFIG_IP6_NF_MATCH_SRH) += ip6t_srh.o

# targets
obj-$(CONFIG_IP6_NF_TARGET_MASQUERADE) += ip6t_MASQUERADE.o
obj-$(CONFIG_IP6_NF_TARGET_NPT) += ip6t_NPT.o
obj-$(CONFIG_IP6_NF_TARGET_REJECT) += ip6t_REJECT.o
obj-$(CONFIG_IP6_NF_TARGET_SYNPROXY) += ip6t_SYNPROXY.o
