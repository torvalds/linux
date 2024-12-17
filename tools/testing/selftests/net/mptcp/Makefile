# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..

CFLAGS =  -Wall -Wl,--no-as-needed -O2 -g -I$(top_srcdir)/usr/include $(KHDR_INCLUDES)

TEST_PROGS := mptcp_connect.sh pm_netlink.sh mptcp_join.sh diag.sh \
	      simult_flows.sh mptcp_sockopt.sh userspace_pm.sh

TEST_GEN_FILES = mptcp_connect pm_nl_ctl mptcp_sockopt mptcp_inq

TEST_FILES := mptcp_lib.sh settings

TEST_INCLUDES := ../lib.sh $(wildcard ../lib/sh/*.sh) ../net_helper.sh

EXTRA_CLEAN := *.pcap

include ../../lib.mk
