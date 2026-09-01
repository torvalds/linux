# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := acct_syscall
TEST_GEN_PROGS += taskstats_fill_stats_tgid
TEST_GEN_PROGS += cgroupstats

NETLINK_HELPER_PROGS := cgroupstats taskstats_fill_stats_tgid

CFLAGS += -Wall
LDLIBS += -lpthread

include ../lib.mk

$(NETLINK_HELPER_PROGS): %: %.c netlink_helper.c netlink_helper.h
	$(call msg,CC,,$@)
	$(Q)$(LINK.c) $< netlink_helper.c $(LDLIBS) -o $@

$(addprefix $(OUTPUT)/,$(NETLINK_HELPER_PROGS)): $(OUTPUT)/%: %.c netlink_helper.c netlink_helper.h
	$(call msg,CC,,$@)
	$(Q)$(LINK.c) $< netlink_helper.c $(LDLIBS) -o $@
