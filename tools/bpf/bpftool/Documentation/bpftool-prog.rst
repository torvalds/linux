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
	{ **show** | **dump xlated** | **dump jited** | **pin** | **help** }

MAP COMMANDS
=============

|	**bpftool** **prog show** [*PROG*]
|	**bpftool** **prog dump xlated** *PROG* [{**file** *FILE* | **opcodes**}]
|	**bpftool** **prog dump jited**  *PROG* [{**file** *FILE* | **opcodes**}]
|	**bpftool** **prog pin** *PROG* *FILE*
|	**bpftool** **prog help**
|
|	*PROG* := { **id** *PROG_ID* | **pinned** *FILE* | **tag** *PROG_TAG* }

DESCRIPTION
===========
	**bpftool prog show** [*PROG*]
		  Show information about loaded programs.  If *PROG* is
		  specified show information only about given program, otherwise
		  list all programs currently loaded on the system.

		  Output will start with program ID followed by program type and
		  zero or more named attributes (depending on kernel version).

	**bpftool prog dump xlated** *PROG* [{ **file** *FILE* | **opcodes** }]
		  Dump eBPF instructions of the program from the kernel.
		  If *FILE* is specified image will be written to a file,
		  otherwise it will be disassembled and printed to stdout.

		  **opcodes** controls if raw opcodes will be printed.

	**bpftool prog dump jited**  *PROG* [{ **file** *FILE* | **opcodes** }]
		  Dump jited image (host machine code) of the program.
		  If *FILE* is specified image will be written to a file,
		  otherwise it will be disassembled and printed to stdout.

		  **opcodes** controls if raw opcodes will be printed.

	**bpftool prog pin** *PROG* *FILE*
		  Pin program *PROG* as *FILE*.

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

  10: xdp  name some_prog  tag 005a3d2123620c8b
	loaded_at Sep 29/20:11  uid 0
	xlated 528B  jited 370B  memlock 4096B  map_ids 10

**# bpftool --json --pretty prog show**

::

    {
        "programs": [{
                "id": 10,
                "type": "xdp",
                "tag": "005a3d2123620c8b",
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
| **# ls -l /sys/fs/bpf/**
|   -rw------- 1 root root 0 Jul 22 01:43 prog

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


SEE ALSO
========
	**bpftool**\ (8), **bpftool-map**\ (8)
