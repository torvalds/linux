# SPDX-License-Identifier: GPL-2.0
# Makefile for netfilter selftests

TEST_PROGS := nft_trans_stress.sh nft_fib.sh nft_nat.sh bridge_brouter.sh \
	conntrack_icmp_related.sh nft_flowtable.sh ipvs.sh \
	nft_concat_range.sh nft_conntrack_helper.sh \
	nft_queue.sh nft_meta.sh nf_nat_edemux.sh \
	ipip-conntrack-mtu.sh conntrack_tcp_unreplied.sh \
	conntrack_vrf.sh nft_synproxy.sh rpath.sh nft_audit.sh \
	conntrack_sctp_collision.sh xt_string.sh

HOSTPKG_CONFIG := pkg-config

CFLAGS += $(shell $(HOSTPKG_CONFIG) --cflags libmnl 2>/dev/null)
LDLIBS += $(shell $(HOSTPKG_CONFIG) --libs libmnl 2>/dev/null || echo -lmnl)

TEST_GEN_FILES =  nf-queue connect_close audit_logread sctp_collision \
	conntrack_dump_flush

include ../lib.mk
