================
bpftool-net
================
-------------------------------------------------------------------------------
tool for inspection of netdev/tc related bpf prog attachments
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** [*OPTIONS*] **net** *COMMAND*

	*OPTIONS* := { [{ **-j** | **--json** }] [{ **-p** | **--pretty** }] }

	*COMMANDS* :=
	{ **show** | **list** } [ **dev** name ] | **help**

NET COMMANDS
============

|	**bpftool** **net { show | list } [ dev name ]**
|	**bpftool** **net help**

DESCRIPTION
===========
	**bpftool net { show | list } [ dev name ]**
                  List bpf program attachments in the kernel networking subsystem.

                  Currently, only device driver xdp attachments and tc filter
                  classification/action attachments are implemented, i.e., for
                  program types **BPF_PROG_TYPE_SCHED_CLS**,
                  **BPF_PROG_TYPE_SCHED_ACT** and **BPF_PROG_TYPE_XDP**.
                  For programs attached to a particular cgroup, e.g.,
                  **BPF_PROG_TYPE_CGROUP_SKB**, **BPF_PROG_TYPE_CGROUP_SOCK**,
                  **BPF_PROG_TYPE_SOCK_OPS** and **BPF_PROG_TYPE_CGROUP_SOCK_ADDR**,
                  users can use **bpftool cgroup** to dump cgroup attachments.
                  For sk_{filter, skb, msg, reuseport} and lwt/seg6
                  bpf programs, users should consult other tools, e.g., iproute2.

                  The current output will start with all xdp program attachments, followed by
                  all tc class/qdisc bpf program attachments. Both xdp programs and
                  tc programs are ordered based on ifindex number. If multiple bpf
                  programs attached to the same networking device through **tc filter**,
                  the order will be first all bpf programs attached to tc classes, then
                  all bpf programs attached to non clsact qdiscs, and finally all
                  bpf programs attached to root and clsact qdisc.

	**bpftool net help**
		  Print short help message.

OPTIONS
=======
	-h, --help
		  Print short generic help message (similar to **bpftool help**).

	-V, --version
		  Print version number (similar to **bpftool version**).

	-j, --json
		  Generate JSON output. For commands that cannot produce JSON, this
		  option has no effect.

	-p, --pretty
		  Generate human-readable JSON output. Implies **-j**.

EXAMPLES
========

| **# bpftool net**

::

      xdp:
      eth0(2) driver id 198

      tc:
      eth0(2) htb name prefix_matcher.o:[cls_prefix_matcher_htb] id 111727 act []
      eth0(2) clsact/ingress fbflow_icmp id 130246 act []
      eth0(2) clsact/egress prefix_matcher.o:[cls_prefix_matcher_clsact] id 111726
      eth0(2) clsact/egress cls_fg_dscp id 108619 act []
      eth0(2) clsact/egress fbflow_egress id 130245

|
| **# bpftool -jp net**

::

    [{
            "xdp": [{
                    "devname": "eth0",
                    "ifindex": 2,
                    "mode": "driver",
                    "id": 198
                }
            ],
            "tc": [{
                    "devname": "eth0",
                    "ifindex": 2,
                    "kind": "htb",
                    "name": "prefix_matcher.o:[cls_prefix_matcher_htb]",
                    "id": 111727,
                    "act": []
                },{
                    "devname": "eth0",
                    "ifindex": 2,
                    "kind": "clsact/ingress",
                    "name": "fbflow_icmp",
                    "id": 130246,
                    "act": []
                },{
                    "devname": "eth0",
                    "ifindex": 2,
                    "kind": "clsact/egress",
                    "name": "prefix_matcher.o:[cls_prefix_matcher_clsact]",
                    "id": 111726,
                },{
                    "devname": "eth0",
                    "ifindex": 2,
                    "kind": "clsact/egress",
                    "name": "cls_fg_dscp",
                    "id": 108619,
                    "act": []
                },{
                    "devname": "eth0",
                    "ifindex": 2,
                    "kind": "clsact/egress",
                    "name": "fbflow_egress",
                    "id": 130245,
                }
            ]
        }
    ]


SEE ALSO
========
	**bpf**\ (2),
	**bpf-helpers**\ (7),
	**bpftool**\ (8),
	**bpftool-prog**\ (8),
	**bpftool-map**\ (8),
	**bpftool-cgroup**\ (8),
	**bpftool-feature**\ (8),
	**bpftool-perf**\ (8)
