================
bpftool-prog
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF progs
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** [*OPTIONS*] **prog** *COMMAND*

	*OPTIONS* := { { **-j** | **--json** } [{ **-p** | **--pretty** }] | { **-f** | **--bpffs** } }

	*COMMANDS* :=
	{ **show** | **list** | **dump xlated** | **dump jited** | **pin** | **load** | **help** }

MAP COMMANDS
=============

|	**bpftool** **prog { show | list }** [*PROG*]
|	**bpftool** **prog dump xlated** *PROG* [{**file** *FILE* | **opcodes** | **visual**}]
|	**bpftool** **prog dump jited**  *PROG* [{**file** *FILE* | **opcodes**}]
|	**bpftool** **prog pin** *PROG* *FILE*
|	**bpftool** **prog load** *OBJ* *FILE* [**type** *TYPE*] [**map** {**idx** *IDX* | **name** *NAME*} *MAP*] [**dev** *NAME*]
|	**bpftool** **prog help**
|
|	*MAP* := { **id** *MAP_ID* | **pinned** *FILE* }
|	*PROG* := { **id** *PROG_ID* | **pinned** *FILE* | **tag** *PROG_TAG* }
|	*TYPE* := {
|		**socket** | **kprobe** | **kretprobe** | **classifier** | **action** |
|		**tracepoint** | **raw_tracepoint** | **xdp** | **perf_event** | **cgroup/skb** |
|		**cgroup/sock** | **cgroup/dev** | **lwt_in** | **lwt_out** | **lwt_xmit** |
|		**lwt_seg6local** | **sockops** | **sk_skb** | **sk_msg** | **lirc_mode2** |
|		**cgroup/bind4** | **cgroup/bind6** | **cgroup/post_bind4** | **cgroup/post_bind6** |
|		**cgroup/connect4** | **cgroup/connect6** | **cgroup/sendmsg4** | **cgroup/sendmsg6**
|	}


DESCRIPTION
===========
	**bpftool prog { show | list }** [*PROG*]
		  Show information about loaded programs.  If *PROG* is
		  specified show information only about given program, otherwise
		  list all programs currently loaded on the system.

		  Output will start with program ID followed by program type and
		  zero or more named attributes (depending on kernel version).

	**bpftool prog dump xlated** *PROG* [{ **file** *FILE* | **opcodes** | **visual** }]
		  Dump eBPF instructions of the program from the kernel. By
		  default, eBPF will be disassembled and printed to standard
		  output in human-readable format. In this case, **opcodes**
		  controls if raw opcodes should be printed as well.

		  If **file** is specified, the binary image will instead be
		  written to *FILE*.

		  If **visual** is specified, control flow graph (CFG) will be
		  built instead, and eBPF instructions will be presented with
		  CFG in DOT format, on standard output.

	**bpftool prog dump jited**  *PROG* [{ **file** *FILE* | **opcodes** }]
		  Dump jited image (host machine code) of the program.
		  If *FILE* is specified image will be written to a file,
		  otherwise it will be disassembled and printed to stdout.

		  **opcodes** controls if raw opcodes will be printed.

	**bpftool prog pin** *PROG* *FILE*
		  Pin program *PROG* as *FILE*.

		  Note: *FILE* must be located in *bpffs* mount.

	**bpftool prog load** *OBJ* *FILE* [**type** *TYPE*] [**map** {**idx** *IDX* | **name** *NAME*} *MAP*] [**dev** *NAME*]
		  Load bpf program from binary *OBJ* and pin as *FILE*.
		  **type** is optional, if not specified program type will be
		  inferred from section names.
		  By default bpftool will create new maps as declared in the ELF
		  object being loaded.  **map** parameter allows for the reuse
		  of existing maps.  It can be specified multiple times, each
		  time for a different map.  *IDX* refers to index of the map
		  to be replaced in the ELF file counting from 0, while *NAME*
		  allows to replace a map by name.  *MAP* specifies the map to
		  use, referring to it by **id** or through a **pinned** file.
		  If **dev** *NAME* is specified program will be loaded onto
		  given networking device (offload).

		  Note: *FILE* must be located in *bpffs* mount.

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
**# bpftool prog show**
::

  10: xdp  name some_prog  tag 005a3d2123620c8b  gpl
	loaded_at Sep 29/20:11  uid 0
	xlated 528B  jited 370B  memlock 4096B  map_ids 10

**# bpftool --json --pretty prog show**

::

    {
        "programs": [{
                "id": 10,
                "type": "xdp",
                "tag": "005a3d2123620c8b",
                "gpl_compatible": true,
                "loaded_at": "Sep 29/20:11",
                "uid": 0,
                "bytes_xlated": 528,
                "jited": true,
                "bytes_jited": 370,
                "bytes_memlock": 4096,
                "map_ids": [10
                ]
            }
        ]
    }

|
| **# bpftool prog dump xlated id 10 file /tmp/t**
| **# ls -l /tmp/t**
|   -rw------- 1 root root 560 Jul 22 01:42 /tmp/t

**# bpftool prog dum jited tag 005a3d2123620c8b**

::

    push   %rbp
    mov    %rsp,%rbp
    sub    $0x228,%rsp
    sub    $0x28,%rbp
    mov    %rbx,0x0(%rbp)

|
| **# mount -t bpf none /sys/fs/bpf/**
| **# bpftool prog pin id 10 /sys/fs/bpf/prog**
| **# bpftool prog load ./my_prog.o /sys/fs/bpf/prog2**
| **# ls -l /sys/fs/bpf/**
|   -rw------- 1 root root 0 Jul 22 01:43 prog
|   -rw------- 1 root root 0 Jul 22 01:44 prog2

**# bpftool prog dum jited pinned /sys/fs/bpf/prog opcodes**

::

    push   %rbp
    55
    mov    %rsp,%rbp
    48 89 e5
    sub    $0x228,%rsp
    48 81 ec 28 02 00 00
    sub    $0x28,%rbp
    48 83 ed 28
    mov    %rbx,0x0(%rbp)
    48 89 5d 00

|
| **# bpftool prog load xdp1_kern.o /sys/fs/bpf/xdp1 type xdp map name rxcnt id 7**
| **# bpftool prog show pinned /sys/fs/bpf/xdp1**
|   9: xdp  name xdp_prog1  tag 539ec6ce11b52f98  gpl
|	loaded_at 2018-06-25T16:17:31-0700  uid 0
|	xlated 488B  jited 336B  memlock 4096B  map_ids 7
| **# rm /sys/fs/bpf/xdp1**
|

SEE ALSO
========
	**bpftool**\ (8), **bpftool-map**\ (8), **bpftool-cgroup**\ (8)
