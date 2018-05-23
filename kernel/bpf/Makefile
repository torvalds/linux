# SPDX-License-Identifier: GPL-2.0
obj-y := core.o

obj-$(CONFIG_BPF_SYSCALL) += syscall.o verifier.o inode.o helpers.o tnum.o
obj-$(CONFIG_BPF_SYSCALL) += hashtab.o arraymap.o percpu_freelist.o bpf_lru_list.o lpm_trie.o map_in_map.o
obj-$(CONFIG_BPF_SYSCALL) += disasm.o
obj-$(CONFIG_BPF_SYSCALL) += btf.o
ifeq ($(CONFIG_NET),y)
obj-$(CONFIG_BPF_SYSCALL) += devmap.o
obj-$(CONFIG_BPF_SYSCALL) += cpumap.o
ifeq ($(CONFIG_XDP_SOCKETS),y)
obj-$(CONFIG_BPF_SYSCALL) += xskmap.o
endif
obj-$(CONFIG_BPF_SYSCALL) += offload.o
ifeq ($(CONFIG_STREAM_PARSER),y)
ifeq ($(CONFIG_INET),y)
obj-$(CONFIG_BPF_SYSCALL) += sockmap.o
endif
endif
endif
ifeq ($(CONFIG_PERF_EVENTS),y)
obj-$(CONFIG_BPF_SYSCALL) += stackmap.o
endif
obj-$(CONFIG_CGROUP_BPF) += cgroup.o
