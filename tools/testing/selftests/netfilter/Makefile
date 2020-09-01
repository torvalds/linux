# SPDX-License-Identifier: GPL-2.0
# Makefile for netfilter selftests

TEST_PROGS := nft_trans_stress.sh nft_nat.sh bridge_brouter.sh \
	conntrack_icmp_related.sh nft_flowtable.sh ipvs.sh \
	nft_concat_range.sh nft_conntrack_helper.sh \
	nft_queue.sh nft_meta.sh

LDLIBS = -lmnl
TEST_GEN_FILES =  nf-queue

include ../lib.mk
