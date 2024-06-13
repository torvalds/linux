# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..

CFLAGS =  -Wall -Wl,--no-as-needed -O2 -g -I$(top_srcdir)/usr/include $(KHDR_INCLUDES)

TEST_PROGS := openvswitch.sh

TEST_FILES := ovs-dpctl.py

EXTRA_CLEAN := test_netlink_checks

include ../../lib.mk
