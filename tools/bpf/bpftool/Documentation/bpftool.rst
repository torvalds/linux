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

	*OBJECT* := { **map** | **program** | **cgroup** | **perf** | **net** | **feature** }

	*OPTIONS* := { { **-V** | **--version** } |
	{ **-j** | **--json** } [{ **-p** | **--pretty** }] | { **-d** | **--debug** } }

	*MAP-COMMANDS* :=
	{ **show** | **list** | **create** | **dump** | **update** | **lookup** | **getnext** |
	**delete** | **pin** | **event_pipe** | **help** }

	*PROG-COMMANDS* := { **show** | **list** | **dump jited** | **dump xlated** | **pin** |
	**load** | **attach** | **detach** | **help** }

	*CGROUP-COMMANDS* := { **show** | **list** | **attach** | **detach** | **help** }

	*PERF-COMMANDS* := { **show** | **list** | **help** }

	*NET-COMMANDS* := { **show** | **list** | **help** }

	*FEATURE-COMMANDS* := { **probe** | **help** }

DESCRIPTION
===========
	*bpftool* allows for inspection and simple modification of BPF objects
	on the system.

	Note that format of the output of all tools is not guaranteed to be
	stable and should not be depended upon.

OPTIONS
=======
	.. include:: common_options.rst

	-m, --mapcompat
		  Allow loading maps with unknown map definitions.

	-n, --nomount
		  Do not automatically attempt to mount any virtual file system
		  (such as tracefs or BPF virtual file system) when necessary.
