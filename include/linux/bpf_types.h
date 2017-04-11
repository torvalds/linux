/* internal file - do not include directly */

#ifdef CONFIG_NET
BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER, sk_filter_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_SCHED_CLS, tc_cls_act_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_SCHED_ACT, tc_cls_act_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_XDP, xdp_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_SKB, cg_skb_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_CGROUP_SOCK, cg_sock_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_LWT_IN, lwt_inout_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_LWT_OUT, lwt_inout_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_LWT_XMIT, lwt_xmit_prog_ops)
#endif
#ifdef CONFIG_BPF_EVENTS
BPF_PROG_TYPE(BPF_PROG_TYPE_KPROBE, kprobe_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_TRACEPOINT, tracepoint_prog_ops)
BPF_PROG_TYPE(BPF_PROG_TYPE_PERF_EVENT, perf_event_prog_ops)
#endif
