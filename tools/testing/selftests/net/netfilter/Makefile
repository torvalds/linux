# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..

HOSTPKG_CONFIG := pkg-config
MNL_CFLAGS := $(shell $(HOSTPKG_CONFIG) --cflags libmnl 2>/dev/null)
MNL_LDLIBS := $(shell $(HOSTPKG_CONFIG) --libs libmnl 2>/dev/null || echo -lmnl)

TEST_PROGS := br_netfilter.sh bridge_brouter.sh
TEST_PROGS += br_netfilter_queue.sh
TEST_PROGS += conntrack_icmp_related.sh
TEST_PROGS += conntrack_ipip_mtu.sh
TEST_PROGS += conntrack_tcp_unreplied.sh
TEST_PROGS += conntrack_sctp_collision.sh
TEST_PROGS += conntrack_vrf.sh
TEST_PROGS += ipvs.sh
TEST_PROGS += nf_conntrack_packetdrill.sh
TEST_PROGS += nf_nat_edemux.sh
TEST_PROGS += nft_audit.sh
TEST_PROGS += nft_concat_range.sh
TEST_PROGS += nft_conntrack_helper.sh
TEST_PROGS += nft_fib.sh
TEST_PROGS += nft_flowtable.sh
TEST_PROGS += nft_meta.sh
TEST_PROGS += nft_nat.sh
TEST_PROGS += nft_nat_zones.sh
TEST_PROGS += nft_queue.sh
TEST_PROGS += nft_synproxy.sh
TEST_PROGS += nft_zones_many.sh
TEST_PROGS += rpath.sh
TEST_PROGS += xt_string.sh

TEST_PROGS_EXTENDED = nft_concat_range_perf.sh

TEST_GEN_PROGS = conntrack_dump_flush

TEST_GEN_FILES = audit_logread
TEST_GEN_FILES += connect_close nf_queue
TEST_GEN_FILES += sctp_collision

include ../../lib.mk

$(OUTPUT)/nf_queue: CFLAGS += $(MNL_CFLAGS)
$(OUTPUT)/nf_queue: LDLIBS += $(MNL_LDLIBS)

$(OUTPUT)/conntrack_dump_flush: CFLAGS += $(MNL_CFLAGS)
$(OUTPUT)/conntrack_dump_flush: LDLIBS += $(MNL_LDLIBS)

TEST_FILES := lib.sh
TEST_FILES += packetdrill

TEST_INCLUDES := \
	../lib.sh
