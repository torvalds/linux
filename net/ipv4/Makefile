#
# Makefile for the Linux TCP/IP (INET) layer.
#

obj-y     := route.o inetpeer.o protocol.o \
	     ip_input.o ip_fragment.o ip_forward.o ip_options.o \
	     ip_output.o ip_sockglue.o inet_hashtables.o \
	     inet_timewait_sock.o inet_connection_sock.o \
	     tcp.o tcp_input.o tcp_output.o tcp_timer.o tcp_ipv4.o \
	     tcp_minisocks.o tcp_cong.o \
	     datagram.o raw.o udp.o udplite.o \
	     arp.o icmp.o devinet.o af_inet.o  igmp.o \
	     fib_frontend.o fib_semantics.o fib_trie.o \
	     inet_fragment.o ping.o

obj-$(CONFIG_SYSCTL) += sysctl_net_ipv4.o
obj-$(CONFIG_SYSFS) += sysfs_net_ipv4.o
obj-$(CONFIG_PROC_FS) += proc.o
obj-$(CONFIG_IP_MULTIPLE_TABLES) += fib_rules.o
obj-$(CONFIG_IP_MROUTE) += ipmr.o
obj-$(CONFIG_NET_IPIP) += ipip.o
obj-$(CONFIG_NET_IPGRE_DEMUX) += gre.o
obj-$(CONFIG_NET_IPGRE) += ip_gre.o
obj-$(CONFIG_SYN_COOKIES) += syncookies.o
obj-$(CONFIG_INET_AH) += ah4.o
obj-$(CONFIG_INET_ESP) += esp4.o
obj-$(CONFIG_INET_IPCOMP) += ipcomp.o
obj-$(CONFIG_INET_XFRM_TUNNEL) += xfrm4_tunnel.o
obj-$(CONFIG_INET_XFRM_MODE_BEET) += xfrm4_mode_beet.o
obj-$(CONFIG_INET_LRO) += inet_lro.o
obj-$(CONFIG_INET_TUNNEL) += tunnel4.o
obj-$(CONFIG_INET_XFRM_MODE_TRANSPORT) += xfrm4_mode_transport.o
obj-$(CONFIG_INET_XFRM_MODE_TUNNEL) += xfrm4_mode_tunnel.o
obj-$(CONFIG_IP_PNP) += ipconfig.o
obj-$(CONFIG_NETFILTER)	+= netfilter.o netfilter/
obj-$(CONFIG_INET_DIAG) += inet_diag.o 
obj-$(CONFIG_INET_TCP_DIAG) += tcp_diag.o
obj-$(CONFIG_NET_TCPPROBE) += tcp_probe.o
obj-$(CONFIG_TCP_CONG_BIC) += tcp_bic.o
obj-$(CONFIG_TCP_CONG_CUBIC) += tcp_cubic.o
obj-$(CONFIG_TCP_CONG_WESTWOOD) += tcp_westwood.o
obj-$(CONFIG_TCP_CONG_HSTCP) += tcp_highspeed.o
obj-$(CONFIG_TCP_CONG_HYBLA) += tcp_hybla.o
obj-$(CONFIG_TCP_CONG_HTCP) += tcp_htcp.o
obj-$(CONFIG_TCP_CONG_VEGAS) += tcp_vegas.o
obj-$(CONFIG_TCP_CONG_VENO) += tcp_veno.o
obj-$(CONFIG_TCP_CONG_SCALABLE) += tcp_scalable.o
obj-$(CONFIG_TCP_CONG_LP) += tcp_lp.o
obj-$(CONFIG_TCP_CONG_YEAH) += tcp_yeah.o
obj-$(CONFIG_TCP_CONG_ILLINOIS) += tcp_illinois.o
obj-$(CONFIG_NETLABEL) += cipso_ipv4.o

obj-$(CONFIG_XFRM) += xfrm4_policy.o xfrm4_state.o xfrm4_input.o \
		      xfrm4_output.o
