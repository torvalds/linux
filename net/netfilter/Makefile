netfilter-objs := core.o nf_log.o nf_queue.o nf_sockopt.o

nf_conntrack-y	:= nf_conntrack_core.o nf_conntrack_standalone.o nf_conntrack_expect.o nf_conntrack_helper.o nf_conntrack_proto.o nf_conntrack_l3proto_generic.o nf_conntrack_proto_generic.o nf_conntrack_proto_tcp.o nf_conntrack_proto_udp.o
nf_conntrack-$(CONFIG_NF_CONNTRACK_EVENTS) += nf_conntrack_ecache.o

obj-$(CONFIG_NETFILTER) = netfilter.o
obj-$(CONFIG_SYSCTL) += nf_sysctl.o

obj-$(CONFIG_NETFILTER_NETLINK) += nfnetlink.o
obj-$(CONFIG_NETFILTER_NETLINK_QUEUE) += nfnetlink_queue.o
obj-$(CONFIG_NETFILTER_NETLINK_LOG) += nfnetlink_log.o

# connection tracking
obj-$(CONFIG_NF_CONNTRACK) += nf_conntrack.o

# SCTP protocol connection tracking
obj-$(CONFIG_NF_CT_PROTO_GRE) += nf_conntrack_proto_gre.o
obj-$(CONFIG_NF_CT_PROTO_SCTP) += nf_conntrack_proto_sctp.o

# netlink interface for nf_conntrack
obj-$(CONFIG_NF_CT_NETLINK) += nf_conntrack_netlink.o

# connection tracking helpers
nf_conntrack_h323-objs := nf_conntrack_h323_main.o nf_conntrack_h323_asn1.o

obj-$(CONFIG_NF_CONNTRACK_AMANDA) += nf_conntrack_amanda.o
obj-$(CONFIG_NF_CONNTRACK_FTP) += nf_conntrack_ftp.o
obj-$(CONFIG_NF_CONNTRACK_H323) += nf_conntrack_h323.o
obj-$(CONFIG_NF_CONNTRACK_IRC) += nf_conntrack_irc.o
obj-$(CONFIG_NF_CONNTRACK_NETBIOS_NS) += nf_conntrack_netbios_ns.o
obj-$(CONFIG_NF_CONNTRACK_PPTP) += nf_conntrack_pptp.o
obj-$(CONFIG_NF_CONNTRACK_SIP) += nf_conntrack_sip.o

# generic X tables 
obj-$(CONFIG_NETFILTER_XTABLES) += x_tables.o xt_tcpudp.o

# targets
obj-$(CONFIG_NETFILTER_XT_TARGET_CLASSIFY) += xt_CLASSIFY.o
obj-$(CONFIG_NETFILTER_XT_TARGET_CONNMARK) += xt_CONNMARK.o
obj-$(CONFIG_NETFILTER_XT_TARGET_DSCP) += xt_DSCP.o
obj-$(CONFIG_NETFILTER_XT_TARGET_MARK) += xt_MARK.o
obj-$(CONFIG_NETFILTER_XT_TARGET_NFQUEUE) += xt_NFQUEUE.o
obj-$(CONFIG_NETFILTER_XT_TARGET_NFLOG) += xt_NFLOG.o
obj-$(CONFIG_NETFILTER_XT_TARGET_NOTRACK) += xt_NOTRACK.o
obj-$(CONFIG_NETFILTER_XT_TARGET_SECMARK) += xt_SECMARK.o
obj-$(CONFIG_NETFILTER_XT_TARGET_CONNSECMARK) += xt_CONNSECMARK.o

# matches
obj-$(CONFIG_NETFILTER_XT_MATCH_COMMENT) += xt_comment.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CONNBYTES) += xt_connbytes.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CONNMARK) += xt_connmark.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CONNTRACK) += xt_conntrack.o
obj-$(CONFIG_NETFILTER_XT_MATCH_DCCP) += xt_dccp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_DSCP) += xt_dscp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_ESP) += xt_esp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_HELPER) += xt_helper.o
obj-$(CONFIG_NETFILTER_XT_MATCH_LENGTH) += xt_length.o
obj-$(CONFIG_NETFILTER_XT_MATCH_LIMIT) += xt_limit.o
obj-$(CONFIG_NETFILTER_XT_MATCH_MAC) += xt_mac.o
obj-$(CONFIG_NETFILTER_XT_MATCH_MARK) += xt_mark.o
obj-$(CONFIG_NETFILTER_XT_MATCH_MULTIPORT) += xt_multiport.o
obj-$(CONFIG_NETFILTER_XT_MATCH_POLICY) += xt_policy.o
obj-$(CONFIG_NETFILTER_XT_MATCH_PKTTYPE) += xt_pkttype.o
obj-$(CONFIG_NETFILTER_XT_MATCH_QUOTA) += xt_quota.o
obj-$(CONFIG_NETFILTER_XT_MATCH_REALM) += xt_realm.o
obj-$(CONFIG_NETFILTER_XT_MATCH_SCTP) += xt_sctp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_STATE) += xt_state.o
obj-$(CONFIG_NETFILTER_XT_MATCH_STATISTIC) += xt_statistic.o
obj-$(CONFIG_NETFILTER_XT_MATCH_STRING) += xt_string.o
obj-$(CONFIG_NETFILTER_XT_MATCH_TCPMSS) += xt_tcpmss.o
obj-$(CONFIG_NETFILTER_XT_MATCH_PHYSDEV) += xt_physdev.o
obj-$(CONFIG_NETFILTER_XT_MATCH_HASHLIMIT) += xt_hashlimit.o
