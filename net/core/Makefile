# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux networking core.
#

obj-y := sock.o request_sock.o skbuff.o datagram.o stream.o scm.o \
	 gen_stats.o gen_estimator.o net_namespace.o secure_seq.o \
	 flow_dissector.o dropreason_str.o

obj-$(CONFIG_SYSCTL) += sysctl_net_core.o

obj-y		     += dev.o dev_addr_lists.o dst.o netevent.o \
			neighbour.o rtnetlink.o utils.o link_watch.o filter.o \
			sock_diag.o dev_ioctl.o tso.o sock_reuseport.o \
			fib_notifier.o xdp.o flow_offload.o gro.o

obj-$(CONFIG_NETDEV_ADDR_LIST_TEST) += dev_addr_lists_test.o

obj-y += net-sysfs.o
obj-$(CONFIG_PAGE_POOL) += page_pool.o
obj-$(CONFIG_PROC_FS) += net-procfs.o
obj-$(CONFIG_NET_PKTGEN) += pktgen.o
obj-$(CONFIG_NETPOLL) += netpoll.o
obj-$(CONFIG_FIB_RULES) += fib_rules.o
obj-$(CONFIG_TRACEPOINTS) += net-traces.o
obj-$(CONFIG_NET_DROP_MONITOR) += drop_monitor.o
obj-$(CONFIG_NET_SELFTESTS) += selftests.o
obj-$(CONFIG_NETWORK_PHY_TIMESTAMPING) += timestamping.o
obj-$(CONFIG_NET_PTP_CLASSIFY) += ptp_classifier.o
obj-$(CONFIG_CGROUP_NET_PRIO) += netprio_cgroup.o
obj-$(CONFIG_CGROUP_NET_CLASSID) += netclassid_cgroup.o
obj-$(CONFIG_LWTUNNEL) += lwtunnel.o
obj-$(CONFIG_LWTUNNEL_BPF) += lwt_bpf.o
obj-$(CONFIG_DST_CACHE) += dst_cache.o
obj-$(CONFIG_HWBM) += hwbm.o
obj-$(CONFIG_NET_DEVLINK) += devlink.o
obj-$(CONFIG_GRO_CELLS) += gro_cells.o
obj-$(CONFIG_FAILOVER) += failover.o
obj-$(CONFIG_NET_SOCK_MSG) += skmsg.o
obj-$(CONFIG_BPF_SYSCALL) += sock_map.o
obj-$(CONFIG_BPF_SYSCALL) += bpf_sk_storage.o
obj-$(CONFIG_OF)	+= of_net.o

clean-files := dropreason_str.c

quiet_cmd_dropreason_str = GEN     $@
cmd_dropreason_str = awk -F ',' 'BEGIN{ print "\#include <net/dropreason.h>\n"; \
	print "const char * const drop_reasons[] = {" }\
	/^enum skb_drop/ { dr=1; }\
	/^\};/ { dr=0; }\
	/^\tSKB_DROP_REASON_/ {\
		if (dr) {\
			sub(/\tSKB_DROP_REASON_/, "", $$1);\
			printf "\t[SKB_DROP_REASON_%s] = \"%s\",\n", $$1, $$1;\
		}\
	}\
	END{ print "};" }' $< > $@

$(obj)/dropreason_str.c: $(srctree)/include/net/dropreason.h
	$(call cmd,dropreason_str)

$(obj)/dropreason_str.o: $(obj)/dropreason_str.c
