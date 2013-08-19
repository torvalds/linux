#
# Makefile for the Linux TCP/IP (INET6) layer.
#

obj-$(CONFIG_IPV6) += ipv6.o

ipv6-objs :=	af_inet6.o anycast.o ip6_output.o ip6_input.o addrconf.o \
		addrlabel.o \
		route.o ip6_fib.o ipv6_sockglue.o ndisc.o udp.o udplite.o \
		raw.o icmp.o mcast.o reassembly.o tcp_ipv6.o ping.o \
		exthdrs.o datagram.o ip6_flowlabel.o inet6_connection_sock.o

ipv6-offload :=	ip6_offload.o tcpv6_offload.o udp_offload.o exthdrs_offload.o

ipv6-$(CONFIG_SYSCTL) = sysctl_net_ipv6.o
ipv6-$(CONFIG_IPV6_MROUTE) += ip6mr.o

ipv6-$(CONFIG_XFRM) += xfrm6_policy.o xfrm6_state.o xfrm6_input.o \
	xfrm6_output.o
ipv6-$(CONFIG_NETFILTER) += netfilter.o
ipv6-$(CONFIG_IPV6_MULTIPLE_TABLES) += fib6_rules.o
ipv6-$(CONFIG_PROC_FS) += proc.o
ipv6-$(CONFIG_SYN_COOKIES) += syncookies.o

ipv6-objs += $(ipv6-y)

obj-$(CONFIG_INET6_AH) += ah6.o
obj-$(CONFIG_INET6_ESP) += esp6.o
obj-$(CONFIG_INET6_IPCOMP) += ipcomp6.o
obj-$(CONFIG_INET6_XFRM_TUNNEL) += xfrm6_tunnel.o
obj-$(CONFIG_INET6_TUNNEL) += tunnel6.o
obj-$(CONFIG_INET6_XFRM_MODE_TRANSPORT) += xfrm6_mode_transport.o
obj-$(CONFIG_INET6_XFRM_MODE_TUNNEL) += xfrm6_mode_tunnel.o
obj-$(CONFIG_INET6_XFRM_MODE_ROUTEOPTIMIZATION) += xfrm6_mode_ro.o
obj-$(CONFIG_INET6_XFRM_MODE_BEET) += xfrm6_mode_beet.o
obj-$(CONFIG_IPV6_MIP6) += mip6.o
obj-$(CONFIG_NETFILTER)	+= netfilter/

obj-$(CONFIG_IPV6_VTI) += ip6_vti.o
obj-$(CONFIG_IPV6_SIT) += sit.o
obj-$(CONFIG_IPV6_TUNNEL) += ip6_tunnel.o
obj-$(CONFIG_IPV6_GRE) += ip6_gre.o

obj-y += addrconf_core.o exthdrs_core.o ip6_checksum.o ip6_icmp.o
obj-$(CONFIG_INET) += output_core.o protocol.o $(ipv6-offload)

obj-$(subst m,y,$(CONFIG_IPV6)) += inet6_hashtables.o
