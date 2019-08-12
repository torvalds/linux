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
	{ **show** | **list** | **tree** | **attach** | **detach** | **help** }

CGROUP COMMANDS
===============

|	**bpftool** **cgroup { show | list }** *CGROUP*
|	**bpftool** **cgroup tree** [*CGROUP_ROOT*]
|	**bpftool** **cgroup attach** *CGROUP* *ATTACH_TYPE* *PROG* [*ATTACH_FLAGS*]
|	**bpftool** **cgroup detach** *CGROUP* *ATTACH_TYPE* *PROG*
|	**bpftool** **cgroup help**
|
|	*PROG* := { **id** *PROG_ID* | **pinned** *FILE* | **tag** *PROG_TAG* }
|	*ATTACH_TYPE* := { **ingress** | **egress** | **sock_create** | **sock_ops** | **device** |
|		**bind4** | **bind6** | **post_bind4** | **post_bind6** | **connect4** | **connect6** |
|		**sendmsg4** | **sendmsg6** | **recvmsg4** | **recvmsg6** | **sysctl** |
|		**getsockopt** | **setsockopt** }
|	*ATTACH_FLAGS* := { **multi** | **override** }

DESCRIPTION
===========
	**bpftool cgroup { show | list }** *CGROUP*
		  List all programs attached to the cgroup *CGROUP*.

		  Output will start with program ID followed by attach type,
		  attach flags and program name.

	**bpftool cgroup tree** [*CGROUP_ROOT*]
		  Iterate over all cgroups in *CGROUP_ROOT* and list all
		  attached programs. If *CGROUP_ROOT* is not specified,
		  bpftool uses cgroup v2 mountpoint.

		  The output is similar to the output of cgroup show/list
		  commands: it starts with absolute cgroup path, followed by
		  program ID, attach type, attach flags and program name.

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
		  **device** device access (since 4.15);
		  **bind4** call to bind(2) for an inet4 socket (since 4.17);
		  **bind6** call to bind(2) for an inet6 socket (since 4.17);
		  **post_bind4** return from bind(2) for an inet4 socket (since 4.17);
		  **post_bind6** return from bind(2) for an inet6 socket (since 4.17);
		  **connect4** call to connect(2) for an inet4 socket (since 4.17);
		  **connect6** call to connect(2) for an inet6 socket (since 4.17);
		  **sendmsg4** call to sendto(2), sendmsg(2), sendmmsg(2) for an
		  unconnected udp4 socket (since 4.18);
		  **sendmsg6** call to sendto(2), sendmsg(2), sendmmsg(2) for an
		  unconnected udp6 socket (since 4.18);
		  **recvmsg4** call to recvfrom(2), recvmsg(2), recvmmsg(2) for
                  an unconnected udp4 socket (since 5.2);
		  **recvmsg6** call to recvfrom(2), recvmsg(2), recvmmsg(2) for
                  an unconnected udp6 socket (since 5.2);
		  **sysctl** sysctl access (since 5.2);
		  **getsockopt** call to getsockopt (since 5.3);
		  **setsockopt** call to setsockopt (since 5.3).

	**bpftool cgroup detach** *CGROUP* *ATTACH_TYPE* *PROG*
		  Detach *PROG* from the cgroup *CGROUP* and attach type
		  *ATTACH_TYPE*.

	**bpftool prog help**
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

	-f, --bpffs
		  Show file names of pinned programs.

	-d, --debug
		  Print all logs available from libbpf, including debug-level
		  information.

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
	**bpf**\ (2),
	**bpf-helpers**\ (7),
	**bpftool**\ (8),
	**bpftool-prog**\ (8),
	**bpftool-map**\ (8),
	**bpftool-feature**\ (8),
	**bpftool-net**\ (8),
	**bpftool-perf**\ (8),
	**bpftool-btf**\ (8)
