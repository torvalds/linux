================
bpftool-prog
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF progs
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

|	**bpftool** prog show [*PROG*]
|	**bpftool** prog dump xlated *PROG*  file *FILE*
|	**bpftool** prog dump jited  *PROG* [file *FILE*] [opcodes]
|	**bpftool** prog pin *PROG* *FILE*
|	**bpftool** prog help
|
|	*PROG* := { id *PROG_ID* | pinned *FILE* | tag *PROG_TAG* }

DESCRIPTION
===========
	**bpftool prog show** [*PROG*]
		  Show information about loaded programs.  If *PROG* is
		  specified show information only about given program, otherwise
		  list all programs currently loaded on the system.

		  Output will start with program ID followed by program type and
		  zero or more named attributes (depending on kernel version).

	**bpftool prog dump xlated** *PROG*  **file** *FILE*
		  Dump eBPF instructions of the program from the kernel to a
		  file.

	**bpftool prog dump jited**  *PROG* [**file** *FILE*] [**opcodes**]
		  Dump jited image (host machine code) of the program.
		  If *FILE* is specified image will be written to a file,
		  otherwise it will be disassembled and printed to stdout.

		  **opcodes** controls if raw opcodes will be printed.

	**bpftool prog pin** *PROG* *FILE*
		  Pin program *PROG* as *FILE*.

		  Note: *FILE* must be located in *bpffs* mount.

	**bpftool prog help**
		  Print short help message.

EXAMPLES
========
**# bpftool prog show**
::

  10: xdp  name some_prog  tag 00:5a:3d:21:23:62:0c:8b
	loaded_at Sep 29/20:11  uid 0
	xlated 528B  jited 370B  memlock 4096B  map_ids 10

|
| **# bpftool prog dump xlated id 10 file /tmp/t**
| **# ls -l /tmp/t**
|   -rw------- 1 root root 560 Jul 22 01:42 /tmp/t

|
| **# bpftool prog dum jited pinned /sys/fs/bpf/prog**

::

    push   %rbp
    mov    %rsp,%rbp
    sub    $0x228,%rsp
    sub    $0x28,%rbp
    mov    %rbx,0x0(%rbp)



SEE ALSO
========
	**bpftool**\ (8), **bpftool-map**\ (8)
