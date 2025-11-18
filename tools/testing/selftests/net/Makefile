# SPDX-License-Identifier: GPL-2.0
# Makefile for net selftests

CFLAGS += -Wall -Wl,--no-as-needed -O2 -g
CFLAGS += -I../../../../usr/include/ $(KHDR_INCLUDES)
# Additional include paths needed by kselftest.h
CFLAGS += -I../

TEST_PROGS := \
	altnames.sh \
	amt.sh \
	arp_ndisc_evict_nocarrier.sh \
	arp_ndisc_untracked_subnets.sh \
	bareudp.sh \
	big_tcp.sh \
	bind_bhash.sh \
	bpf_offload.py \
	broadcast_ether_dst.sh \
	broadcast_pmtu.sh \
	busy_poll_test.sh \
	cmsg_ip.sh \
	cmsg_so_mark.sh \
	cmsg_so_priority.sh \
	cmsg_time.sh \
	drop_monitor_tests.sh \
	fcnal-ipv4.sh \
	fcnal-ipv6.sh \
	fcnal-other.sh \
	fdb_flush.sh \
	fdb_notify.sh \
	fib-onlink-tests.sh \
	fib_nexthop_multiprefix.sh \
	fib_nexthop_nongw.sh \
	fib_nexthops.sh \
	fib_rule_tests.sh \
	fib_tests.sh \
	fin_ack_lat.sh \
	fq_band_pktlimit.sh \
	gre_gso.sh \
	gre_ipv6_lladdr.sh \
	gro.sh \
	icmp.sh \
	icmp_redirect.sh \
	io_uring_zerocopy_tx.sh \
	ioam6.sh \
	ip6_gre_headroom.sh \
	ip_defrag.sh \
	ip_local_port_range.sh \
	ipv6_flowlabel.sh \
	ipv6_force_forwarding.sh \
	ipv6_route_update_soft_lockup.sh \
	l2_tos_ttl_inherit.sh \
	l2tp.sh \
	link_netns.py \
	lwt_dst_cache_ref_loop.sh \
	msg_zerocopy.sh \
	nat6to4.sh \
	ndisc_unsolicited_na_test.sh \
	netdev-l2addr.sh \
	netdevice.sh \
	netns-name.sh \
	netns-sysctl.sh \
	nl_netdev.py \
	pmtu.sh \
	psock_snd.sh \
	reuseaddr_ports_exhausted.sh \
	reuseport_addr_any.sh \
	route_hint.sh \
	route_localnet.sh \
	rps_default_mask.sh \
	rtnetlink.py \
	rtnetlink.sh \
	rtnetlink_notification.sh \
	run_afpackettests \
	run_netsocktests \
	rxtimestamp.sh \
	sctp_vrf.sh \
	skf_net_off.sh \
	so_txtime.sh \
	srv6_end_dt46_l3vpn_test.sh \
	srv6_end_dt4_l3vpn_test.sh \
	srv6_end_dt6_l3vpn_test.sh \
	srv6_end_dx4_netfilter_test.sh \
	srv6_end_dx6_netfilter_test.sh \
	srv6_end_flavors_test.sh \
	srv6_end_next_csid_l3vpn_test.sh \
	srv6_end_x_next_csid_l3vpn_test.sh \
	srv6_hencap_red_l3vpn_test.sh \
	srv6_hl2encap_red_l2vpn_test.sh \
	stress_reuseport_listen.sh \
	tcp_fastopen_backup_key.sh \
	test_bpf.sh \
	test_bridge_backup_port.sh \
	test_bridge_neigh_suppress.sh \
	test_ingress_egress_chaining.sh \
	test_neigh.sh \
	test_so_rcv.sh \
	test_vxlan_fdb_changelink.sh \
	test_vxlan_mdb.sh \
	test_vxlan_nh.sh \
	test_vxlan_nolocalbypass.sh \
	test_vxlan_under_vrf.sh \
	test_vxlan_vnifiltering.sh \
	tfo_passive.sh \
	traceroute.sh \
	txtimestamp.sh \
	udpgro.sh \
	udpgro_bench.sh \
	udpgro_frglist.sh \
	udpgro_fwd.sh \
	udpgso.sh \
	udpgso_bench.sh \
	unicast_extensions.sh \
	veth.sh \
	vlan_bridge_binding.sh \
	vlan_hw_filter.sh \
	vrf-xfrm-tests.sh \
	vrf_route_leaking.sh \
	vrf_strict_mode_test.sh \
	xfrm_policy.sh \
# end of TEST_PROGS

TEST_PROGS_EXTENDED := \
	toeplitz.sh \
	toeplitz_client.sh \
	xfrm_policy_add_speed.sh \
# end of TEST_PROGS_EXTENDED

TEST_GEN_FILES := \
	bind_bhash \
	cmsg_sender \
	fin_ack_lat \
	gro \
	hwtstamp_config \
	io_uring_zerocopy_tx \
	ioam6_parser \
	ip_defrag \
	ip_local_port_range \
	ipsec \
	ipv6_flowlabel \
	ipv6_flowlabel_mgr \
	msg_zerocopy \
	nettest \
	psock_fanout \
	psock_snd \
	psock_tpacket \
	reuseaddr_ports_exhausted \
	reuseport_addr_any \
	rxtimestamp \
	sctp_hello \
	skf_net_off \
	so_netns_cookie \
	so_rcv_listener \
	so_txtime \
	socket \
	stress_reuseport_listen \
	tcp_fastopen_backup_key \
	tcp_inq \
	tcp_mmap \
	tfo \
	timestamping \
	toeplitz \
	txring_overwrite \
	txtimestamp \
	udpgso \
	udpgso_bench_rx \
	udpgso_bench_tx \
# end of TEST_GEN_FILES

TEST_GEN_PROGS := \
	bind_timewait \
	bind_wildcard \
	epoll_busy_poll \
	ipv6_fragmentation \
	proc_net_pktgen \
	reuseaddr_conflict \
	reuseport_bpf \
	reuseport_bpf_cpu \
	reuseport_bpf_numa \
	reuseport_dualstack \
	sk_bind_sendto_listen \
	sk_connect_zero_addr \
	sk_so_peek_off \
	so_incoming_cpu \
	tap \
	tcp_port_share \
	tls \
	tun \
# end of TEST_GEN_PROGS

TEST_FILES := \
	fcnal-test.sh \
	in_netns.sh \
	lib.sh \
	settings \
	setup_loopback.sh \
	setup_veth.sh \
# end of TEST_FILES

# YNL files, must be before "include ..lib.mk"
YNL_GEN_FILES := busy_poller
YNL_GEN_PROGS := netlink-dumps
TEST_GEN_FILES += $(YNL_GEN_FILES)
TEST_GEN_PROGS += $(YNL_GEN_PROGS)

TEST_GEN_FILES += $(patsubst %.c,%.o,$(wildcard *.bpf.c))

TEST_INCLUDES := forwarding/lib.sh

include ../lib.mk

# YNL build
YNL_GENS := netdev
include ynl.mk

$(OUTPUT)/epoll_busy_poll: LDLIBS += -lcap
$(OUTPUT)/reuseport_bpf_numa: LDLIBS += -lnuma
$(OUTPUT)/tcp_mmap: LDLIBS += -lpthread -lcrypto
$(OUTPUT)/tcp_inq: LDLIBS += -lpthread
$(OUTPUT)/bind_bhash: LDLIBS += -lpthread
$(OUTPUT)/io_uring_zerocopy_tx: CFLAGS += -I../../../include/

include bpf.mk
