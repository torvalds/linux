netfilter-objs := core.o nf_log.o nf_queue.o nf_sockopt.o

obj-$(CONFIG_NETFILTER) = netfilter.o

obj-$(CONFIG_NETFILTER_NETLINK) += nfnetlink.o
obj-$(CONFIG_NETFILTER_NETLINK_QUEUE) += nfnetlink_queue.o
obj-$(CONFIG_NETFILTER_NETLINK_LOG) += nfnetlink_log.o

nf_conntrack-objs	:= nf_conntrack_core.o nf_conntrack_standalone.o nf_conntrack_l3proto_generic.o nf_conntrack_proto_generic.o nf_conntrack_proto_tcp.o nf_conntrack_proto_udp.o

obj-$(CONFIG_NF_CONNTRACK) += nf_conntrack.o
obj-$(CONFIG_NF_CONNTRACK_FTP) += nf_conntrack_ftp.o

# SCTP protocol connection tracking
obj-$(CONFIG_NF_CT_PROTO_SCTP) += nf_conntrack_proto_sctp.o
