# SPDX-License-Identifier: GPL-2.0
obj-y := core.o

obj-$(CONFIG_BPF_SYSCALL) += syscall.o verifier.o inode.o helpers.o tnum.o
obj-$(CONFIG_BPF_SYSCALL) += hashtab.o arraymap.o percpu_freelist.o bpf_lru_list.o lpm_trie.o map_in_map.o
ifeq ($(CONFIG_NET),y)
obj-$(CONFIG_BPF_SYSCALL) += devmap.o
ifeq ($(CONFIG_STREAM_PARSER),y)
obj-$(CONFIG_BPF_SYSCALL) += sockmap.o
endif
endif
ifeq ($(CONFIG_PERF_EVENTS),y)
obj-$(CONFIG_BPF_SYSCALL) += stackmap.o
endif
obj-$(CONFIG_CGROUP_BPF) += cgroup.o
