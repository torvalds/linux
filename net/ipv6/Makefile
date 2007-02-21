#
# Makefile for the Linux TCP/IP (INET6) layer.
#

obj-$(CONFIG_IPV6) += ipv6.o

ipv6-objs :=	af_inet6.o anycast.o ip6_output.o ip6_input.o addrconf.o \
		route.o ip6_fib.o ipv6_sockglue.o ndisc.o udp.o udplite.o \
		raw.o protocol.o icmp.o mcast.o reassembly.o tcp_ipv6.o \
		exthdrs.o sysctl_net_ipv6.o datagram.o proc.o \
		ip6_flowlabel.o ipv6_syms.o inet6_connection_sock.o

ipv6-$(CONFIG_XFRM) += xfrm6_policy.o xfrm6_state.o xfrm6_input.o \
	xfrm6_output.o
ipv6-$(CONFIG_NETFILTER) += netfilter.o
ipv6-$(CONFIG_IPV6_MULTIPLE_TABLES) += fib6_rules.o
ipv6-$(CONFIG_IPV6_MIP6) += mip6.o

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
obj-$(CONFIG_NETFILTER)	+= netfilter/

obj-$(CONFIG_IPV6_SIT) += sit.o
obj-$(CONFIG_IPV6_TUNNEL) += ip6_tunnel.o

obj-y += addrconf_core.o exthdrs_core.o

obj-$(subst m,y,$(CONFIG_IPV6)) += inet6_hashtables.o
