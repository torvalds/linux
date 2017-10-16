================
BPFTOOL
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF programs and maps
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** *OBJECT* { *COMMAND* | help }

	**bpftool** batch file *FILE*

	*OBJECT* := { **map** | **program** }

	*MAP-COMMANDS* :=
	{ show | dump | update | lookup | getnext | delete | pin | help }

	*PROG-COMMANDS* := { show | dump jited | dump xlated | pin | help }

DESCRIPTION
===========
	*bpftool* allows for inspection and simple modification of BPF objects
	on the system.

	Note that format of the output of all tools is not guaranteed to be
	stable and should not be depended upon.

SEE ALSO
========
	**bpftool-map**\ (8), **bpftool-prog**\ (8)
