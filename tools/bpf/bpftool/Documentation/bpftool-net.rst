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
		  List all networking device driver and tc attachment in the system.

                  Output will start with all xdp program attachment, followed by
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

	-v, --version
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

      xdp [
      ifindex 2 devname eth0 prog_id 198
      ]
      tc_filters [
      ifindex 2 kind qdisc_htb name prefix_matcher.o:[cls_prefix_matcher_htb]
                prog_id 111727 tag d08fe3b4319bc2fd act []
      ifindex 2 kind qdisc_clsact_ingress name fbflow_icmp
                prog_id 130246 tag 3f265c7f26db62c9 act []
      ifindex 2 kind qdisc_clsact_egress name prefix_matcher.o:[cls_prefix_matcher_clsact]
                prog_id 111726 tag 99a197826974c876
      ifindex 2 kind qdisc_clsact_egress name cls_fg_dscp
                prog_id 108619 tag dc4630674fd72dcc act []
      ifindex 2 kind qdisc_clsact_egress name fbflow_egress
                prog_id 130245 tag 72d2d830d6888d2c
      ]

|
| **# bpftool -jp net**

::

    [{
            "xdp": [{
                    "ifindex": 2,
                    "devname": "eth0",
                    "prog_id": 198
                }
            ],
            "tc_filters": [{
                    "ifindex": 2,
                    "kind": "qdisc_htb",
                    "name": "prefix_matcher.o:[cls_prefix_matcher_htb]",
                    "prog_id": 111727,
                    "tag": "d08fe3b4319bc2fd",
                    "act": []
                },{
                    "ifindex": 2,
                    "kind": "qdisc_clsact_ingress",
                    "name": "fbflow_icmp",
                    "prog_id": 130246,
                    "tag": "3f265c7f26db62c9",
                    "act": []
                },{
                    "ifindex": 2,
                    "kind": "qdisc_clsact_egress",
                    "name": "prefix_matcher.o:[cls_prefix_matcher_clsact]",
                    "prog_id": 111726,
                    "tag": "99a197826974c876"
                },{
                    "ifindex": 2,
                    "kind": "qdisc_clsact_egress",
                    "name": "cls_fg_dscp",
                    "prog_id": 108619,
                    "tag": "dc4630674fd72dcc",
                    "act": []
                },{
                    "ifindex": 2,
                    "kind": "qdisc_clsact_egress",
                    "name": "fbflow_egress",
                    "prog_id": 130245,
                    "tag": "72d2d830d6888d2c"
                }
            ]
        }
    ]


SEE ALSO
========
	**bpftool**\ (8), **bpftool-prog**\ (8), **bpftool-map**\ (8)
