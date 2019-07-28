# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux TCP/IP (INET) layer.
#

obj-y     := route.o inetpeer.o protocol.o \
	     ip_input.o ip_fragment.o ip_forward.o ip_options.o \
	     ip_output.o ip_sockglue.o inet_hashtables.o \
	     inet_timewait_sock.o inet_connection_sock.o \
	     tcp.o tcp_input.o tcp_output.o tcp_timer.o tcp_ipv4.o \
	     tcp_minisocks.o tcp_cong.o tcp_metrics.o tcp_fastopen.o \
	     tcp_rate.o tcp_recovery.o tcp_ulp.o \
	     tcp_offload.o datagram.o raw.o udp.o udplite.o \
	     udp_offload.o arp.o icmp.o devinet.o af_inet.o igmp.o \
	     fib_frontend.o fib_semantics.o fib_trie.o fib_notifier.o \
	     inet_fragment.o ping.o ip_tunnel_core.o gre_offload.o \
	     metrics.o netlink.o nexthop.o

obj-$(CONFIG_BPFILTER) += bpfilter/

obj-$(CONFIG_NET_IP_TUNNEL) += ip_tunnel.o
obj-$(CONFIG_SYSCTL) += sysctl_net_ipv4.o
obj-$(CONFIG_PROC_FS) += proc.o
obj-$(CONFIG_IP_MULTIPLE_TABLES) += fib_rules.o
obj-$(CONFIG_IP_MROUTE) += ipmr.o
obj-$(CONFIG_IP_MROUTE_COMMON) += ipmr_base.o
obj-$(CONFIG_NET_IPIP) += ipip.o
gre-y := gre_demux.o
obj-$(CONFIG_NET_FOU) += fou.o
obj-$(CONFIG_NET_IPGRE_DEMUX) += gre.o
obj-$(CONFIG_NET_IPGRE) += ip_gre.o
obj-$(CONFIG_NET_UDP_TUNNEL) += udp_tunnel.o
obj-$(CONFIG_NET_IPVTI) += ip_vti.o
obj-$(CONFIG_SYN_COOKIES) += syncookies.o
obj-$(CONFIG_INET_AH) += ah4.o
obj-$(CONFIG_INET_ESP) += esp4.o
obj-$(CONFIG_INET_ESP_OFFLOAD) += esp4_offload.o
obj-$(CONFIG_INET_IPCOMP) += ipcomp.o
obj-$(CONFIG_INET_XFRM_TUNNEL) += xfrm4_tunnel.o
obj-$(CONFIG_INET_TUNNEL) += tunnel4.o
obj-$(CONFIG_IP_PNP) += ipconfig.o
obj-$(CONFIG_NETFILTER)	+= netfilter.o netfilter/
obj-$(CONFIG_INET_DIAG) += inet_diag.o
obj-$(CONFIG_INET_TCP_DIAG) += tcp_diag.o
obj-$(CONFIG_INET_UDP_DIAG) += udp_diag.o
obj-$(CONFIG_INET_RAW_DIAG) += raw_diag.o
obj-$(CONFIG_TCP_CONG_BBR) += tcp_bbr.o
obj-$(CONFIG_TCP_CONG_BIC) += tcp_bic.o
obj-$(CONFIG_TCP_CONG_CDG) += tcp_cdg.o
obj-$(CONFIG_TCP_CONG_CUBIC) += tcp_cubic.o
obj-$(CONFIG_TCP_CONG_DCTCP) += tcp_dctcp.o
obj-$(CONFIG_TCP_CONG_WESTWOOD) += tcp_westwood.o
obj-$(CONFIG_TCP_CONG_HSTCP) += tcp_highspeed.o
obj-$(CONFIG_TCP_CONG_HYBLA) += tcp_hybla.o
obj-$(CONFIG_TCP_CONG_HTCP) += tcp_htcp.o
obj-$(CONFIG_TCP_CONG_VEGAS) += tcp_vegas.o
obj-$(CONFIG_TCP_CONG_NV) += tcp_nv.o
obj-$(CONFIG_TCP_CONG_VENO) += tcp_veno.o
obj-$(CONFIG_TCP_CONG_SCALABLE) += tcp_scalable.o
obj-$(CONFIG_TCP_CONG_LP) += tcp_lp.o
obj-$(CONFIG_TCP_CONG_YEAH) += tcp_yeah.o
obj-$(CONFIG_TCP_CONG_ILLINOIS) += tcp_illinois.o
obj-$(CONFIG_NET_SOCK_MSG) += tcp_bpf.o
obj-$(CONFIG_NETLABEL) += cipso_ipv4.o

obj-$(CONFIG_XFRM) += xfrm4_policy.o xfrm4_state.o xfrm4_input.o \
		      xfrm4_output.o xfrm4_protocol.o
