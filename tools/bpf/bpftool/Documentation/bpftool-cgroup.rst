================
bpftool-cgroup
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF progs
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** [*OPTIONS*] **cgroup** *COMMAND*

	*OPTIONS* := { { **-j** | **--json** } [{ **-p** | **--pretty** }] | { **-f** | **--bpffs** } }

	*COMMANDS* :=
	{ **list** | **attach** | **detach** | **help** }

MAP COMMANDS
=============

|	**bpftool** **cgroup list** *CGROUP*
|	**bpftool** **cgroup attach** *CGROUP* *ATTACH_TYPE* *PROG* [*ATTACH_FLAGS*]
|	**bpftool** **cgroup detach** *CGROUP* *ATTACH_TYPE* *PROG*
|	**bpftool** **cgroup help**
|
|	*PROG* := { **id** *PROG_ID* | **pinned** *FILE* | **tag** *PROG_TAG* }
|	*ATTACH_TYPE* := { *ingress* | *egress* | *sock_create* | *sock_ops* | *device* }
|	*ATTACH_FLAGS* := { *multi* | *override* }

DESCRIPTION
===========
	**bpftool cgroup list** *CGROUP*
		  List all programs attached to the cgroup *CGROUP*.

		  Output will start with program ID followed by attach type,
		  attach flags and program name.

	**bpftool cgroup attach** *CGROUP* *ATTACH_TYPE* *PROG* [*ATTACH_FLAGS*]
		  Attach program *PROG* to the cgroup *CGROUP* with attach type
		  *ATTACH_TYPE* and optional *ATTACH_FLAGS*.

		  *ATTACH_FLAGS* can be one of: **override** if a sub-cgroup installs
		  some bpf program, the program in this cgroup yields to sub-cgroup
		  program; **multi** if a sub-cgroup installs some bpf program,
		  that cgroup program gets run in addition to the program in this
		  cgroup.

		  Only one program is allowed to be attached to a cgroup with
		  no attach flags or the **override** flag. Attaching another
		  program will release old program and attach the new one.

		  Multiple programs are allowed to be attached to a cgroup with
		  **multi**. They are executed in FIFO order (those that were
		  attached first, run first).

		  Non-default *ATTACH_FLAGS* are supported by kernel version 4.14
		  and later.

		  *ATTACH_TYPE* can be on of:
		  **ingress** ingress path of the inet socket (since 4.10);
		  **egress** egress path of the inet socket (since 4.10);
		  **sock_create** opening of an inet socket (since 4.10);
		  **sock_ops** various socket operations (since 4.12);
		  **device** device access (since 4.15).

	**bpftool cgroup detach** *CGROUP* *ATTACH_TYPE* *PROG*
		  Detach *PROG* from the cgroup *CGROUP* and attach type
		  *ATTACH_TYPE*.

	**bpftool prog help**
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

	-f, --bpffs
		  Show file names of pinned programs.

EXAMPLES
========
|
| **# mount -t bpf none /sys/fs/bpf/**
| **# mkdir /sys/fs/cgroup/test.slice**
| **# bpftool prog load ./device_cgroup.o /sys/fs/bpf/prog**
| **# bpftool cgroup attach /sys/fs/cgroup/test.slice/ device id 1 allow_multi**

**# bpftool cgroup list /sys/fs/cgroup/test.slice/**

::

    ID       AttachType      AttachFlags     Name
    1        device          allow_multi     bpf_prog1

|
| **# bpftool cgroup detach /sys/fs/cgroup/test.slice/ device id 1**
| **# bpftool cgroup list /sys/fs/cgroup/test.slice/**

::

    ID       AttachType      AttachFlags     Name

SEE ALSO
========
	**bpftool**\ (8), **bpftool-prog**\ (8), **bpftool-map**\ (8)
