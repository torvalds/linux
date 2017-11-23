================
BPFTOOL
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF programs and maps
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** [*OPTIONS*] *OBJECT* { *COMMAND* | **help** }

	**bpftool** **batch file** *FILE*

	**bpftool** **version**

	*OBJECT* := { **map** | **program** }

	*OPTIONS* := { { **-V** | **--version** } | { **-h** | **--help** }
	| { **-j** | **--json** } [{ **-p** | **--pretty** }] }

	*MAP-COMMANDS* :=
	{ **show** | **dump** | **update** | **lookup** | **getnext** | **delete**
	| **pin** | **help** }

	*PROG-COMMANDS* := { **show** | **dump jited** | **dump xlated** | **pin**
	| **help** }

DESCRIPTION
===========
	*bpftool* allows for inspection and simple modification of BPF objects
	on the system.

	Note that format of the output of all tools is not guaranteed to be
	stable and should not be depended upon.

OPTIONS
=======
	-h, --help
		  Print short help message (similar to **bpftool help**).

	-v, --version
		  Print version number (similar to **bpftool version**).

	-j, --json
		  Generate JSON output. For commands that cannot produce JSON, this
		  option has no effect.

	-p, --pretty
		  Generate human-readable JSON output. Implies **-j**.

SEE ALSO
========
	**bpftool-map**\ (8), **bpftool-prog**\ (8)
