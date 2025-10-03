# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..

CFLAGS += -Wall -Wl,--no-as-needed -O2 -g -I$(top_srcdir)/usr/include $(KHDR_INCLUDES)

TEST_PROGS := \
	diag.sh \
	mptcp_connect.sh \
	mptcp_connect_checksum.sh \
	mptcp_connect_mmap.sh \
	mptcp_connect_sendfile.sh \
	mptcp_join.sh \
	mptcp_sockopt.sh \
	pm_netlink.sh \
	simult_flows.sh \
	userspace_pm.sh \
# end of TEST_PROGS

TEST_GEN_FILES := \
	mptcp_connect \
	mptcp_diag \
	mptcp_inq \
	mptcp_sockopt \
	pm_nl_ctl \
# end of TEST_GEN_FILES

TEST_FILES := \
	mptcp_lib.sh \
	settings \
# end of TEST_FILES

TEST_INCLUDES := ../lib.sh $(wildcard ../lib/sh/*.sh)

EXTRA_CLEAN := *.pcap

include ../../lib.mk
