/* SPDX-License-Identifier: GPL-2.0 */
/* internal file - do not include directly */

#ifdef CONFIG_NET
BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER, sk_filter,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_SCHED_CLS, tc_cls_act,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_SCHED_ACT, tc_cls_act,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_XDP, xdp,
	      struct xdp_md, struct xdp_buff)
#ifdef CONFIG_CGROUP_BPF
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_SKB, cg_skb,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_SOCK, cg_sock,
	      struct bpf_sock, struct sock)
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_SOCK_ADDR, cg_sock_addr,
	      struct bpf_sock_addr, struct bpf_sock_addr_kern)
#endif
BPF_PROG_TYPE(BPF_PROG_TYPE_LWT_IN, lwt_in,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_LWT_OUT, lwt_out,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_LWT_XMIT, lwt_xmit,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_LWT_SEG6LOCAL, lwt_seg6local,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_SOCK_OPS, sock_ops,
	      struct bpf_sock_ops, struct bpf_sock_ops_kern)
BPF_PROG_TYPE(BPF_PROG_TYPE_SK_SKB, sk_skb,
	      struct __sk_buff, struct sk_buff)
BPF_PROG_TYPE(BPF_PROG_TYPE_SK_MSG, sk_msg,
	      struct sk_msg_md, struct sk_msg)
BPF_PROG_TYPE(BPF_PROG_TYPE_FLOW_DISSECTOR, flow_dissector,
	      struct __sk_buff, struct bpf_flow_dissector)
#endif
#ifdef CONFIG_BPF_EVENTS
BPF_PROG_TYPE(BPF_PROG_TYPE_KPROBE, kprobe,
	      bpf_user_pt_regs_t, struct pt_regs)
BPF_PROG_TYPE(BPF_PROG_TYPE_TRACEPOINT, tracepoint,
	      __u64, u64)
BPF_PROG_TYPE(BPF_PROG_TYPE_PERF_EVENT, perf_event,
	      struct bpf_perf_event_data, struct bpf_perf_event_data_kern)
BPF_PROG_TYPE(BPF_PROG_TYPE_RAW_TRACEPOINT, raw_tracepoint,
	      struct bpf_raw_tracepoint_args, u64)
BPF_PROG_TYPE(BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE, raw_tracepoint_writable,
	      struct bpf_raw_tracepoint_args, u64)
BPF_PROG_TYPE(BPF_PROG_TYPE_TRACING, tracing,
	      void *, void *)
#endif
#ifdef CONFIG_CGROUP_BPF
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_DEVICE, cg_dev,
	      struct bpf_cgroup_dev_ctx, struct bpf_cgroup_dev_ctx)
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_SYSCTL, cg_sysctl,
	      struct bpf_sysctl, struct bpf_sysctl_kern)
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_SOCKOPT, cg_sockopt,
	      struct bpf_sockopt, struct bpf_sockopt_kern)
#endif
#ifdef CONFIG_BPF_LIRC_MODE2
BPF_PROG_TYPE(BPF_PROG_TYPE_LIRC_MODE2, lirc_mode2,
	      __u32, u32)
#endif
#ifdef CONFIG_INET
BPF_PROG_TYPE(BPF_PROG_TYPE_SK_REUSEPORT, sk_reuseport,
	      struct sk_reuseport_md, struct sk_reuseport_kern)
BPF_PROG_TYPE(BPF_PROG_TYPE_SK_LOOKUP, sk_lookup,
	      struct bpf_sk_lookup, struct bpf_sk_lookup_kern)
#endif
#if defined(CONFIG_BPF_JIT)
BPF_PROG_TYPE(BPF_PROG_TYPE_STRUCT_OPS, bpf_struct_ops,
	      void *, void *)
BPF_PROG_TYPE(BPF_PROG_TYPE_EXT, bpf_extension,
	      void *, void *)
#ifdef CONFIG_BPF_LSM
BPF_PROG_TYPE(BPF_PROG_TYPE_LSM, lsm,
	       void *, void *)
#endif /* CONFIG_BPF_LSM */
#endif
BPF_PROG_TYPE(BPF_PROG_TYPE_SYSCALL, bpf_syscall,
	      void *, void *)

BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY, array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PERCPU_ARRAY, percpu_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PROG_ARRAY, prog_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PERF_EVENT_ARRAY, perf_event_array_map_ops)
#ifdef CONFIG_CGROUPS
BPF_MAP_TYPE(BPF_MAP_TYPE_CGROUP_ARRAY, cgroup_array_map_ops)
#endif
#ifdef CONFIG_CGROUP_BPF
BPF_MAP_TYPE(BPF_MAP_TYPE_CGROUP_STORAGE, cgroup_storage_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE, cgroup_storage_map_ops)
#endif
BPF_MAP_TYPE(BPF_MAP_TYPE_HASH, htab_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PERCPU_HASH, htab_percpu_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_LRU_HASH, htab_lru_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_LRU_PERCPU_HASH, htab_lru_percpu_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_LPM_TRIE, trie_map_ops)
#ifdef CONFIG_PERF_EVENTS
BPF_MAP_TYPE(BPF_MAP_TYPE_STACK_TRACE, stack_trace_map_ops)
#endif
BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY_OF_MAPS, array_of_maps_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_HASH_OF_MAPS, htab_of_maps_map_ops)
#ifdef CONFIG_NET
BPF_MAP_TYPE(BPF_MAP_TYPE_DEVMAP, dev_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_DEVMAP_HASH, dev_map_hash_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_SK_STORAGE, sk_storage_map_ops)
#ifdef CONFIG_BPF_LSM
BPF_MAP_TYPE(BPF_MAP_TYPE_INODE_STORAGE, inode_storage_map_ops)
#endif
BPF_MAP_TYPE(BPF_MAP_TYPE_TASK_STORAGE, task_storage_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_CPUMAP, cpu_map_ops)
#if defined(CONFIG_XDP_SOCKETS)
BPF_MAP_TYPE(BPF_MAP_TYPE_XSKMAP, xsk_map_ops)
#endif
#ifdef CONFIG_INET
BPF_MAP_TYPE(BPF_MAP_TYPE_SOCKMAP, sock_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_SOCKHASH, sock_hash_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_REUSEPORT_SOCKARRAY, reuseport_array_ops)
#endif
#endif
BPF_MAP_TYPE(BPF_MAP_TYPE_QUEUE, queue_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_STACK, stack_map_ops)
#if defined(CONFIG_BPF_JIT)
BPF_MAP_TYPE(BPF_MAP_TYPE_STRUCT_OPS, bpf_struct_ops_map_ops)
#endif
BPF_MAP_TYPE(BPF_MAP_TYPE_RINGBUF, ringbuf_map_ops)

BPF_LINK_TYPE(BPF_LINK_TYPE_RAW_TRACEPOINT, raw_tracepoint)
BPF_LINK_TYPE(BPF_LINK_TYPE_TRACING, tracing)
#ifdef CONFIG_CGROUP_BPF
BPF_LINK_TYPE(BPF_LINK_TYPE_CGROUP, cgroup)
#endif
BPF_LINK_TYPE(BPF_LINK_TYPE_ITER, iter)
#ifdef CONFIG_NET
BPF_LINK_TYPE(BPF_LINK_TYPE_NETNS, netns)
BPF_LINK_TYPE(BPF_LINK_TYPE_XDP, xdp)
#endif
