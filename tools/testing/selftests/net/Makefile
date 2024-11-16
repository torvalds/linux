# SPDX-License-Identifier: GPL-2.0
# Makefile for net selftests

CFLAGS +=  -Wall -Wl,--no-as-needed -O2 -g
CFLAGS += -I../../../../usr/include/ $(KHDR_INCLUDES)
# Additional include paths needed by kselftest.h
CFLAGS += -I../

TEST_PROGS := run_netsocktests run_afpackettests test_bpf.sh netdevice.sh \
	      rtnetlink.sh xfrm_policy.sh test_blackhole_dev.sh
TEST_PROGS += fib_tests.sh fib-onlink-tests.sh pmtu.sh udpgso.sh ip_defrag.sh
TEST_PROGS += udpgso_bench.sh fib_rule_tests.sh msg_zerocopy.sh psock_snd.sh
TEST_PROGS += udpgro_bench.sh udpgro.sh test_vxlan_under_vrf.sh reuseport_addr_any.sh
TEST_PROGS += test_vxlan_fdb_changelink.sh so_txtime.sh ipv6_flowlabel.sh
TEST_PROGS += tcp_fastopen_backup_key.sh fcnal-test.sh l2tp.sh traceroute.sh
TEST_PROGS += fin_ack_lat.sh fib_nexthop_multiprefix.sh fib_nexthops.sh fib_nexthop_nongw.sh
TEST_PROGS += altnames.sh icmp.sh icmp_redirect.sh ip6_gre_headroom.sh
TEST_PROGS += route_localnet.sh
TEST_PROGS += reuseaddr_ports_exhausted.sh
TEST_PROGS += txtimestamp.sh
TEST_PROGS += vrf-xfrm-tests.sh
TEST_PROGS += rxtimestamp.sh
TEST_PROGS += drop_monitor_tests.sh
TEST_PROGS += vrf_route_leaking.sh
TEST_PROGS += bareudp.sh
TEST_PROGS += amt.sh
TEST_PROGS += unicast_extensions.sh
TEST_PROGS += udpgro_fwd.sh
TEST_PROGS += udpgro_frglist.sh
TEST_PROGS += veth.sh
TEST_PROGS += ioam6.sh
TEST_PROGS += gro.sh
TEST_PROGS += gre_gso.sh
TEST_PROGS += cmsg_so_mark.sh
TEST_PROGS += cmsg_time.sh cmsg_ipv6.sh
TEST_PROGS += netns-name.sh
TEST_PROGS += nl_netdev.py
TEST_PROGS += srv6_end_dt46_l3vpn_test.sh
TEST_PROGS += srv6_end_dt4_l3vpn_test.sh
TEST_PROGS += srv6_end_dt6_l3vpn_test.sh
TEST_PROGS += srv6_hencap_red_l3vpn_test.sh
TEST_PROGS += srv6_hl2encap_red_l2vpn_test.sh
TEST_PROGS += srv6_end_next_csid_l3vpn_test.sh
TEST_PROGS += srv6_end_x_next_csid_l3vpn_test.sh
TEST_PROGS += srv6_end_flavors_test.sh
TEST_PROGS += srv6_end_dx4_netfilter_test.sh
TEST_PROGS += srv6_end_dx6_netfilter_test.sh
TEST_PROGS += vrf_strict_mode_test.sh
TEST_PROGS += arp_ndisc_evict_nocarrier.sh
TEST_PROGS += ndisc_unsolicited_na_test.sh
TEST_PROGS += arp_ndisc_untracked_subnets.sh
TEST_PROGS += stress_reuseport_listen.sh
TEST_PROGS += l2_tos_ttl_inherit.sh
TEST_PROGS += bind_bhash.sh
TEST_PROGS += ip_local_port_range.sh
TEST_PROGS += rps_default_mask.sh
TEST_PROGS += big_tcp.sh
TEST_PROGS += netns-sysctl.sh
TEST_PROGS_EXTENDED := toeplitz_client.sh toeplitz.sh xfrm_policy_add_speed.sh
TEST_GEN_FILES =  socket nettest
TEST_GEN_FILES += psock_fanout psock_tpacket msg_zerocopy reuseport_addr_any
TEST_GEN_FILES += tcp_mmap tcp_inq psock_snd txring_overwrite
TEST_GEN_FILES += udpgso udpgso_bench_tx udpgso_bench_rx ip_defrag
TEST_GEN_FILES += so_txtime ipv6_flowlabel ipv6_flowlabel_mgr so_netns_cookie
TEST_GEN_FILES += tcp_fastopen_backup_key
TEST_GEN_FILES += fin_ack_lat
TEST_GEN_FILES += reuseaddr_ports_exhausted
TEST_GEN_FILES += hwtstamp_config rxtimestamp timestamping txtimestamp
TEST_GEN_FILES += ipsec
TEST_GEN_FILES += ioam6_parser
TEST_GEN_FILES += gro
TEST_GEN_PROGS = reuseport_bpf reuseport_bpf_cpu reuseport_bpf_numa
TEST_GEN_PROGS += reuseport_dualstack reuseaddr_conflict tls tun tap epoll_busy_poll
TEST_GEN_FILES += toeplitz
TEST_GEN_FILES += cmsg_sender
TEST_GEN_FILES += stress_reuseport_listen
TEST_PROGS += test_vxlan_vnifiltering.sh
TEST_GEN_FILES += io_uring_zerocopy_tx
TEST_PROGS += io_uring_zerocopy_tx.sh
TEST_GEN_FILES += bind_bhash
TEST_GEN_PROGS += netlink-dumps
TEST_GEN_PROGS += sk_bind_sendto_listen
TEST_GEN_PROGS += sk_connect_zero_addr
TEST_GEN_PROGS += sk_so_peek_off
TEST_PROGS += test_ingress_egress_chaining.sh
TEST_GEN_PROGS += so_incoming_cpu
TEST_PROGS += sctp_vrf.sh
TEST_GEN_FILES += sctp_hello
TEST_GEN_FILES += ip_local_port_range
TEST_GEN_PROGS += bind_wildcard
TEST_GEN_PROGS += bind_timewait
TEST_PROGS += test_vxlan_mdb.sh
TEST_PROGS += test_bridge_neigh_suppress.sh
TEST_PROGS += test_vxlan_nolocalbypass.sh
TEST_PROGS += test_bridge_backup_port.sh
TEST_PROGS += fdb_flush.sh fdb_notify.sh
TEST_PROGS += fq_band_pktlimit.sh
TEST_PROGS += vlan_hw_filter.sh
TEST_PROGS += bpf_offload.py
TEST_PROGS += ipv6_route_update_soft_lockup.sh
TEST_PROGS += busy_poll_test.sh

# YNL files, must be before "include ..lib.mk"
YNL_GEN_FILES := busy_poller
TEST_GEN_FILES += $(YNL_GEN_FILES)

TEST_FILES := settings
TEST_FILES += in_netns.sh lib.sh net_helper.sh setup_loopback.sh setup_veth.sh

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
