obj-$(CONFIG_IP_DCCP) += dccp.o dccp_ipv4.o

dccp-y := ccid.o feat.o input.o minisocks.o options.o output.o proto.o timer.o \
	  qpolicy.o
#
# CCID algorithms to be used by dccp.ko
#
# CCID-2 is default (RFC 4340, p. 77) and has Ack Vectors as dependency
dccp-y += ccids/ccid2.o ackvec.o
dccp-$(CONFIG_IP_DCCP_CCID3)	+= ccids/ccid3.o
dccp-$(CONFIG_IP_DCCP_TFRC_LIB) += ccids/lib/tfrc.o		\
				   ccids/lib/tfrc_equation.o	\
				   ccids/lib/packet_history.o	\
				   ccids/lib/loss_interval.o

dccp_ipv4-y := ipv4.o

# build dccp_ipv6 as module whenever either IPv6 or DCCP is a module
obj-$(subst y,$(CONFIG_IP_DCCP),$(CONFIG_IPV6)) += dccp_ipv6.o
dccp_ipv6-y := ipv6.o

obj-$(CONFIG_INET_DCCP_DIAG) += dccp_diag.o
obj-$(CONFIG_NET_DCCPPROBE) += dccp_probe.o

dccp-$(CONFIG_SYSCTL) += sysctl.o

dccp_diag-y := diag.o
dccp_probe-y := probe.o
