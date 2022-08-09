.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

================
bpftool-link
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF links
-------------------------------------------------------------------------------

:Manual section: 8

.. include:: substitutions.rst

SYNOPSIS
========

	**bpftool** [*OPTIONS*] **link** *COMMAND*

	*OPTIONS* := { |COMMON_OPTIONS| | { **-f** | **--bpffs** } | { **-n** | **--nomount** } }

	*COMMANDS* := { **show** | **list** | **pin** | **help** }

LINK COMMANDS
=============

|	**bpftool** **link { show | list }** [*LINK*]
|	**bpftool** **link pin** *LINK* *FILE*
|	**bpftool** **link detach** *LINK*
|	**bpftool** **link help**
|
|	*LINK* := { **id** *LINK_ID* | **pinned** *FILE* }


DESCRIPTION
===========
	**bpftool link { show | list }** [*LINK*]
		  Show information about active links. If *LINK* is
		  specified show information only about given link,
		  otherwise list all links currently active on the system.

		  Output will start with link ID followed by link type and
		  zero or more named attributes, some of which depend on type
		  of link.

		  Since Linux 5.8 bpftool is able to discover information about
		  processes that hold open file descriptors (FDs) against BPF
		  links. On such kernels bpftool will automatically emit this
		  information as well.

	**bpftool link pin** *LINK* *FILE*
		  Pin link *LINK* as *FILE*.

		  Note: *FILE* must be located in *bpffs* mount. It must not
		  contain a dot character ('.'), which is reserved for future
		  extensions of *bpffs*.

	**bpftool link detach** *LINK*
		  Force-detach link *LINK*. BPF link and its underlying BPF
		  program will stay valid, but they will be detached from the
		  respective BPF hook and BPF link will transition into
		  a defunct state until last open file descriptor for that
		  link is closed.

	**bpftool link help**
		  Print short help message.

OPTIONS
=======
	.. include:: common_options.rst

	-f, --bpffs
		  When showing BPF links, show file names of pinned
		  links.

	-n, --nomount
		  Do not automatically attempt to mount any virtual file system
		  (such as tracefs or BPF virtual file system) when necessary.

EXAMPLES
========
**# bpftool link show**

::

    10: cgroup  prog 25
            cgroup_id 614  attach_type egress
            pids test_progs(223)

**# bpftool --json --pretty link show**

::

    [{
            "type": "cgroup",
            "prog_id": 25,
            "cgroup_id": 614,
            "attach_type": "egress",
            "pids": [{
                    "pid": 223,
                    "comm": "test_progs"
                }
            ]
        }
    ]

|
| **# bpftool link pin id 10 /sys/fs/bpf/link**
| **# ls -l /sys/fs/bpf/**

::

    -rw------- 1 root root 0 Apr 23 21:39 link
