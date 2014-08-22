netfilter-objs := core.o nf_log.o nf_queue.o nf_sockopt.o

nf_conntrack-y	:= nf_conntrack_core.o nf_conntrack_standalone.o nf_conntrack_expect.o nf_conntrack_helper.o nf_conntrack_proto.o nf_conntrack_l3proto_generic.o nf_conntrack_proto_generic.o nf_conntrack_proto_tcp.o nf_conntrack_proto_udp.o nf_conntrack_extend.o nf_conntrack_acct.o nf_conntrack_seqadj.o
nf_conntrack-$(CONFIG_NF_CONNTRACK_TIMEOUT) += nf_conntrack_timeout.o
nf_conntrack-$(CONFIG_NF_CONNTRACK_TIMESTAMP) += nf_conntrack_timestamp.o
nf_conntrack-$(CONFIG_NF_CONNTRACK_EVENTS) += nf_conntrack_ecache.o
nf_conntrack-$(CONFIG_NF_CONNTRACK_LABELS) += nf_conntrack_labels.o

obj-$(CONFIG_NETFILTER) = netfilter.o

obj-$(CONFIG_NETFILTER_NETLINK) += nfnetlink.o
obj-$(CONFIG_NETFILTER_NETLINK_ACCT) += nfnetlink_acct.o
nfnetlink_queue-y := nfnetlink_queue_core.o
nfnetlink_queue-$(CONFIG_NETFILTER_NETLINK_QUEUE_CT) += nfnetlink_queue_ct.o
obj-$(CONFIG_NETFILTER_NETLINK_QUEUE) += nfnetlink_queue.o
obj-$(CONFIG_NETFILTER_NETLINK_LOG) += nfnetlink_log.o

# connection tracking
obj-$(CONFIG_NF_CONNTRACK) += nf_conntrack.o

# SCTP protocol connection tracking
obj-$(CONFIG_NF_CT_PROTO_DCCP) += nf_conntrack_proto_dccp.o
obj-$(CONFIG_NF_CT_PROTO_GRE) += nf_conntrack_proto_gre.o
obj-$(CONFIG_NF_CT_PROTO_SCTP) += nf_conntrack_proto_sctp.o
obj-$(CONFIG_NF_CT_PROTO_UDPLITE) += nf_conntrack_proto_udplite.o

# netlink interface for nf_conntrack
obj-$(CONFIG_NF_CT_NETLINK) += nf_conntrack_netlink.o
obj-$(CONFIG_NF_CT_NETLINK_TIMEOUT) += nfnetlink_cttimeout.o
obj-$(CONFIG_NF_CT_NETLINK_HELPER) += nfnetlink_cthelper.o

# connection tracking helpers
nf_conntrack_h323-objs := nf_conntrack_h323_main.o nf_conntrack_h323_asn1.o

obj-$(CONFIG_NF_CONNTRACK_AMANDA) += nf_conntrack_amanda.o
obj-$(CONFIG_NF_CONNTRACK_FTP) += nf_conntrack_ftp.o
obj-$(CONFIG_NF_CONNTRACK_H323) += nf_conntrack_h323.o
obj-$(CONFIG_NF_CONNTRACK_IRC) += nf_conntrack_irc.o
obj-$(CONFIG_NF_CONNTRACK_BROADCAST) += nf_conntrack_broadcast.o
obj-$(CONFIG_NF_CONNTRACK_NETBIOS_NS) += nf_conntrack_netbios_ns.o
obj-$(CONFIG_NF_CONNTRACK_SNMP) += nf_conntrack_snmp.o
obj-$(CONFIG_NF_CONNTRACK_PPTP) += nf_conntrack_pptp.o
obj-$(CONFIG_NF_CONNTRACK_SANE) += nf_conntrack_sane.o
obj-$(CONFIG_NF_CONNTRACK_SIP) += nf_conntrack_sip.o
obj-$(CONFIG_NF_CONNTRACK_TFTP) += nf_conntrack_tftp.o

nf_nat-y	:= nf_nat_core.o nf_nat_proto_unknown.o nf_nat_proto_common.o \
		   nf_nat_proto_udp.o nf_nat_proto_tcp.o nf_nat_helper.o

# generic transport layer logging
obj-$(CONFIG_NF_LOG_COMMON) += nf_log_common.o

obj-$(CONFIG_NF_NAT) += nf_nat.o

# NAT protocols (nf_nat)
obj-$(CONFIG_NF_NAT_PROTO_DCCP) += nf_nat_proto_dccp.o
obj-$(CONFIG_NF_NAT_PROTO_UDPLITE) += nf_nat_proto_udplite.o
obj-$(CONFIG_NF_NAT_PROTO_SCTP) += nf_nat_proto_sctp.o

# NAT helpers
obj-$(CONFIG_NF_NAT_AMANDA) += nf_nat_amanda.o
obj-$(CONFIG_NF_NAT_FTP) += nf_nat_ftp.o
obj-$(CONFIG_NF_NAT_IRC) += nf_nat_irc.o
obj-$(CONFIG_NF_NAT_SIP) += nf_nat_sip.o
obj-$(CONFIG_NF_NAT_TFTP) += nf_nat_tftp.o

# SYNPROXY
obj-$(CONFIG_NETFILTER_SYNPROXY) += nf_synproxy_core.o

# nf_tables
nf_tables-objs += nf_tables_core.o nf_tables_api.o
nf_tables-objs += nft_immediate.o nft_cmp.o nft_lookup.o
nf_tables-objs += nft_bitwise.o nft_byteorder.o nft_payload.o

obj-$(CONFIG_NF_TABLES)		+= nf_tables.o
obj-$(CONFIG_NF_TABLES_INET)	+= nf_tables_inet.o
obj-$(CONFIG_NFT_COMPAT)	+= nft_compat.o
obj-$(CONFIG_NFT_EXTHDR)	+= nft_exthdr.o
obj-$(CONFIG_NFT_META)		+= nft_meta.o
obj-$(CONFIG_NFT_CT)		+= nft_ct.o
obj-$(CONFIG_NFT_LIMIT)		+= nft_limit.o
obj-$(CONFIG_NFT_NAT)		+= nft_nat.o
obj-$(CONFIG_NFT_QUEUE)		+= nft_queue.o
obj-$(CONFIG_NFT_REJECT) 	+= nft_reject.o
obj-$(CONFIG_NFT_REJECT_INET)	+= nft_reject_inet.o
obj-$(CONFIG_NFT_RBTREE)	+= nft_rbtree.o
obj-$(CONFIG_NFT_HASH)		+= nft_hash.o
obj-$(CONFIG_NFT_COUNTER)	+= nft_counter.o
obj-$(CONFIG_NFT_LOG)		+= nft_log.o

# generic X tables 
obj-$(CONFIG_NETFILTER_XTABLES) += x_tables.o xt_tcpudp.o

# combos
obj-$(CONFIG_NETFILTER_XT_MARK) += xt_mark.o
obj-$(CONFIG_NETFILTER_XT_CONNMARK) += xt_connmark.o
obj-$(CONFIG_NETFILTER_XT_SET) += xt_set.o
obj-$(CONFIG_NF_NAT) += xt_nat.o

# targets
obj-$(CONFIG_NETFILTER_XT_TARGET_AUDIT) += xt_AUDIT.o
obj-$(CONFIG_NETFILTER_XT_TARGET_CHECKSUM) += xt_CHECKSUM.o
obj-$(CONFIG_NETFILTER_XT_TARGET_CLASSIFY) += xt_CLASSIFY.o
obj-$(CONFIG_NETFILTER_XT_TARGET_CONNSECMARK) += xt_CONNSECMARK.o
obj-$(CONFIG_NETFILTER_XT_TARGET_CT) += xt_CT.o
obj-$(CONFIG_NETFILTER_XT_TARGET_DSCP) += xt_DSCP.o
obj-$(CONFIG_NETFILTER_XT_TARGET_HL) += xt_HL.o
obj-$(CONFIG_NETFILTER_XT_TARGET_HMARK) += xt_HMARK.o
obj-$(CONFIG_NETFILTER_XT_TARGET_LED) += xt_LED.o
obj-$(CONFIG_NETFILTER_XT_TARGET_LOG) += xt_LOG.o
obj-$(CONFIG_NETFILTER_XT_TARGET_NETMAP) += xt_NETMAP.o
obj-$(CONFIG_NETFILTER_XT_TARGET_NFLOG) += xt_NFLOG.o
obj-$(CONFIG_NETFILTER_XT_TARGET_NFQUEUE) += xt_NFQUEUE.o
obj-$(CONFIG_NETFILTER_XT_TARGET_RATEEST) += xt_RATEEST.o
obj-$(CONFIG_NETFILTER_XT_TARGET_REDIRECT) += xt_REDIRECT.o
obj-$(CONFIG_NETFILTER_XT_TARGET_SECMARK) += xt_SECMARK.o
obj-$(CONFIG_NETFILTER_XT_TARGET_TPROXY) += xt_TPROXY.o
obj-$(CONFIG_NETFILTER_XT_TARGET_TCPMSS) += xt_TCPMSS.o
obj-$(CONFIG_NETFILTER_XT_TARGET_TCPOPTSTRIP) += xt_TCPOPTSTRIP.o
obj-$(CONFIG_NETFILTER_XT_TARGET_TEE) += xt_TEE.o
obj-$(CONFIG_NETFILTER_XT_TARGET_TRACE) += xt_TRACE.o
obj-$(CONFIG_NETFILTER_XT_TARGET_IDLETIMER) += xt_IDLETIMER.o

# matches
obj-$(CONFIG_NETFILTER_XT_MATCH_ADDRTYPE) += xt_addrtype.o
obj-$(CONFIG_NETFILTER_XT_MATCH_BPF) += xt_bpf.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CLUSTER) += xt_cluster.o
obj-$(CONFIG_NETFILTER_XT_MATCH_COMMENT) += xt_comment.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CONNBYTES) += xt_connbytes.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CONNLABEL) += xt_connlabel.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CONNLIMIT) += xt_connlimit.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CONNTRACK) += xt_conntrack.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CPU) += xt_cpu.o
obj-$(CONFIG_NETFILTER_XT_MATCH_DCCP) += xt_dccp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_DEVGROUP) += xt_devgroup.o
obj-$(CONFIG_NETFILTER_XT_MATCH_DSCP) += xt_dscp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_ECN) += xt_ecn.o
obj-$(CONFIG_NETFILTER_XT_MATCH_ESP) += xt_esp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_HASHLIMIT) += xt_hashlimit.o
obj-$(CONFIG_NETFILTER_XT_MATCH_HELPER) += xt_helper.o
obj-$(CONFIG_NETFILTER_XT_MATCH_HL) += xt_hl.o
obj-$(CONFIG_NETFILTER_XT_MATCH_IPCOMP) += xt_ipcomp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_IPRANGE) += xt_iprange.o
obj-$(CONFIG_NETFILTER_XT_MATCH_IPVS) += xt_ipvs.o
obj-$(CONFIG_NETFILTER_XT_MATCH_L2TP) += xt_l2tp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_LENGTH) += xt_length.o
obj-$(CONFIG_NETFILTER_XT_MATCH_LIMIT) += xt_limit.o
obj-$(CONFIG_NETFILTER_XT_MATCH_MAC) += xt_mac.o
obj-$(CONFIG_NETFILTER_XT_MATCH_MULTIPORT) += xt_multiport.o
obj-$(CONFIG_NETFILTER_XT_MATCH_NFACCT) += xt_nfacct.o
obj-$(CONFIG_NETFILTER_XT_MATCH_OSF) += xt_osf.o
obj-$(CONFIG_NETFILTER_XT_MATCH_OWNER) += xt_owner.o
obj-$(CONFIG_NETFILTER_XT_MATCH_CGROUP) += xt_cgroup.o
obj-$(CONFIG_NETFILTER_XT_MATCH_PHYSDEV) += xt_physdev.o
obj-$(CONFIG_NETFILTER_XT_MATCH_PKTTYPE) += xt_pkttype.o
obj-$(CONFIG_NETFILTER_XT_MATCH_POLICY) += xt_policy.o
obj-$(CONFIG_NETFILTER_XT_MATCH_QUOTA) += xt_quota.o
obj-$(CONFIG_NETFILTER_XT_MATCH_RATEEST) += xt_rateest.o
obj-$(CONFIG_NETFILTER_XT_MATCH_REALM) += xt_realm.o
obj-$(CONFIG_NETFILTER_XT_MATCH_RECENT) += xt_recent.o
obj-$(CONFIG_NETFILTER_XT_MATCH_SCTP) += xt_sctp.o
obj-$(CONFIG_NETFILTER_XT_MATCH_SOCKET) += xt_socket.o
obj-$(CONFIG_NETFILTER_XT_MATCH_STATE) += xt_state.o
obj-$(CONFIG_NETFILTER_XT_MATCH_STATISTIC) += xt_statistic.o
obj-$(CONFIG_NETFILTER_XT_MATCH_STRING) += xt_string.o
obj-$(CONFIG_NETFILTER_XT_MATCH_TCPMSS) += xt_tcpmss.o
obj-$(CONFIG_NETFILTER_XT_MATCH_TIME) += xt_time.o
obj-$(CONFIG_NETFILTER_XT_MATCH_U32) += xt_u32.o

# ipset
obj-$(CONFIG_IP_SET) += ipset/

# IPVS
obj-$(CONFIG_IP_VS) += ipvs/
