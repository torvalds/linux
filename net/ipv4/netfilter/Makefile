#
# Makefile for the netfilter modules on top of IPv4.
#

# objects for the standalone - connection tracking / NAT
ip_conntrack-objs	:= ip_conntrack_standalone.o ip_conntrack_core.o ip_conntrack_proto_generic.o ip_conntrack_proto_tcp.o ip_conntrack_proto_udp.o ip_conntrack_proto_icmp.o
ip_nat-objs	:= ip_nat_core.o ip_nat_helper.o ip_nat_proto_unknown.o ip_nat_proto_tcp.o ip_nat_proto_udp.o ip_nat_proto_icmp.o
iptable_nat-objs	:= ip_nat_rule.o ip_nat_standalone.o

ip_conntrack_pptp-objs	:= ip_conntrack_helper_pptp.o ip_conntrack_proto_gre.o
ip_nat_pptp-objs	:= ip_nat_helper_pptp.o ip_nat_proto_gre.o

# connection tracking
obj-$(CONFIG_IP_NF_CONNTRACK) += ip_conntrack.o
obj-$(CONFIG_IP_NF_NAT) += ip_nat.o

# conntrack netlink interface
obj-$(CONFIG_IP_NF_CONNTRACK_NETLINK) += ip_conntrack_netlink.o


# SCTP protocol connection tracking
obj-$(CONFIG_IP_NF_CT_PROTO_SCTP) += ip_conntrack_proto_sctp.o

# connection tracking helpers
obj-$(CONFIG_IP_NF_PPTP) += ip_conntrack_pptp.o
obj-$(CONFIG_IP_NF_AMANDA) += ip_conntrack_amanda.o
obj-$(CONFIG_IP_NF_TFTP) += ip_conntrack_tftp.o
obj-$(CONFIG_IP_NF_FTP) += ip_conntrack_ftp.o
obj-$(CONFIG_IP_NF_IRC) += ip_conntrack_irc.o
obj-$(CONFIG_IP_NF_NETBIOS_NS) += ip_conntrack_netbios_ns.o

# NAT helpers 
obj-$(CONFIG_IP_NF_NAT_PPTP) += ip_nat_pptp.o
obj-$(CONFIG_IP_NF_NAT_AMANDA) += ip_nat_amanda.o
obj-$(CONFIG_IP_NF_NAT_TFTP) += ip_nat_tftp.o
obj-$(CONFIG_IP_NF_NAT_FTP) += ip_nat_ftp.o
obj-$(CONFIG_IP_NF_NAT_IRC) += ip_nat_irc.o

# generic IP tables 
obj-$(CONFIG_IP_NF_IPTABLES) += ip_tables.o

# the three instances of ip_tables
obj-$(CONFIG_IP_NF_FILTER) += iptable_filter.o
obj-$(CONFIG_IP_NF_MANGLE) += iptable_mangle.o
obj-$(CONFIG_IP_NF_NAT) += iptable_nat.o
obj-$(CONFIG_IP_NF_RAW) += iptable_raw.o

# matches
obj-$(CONFIG_IP_NF_MATCH_HELPER) += ipt_helper.o
obj-$(CONFIG_IP_NF_MATCH_LIMIT) += ipt_limit.o
obj-$(CONFIG_IP_NF_MATCH_HASHLIMIT) += ipt_hashlimit.o
obj-$(CONFIG_IP_NF_MATCH_SCTP) += ipt_sctp.o
obj-$(CONFIG_IP_NF_MATCH_DCCP) += ipt_dccp.o
obj-$(CONFIG_IP_NF_MATCH_MARK) += ipt_mark.o
obj-$(CONFIG_IP_NF_MATCH_MAC) += ipt_mac.o
obj-$(CONFIG_IP_NF_MATCH_IPRANGE) += ipt_iprange.o
obj-$(CONFIG_IP_NF_MATCH_PKTTYPE) += ipt_pkttype.o
obj-$(CONFIG_IP_NF_MATCH_MULTIPORT) += ipt_multiport.o
obj-$(CONFIG_IP_NF_MATCH_OWNER) += ipt_owner.o
obj-$(CONFIG_IP_NF_MATCH_TOS) += ipt_tos.o
obj-$(CONFIG_IP_NF_MATCH_RECENT) += ipt_recent.o
obj-$(CONFIG_IP_NF_MATCH_ECN) += ipt_ecn.o
obj-$(CONFIG_IP_NF_MATCH_DSCP) += ipt_dscp.o
obj-$(CONFIG_IP_NF_MATCH_AH_ESP) += ipt_ah.o ipt_esp.o
obj-$(CONFIG_IP_NF_MATCH_LENGTH) += ipt_length.o
obj-$(CONFIG_IP_NF_MATCH_TTL) += ipt_ttl.o
obj-$(CONFIG_IP_NF_MATCH_STATE) += ipt_state.o
obj-$(CONFIG_IP_NF_MATCH_CONNMARK) += ipt_connmark.o
obj-$(CONFIG_IP_NF_MATCH_CONNTRACK) += ipt_conntrack.o
obj-$(CONFIG_IP_NF_MATCH_CONNBYTES) += ipt_connbytes.o
obj-$(CONFIG_IP_NF_MATCH_TCPMSS) += ipt_tcpmss.o
obj-$(CONFIG_IP_NF_MATCH_REALM) += ipt_realm.o
obj-$(CONFIG_IP_NF_MATCH_ADDRTYPE) += ipt_addrtype.o
obj-$(CONFIG_IP_NF_MATCH_PHYSDEV) += ipt_physdev.o
obj-$(CONFIG_IP_NF_MATCH_COMMENT) += ipt_comment.o
obj-$(CONFIG_IP_NF_MATCH_STRING) += ipt_string.o

# targets
obj-$(CONFIG_IP_NF_TARGET_REJECT) += ipt_REJECT.o
obj-$(CONFIG_IP_NF_TARGET_TOS) += ipt_TOS.o
obj-$(CONFIG_IP_NF_TARGET_ECN) += ipt_ECN.o
obj-$(CONFIG_IP_NF_TARGET_DSCP) += ipt_DSCP.o
obj-$(CONFIG_IP_NF_TARGET_MARK) += ipt_MARK.o
obj-$(CONFIG_IP_NF_TARGET_MASQUERADE) += ipt_MASQUERADE.o
obj-$(CONFIG_IP_NF_TARGET_REDIRECT) += ipt_REDIRECT.o
obj-$(CONFIG_IP_NF_TARGET_NETMAP) += ipt_NETMAP.o
obj-$(CONFIG_IP_NF_TARGET_SAME) += ipt_SAME.o
obj-$(CONFIG_IP_NF_TARGET_CLASSIFY) += ipt_CLASSIFY.o
obj-$(CONFIG_IP_NF_NAT_SNMP_BASIC) += ip_nat_snmp_basic.o
obj-$(CONFIG_IP_NF_TARGET_LOG) += ipt_LOG.o
obj-$(CONFIG_IP_NF_TARGET_CONNMARK) += ipt_CONNMARK.o
obj-$(CONFIG_IP_NF_TARGET_ULOG) += ipt_ULOG.o
obj-$(CONFIG_IP_NF_TARGET_TCPMSS) += ipt_TCPMSS.o
obj-$(CONFIG_IP_NF_TARGET_NOTRACK) += ipt_NOTRACK.o
obj-$(CONFIG_IP_NF_TARGET_CLUSTERIP) += ipt_CLUSTERIP.o
obj-$(CONFIG_IP_NF_TARGET_TTL) += ipt_TTL.o
obj-$(CONFIG_IP_NF_TARGET_NFQUEUE) += ipt_NFQUEUE.o

# generic ARP tables
obj-$(CONFIG_IP_NF_ARPTABLES) += arp_tables.o
obj-$(CONFIG_IP_NF_ARP_MANGLE) += arpt_mangle.o

# just filtering instance of ARP tables for now
obj-$(CONFIG_IP_NF_ARPFILTER) += arptable_filter.o

obj-$(CONFIG_IP_NF_QUEUE) += ip_queue.o

# objects for l3 independent conntrack
nf_conntrack_ipv4-objs  :=  nf_conntrack_l3proto_ipv4.o nf_conntrack_proto_icmp.o

# l3 independent conntrack
obj-$(CONFIG_NF_CONNTRACK_IPV4) += nf_conntrack_ipv4.o
