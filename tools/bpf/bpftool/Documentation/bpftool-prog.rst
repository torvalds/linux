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
	{ **show** | **list** | **dump xlated** | **dump jited** | **pin** | **load**
	| **loadall** | **help** }

PROG COMMANDS
=============

|	**bpftool** **prog { show | list }** [*PROG*]
|	**bpftool** **prog dump xlated** *PROG* [{**file** *FILE* | **opcodes** | **visual** | **linum**}]
|	**bpftool** **prog dump jited**  *PROG* [{**file** *FILE* | **opcodes** | **linum**}]
|	**bpftool** **prog pin** *PROG* *FILE*
|	**bpftool** **prog { load | loadall }** *OBJ* *PATH* [**type** *TYPE*] [**map** {**idx** *IDX* | **name** *NAME*} *MAP*] [**dev** *NAME*] [**pinmaps** *MAP_DIR*]
|	**bpftool** **prog attach** *PROG* *ATTACH_TYPE* [*MAP*]
|	**bpftool** **prog detach** *PROG* *ATTACH_TYPE* [*MAP*]
|	**bpftool** **prog tracelog**
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
|       *ATTACH_TYPE* := {
|		**msg_verdict** | **stream_verdict** | **stream_parser** | **flow_dissector**
|	}


DESCRIPTION
===========
	**bpftool prog { show | list }** [*PROG*]
		  Show information about loaded programs.  If *PROG* is
		  specified show information only about given program, otherwise
		  list all programs currently loaded on the system.

		  Output will start with program ID followed by program type and
		  zero or more named attributes (depending on kernel version).

		  Since Linux 5.1 the kernel can collect statistics on BPF
		  programs (such as the total time spent running the program,
		  and the number of times it was run). If available, bpftool
		  shows such statistics. However, the kernel does not collect
		  them by defaults, as it slightly impacts performance on each
		  program run. Activation or deactivation of the feature is
		  performed via the **kernel.bpf_stats_enabled** sysctl knob.

	**bpftool prog dump xlated** *PROG* [{ **file** *FILE* | **opcodes** | **visual** | **linum** }]
		  Dump eBPF instructions of the program from the kernel. By
		  default, eBPF will be disassembled and printed to standard
		  output in human-readable format. In this case, **opcodes**
		  controls if raw opcodes should be printed as well.

		  If **file** is specified, the binary image will instead be
		  written to *FILE*.

		  If **visual** is specified, control flow graph (CFG) will be
		  built instead, and eBPF instructions will be presented with
		  CFG in DOT format, on standard output.

		  If the prog has line_info available, the source line will
		  be displayed by default.  If **linum** is specified,
		  the filename, line number and line column will also be
		  displayed on top of the source line.

	**bpftool prog dump jited**  *PROG* [{ **file** *FILE* | **opcodes** | **linum** }]
		  Dump jited image (host machine code) of the program.
		  If *FILE* is specified image will be written to a file,
		  otherwise it will be disassembled and printed to stdout.

		  **opcodes** controls if raw opcodes will be printed.

		  If the prog has line_info available, the source line will
		  be displayed by default.  If **linum** is specified,
		  the filename, line number and line column will also be
		  displayed on top of the source line.

	**bpftool prog pin** *PROG* *FILE*
		  Pin program *PROG* as *FILE*.

		  Note: *FILE* must be located in *bpffs* mount. It must not
		  contain a dot character ('.'), which is reserved for future
		  extensions of *bpffs*.

	**bpftool prog { load | loadall }** *OBJ* *PATH* [**type** *TYPE*] [**map** {**idx** *IDX* | **name** *NAME*} *MAP*] [**dev** *NAME*] [**pinmaps** *MAP_DIR*]
		  Load bpf program(s) from binary *OBJ* and pin as *PATH*.
		  **bpftool prog load** pins only the first program from the
		  *OBJ* as *PATH*. **bpftool prog loadall** pins all programs
		  from the *OBJ* under *PATH* directory.
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
		  Optional **pinmaps** argument can be provided to pin all
		  maps under *MAP_DIR* directory.

		  Note: *PATH* must be located in *bpffs* mount. It must not
		  contain a dot character ('.'), which is reserved for future
		  extensions of *bpffs*.

	**bpftool prog attach** *PROG* *ATTACH_TYPE* [*MAP*]
		  Attach bpf program *PROG* (with type specified by
		  *ATTACH_TYPE*). Most *ATTACH_TYPEs* require a *MAP*
		  parameter, with the exception of *flow_dissector* which is
		  attached to current networking name space.

	**bpftool prog detach** *PROG* *ATTACH_TYPE* [*MAP*]
		  Detach bpf program *PROG* (with type specified by
		  *ATTACH_TYPE*). Most *ATTACH_TYPEs* require a *MAP*
		  parameter, with the exception of *flow_dissector* which is
		  detached from the current networking name space.

	**bpftool prog tracelog**
		  Dump the trace pipe of the system to the console (stdout).
		  Hit <Ctrl+C> to stop printing. BPF programs can write to this
		  trace pipe at runtime with the **bpf_trace_printk()** helper.
		  This should be used only for debugging purposes. For
		  streaming data from BPF programs to user space, one can use
		  perf events (see also **bpftool-map**\ (8)).

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
		  When showing BPF programs, show file names of pinned
		  programs.

	-m, --mapcompat
		  Allow loading maps with unknown map definitions.

	-n, --nomount
		  Do not automatically attempt to mount any virtual file system
		  (such as tracefs or BPF virtual file system) when necessary.

EXAMPLES
========
**# bpftool prog show**

::

    10: xdp  name some_prog  tag 005a3d2123620c8b  gpl run_time_ns 81632 run_cnt 10
            loaded_at 2017-09-29T20:11:00+0000  uid 0
            xlated 528B  jited 370B  memlock 4096B  map_ids 10

**# bpftool --json --pretty prog show**

::

    [{
            "id": 10,
            "type": "xdp",
            "tag": "005a3d2123620c8b",
            "gpl_compatible": true,
            "run_time_ns": 81632,
            "run_cnt": 10,
            "loaded_at": 1506715860,
            "uid": 0,
            "bytes_xlated": 528,
            "jited": true,
            "bytes_jited": 370,
            "bytes_memlock": 4096,
            "map_ids": [10
            ]
        }
    ]

|
| **# bpftool prog dump xlated id 10 file /tmp/t**
| **# ls -l /tmp/t**

::

    -rw------- 1 root root 560 Jul 22 01:42 /tmp/t

**# bpftool prog dump jited tag 005a3d2123620c8b**

::

    0:   push   %rbp
    1:   mov    %rsp,%rbp
    2:   sub    $0x228,%rsp
    3:   sub    $0x28,%rbp
    4:   mov    %rbx,0x0(%rbp)

|
| **# mount -t bpf none /sys/fs/bpf/**
| **# bpftool prog pin id 10 /sys/fs/bpf/prog**
| **# bpftool prog load ./my_prog.o /sys/fs/bpf/prog2**
| **# ls -l /sys/fs/bpf/**

::

    -rw------- 1 root root 0 Jul 22 01:43 prog
    -rw------- 1 root root 0 Jul 22 01:44 prog2

**# bpftool prog dump jited pinned /sys/fs/bpf/prog opcodes**

::

   0:   push   %rbp
        55
   1:   mov    %rsp,%rbp
        48 89 e5
   4:   sub    $0x228,%rsp
        48 81 ec 28 02 00 00
   b:   sub    $0x28,%rbp
        48 83 ed 28
   f:   mov    %rbx,0x0(%rbp)
        48 89 5d 00

|
| **# bpftool prog load xdp1_kern.o /sys/fs/bpf/xdp1 type xdp map name rxcnt id 7**
| **# bpftool prog show pinned /sys/fs/bpf/xdp1**

::

    9: xdp  name xdp_prog1  tag 539ec6ce11b52f98  gpl
            loaded_at 2018-06-25T16:17:31-0700  uid 0
            xlated 488B  jited 336B  memlock 4096B  map_ids 7

**# rm /sys/fs/bpf/xdp1**

SEE ALSO
========
	**bpf**\ (2),
	**bpf-helpers**\ (7),
	**bpftool**\ (8),
	**bpftool-map**\ (8),
	**bpftool-cgroup**\ (8),
	**bpftool-feature**\ (8),
	**bpftool-net**\ (8),
	**bpftool-perf**\ (8)
