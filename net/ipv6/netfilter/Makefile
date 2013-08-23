#
# Makefile for the netfilter modules on top of IPv6.
#

# Link order matters here.
obj-$(CONFIG_IP6_NF_IPTABLES) += ip6_tables.o
obj-$(CONFIG_IP6_NF_FILTER) += ip6table_filter.o
obj-$(CONFIG_IP6_NF_MANGLE) += ip6table_mangle.o
obj-$(CONFIG_IP6_NF_RAW) += ip6table_raw.o
obj-$(CONFIG_IP6_NF_SECURITY) += ip6table_security.o
obj-$(CONFIG_NF_NAT_IPV6) += ip6table_nat.o

# objects for l3 independent conntrack
nf_conntrack_ipv6-y  :=  nf_conntrack_l3proto_ipv6.o nf_conntrack_proto_icmpv6.o

# l3 independent conntrack
obj-$(CONFIG_NF_CONNTRACK_IPV6) += nf_conntrack_ipv6.o

nf_nat_ipv6-y		:= nf_nat_l3proto_ipv6.o nf_nat_proto_icmpv6.o
obj-$(CONFIG_NF_NAT_IPV6) += nf_nat_ipv6.o

# defrag
nf_defrag_ipv6-y := nf_defrag_ipv6_hooks.o nf_conntrack_reasm.o
obj-$(CONFIG_NF_DEFRAG_IPV6) += nf_defrag_ipv6.o

# matches
obj-$(CONFIG_IP6_NF_MATCH_AH) += ip6t_ah.o
obj-$(CONFIG_IP6_NF_MATCH_EUI64) += ip6t_eui64.o
obj-$(CONFIG_IP6_NF_MATCH_FRAG) += ip6t_frag.o
obj-$(CONFIG_IP6_NF_MATCH_IPV6HEADER) += ip6t_ipv6header.o
obj-$(CONFIG_IP6_NF_MATCH_MH) += ip6t_mh.o
obj-$(CONFIG_IP6_NF_MATCH_OPTS) += ip6t_hbh.o
obj-$(CONFIG_IP6_NF_MATCH_RPFILTER) += ip6t_rpfilter.o
obj-$(CONFIG_IP6_NF_MATCH_RT) += ip6t_rt.o

# targets
obj-$(CONFIG_IP6_NF_TARGET_MASQUERADE) += ip6t_MASQUERADE.o
obj-$(CONFIG_IP6_NF_TARGET_NPT) += ip6t_NPT.o
obj-$(CONFIG_IP6_NF_TARGET_REJECT) += ip6t_REJECT.o
