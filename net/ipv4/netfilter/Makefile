#
# Makefile for the netfilter modules on top of IPv4.
#

# objects for l3 independent conntrack
nf_conntrack_ipv4-y	:=  nf_conntrack_l3proto_ipv4.o nf_conntrack_proto_icmp.o
ifeq ($(CONFIG_NF_CONNTRACK_PROC_COMPAT),y)
ifeq ($(CONFIG_PROC_FS),y)
nf_conntrack_ipv4-objs	+= nf_conntrack_l3proto_ipv4_compat.o
endif
endif

# connection tracking
obj-$(CONFIG_NF_CONNTRACK_IPV4) += nf_conntrack_ipv4.o

nf_nat_ipv4-y		:= nf_nat_l3proto_ipv4.o nf_nat_proto_icmp.o
obj-$(CONFIG_NF_NAT_IPV4) += nf_nat_ipv4.o

# defrag
obj-$(CONFIG_NF_DEFRAG_IPV4) += nf_defrag_ipv4.o

# NAT helpers (nf_conntrack)
obj-$(CONFIG_NF_NAT_H323) += nf_nat_h323.o
obj-$(CONFIG_NF_NAT_PPTP) += nf_nat_pptp.o
obj-$(CONFIG_NF_NAT_SNMP_BASIC) += nf_nat_snmp_basic.o

# NAT protocols (nf_nat)
obj-$(CONFIG_NF_NAT_PROTO_GRE) += nf_nat_proto_gre.o

# generic IP tables 
obj-$(CONFIG_IP_NF_IPTABLES) += ip_tables.o

# the three instances of ip_tables
obj-$(CONFIG_IP_NF_FILTER) += iptable_filter.o
obj-$(CONFIG_IP_NF_MANGLE) += iptable_mangle.o
obj-$(CONFIG_NF_NAT_IPV4) += iptable_nat.o
obj-$(CONFIG_IP_NF_RAW) += iptable_raw.o
obj-$(CONFIG_IP_NF_SECURITY) += iptable_security.o

# matches
obj-$(CONFIG_IP_NF_MATCH_AH) += ipt_ah.o
obj-$(CONFIG_IP_NF_MATCH_RPFILTER) += ipt_rpfilter.o

# targets
obj-$(CONFIG_IP_NF_TARGET_CLUSTERIP) += ipt_CLUSTERIP.o
obj-$(CONFIG_IP_NF_TARGET_ECN) += ipt_ECN.o
obj-$(CONFIG_IP_NF_TARGET_MASQUERADE) += ipt_MASQUERADE.o
obj-$(CONFIG_IP_NF_TARGET_NETMAP) += ipt_NETMAP.o
obj-$(CONFIG_IP_NF_TARGET_REDIRECT) += ipt_REDIRECT.o
obj-$(CONFIG_IP_NF_TARGET_REJECT) += ipt_REJECT.o
obj-$(CONFIG_IP_NF_TARGET_ULOG) += ipt_ULOG.o

# generic ARP tables
obj-$(CONFIG_IP_NF_ARPTABLES) += arp_tables.o
obj-$(CONFIG_IP_NF_ARP_MANGLE) += arpt_mangle.o

# just filtering instance of ARP tables for now
obj-$(CONFIG_IP_NF_ARPFILTER) += arptable_filter.o
