# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..

HOSTPKG_CONFIG := pkg-config
MNL_CFLAGS := $(shell $(HOSTPKG_CONFIG) --cflags libmnl 2>/dev/null)
MNL_LDLIBS := $(shell $(HOSTPKG_CONFIG) --libs libmnl 2>/dev/null || echo -lmnl)

TEST_PROGS := \
	br_netfilter.sh \
	br_netfilter_queue.sh \
	bridge_brouter.sh \
	conntrack_clash.sh \
	conntrack_dump_flush.sh \
	conntrack_icmp_related.sh \
	conntrack_ipip_mtu.sh \
	conntrack_resize.sh \
	conntrack_reverse_clash.sh \
	conntrack_sctp_collision.sh \
	conntrack_tcp_unreplied.sh \
	conntrack_vrf.sh \
	ipvs.sh \
	nf_conntrack_packetdrill.sh \
	nf_nat_edemux.sh \
	nft_audit.sh \
	nft_concat_range.sh \
	nft_conntrack_helper.sh \
	nft_fib.sh \
	nft_flowtable.sh \
	nft_interface_stress.sh \
	nft_meta.sh \
	nft_nat.sh \
	nft_nat_zones.sh \
	nft_queue.sh \
	nft_synproxy.sh \
	nft_tproxy_tcp.sh \
	nft_tproxy_udp.sh \
	nft_zones_many.sh \
	rpath.sh \
	vxlan_mtu_frag.sh \
	xt_string.sh \
# end of TEST_PROGS

TEST_PROGS_EXTENDED = nft_concat_range_perf.sh

TEST_GEN_FILES = \
	audit_logread \
	connect_close \
	conntrack_dump_flush \
	conntrack_reverse_clash \
	nf_queue \
	sctp_collision \
	udpclash \
# end of TEST_GEN_FILES

include ../../lib.mk

$(OUTPUT)/nf_queue: CFLAGS += $(MNL_CFLAGS)
$(OUTPUT)/nf_queue: LDLIBS += $(MNL_LDLIBS)

$(OUTPUT)/conntrack_dump_flush: CFLAGS += $(MNL_CFLAGS)
$(OUTPUT)/conntrack_dump_flush: LDLIBS += $(MNL_LDLIBS)
$(OUTPUT)/udpclash: LDLIBS += -lpthread

TEST_FILES := \
	lib.sh \
	packetdrill \
# end of TEST_FILES

TEST_INCLUDES := \
	$(wildcard ../lib/sh/*.sh) \
	../lib.sh \
# end of TEST_INCLUDES
