# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..
KSFT_KHDR_INSTALL := 1

CFLAGS =  -Wall -Wl,--no-as-needed -O2 -g  -I$(top_srcdir)/usr/include

TEST_PROGS := mptcp_connect.sh pm_netlink.sh mptcp_join.sh diag.sh \
	      simult_flows.sh mptcp_sockopt.sh

TEST_GEN_FILES = mptcp_connect pm_nl_ctl mptcp_sockopt

TEST_FILES := settings

EXTRA_CLEAN := *.pcap

include ../../lib.mk
