.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

================
BPFTOOL
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF programs and maps
-------------------------------------------------------------------------------

:Manual section: 8

.. include:: substitutions.rst

SYNOPSIS
========

	**bpftool** [*OPTIONS*] *OBJECT* { *COMMAND* | **help** }

	**bpftool** **batch file** *FILE*

	**bpftool** **version**

	*OBJECT* := { **map** | **program** | **link** | **cgroup** | **perf** | **net** | **feature** |
	**btf** | **gen** | **struct_ops** | **iter** }

	*OPTIONS* := { { **-V** | **--version** } | |COMMON_OPTIONS| }

	*MAP-COMMANDS* :=
	{ **show** | **list** | **create** | **dump** | **update** | **lookup** | **getnext** |
	**delete** | **pin** | **event_pipe** | **help** }

	*PROG-COMMANDS* := { **show** | **list** | **dump jited** | **dump xlated** | **pin** |
	**load** | **attach** | **detach** | **help** }

	*LINK-COMMANDS* := { **show** | **list** | **pin** | **detach** | **help** }

	*CGROUP-COMMANDS* := { **show** | **list** | **attach** | **detach** | **help** }

	*PERF-COMMANDS* := { **show** | **list** | **help** }

	*NET-COMMANDS* := { **show** | **list** | **help** }

	*FEATURE-COMMANDS* := { **probe** | **help** }

	*BTF-COMMANDS* := { **show** | **list** | **dump** | **help** }

	*GEN-COMMANDS* := { **object** | **skeleton** | **min_core_btf** | **help** }

	*STRUCT-OPS-COMMANDS* := { **show** | **list** | **dump** | **register** | **unregister** | **help** }

	*ITER-COMMANDS* := { **pin** | **help** }

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
