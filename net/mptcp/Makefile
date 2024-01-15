# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_MPTCP) += mptcp.o

mptcp-y := protocol.o subflow.o options.o token.o crypto.o ctrl.o pm.o diag.o \
	   mib.o pm_netlink.o sockopt.o pm_userspace.o fastopen.o sched.o \
	   mptcp_pm_gen.o

obj-$(CONFIG_SYN_COOKIES) += syncookies.o
obj-$(CONFIG_INET_MPTCP_DIAG) += mptcp_diag.o

mptcp_crypto_test-objs := crypto_test.o
mptcp_token_test-objs := token_test.o
obj-$(CONFIG_MPTCP_KUNIT_TEST) += mptcp_crypto_test.o mptcp_token_test.o

obj-$(CONFIG_BPF_SYSCALL) += bpf.o
